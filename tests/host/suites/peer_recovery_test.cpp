#include "../fixtures/scenario_helpers.h"

void testEspNowFallback()
{
    using Class = ProximityClassification;
    for (auto classification : {Class::UNKNOWN, Class::CLOSE, Class::FAR})
    for (bool accepted : {false, true})
    {
        freshApp();
        if (classification != Class::UNKNOWN)
        {
            completePolicyMeasurement(classification == Class::CLOSE ? -50 : -85);
            selectTransportForTest(Transport::ESP_NOW); // Includes the FAR with an ESP-NOW fixture route.
        }
        radioAccepts = accepted;
        const auto failed = requestFallbackFromEvent();
        const auto nextEvent = nextEventTime, ackStart = ackWaitStart;
        for (unsigned i = 0; i < 100; ++i) loop();
        assert(espNowFallbackPending && selectedTransport == Transport::ESP_NOW);
        assert(Serial.log.find("APP TRANSPORT FALLBACK") == std::string::npos);
        assert(Serial.log.find("APP TRANSPORT | REFUSED") == std::string::npos);
        mockedTxInFlight = 0; loop();
        assert(selectedTransport == Transport::CC1101 && !espNowFallbackPending && !automaticSelectionPending);
        assert(peerState() == PeerState::OFFLINE && proximityClassification == classification);
        assert(!waitingForAck && retryCount == 0 && pendingTransport == Transport::ESP_NOW);
        assert(memcmp(&pendingMessage, &failed, sizeof(failed)) == 0 && ackWaitStart == ackStart && nextEventTime == nextEvent);
        assert(ccWire.empty()); // Applying fallback did not replay the failed EVENT.
        for (unsigned i = 0; i < 20; ++i) loop();
        assert(occurrences(Serial.log, "APP TRANSPORT FALLBACK | selected=CC1101 | reason=ESP_NOW_EVENT_GIVE_UP") == 1);
        startHeartbeatEvent();
        assert(pendingTransport == Transport::CC1101 && pendingMessage.messageId != failed.messageId);
        for (unsigned i = 0; i <= MAX_RETRIES; ++i) { hostNow = ackWaitStart + ACK_TIMEOUT_MS; loop(); }
        assert(!waitingForAck && !espNowFallbackPending && selectedTransport == Transport::CC1101);
        assert(proximityClassification == classification && peerState() == PeerState::OFFLINE);
        assert(occurrences(Serial.log, "APP TRANSPORT FALLBACK") == 1); // No failure ping-pong.
    }
    // A reliable sleep control also uses ESP-NOW, but its exhaustion must not request fallback.
    freshApp(); loop(); requestSleepForTest();
    for (unsigned i = 0; i <= MAX_RETRIES; ++i) { hostNow = ackWaitStart + ACK_TIMEOUT_MS; loop(); }
    assert(!espNowFallbackPending && selectedTransport == Transport::ESP_NOW);
    assert(Serial.log.find("APP TRANSPORT FALLBACK") == std::string::npos);
    puts("PASS: UNKNOWN/CLOSE/FAR preserved across ESP-NOW GIVE_UP, identical three attempts, callback drain, one fallback, new CC1101 EVENT only; no CC1101/sleep-control fallback");
}

void testFallbackPrecedenceAndSerial()
{
    for (int8_t median : {int8_t(-50), int8_t(-85)})
    {
        freshApp(); requestFallbackFromEvent();
        // OFFLINE otherwise prevents measurements. An actual incoming EVENT restores ONLINE.
        receiveVia(proximityObservation(901, -50, hostNow).message, Transport::CC1101);
        assert(peerState() == PeerState::ONLINE && espNowFallbackPending);
        completePolicyMeasurement(median);
        assert(!espNowFallbackPending && automaticSelectionPending);
        mockedTxInFlight = 0; loop();
        assert(selectedTransport == (median == -50 ? Transport::ESP_NOW : Transport::CC1101));
        assert(!automaticSelectionPending && Serial.log.find("APP TRANSPORT FALLBACK") == std::string::npos);
    }
    freshApp(); completePolicyMeasurement(-85); assert(automaticSelectionPending);
    requestFallbackFromEvent();
    assert(!automaticSelectionPending && espNowFallbackPending && proximityClassification == ProximityClassification::FAR);
    mockedTxInFlight = 0; loop();
    assert(selectedTransport == Transport::CC1101 && occurrences(Serial.log, "APP TRANSPORT FALLBACK") == 1);
    assert(Serial.log.find("APP TRANSPORT AUTO") == std::string::npos);

    for (bool alreadyCc1101 : {false, true})
    {
        freshApp(); completePolicyMeasurement(-50); selectTransportForTest(Transport::ESP_NOW); requestFallbackFromEvent();
        automaticSelectionPending = true; // Defensive conflicting flags: fallback wins, without later bouncing to CLOSE's ESP-NOW.
        if (alreadyCc1101) selectedTransport = Transport::CC1101;
        serviceAutomaticTransportSelection(); assert(espNowFallbackPending && automaticSelectionPending);
        mockedTxInFlight = 0; loop(); loop();
        assert(selectedTransport == Transport::CC1101 && !espNowFallbackPending && !automaticSelectionPending);
        assert(occurrences(Serial.log, "APP TRANSPORT FALLBACK") == (alreadyCc1101 ? 0U : 1U));
        assert(Serial.log.find("APP TRANSPORT AUTO") == std::string::npos);
    }
    for (const char* text : {"e", "c"})
    {
        freshApp(); requestFallbackFromEvent(); automaticSelectionPending = true;
        typeSerialForTest(text);
        assert(espNowFallbackPending && automaticSelectionPending && selectedTransport == Transport::ESP_NOW);
        mockedTxInFlight = 0; typeSerialForTest(text);
        assert(!espNowFallbackPending && !automaticSelectionPending && selectedTransport == Transport::CC1101);
        assert(occurrences(Serial.log, "APP TRANSPORT FALLBACK") == 1);
    }
    puts("PASS: fresh median supersedes fallback, newer exhaustion supersedes classifier intent, fallback wins conflicts; e/c cannot override fallback");
}

void testPendingFallbackRecovery()
{
    for (bool recoverByAck : {false, true})
    {
        freshKnownApp(); notePeerSeen();
        const auto failed = requestFallbackFromEvent(); // Real three-attempt exhaustion; callback still outstanding.
        assert(selectedTransport == Transport::ESP_NOW && espNowFallbackPending);
        if (recoverByAck)
        {
            // A later real periodic EVENT can recover the peer before fallback drains.
            suppressPeriodicForTest = false; hostNow = nextEventTime; loop();
            suppressPeriodicForTest = true;
            assert(waitingForAck && pendingTransport == Transport::ESP_NOW && pendingMessage.messageId != failed.messageId);
            receive(incoming(Type::Ack, pendingMessage.messageId));
        }
        else
        {
            const auto recovery = proximityObservation(900, -53, hostNow);
            rssiObservations.push_back(recovery); // Same-tick recovery RSSI is not a fresh measurement sample.
            receive(recovery.message);
            assert(memcmp(&pendingMessage, &failed, sizeof(failed)) == 0 && pendingTransport == Transport::ESP_NOW);
        }
        assert(peerState() == PeerState::ONLINE && !waitingForAck);
        assert(proximityUpdateState == ProximityUpdateState::CHECKING);
        assert(proximitySampleCount == 0 && espNowFallbackPending && !automaticSelectionPending);
        assert(selectedTransport == Transport::ESP_NOW && ccWire.empty());
        const auto began = checkStartedAt;
        mockedTxInFlight = 0; loop(); // Measurement, not callback drain, now holds fallback.
        assert(espNowFallbackPending && selectedTransport == Transport::ESP_NOW);

        // Start the next periodic transaction through loop(), then keep it live
        // across classification. Neither recovery nor a median can retarget it.
        suppressPeriodicForTest = false; hostNow = nextEventTime; loop();
        suppressPeriodicForTest = true;
        const auto active = pendingMessage;
        assert(waitingForAck && pendingTransport == Transport::ESP_NOW && active.messageId != failed.messageId);
        classificationSample(910, -52);
        rssiObservations.push_back(proximityObservation(910, -20, hostNow)); loop(); // Duplicate ID rejected.
        classificationSample(911, -54);
        assert(proximitySampleCount == 2 && espNowFallbackPending && selectedTransport == Transport::ESP_NOW);
        const auto duringCheck = proximityObservation(915, -53, hostNow).message;
        receive(duringCheck); receive(duringCheck);
        assert(checkStartedAt == began && proximitySampleCount == 2);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        hostNow = ackWaitStart + ACK_TIMEOUT_MS; loop();
        assert(retryCount == 1 && memcmp(&pendingMessage, &active, sizeof(active)) == 0);
        assert(pendingTransport == Transport::ESP_NOW);
        classificationSample(912, -53);
        assertProximityReset();
        assert(proximityClassification == ProximityClassification::CLOSE && !espNowFallbackPending);
        assert(waitingForAck && pendingTransport == Transport::ESP_NOW && memcmp(&pendingMessage, &active, sizeof(active)) == 0);
        receive(incoming(Type::Ack, active.messageId));
        assert(!waitingForAck && !automaticSelectionPending && selectedTransport == Transport::ESP_NOW);

        const auto peerEvent = proximityObservation(920, -52, hostNow).message;
        receive(peerEvent); receive(peerEvent); // New and duplicate ONLINE traffic cannot restart the check.
        assertProximityReset();
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | COMPLETE") == 1);
        assert(Serial.log.find("PROXIMITY | median=-53 dBm | CLOSE") != std::string::npos);
        assert(Serial.log.find("APP TRANSPORT FALLBACK") == std::string::npos && ccWire.empty());
        assert(hostNow - began < CHECK_TIMEOUT_MS);
        suppressPeriodicForTest = false; hostNow = nextEventTime; loop();
        assert(waitingForAck && pendingTransport == Transport::ESP_NOW && pendingMessage.messageId != active.messageId);
    }
    puts("PASS: pending ESP-NOW fallback + OFFLINE->ONLINE EVENT/matched ACK starts one bounded check; fresh CLOSE median cancels fallback, real periodic transactions/retries retain radio and ID, ONLINE traffic does not restart");
}

void testPendingFallbackRecoveryTimeout()
{
    for (auto prior : {ProximityClassification::UNKNOWN, ProximityClassification::CLOSE, ProximityClassification::FAR})
    for (unsigned samples : {0U, 2U})
    {
        freshKnownApp(prior); requestFallbackFromEvent();
        receive(proximityObservation(930, -52, hostNow).message);
        assert(proximityUpdateState == ProximityUpdateState::CHECKING);
        const auto began = checkStartedAt;
        mockedTxInFlight = 0; loop();
        for (unsigned i = 0; i < samples; ++i) classificationSample(931 + i, -53);
        assert(proximityClassification == prior && espNowFallbackPending);
        radioAccepts = false; // Any remaining UNKNOWN probes fail; no invented samples or restart.
        mockedTxInFlight = 1;
        hostNow = began + CHECK_TIMEOUT_MS; loop();
        assertProximityReset();
        assert(proximityClassification == prior && espNowFallbackPending && !automaticSelectionPending);
        assert(selectedTransport == Transport::ESP_NOW && ccWire.empty());
        for (unsigned i = 0; i < 10; ++i) loop(); // Physical drain still protects selection after timeout.
        assert(espNowFallbackPending && selectedTransport == Transport::ESP_NOW);
        mockedTxInFlight = 0; radioAccepts = true; loop();
        assert(selectedTransport == Transport::CC1101 && !espNowFallbackPending && ccWire.empty());
        assert(proximityClassification == prior && peerState() == PeerState::ONLINE);
        receive(proximityObservation(940, -52, hostNow).message);
        for (unsigned i = 0; i < 100; ++i) loop();
        assertProximityReset();
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | TIMEOUT") == 1);
        assert(occurrences(Serial.log, "APP TRANSPORT FALLBACK") == 1);
    }
    puts("PASS: pending-fallback recovery zero/partial/failed measurement retains UNKNOWN/CLOSE/FAR, bounded timeout and callback-drained fallback; no replay or repeated ONLINE recheck");
}

void testPendingFallbackRecoveryEligibility()
{
    for (unsigned guard = 0; guard < 2; ++guard)
    {
        freshKnownApp(); requestFallbackFromEvent();
        if (guard == 1) buttonHeartbeatPending = true;
        receive(proximityObservation(950, -53, hostNow).message);
        assert(peerState() == PeerState::ONLINE && selectedTransport == Transport::ESP_NOW && espNowFallbackPending);
        assert(localState() == LocalState::ACTIVE && movementState == MovementState::READY);
        if (guard == 0)
            assert(proximityUpdateState == ProximityUpdateState::CHECKING);
        else
        {
            assertProximityReset();
            assert(Serial.log.find("PROXIMITY CHECK | START") == std::string::npos);
            assert(buttonHeartbeatPending && !waitingForAck);
        }
    }
    puts("PASS: pending-fallback recovery preserves ACTIVE-only and button-priority measurement admission");
}

void testFallbackRecovery()
{
    freshKnownApp(); const auto failed = requestFallbackFromEvent();
    mockedTxInFlight = 0; loop();
    assert(selectedTransport == Transport::CC1101 && peerState() == PeerState::OFFLINE);
    assert(proximityClassification == ProximityClassification::CLOSE);
    assert(displayFrames.back().radio == "CC1101" && displayFrames.back().peer == "OFFLINE");
    assert(displayFrames.back().distance == "CLOSE" && pendingTransport == Transport::ESP_NOW);
    suppressPeriodicForTest = false; hostNow = nextEventTime; loop();
    const auto recovery = pendingMessage;
    assert(waitingForAck && pendingTransport == Transport::CC1101 && recovery.messageId != failed.messageId);
    assert(ccWire.size() == 1 && memcmp(&ccWire.back(), &recovery, sizeof(recovery)) == 0);
    receiveVia(incoming(Type::Ack, recovery.messageId), Transport::CC1101);
    assert(!waitingForAck && peerState() == PeerState::ONLINE);
    assert(proximityClassification == ProximityClassification::CLOSE);
    assert(proximityUpdateState == ProximityUpdateState::CHECKING && countWire(Type::ProximityProbe) == 1);
    // The outstanding probe defers OLED updates until a normal safe boundary.
    assert(displayFrames.back().radio == "CC1101" && displayFrames.back().peer == "OFFLINE");
    suppressPeriodicForTest = true;
    completePeerReturnMeasurement(-50); // The matched CC1101 ACK starts this check without movement.
    assert(countWire(Type::ProximityProbe) == 3 && proximityClassification == ProximityClassification::CLOSE);
    assert(selectedTransport == Transport::ESP_NOW && !automaticSelectionPending && !espNowFallbackPending);
    assert(displayFrames.back().radio == "ESP-NOW" && displayFrames.back().distance == "CLOSE");
    startHeartbeatEvent(); assert(pendingTransport == Transport::ESP_NOW);
    receive(incoming(Type::Ack, pendingMessage.messageId)); assert(!waitingForAck && peerState() == PeerState::ONLINE);
    assert(occurrences(Serial.log, "APP TRANSPORT FALLBACK") == 1 && occurrences(Serial.log, "APP TRANSPORT AUTO") == 1);
    puts("PASS: ESP-NOW exhaustion -> CC1101 fallback -> matched CC1101 ACK starts recovery check -> CLOSE -> ESP-NOW EVENT/ACK and OLED recovery, without movement");
}

void testPeerReturnEvents()
{
    for (auto evidenceRadio : {Transport::ESP_NOW, Transport::CC1101})
    for (int8_t median : {int8_t(-50), int8_t(-85)})
    {
        freshApp(); requestFallbackFromEvent(); mockedTxInFlight = 0; loop();
        assert(peerState() == PeerState::OFFLINE && selectedTransport == Transport::CC1101);
        assert(displayFrames.back().peer == "OFFLINE" && displayFrames.back().distance == "UNKNOWN");
        const auto event = proximityObservation(1, -47, hostNow).message;
        receiveVia(event, evidenceRadio); // Real EVENT path, including applicationEvent()'s early ONLINE update.
        assert(peerState() == PeerState::ONLINE && proximityUpdateState == ProximityUpdateState::CHECKING);
        assert(selectedTransport == Transport::CC1101 && proximityClassification == ProximityClassification::UNKNOWN);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1 && countWire(Type::ProximityProbe) == 1);
        // A strong RSSI on the returning EVENT cannot stand in for a correlated probe median.
        rssiObservations.push_back(proximityObservation(1, -47, hostNow)); loop();
        assert(proximitySampleCount == 0 && proximityClassification == ProximityClassification::UNKNOWN);
        completePeerReturnMeasurement(median);
        assert(countWire(Type::ProximityProbe) == 3 && peerState() == PeerState::ONLINE);
        assert(displayFrames.back().peer == "ONLINE");
        assert(displayFrames.back().distance == (median <= ENTER_FAR_DBM ? "FAR" : "CLOSE"));
        assert(displayFrames.back().radio == (median <= ENTER_FAR_DBM ? "CC1101" : "ESP-NOW"));
        startHeartbeatEvent(); assert(pendingTransport == selectedTransport);
        receiveVia(incoming(Type::Ack, pendingMessage.messageId), pendingTransport);
        assert(!waitingForAck && occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
    }
    // Transition detection changes check state only; the receive handler sends no probe itself.
    freshApp(); selectedTransport = Transport::CC1101; notePeerUnreachable();
    const auto event = proximityObservation(20, -47, hostNow).message;
    handleReceivedData(reinterpret_cast<const uint8_t*>(&event), sizeof(event));
    assert(proximityUpdateState == ProximityUpdateState::CHECKING && countWire(Type::ProximityProbe) == 0);
    loop(); assert(countWire(Type::ProximityProbe) == 1);
    puts("PASS: peer return via ESP-NOW/CC1101 EVENT starts one loop-owned probe check; flash scenario, CLOSE/FAR policy, OLED sequence and future EVENT routes without movement");
}

void testPeerReturnIsolation()
{
    freshApp(); selectedTransport = Transport::CC1101; notePeerUnreachable(); startHeartbeatEvent();
    const auto id = pendingMessage.messageId;
    receiveVia(incoming(Type::Ack, id), Transport::ESP_NOW); // Right ID, wrong radio.
    receiveVia(incoming(Type::Ack, id + 1), Transport::CC1101);
    auto malformedAck = incoming(Type::Ack, id); malformedAck.event = Protocol::EventType::Heartbeat;
    receiveVia(malformedAck, Transport::CC1101);
    assert(waitingForAck && peerState() == PeerState::OFFLINE && proximityUpdateState == ProximityUpdateState::READY);
    receiveVia(incoming(Type::Ack, id), Transport::CC1101);
    assert(!waitingForAck && peerState() == PeerState::ONLINE && proximityUpdateState == ProximityUpdateState::CHECKING);
    classificationSample(860, -100);
    const auto began = checkStartedAt;
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        const auto event = proximityObservation(21, -47, hostNow).message;
        receiveVia(event, radio); receiveVia(event, radio); // New/duplicate real evidence while already ONLINE.
        startHeartbeatEvent(); receiveVia(incoming(Type::Ack, pendingMessage.messageId), Transport::CC1101);
        assert(checkStartedAt == began && proximitySampleCount == 1);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
    }
    classificationSample(861, -85); classificationSample(862, -40); loop();
    receiveVia(proximityObservation(22, -47, hostNow).message, Transport::CC1101);
    assert(selectedTransport == Transport::CC1101 && proximityUpdateState == ProximityUpdateState::READY);
    assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1); // No recurring monitor after completion.

    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    for (auto type : {Type::ProximityProbe, Type::ProximityProbeReply, Type::SleepRequest,
                      Type::SleepReady, Type::SleepCommit, Type::SleepAck, Type::SleepCancel, Type::Ack})
    {
        freshApp(); selectedTransport = Transport::CC1101; notePeerUnreachable();
        const auto packet = incoming(type, type == Type::ProximityProbe ? 0 : 23, 23);
        receiveVia(packet, radio);
        assert(proximityUpdateState == ProximityUpdateState::READY && proximityClassification == ProximityClassification::UNKNOWN);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 0 && selectedTransport == Transport::CC1101);
    }
    for (unsigned invalid = 0; invalid < 6; ++invalid)
    {
        freshApp(); selectedTransport = Transport::CC1101; notePeerUnreachable();
        auto packet = proximityObservation(23, -47, hostNow).message;
        if (invalid == 0) ++packet.version;
        if (invalid == 1) packet.sender = LOCAL_DEVICE;
        if (invalid == 2) packet.event = Protocol::EventType::None;
        if (invalid == 3) packet.ackForMessageId = 1;
        if (invalid == 4) packet.type = static_cast<Type>(99);
        handleReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet) - (invalid == 5));
        assert(peerState() == PeerState::OFFLINE && proximityUpdateState == ProximityUpdateState::READY);
        assert(countWire(Type::ProximityProbe) == 0 && occurrences(Serial.log, "PROXIMITY CHECK | START") == 0);
    }
    // First contact starts an initial check; OFFLINE recovery still requires CC1101.
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    for (bool offline : {false, true})
    {
        freshApp(); selectedTransport = radio;
        if (offline) notePeerUnreachable();
        startHeartbeatEvent(); receiveVia(incoming(Type::Ack, pendingMessage.messageId), radio);
        assert((proximityUpdateState == ProximityUpdateState::CHECKING) == (!offline || radio == Transport::CC1101));
    }
    freshApp(); selectedTransport = Transport::CC1101; notePeerUnreachable(); loop();
    receiveVia(proximityObservation(24, -47, hostNow).message, Transport::CC1101);
    assert(peerState() == PeerState::ONLINE && localState() == LocalState::ACTIVE);
    assert(proximityUpdateState == ProximityUpdateState::CHECKING); // Stationary remains eligible while ACTIVE.
    freshApp(); selectedTransport = Transport::CC1101; loop(); requestSleepForTest();
    receive(incoming(Type::Ack, pendingMessage.messageId)); // Matching sleep-control receipt is not application evidence.
    assert(localState() == LocalState::SLEEP_NEGOTIATING && proximityUpdateState == ProximityUpdateState::READY);
    puts("PASS: no restart for already-ONLINE EVENTs/ACKs; probes, sleep controls/receipts, wrong/unrelated/malformed ACKs and malformed EVENTs cannot trigger; ACTIVE/CC1101 guards retained");
}

void testPeerReturnTimeout()
{
    for (auto prior : {ProximityClassification::UNKNOWN, ProximityClassification::CLOSE, ProximityClassification::FAR})
    for (unsigned samples : {0U, 2U})
    {
        freshApp();
        if (prior != ProximityClassification::UNKNOWN)
            completePolicyMeasurement(prior == ProximityClassification::CLOSE ? -50 : -85);
        selectTransportForTest(Transport::CC1101); notePeerUnreachable();
        receiveVia(proximityObservation(30, -47, hostNow).message, Transport::CC1101);
        const auto began = checkStartedAt;
        const auto starts = occurrences(Serial.log, "PROXIMITY CHECK | START");
        for (unsigned i = 0; i < samples; ++i) classificationSample(870 + i, -40);
        radioAccepts = false; // Remaining probe attempts fail, without manufacturing distance.
        for (uint32_t elapsed = PROBE_REPLY_WAIT_MS; elapsed < CHECK_TIMEOUT_MS; elapsed += PROBE_REPLY_WAIT_MS)
        {
            hostNow = began + elapsed; loop();
            assert(checkStartedAt == began && proximitySampleCount == samples);
        }
        hostNow = began + CHECK_TIMEOUT_MS; loop();
        assertProximityReset();
        assert(selectedTransport == Transport::CC1101 && proximityClassification == prior);
        assert(peerState() == PeerState::ONLINE && !automaticSelectionPending && !espNowFallbackPending);
        const auto probes = countWire(Type::ProximityProbe);
        for (unsigned i = 0; i < 100; ++i) loop();
        receiveVia(proximityObservation(31, -47, hostNow).message, Transport::CC1101);
        assert(countWire(Type::ProximityProbe) == probes && occurrences(Serial.log, "PROXIMITY CHECK | START") == starts);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | TIMEOUT") == 1);
    }
    puts("PASS: peer-return probe failure/partial timeout stays bounded to 12s, preserves UNKNOWN/CLOSE/FAR and CC1101, never repeats while ONLINE");
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    runCase(testEspNowFallback);
    runCase(testFallbackPrecedenceAndSerial);
    runCase(testFallbackRecovery);
    runCase(testPendingFallbackRecovery);
    runCase(testPendingFallbackRecoveryTimeout);
    runCase(testPendingFallbackRecoveryEligibility);
    runCase(testPeerReturnEvents);
    runCase(testPeerReturnIsolation);
    runCase(testPeerReturnTimeout);
    finishSuite("peer_recovery");
}
