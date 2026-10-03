#include "../fixtures/scenario_helpers.h"

void testAutomaticHeartbeatCadence()
{
    static_assert(FIRST_EVENT_DELAY_MS == 1000, "startup heartbeat delay changed");
    using Class = ProximityClassification;
    struct Case { Class classification; uint32_t interval; };
    const Case cases[]{{Class::CLOSE, 2500}, {Class::FAR, 6000}};
    for (const auto& test : cases)
    for (auto transport : {Transport::ESP_NOW, Transport::CC1101})
    for (bool acknowledged : {false, true})
    {
        freshApp(); notePeerSeen(); selectedTransport = transport;
        proximityClassification = test.classification;
        suppressPeriodicForTest = false; nextEventTime = FIRST_EVENT_DELAY_MS;
        hostNow = 999; loop();
        assert(!waitingForAck && wire.empty() && ccWire.empty() && led.requests == 0);
        hostNow = 1000; loop();
        const auto original = pendingMessage;
        assert(waitingForAck && pendingTransport == transport && ackWaitStart == 1000 && led.requests == 1);
        const uint32_t completedAt = acknowledged ? 1100 : 1900;
        if (acknowledged)
        {
            hostNow = completedAt; receiveVia(incoming(Type::Ack, original.messageId), transport);
        }
        else
        {
            for (unsigned retry = 1; retry <= 2; ++retry)
            {
                const uint32_t retryAt = 1000 + retry * 300;
                hostNow = retryAt - 1; loop();
                assert(waitingForAck && retryCount == retry - 1);
                hostNow = retryAt; loop();
                assert(waitingForAck && retryCount == retry && ackWaitStart == retryAt);
                assert(memcmp(&pendingMessage, &original, sizeof(original)) == 0 && led.requests == 1);
            }
            hostNow = completedAt; loop();
            assert(selectedTransport == Transport::CC1101); // Includes real ESP-NOW failure fallback.
        }
        assert(!waitingForAck && retryCount == 0 && led.requests == 1);
        assert(proximityClassification == test.classification && nextEventTime == completedAt + test.interval);
        assert(Serial.log.find("PROXIMITY CHECK | START") == std::string::npos);
        const auto due = nextEventTime;
        if (test.classification == Class::FAR)
        {
            hostNow = 6999; loop(); assert(led.requests == 1 && !waitingForAck);
            hostNow = 7000; loop(); assert(led.requests == 2 && !waitingForAck);
            assert(nextEventTime == due); // The visual tick does not reschedule radio traffic.
        }
        hostNow = due - 1; loop();
        assert(!waitingForAck && led.requests == (test.classification == Class::FAR ? 2U : 1U) && nextEventTime == due);
        assert(memcmp(&pendingMessage, &original, sizeof(original)) == 0);
        hostNow = due; loop();
        assert(waitingForAck && ackWaitStart == due && led.requests == 2);
        assert(pendingMessage.type == Type::Event && pendingMessage.messageId != original.messageId);
        assert(pendingTransport == selectedTransport);
    }
    puts("PASS: radio EVENT intervals remain CLOSE=2500ms/FAR=6000ms after ACK/exhaustion; FAR visual repeats at 7000ms independently; first delay=1000ms, CLOSE fallback keeps 2500ms, no cadence-driven probes");
}

void testAutomaticSelection()
{
    for (auto initial : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshApp(); selectTransportForTest(initial);
        for (unsigned i = 0; i < 20; ++i) loop();
        assert(proximityClassification == ProximityClassification::UNKNOWN && selectedTransport == initial);
        assert(!automaticSelectionPending && Serial.log.find("APP TRANSPORT AUTO") == std::string::npos);
        automaticSelectionPending = true; // Defensive UNKNOWN service path.
        serviceAutomaticTransportSelection();
        assert(!automaticSelectionPending && selectedTransport == initial);

        const auto target = initial == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW;
        completePolicyMeasurement(target == Transport::CC1101 ? -85 : -50);
        const auto pending = pendingMessage; const auto pendingRoute = pendingTransport;
        const auto ackStart = ackWaitStart; const auto id = nextMessageId;
        loop();
        assert(selectedTransport == target && !automaticSelectionPending);
        assert(pendingTransport == pendingRoute && memcmp(&pendingMessage, &pending, sizeof(pending)) == 0);
        assert(!waitingForAck && retryCount == 0 && ackWaitStart == ackStart && nextMessageId == id);
        const std::string expected = target == Transport::CC1101 ?
            "APP TRANSPORT AUTO | selected=CC1101 | proximity=FAR" :
            "APP TRANSPORT AUTO | selected=ESP-NOW | proximity=CLOSE";
        assert(occurrences(Serial.log, expected) == 1);
        for (unsigned i = 0; i < 20; ++i) loop();
        assert(occurrences(Serial.log, "APP TRANSPORT AUTO") == 1);
        suppressPeriodicForTest = false; nextEventTime = hostNow;
        loop(); // Service runs before a due automatic EVENT, which snapshots the applied selection.
        assert(waitingForAck && pendingTransport == target);
        const auto& sent = target == Transport::ESP_NOW ? wire : ccWire;
        assert(sent.back().type == Type::Event && sent.back().messageId == pendingMessage.messageId);
    }
    // Repeated valid measurements matching the applied route clear silently.
    freshApp(); completePolicyMeasurement(-50); loop();
    completePolicyMeasurement(-50); loop();
    assert(!automaticSelectionPending && Serial.log.find("APP TRANSPORT AUTO") == std::string::npos);

    // The newer completed classification replaces the older intent while busy.
    for (auto initial : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshApp(); selectedTransport = initial; mockedTxInFlight = 1;
        completePolicyMeasurement(initial == Transport::ESP_NOW ? -85 : -50);
        completePolicyMeasurement(initial == Transport::ESP_NOW ? -50 : -85);
        mockedTxInFlight = 0; loop();
        assert(selectedTransport == initial && !automaticSelectionPending);
        assert(Serial.log.find("APP TRANSPORT AUTO") == std::string::npos);
    }
    puts("PASS: UNKNOWN no-op, CLOSE/FAR future EVENT routes, one-loop application, latest completed classification and silent matching policy");
}

void testAutomaticSelectionRetries()
{
    for (auto initial : {Transport::ESP_NOW, Transport::CC1101})
    for (bool acknowledged : {false, true})
    for (bool classified : {false, true})
    {
        freshApp(); selectedTransport = initial;
        if (classified)
            proximityClassification = initial == Transport::ESP_NOW ?
                ProximityClassification::CLOSE : ProximityClassification::FAR;
        startHeartbeatEvent();
        const auto original = pendingMessage; const auto began = ackWaitStart;
        const auto scheduled = nextEventTime;
        const auto target = initial == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW;
        completePolicyMeasurement(target == Transport::CC1101 ? -85 : -50);
        assert(proximityClassification == (target == Transport::CC1101 ?
            ProximityClassification::FAR : ProximityClassification::CLOSE));
        assert(nextEventTime == scheduled && led.requests == unsigned(classified));
        assert(waitingForAck && pendingTransport == initial && retryCount == 0 && ackWaitStart == began);
        assert(memcmp(&pendingMessage, &original, sizeof(original)) == 0);
        receiveVia(incoming(Type::Ack, original.messageId), target); // Wrong radio cannot finish the EVENT.
        assert(waitingForAck && automaticSelectionPending && selectedTransport == initial);
        hostNow = began + ACK_TIMEOUT_MS - 1; loop(); assert(retryCount == 0 && ackWaitStart == began);
        for (unsigned retry = 1; retry <= MAX_RETRIES; ++retry)
        {
            const auto due = ackWaitStart + ACK_TIMEOUT_MS;
            hostNow = due; loop();
            assert(retryCount == retry && ackWaitStart == due && pendingTransport == initial);
            assert(selectedTransport == initial && automaticSelectionPending);
            assert(memcmp(&pendingMessage, &original, sizeof(original)) == 0);
            assert(nextEventTime == scheduled && led.requests == unsigned(classified));
        }
        const auto& sent = initial == Transport::ESP_NOW ? wire : ccWire;
        unsigned events = 0;
        for (const auto& packet : sent)
            if (packet.type == Type::Event) { ++events; assert(memcmp(&packet, &original, sizeof(original)) == 0); }
        assert(events == 3);
        const auto& other = initial == Transport::ESP_NOW ? ccWire : wire;
        for (const auto& packet : other) assert(packet.type != Type::Event);

        mockedTxInFlight = 1; // Semantic completion alone is not the drained switch boundary.
        const auto completedAt = acknowledged ? hostNow : ackWaitStart + ACK_TIMEOUT_MS;
        if (acknowledged) receiveVia(incoming(Type::Ack, original.messageId), initial);
        else { hostNow = ackWaitStart + ACK_TIMEOUT_MS; loop(); }
        assert(nextEventTime == completedAt + (target == Transport::CC1101 ? 6000U : 2500U));
        assert(led.requests == unsigned(classified));
        const bool fallback = !acknowledged && initial == Transport::ESP_NOW;
        assert(!waitingForAck && retryCount == 0 && automaticSelectionPending == !fallback && selectedTransport == initial);
        assert(espNowFallbackPending == fallback);
        assert(pendingTransport == initial && memcmp(&pendingMessage, &original, sizeof(original)) == 0);
        assert(peerState() == (acknowledged ? PeerState::ONLINE : PeerState::OFFLINE));
        mockedTxInFlight = 0; suppressPeriodicForTest = false; nextEventTime = hostNow;
        loop();
        assert(selectedTransport == target && pendingTransport == target && waitingForAck);
        assert(pendingMessage.messageId != original.messageId && !automaticSelectionPending);
        assert(occurrences(Serial.log, "APP TRANSPORT AUTO") == (fallback ? 0U : 1U));
        assert(occurrences(Serial.log, "APP TRANSPORT FALLBACK") == (fallback ? 1U : 0U));
    }
    // Failures never manufacture FAR/classifier requests; only ESP-NOW exhaustion falls back.
    for (auto initial : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshApp(); selectedTransport = initial; radioAccepts = ccAccepts = false;
        startHeartbeatEvent();
        for (unsigned i = 0; i <= MAX_RETRIES; ++i) { hostNow = ackWaitStart + ACK_TIMEOUT_MS; loop(); }
        assert(peerState() == PeerState::OFFLINE && !waitingForAck && !automaticSelectionPending);
        assert(proximityClassification == ProximityClassification::UNKNOWN && selectedTransport == Transport::CC1101);
        assert(!espNowFallbackPending);
        assert(occurrences(Serial.log, "APP TRANSPORT FALLBACK") == (initial == Transport::ESP_NOW ? 1U : 0U));
        assert(Serial.log.find("APP TRANSPORT AUTO") == std::string::npos);
    }
    puts("PASS: both pending EVENT routes and exact retries/ACK deadlines survive policy changes; ACK/drain or exhaustion/drain precedes selection");
    puts("PASS: FAR -> CLOSE and CLOSE -> FAR use 2500/6000ms at the next ACK/exhaustion without rescheduling the in-flight EVENT or replaying its LED");
}

void testAutomaticSelectionGuards()
{
    for (bool fallback : {false, true})
    for (unsigned guard = 0; guard < 11; ++guard)
    {
        freshApp();
        if (fallback)
        {
            requestFallbackFromEvent();
            // Real peer evidence restores check eligibility; pending TX still holds fallback.
            receive(proximityObservation(900, -50, hostNow).message);
            assert(proximityUpdateState == ProximityUpdateState::CHECKING);
            // Recovery now starts a check. Let its bounded timeout finish so
            // each guard below is still tested independently.
            hostNow = checkStartedAt + CHECK_TIMEOUT_MS; loop();
            assertProximityReset();
            mockedTxInFlight = 0;
        }
        else completePolicyMeasurement(-85);
        const auto savedQueue = receiveQueue;
        const auto packet = incoming(Type::Ack, 999);
        switch (guard)
        {
            case 0: protocolReady = false; break;
            case 1: startHeartbeatEvent(); break;
            case 2: controlCount = 1; break;
            case 3: PowerManager::requestSleep(nextMessageId++, hostNow); break;
            case 4: sleepDrainWaiting = true; break;
            case 5: awakeAckBusy = true; break;
            case 6: mockedTxInFlight = 1; break;
            case 7: mockedRxActive = true; break;
            case 8: receiveQueue = nullptr; break;
            case 9: queueReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)); break;
            case 10: startProximityCheck(hostNow); break;
        }
        const auto log = Serial.log;
        for (unsigned i = 0; i < 100; ++i) serviceAutomaticTransportSelection();
        assert(automaticSelectionPending == !fallback && espNowFallbackPending == fallback);
        assert(selectedTransport == Transport::ESP_NOW && Serial.log == log);
        switch (guard)
        {
            case 0: protocolReady = true; break;
            case 1: handleAck(incoming(Type::Ack, pendingMessage.messageId), Transport::ESP_NOW); break;
            case 2: controlCount = 0; break;
            case 3: PowerManager::injectActivity(hostNow); discardObsoleteControls(); break;
            case 4: sleepDrainWaiting = false; break;
            case 5: awakeAckBusy = false; break;
            case 6: mockedTxInFlight = 0; break;
            case 7: mockedRxActive = false; break;
            case 8: receiveQueue = savedQueue; break;
            case 9: { Protocol::Message out{}; assert(xQueueReceive(receiveQueue, &out, 0) == pdPASS); break; }
            case 10: cancelProximityCheck("TEST"); break; // Keeps the earlier valid request; creates no new one.
        }
        serviceAutomaticTransportSelection();
        assert(selectedTransport == Transport::CC1101 && !automaticSelectionPending && !espNowFallbackPending);
        for (unsigned i = 0; i < 100; ++i) serviceAutomaticTransportSelection();
        assert(occurrences(Serial.log, fallback ? "APP TRANSPORT FALLBACK" : "APP TRANSPORT AUTO") == 1);
        assert(Serial.log.find("APP TRANSPORT | REFUSED") == std::string::npos);
    }
    puts("PASS: classifier and fallback share every awake/drained/CHECKING guard, wait silently and apply once");
}

void testAutomaticSelectionIgnoresSerial()
{
    for (auto initial : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshApp(); selectedTransport = initial;
        completePolicyMeasurement(initial == Transport::ESP_NOW ? -50 : -85);
        typeSerialForTest(initial == Transport::ESP_NOW ? "c" : "e");
        assert(selectedTransport == initial && !automaticSelectionPending);
        assert(Serial.log.find("APP TRANSPORT | selected=") == std::string::npos);

        freshApp(); selectedTransport = initial;
        completePolicyMeasurement(initial == Transport::ESP_NOW ? -85 : -50);
        const auto target = initial == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW;
        startHeartbeatEvent(); const auto event = pendingMessage;
        typeSerialForTest(target == Transport::CC1101 ? "c" : "e");
        assert(selectedTransport == initial && automaticSelectionPending && waitingForAck);
        assert(memcmp(&event, &pendingMessage, sizeof(event)) == 0);
        receiveVia(incoming(Type::Ack, event.messageId), initial);
        assert(selectedTransport == target && !automaticSelectionPending);
        assert(occurrences(Serial.log, "APP TRANSPORT AUTO") == 1);
    }
    puts("PASS: e/c cannot override classifier evidence or consume pending policy; in-flight route stays pinned until ACK");
}

void testAutomaticSelectionSleep()
{
    // CLOSE/FAR requests from either radio, plus an exhausted ESP-NOW EVENT's fallback.
    for (unsigned request = 0; request < 3; ++request)
    for (bool wakeInstead : {false, true})
    {
        const bool fallback = request == 2;
        const auto initial = request == 1 ? Transport::CC1101 : Transport::ESP_NOW;
        freshApp(); selectedTransport = initial;
        if (fallback) requestFallbackFromEvent();
        else completePolicyMeasurement(initial == Transport::ESP_NOW ? -85 : -50);
        mockedTxInFlight = 1; // Hold the previous TX while requesting sleep.
        loop(); requestSleepForTest();
        const auto id = transaction().sleepId;
        const auto phaseDeadline = transaction().phaseDeadline, hardDeadline = transaction().hardDeadline;
        mockedTxInFlight = 0;
        for (unsigned i = 0; i < 10; ++i) loop();
        assert(transaction().phaseDeadline == phaseDeadline && transaction().hardDeadline == hardDeadline);
        assert(localState() == LocalState::SLEEP_NEGOTIATING && selectedTransport == initial);
        assert(automaticSelectionPending == !fallback && espNowFallbackPending == fallback);
        assert(pendingMessage.type == Type::SleepRequest && pendingTransport == Transport::ESP_NOW);
        receive(incoming(Type::Ack, pendingMessage.messageId));
        receive(incoming(Type::SleepReady, id));
        assert(pendingMessage.type == Type::SleepCommit && pendingTransport == Transport::ESP_NOW);
        receive(incoming(Type::Ack, pendingMessage.messageId));
        mockedTxInFlight = 1;
        receive(incoming(Type::SleepAck, id));
        assert(localState() == LocalState::SLEEPING && physicalSleeps == 0);
        assert(automaticSelectionPending == !fallback && espNowFallbackPending == fallback);
        assert(selectedTransport == initial && ccWire.empty());
        assert(Serial.log.find("APP TRANSPORT AUTO") == std::string::npos);
        assert(Serial.log.find("APP TRANSPORT FALLBACK") == std::string::npos);
        mockedTxInFlight = 0;
        if (wakeInstead)
        {
            PowerManager::injectActivity(hostNow);
            loop(); assert(localState() == LocalState::WAKING && selectedTransport == initial);
            assert(automaticSelectionPending == !fallback && espNowFallbackPending == fallback);
            hostNow += 250; loop();
            assert(localState() == LocalState::ACTIVE && selectedTransport != initial && !automaticSelectionPending);
            assert(!espNowFallbackPending);
        }
        else
        {
            assert(sleepTransportBlockedReason() == nullptr); // Pending policy is not a sleep blocker.
            loop(); assert(physicalSleeps == 1 && selectedTransport == initial);
            assert(automaticSelectionPending == !fallback && espNowFallbackPending == fallback);
            protocolReady = false; injectedBoot.deep = true; setup();
            assert(rtcRestored && proximityClassification == ProximityClassification::UNKNOWN);
            assert(selectedTransport == Transport::ESP_NOW && !automaticSelectionPending && !espNowFallbackPending);
        }
    }
    puts("PASS: classifier/fallback wait through negotiation/SLEEPING/WAKING without changing sleep routes/deadlines or blocking physical entry; reboot clears both");
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    runCase(testAutomaticHeartbeatCadence);
    runCase(testAutomaticSelection);
    runCase(testAutomaticSelectionRetries);
    runCase(testAutomaticSelectionGuards);
    runCase(testAutomaticSelectionIgnoresSerial);
    runCase(testAutomaticSelectionSleep);
    finishSuite("radio_selection");
}
