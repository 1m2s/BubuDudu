#include "../fixtures/scenario_helpers.h"

void testTransport()
{
    // Receipt ACK never substitutes for SLEEP_ACK; an out-of-order READY can
    // nevertheless retire REQUEST whose receipt ACK was lost.
    freshApp(); loop(); requestSleepForTest(); const auto request = pendingMessage;
    assert(request.type == Type::SleepRequest && request.messageId == request.ackForMessageId);
    receive(incoming(Type::Ack, request.messageId));
    assert(transaction().active && transaction().phase == SleepPhase::WAIT_READY);
    receive(incoming(Type::SleepReady, request.messageId));
    assert(pendingMessage.type == Type::SleepCommit && waitingForAck);
    const auto commit = pendingMessage;
    receive(incoming(Type::Ack, commit.messageId)); assert(transaction().active);
    receive(incoming(Type::SleepAck, request.messageId));
    assert(localState() == LocalState::SLEEPING && peerState() == PeerState::SLEEPING);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(coordinatedAttempts == 1 && physicalSleeps == 1);
    assert(armAttempts == 1);
    assert(Serial.log.find("sleepId=" + std::to_string(request.messageId) + " | role=COORDINATOR") != std::string::npos);
    receive(incoming(Type::SleepAck, request.messageId));
    for (int i = 0; i < 100; ++i) loop();
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(coordinatedAttempts == 1 && physicalSleeps == 1);
    assert(armAttempts == 1);

    // Missing peer: exactly three sends of the same REQUEST ID, then no loop.
    freshApp(); loop(); requestSleepForTest(); auto began = ackWaitStart; auto id = pendingMessage.messageId;
    hostNow = began + 299; loop(); assert(countWire(Type::SleepRequest) == 1);
    hostNow = began + 300; loop(); assert(retryCount == 1);
    hostNow = began + 600; loop(); assert(retryCount == 2);
    hostNow = began + 900; loop();
    assert(!waitingForAck && !transaction().active && peerState() == PeerState::OFFLINE);
    for (const auto& message : wire) if (message.type == Type::SleepRequest) assert(message.messageId == id);
    for (int i = 0; i < 1000; ++i) loop(); assert(countWire(Type::SleepRequest) == 3);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);

    // Duplicates coalesce with in-flight responses, never restart their budget.
    freshApp(); loop(); const auto peerRequest = incoming(Type::SleepRequest, 20);
    receive(peerRequest); assert(pendingMessage.type == Type::SleepReady);
    began = ackWaitStart; id = pendingMessage.messageId;
    receive(peerRequest);
    assert(ackWaitStart == began && pendingMessage.messageId == id && countWire(Type::SleepReady) == 1);
    hostNow = began + 300; loop(); assert(retryCount == 1 && countWire(Type::SleepReady) == 2);
    auto peerCommit = incoming(Type::SleepCommit, 20, 21);
    receive(peerCommit); // Retires READY, sends SLEEP_ACK, and only then closes.
    assert(localState() == LocalState::SLEEPING && pendingMessage.type == Type::SleepAck);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);
    const auto finalAck = pendingMessage;
    auto transitions = occurrences(Serial.log, "HANDSHAKE_COMPLETE");
    began = ackWaitStart; receive(peerCommit);
    assert(ackWaitStart == began && countWire(Type::SleepAck) == 1);
    hostNow = began + 300; loop(); assert(countWire(Type::SleepAck) == 2 && pendingMessage.messageId == finalAck.messageId);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);
    receive(incoming(Type::Ack, finalAck.messageId)); assert(!waitingForAck && peerState() == PeerState::SLEEPING);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(coordinatedAttempts == 1 && physicalSleeps == 1);
    assert(armAttempts == 1);
    assert(Serial.log.find("sleepId=20 | role=PARTICIPANT") != std::string::npos);
    receive(peerCommit); assert(countWire(Type::SleepAck) == 3);
    assert(occurrences(Serial.log, "HANDSHAKE_COMPLETE") == transitions);
    receive(incoming(Type::Ack, pendingMessage.messageId));
    for (int i = 0; i < 100; ++i) loop();
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(coordinatedAttempts == 1 && physicalSleeps == 1);
    assert(armAttempts == 1);

    // Real loop collision retires the losing request, even with equal IDs.
    freshApp(); loop(); requestSleepForTest(); id = pendingMessage.messageId;
    receive(incoming(Type::SleepRequest, id));
    if (LOCAL_DEVICE == Device::Bubu)
        assert(transaction().role == SleepRole::COORDINATOR && pendingMessage.type == Type::SleepRequest);
    else
        assert(transaction().role == SleepRole::PARTICIPANT && pendingMessage.type == Type::SleepReady);
    assert(countWire(Type::SleepCancel) == 0);

    // Callback still copies only: state changes occur when loop drains the queue.
    freshApp(); loop(); auto message = incoming(Type::SleepRequest, 30);
    queueReceivedData(reinterpret_cast<const uint8_t*>(&message), sizeof(message));
    assert(!transaction().active && wire.empty()); loop(); assert(transaction().active);
    // Background Heartbeat and its duplicate preserve negotiation; real local Activity cancels.
    Protocol::Message event{1, Type::Event, 70, PEER_DEVICE, Protocol::EventType::Heartbeat, 0};
    receive(event); assert(localState() == LocalState::SLEEP_NEGOTIATING && transaction().active && countWire(Type::SleepCancel) == 0);
    receive(event); assert(countWire(Type::SleepCancel) == 0 && Serial.log.find("RX DUPLICATE") != std::string::npos);
    motionPendingEvent = MotionEvent::Activity; loop();
    assert(localState() == LocalState::ACTIVE && !transaction().active && countWire(Type::SleepCancel) == 1);
    receive(incoming(Type::SleepCommit, 30)); assert(localState() == LocalState::ACTIVE);

    // Cancel removes a queued unsent COMMIT and an old in-flight REQUEST.
    freshApp(); deferControls(); loop(); requestSleepForTest();
    assert(controlCount == 1 && !waitingForAck); activityForTest();
    assert(controlCount == 0 && localState() == LocalState::ACTIVE);
    for (int i = 0; i < 600; ++i) loop(); assert(countWire(Type::SleepRequest) == 0);
    freshApp(); loop(); requestSleepForTest(); id = pendingMessage.messageId; deferControls();
    receive(incoming(Type::SleepReady, id)); assert(controlCount == 1 && !waitingForAck);
    activityForTest(); for (int i = 0; i < 600; ++i) loop(); assert(countWire(Type::SleepCommit) == 0);

    // Existing EVENT retry budget/ID/ACK matching remains unchanged.
    freshApp(); startHeartbeatEvent(); id = pendingMessage.messageId; began = ackWaitStart;
    receive(incoming(Type::Ack, uint16_t(id + 1))); assert(waitingForAck);
    hostNow = began + 300; loop(); hostNow = began + 600; loop();
    assert(countWire(Type::Event) == 3 && retryCount == 2);
    for (const auto& packet : wire) if (packet.type == Type::Event) assert(packet.messageId == id);
    receive(incoming(Type::Ack, id)); assert(!waitingForAck);
    // Automatic EVENTs are suppressed in negotiation and simulated SLEEPING.
    freshApp(); suppressPeriodicForTest = false; nextEventTime = 0;
    PowerManager::requestSleep(nextMessageId++, 0); loop();
    assert(countWire(Type::Event) == 0);
    id = transaction().sleepId; receive(incoming(Type::SleepReady, id)); receive(incoming(Type::SleepAck, id));
    for (int i = 0; i < 600; ++i) loop(); assert(countWire(Type::Event) == 0);
    // A rejected first send cannot close the participant before a later accepted retry.
    freshApp(); loop(); receive(incoming(Type::SleepRequest, 20)); radioAccepts = false;
    receive(incoming(Type::SleepCommit, 20, 21)); assert(transaction().active);
    radioAccepts = true; hostNow = ackWaitStart + 300; loop(); assert(localState() == LocalState::SLEEPING);
    // Even a delayed direct retry checks the absolute deadline before sending.
    freshApp(); loop(); requestSleepForTest(); id = transaction().sleepId;
    receive(incoming(Type::SleepReady, id)); auto commits = countWire(Type::SleepCommit);
    hostNow = transaction().hardDeadline; transmitPendingMessage(true);
    assert(countWire(Type::SleepCommit) == commits && !transaction().active && !waitingForAck);
    assert(peerState() == PeerState::UNKNOWN);
    receive(incoming(Type::Ack, wire.back().messageId)); // Late CANCEL receipt, not sleep evidence.
    assert(peerState() == PeerState::UNKNOWN);
    // Losing all receipts for the final SLEEP_ACK is bounded and reported UNKNOWN.
    freshApp(); loop(); receive(incoming(Type::SleepRequest, 20));
    receive(incoming(Type::SleepCommit, 20, 21)); began = ackWaitStart;
    hostNow = began + 300; loop(); hostNow = began + 600; loop(); hostNow = began + 900; loop();
    assert(countWire(Type::SleepAck) == 3 && !waitingForAck && !transaction().active);
    assert(localState() == LocalState::ACTIVE && peerState() == PeerState::UNKNOWN);
}

void testAwakeWakeService()
{
    freshApp();
    awakeAckBusy = true;
    assert(std::string(sleepTransportBlockedReason()) == "CC1101_RUNTIME_BUSY");
    wakePeerForTest(); assert(wakeEvents.empty());
    typeSerialForTest("x"); assert(coordinatedAttempts == 0);
    // The radio's in-flight ACK must not prevent ordinary ESP-NOW receipt work.
    startHeartbeatEvent();
    const auto id = pendingMessage.messageId;
    receive(incoming(Type::Ack, id));
    assert(!waitingForAck && peerState() == PeerState::ONLINE && awakeServices >= 3);
    awakeAckBusy = false;
    assert(sleepTransportBlockedReason() == nullptr);
    wakePeerForTest(); assert(wakeEvents.size() == 1);
    completedAwaitingCallbacks(false);
    mockedTxInFlight = 0; awakeAckBusy = true;
    loop(); assert(physicalSleeps == 0 && coordinatedAttempts == 0);
    awakeAckBusy = false;
    loop(); assert(physicalSleeps == 1 && coordinatedAttempts == 1);
    freshApp(); protocolReady = false;
    loop(); assert(awakeServices == 0);
    puts("PASS: awake CC1101 service stays in loop, ACK blocks competing sleep/TX, ESP-NOW still drains");
}

void testApplicationTransports()
{
    freshKnownApp(); assert(selectedTransport == Transport::ESP_NOW);
    selectTransportForTest(Transport::CC1101); assert(selectedTransport == Transport::CC1101);
    selectTransportForTest(Transport::ESP_NOW); assert(selectedTransport == Transport::ESP_NOW);
    typeSerialForTest("p"); assert(Serial.log.find("APP TRANSPORT | selected=") == std::string::npos);
    for (unsigned guard = 0; guard < 6; ++guard)
    {
        freshKnownApp();
        if (guard == 0) startHeartbeatEvent();
        if (guard == 1) { loop(); requestSleepForTest(); }
        if (guard == 2) awakeAckBusy = true;
        if (guard == 3) mockedTxInFlight = 1;
        if (guard == 4) protocolReady = false;
        if (guard == 5) completedAwaitingCallbacks(false);
        typeSerialForTest("c"); assert(selectedTransport == Transport::ESP_NOW);
        assert(Serial.log.find("APP TRANSPORT | REFUSED") == std::string::npos);
    }
    for (auto transport : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshKnownApp(); selectedTransport = transport;
        startHeartbeatEvent(); const auto original = pendingMessage;
        assert(waitingForAck && pendingTransport == transport);
        auto& sent = transport == Transport::ESP_NOW ? wire : ccWire;
        assert(sent.size() == 1 && memcmp(&sent[0], &original, 8) == 0);
        typeSerialForTest(transport == Transport::ESP_NOW ? "c" : "e");
        assert(selectedTransport == transport && waitingForAck);
        const auto other = transport == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW;
        receiveVia(incoming(Type::Ack, original.messageId), other);
        assert(waitingForAck && peerState() == PeerState::UNKNOWN);
        auto wrongPeer = incoming(Type::Ack, original.messageId); wrongPeer.sender = LOCAL_DEVICE;
        receiveVia(wrongPeer, transport); assert(waitingForAck);
        receiveVia(incoming(Type::Ack, original.messageId + 1), transport); assert(waitingForAck);
        hostNow = ackWaitStart + 300;
        receiveVia(incoming(Type::Ack, original.messageId), transport);
        assert(!waitingForAck && retryCount == 0 && sent.size() == 1); // ACK beats exact deadline.
        assert(peerState() == PeerState::ONLINE);
        assert(Serial.log.find(std::string("ACK MATCHED | message=") + std::to_string(original.messageId) +
                               " | via=" + transportName(transport)) != std::string::npos);

        // One shared retry machine; even a forced selector change cannot reroute
        // an in-flight packet (automatic policy defers until the drained boundary).
        freshKnownApp(); selectedTransport = transport; startHeartbeatEvent();
        const auto retryPacket = pendingMessage;
        selectedTransport = other;
        for (unsigned retry = 1; retry <= 2; ++retry)
        { hostNow = ackWaitStart + 300; loop(); assert(retryCount == retry && pendingTransport == transport); }
        auto& retries = transport == Transport::ESP_NOW ? wire : ccWire;
        assert(retries.size() == 3);
        for (const auto& packet : retries) assert(memcmp(&packet, &retryPacket, 8) == 0);
        hostNow = ackWaitStart + 300; loop();
        assert(!waitingForAck && peerState() == PeerState::OFFLINE);
        for (unsigned i = 0; i < 50; ++i) loop();
        assert(retries.size() == 3);
    }
    for (auto first : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshKnownApp(); notePeerSeen(); // Isolate cross-radio duplicate receipts from first-contact probes.
        const auto other = first == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW;
        selectedTransport = other;
        const auto event = proximityObservation(77, -50, 0).message;
        receiveVia(event, first); receiveVia(event, other);
        assert(occurrences(Serial.log, "RX NEW EVENT") == 1 && occurrences(Serial.log, "RX DUPLICATE") == 1);
        assert(wire.size() == 1 && ccWire.size() == 1);
        assert(wire[0].type == Type::Ack && ccWire[0].type == Type::Ack);
        assert(wire[0].ackForMessageId == 77 && ccWire[0].ackForMessageId == 77);
    }
    // Bidirectional independent EVENTs: receiving/re-ACKing the peer's EVENT
    // cannot replace our own pending packet. First peer receipt is lost.
    freshKnownApp(); selectTransportForTest(Transport::CC1101); startHeartbeatEvent(); const auto ours = pendingMessage;
    const auto peerEvent = proximityObservation(99, -50, 0).message;
    receiveVia(peerEvent, Transport::CC1101);
    receiveVia(peerEvent, Transport::CC1101);
    assert(waitingForAck && memcmp(&ours, &pendingMessage, 8) == 0);
    assert(ccWire.size() == 3 && ccWire[1].ackForMessageId == 99 && ccWire[2].ackForMessageId == 99);
    assert(occurrences(Serial.log, "RX NEW EVENT") == 1);
    hostNow = ackWaitStart + 300; loop(); assert(retryCount == 1);
    assert(memcmp(&ccWire.back(), &ours, 8) == 0);
    receiveVia(incoming(Type::Ack, ours.messageId), Transport::CC1101);
    assert(!waitingForAck && retryCount == 0);

    // Receipt TX blocks a due heartbeat, automatic mode change, peer wake, and sleep.
    freshKnownApp(); selectTransportForTest(Transport::CC1101); ccHoldTx = true;
    suppressPeriodicForTest = false; nextEventTime = hostNow;
    receiveVia(peerEvent, Transport::CC1101);
    assert(awakeAckBusy && ccWire.size() == 1 && ccWire[0].type == Type::Ack && !waitingForAck);
    typeSerialForTest("ewx"); wakePeerForTest();
    assert(selectedTransport == Transport::CC1101 && wakeEvents.empty() && coordinatedAttempts == 0);
    awakeAckBusy = false; ccHoldTx = false; loop();
    assert(waitingForAck && ccWire.size() == 2 && ccWire.back().type == Type::Event);

    // Every sleep control stays on ESP-NOW, independent of the app selector.
    freshKnownApp(); selectTransportForTest(Transport::CC1101); loop(); requestSleepForTest();
    const auto request = pendingMessage;
    assert(request.type == Type::SleepRequest && pendingTransport == Transport::ESP_NOW && ccWire.empty());
    receiveVia(incoming(Type::Ack, request.messageId), Transport::CC1101); assert(waitingForAck);
    receive(incoming(Type::Ack, request.messageId));
    receive(incoming(Type::SleepReady, request.messageId));
    assert(pendingMessage.type == Type::SleepCommit && pendingTransport == Transport::ESP_NOW);
    const auto commit = pendingMessage;
    receive(incoming(Type::Ack, commit.messageId));
    receive(incoming(Type::SleepAck, request.messageId));
    assert(physicalSleeps == 1 && ccWire.empty());
    freshKnownApp(); selectTransportForTest(Transport::CC1101); loop(); requestSleepForTest(); activityForTest();
    assert(countWire(Type::SleepCancel) == 1 && ccWire.empty());
    freshKnownApp(); selectTransportForTest(Transport::CC1101); loop(); receive(incoming(Type::SleepRequest, 40, 40));
    assert(pendingMessage.type == Type::SleepReady && pendingTransport == Transport::ESP_NOW);
    receive(incoming(Type::Ack, pendingMessage.messageId)); receive(incoming(Type::SleepCommit, 40));
    assert(pendingMessage.type == Type::SleepAck && pendingTransport == Transport::ESP_NOW && ccWire.empty());
    receive(incoming(Type::Ack, pendingMessage.messageId)); assert(physicalSleeps == 1);

    // CC mode keeps normal ESP-NOW RX/ACK alive, but samples only matched probe replies.
    freshKnownApp(); selectTransportForTest(Transport::CC1101); settleForCheck(2000);
    assert(wire.size() == 1 && wire[0].type == Type::ProximityProbe);
    receive(peerEvent); assert(wire.size() == 2 && wire.back().type == Type::Ack);
    rssiObservations.push_back(proximityObservation(100, -20, 2010));
    movementStep(2020); assert(proximitySampleCount == 0);
    for (unsigned i = 0; i < 3; ++i)
    {
        movementStep(2030 + 20 * i);
        auto observation = proximityObservation(101 + i, -50 - i, 2031 + 20 * i);
        observation.message.type = Type::ProximityProbeReply;
        observation.message.event = Protocol::EventType::None;
        observation.message.ackForMessageId = probeMessageId;
        rssiObservations.push_back(observation);
        rssiObservations.push_back(observation); // Same reply cannot count twice.
        movementStep(2040 + 20 * i);
    }
    assertProximityReset(); assert(Serial.log.find("median=-51 dBm") != std::string::npos);
    assert(countWire(Type::ProximityProbe) == 3 && !waitingForAck);
    assert(selectedTransport == Transport::CC1101 && ccWire.empty());
    selectTransportForTest(Transport::CC1101); // Set the fixture route after the completed CLOSE measurement.
    failedSleepEntryForTest(); assert(coordinatedAttempts == 1 && motionCancels == 1);
    startHeartbeatEvent(); assert(ccWire.size() == 1); // Aborted bench entry resumes runtime.
    freshKnownApp(); selectTransportForTest(Transport::CC1101); ccAccepts = false; startHeartbeatEvent();
    for (unsigned i = 0; i < 3; ++i) { hostNow = ackWaitStart + 300; loop(); }
    assert(!waitingForAck && ccWire.empty() && peerState() == PeerState::OFFLINE);
    puts("PASS: both transports, route-pinned retries, matching ACK before expiry, cross-radio dedup and receipt priority");
    puts("PASS: simultaneous independent EVENTs, lost ACK/retry/re-ACK, sleep guards/ESP-NOW controls, RSSI/Motion in CC mode");
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    runCase(testTransport);
    runCase(testApplicationTransports);
    runCase(testAwakeWakeService);
    finishSuite("protocol_delivery");
}
