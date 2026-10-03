#include "../fixtures/scenario_helpers.h"

void testRssiDiagnostics()
{
    freshApp();
    ESPNowRadio::RssiObservation observation{};
    observation.message = {Protocol::VERSION, Type::Event, 42, PEER_DEVICE, Protocol::EventType::Heartbeat, 0};
    observation.rssi = -61; observation.receivedAt = UINT32_MAX - 5;
    rssiObservations.push_back(observation); refillRssi = true;
    hostNow = 20;
    loop();
    assert(rssiReads == 2 && rssiObservations.size() == 1); // Continuous producer cannot extend the batch.
    assert(occurrences(Serial.log, "ESPNOW RSSI |") == 2);
    assert(Serial.log.find("rssi=-61 dBm | age=26 ms") != std::string::npos);
    assert(nextMessageId == 1 && wire.empty() && !haveLastPeerEvent && wakeEvents.empty());
    assert(localState() == LocalState::ACTIVE && peerState() == PeerState::UNKNOWN);

    // Same traffic timeline with and without observations, including duplicate
    // EVENT delivery, matched ACK, and an exhausted 300 ms / two-retry episode.
    std::vector<Protocol::Message> baselineWire;
    std::string baselineState;
    for (bool diagnostics : {false, true})
    {
        freshApp(); ackWaitStart = 0; pendingMessage = {};
        if (diagnostics) { rssiObservations.push_back(observation); refillRssi = true; }
        std::string states;
        for (unsigned step = 0; step < 7; ++step)
        {
            hostNow = step * 300;
            if (step == 0) receive(observation.message);
            if (step == 1) receive(observation.message);
            if (step == 2)
            {
                startHeartbeatEvent();
                receive(incoming(Type::Ack, pendingMessage.messageId));
            }
            if (step == 3) startHeartbeatEvent();
            loop();
            states += std::to_string(nextMessageId) + ":" + std::to_string(waitingForAck) + ":" +
                std::to_string(retryCount) + ":" + std::to_string(nextEventTime) + ":" +
                std::to_string(controlCount) + ":" + std::to_string(lastPeerEventId) + ":" +
                std::to_string(static_cast<int>(peerState())) + ":" +
                std::to_string(static_cast<int>(localState())) + ";";
            PowerManager::SleepDecision decision{};
            assert(!transaction().active && !PowerManager::takeSleepDecision(decision));
            assert(wakeEvents.empty() && physicalSleeps == 0 && movementState == MovementState::READY);
        }
        assert(!waitingForAck && lastPeerEventId == 42 && haveLastPeerEvent);
        if (!diagnostics) { baselineState = states; baselineWire = wire; }
        else
        {
            assert(states == baselineState && wire.size() == baselineWire.size());
            for (size_t i = 0; i < wire.size(); ++i)
                assert(memcmp(&wire[i], &baselineWire[i], sizeof(Protocol::Message)) == 0);
        }
    }
    freshApp(); loop(); requestSleepForTest();
    const auto sleep = transaction();
    const auto id = nextMessageId, pendingId = pendingMessage.messageId;
    const auto sent = wire.size(), queued = controlCount;
    const auto peer = peerState();
    auto controlObservation = observation;
    controlObservation.message = incoming(Type::SleepCancel, sleep.sleepId);
    rssiObservations.push_back(controlObservation);
    loop();
    assert(localState() == LocalState::SLEEP_NEGOTIATING && peerState() == peer);
    assert(transaction().active && transaction().sleepId == sleep.sleepId && transaction().phase == sleep.phase);
    assert(transaction().phaseDeadline == sleep.phaseDeadline && transaction().hardDeadline == sleep.hardDeadline);
    assert(nextMessageId == id && waitingForAck && pendingMessage.messageId == pendingId && retryCount == 0);
    assert(wire.size() == sent && controlCount == queued);
    completedAwaitingCallbacks(false);
    rssiObservations.assign(4, observation);
    mockedTxInFlight = 0;
    const auto reads = rssiReads;
    loop();
    assert(physicalSleeps == 1 && rssiObservations.size() == 4 && rssiReads == reads);
    freshApp(); protocolReady = false;
    rssiObservations.push_back(observation); loop();
    assert(rssiReads == 0 && rssiObservations.size() == 1);
    puts("PASS: RSSI diagnostics bounded, no EVENT execution/ACK/IDs/power effects, retries unchanged, backlog does not defer sleep");
}

void testProximitySamples()
{
    freshApp(); assertProximityReset();
    movementStep(0, MotionEvent::Activity); movementStep(10, MotionEvent::Inactivity);
    // A prior partial batch must be cleared when a new operation starts.
    proximitySampleCount = 2; proximitySamples[0] = {40, -30}; proximitySamples[1] = {41, -31};
    rssiObservations.push_back(proximityObservation(1, -10, 1009));
    rssiObservations.push_back(proximityObservation(2, -11, 1010));
    movementStep(1010); // Strictly older AND equal-timestamp queued samples excluded.
    assert(proximityUpdateState == ProximityUpdateState::CHECKING && checkStartedAt == 1010 && proximitySampleCount == 0);
    for (const auto& sample : proximitySamples) assert(sample.messageId == 0 && sample.rssi == 0);
    assert(rssiObservations.empty());
    for (unsigned i = 0; i < 5; ++i) loop();
    assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
    auto wrongPeer = proximityObservation(9, -20, 1090);
    wrongPeer.message.sender = LOCAL_DEVICE;
    rssiObservations.push_back(wrongPeer);
    rssiObservations.push_back(proximityObservation(10, -20, 1200)); // Future timestamp.
    movementStep(1090); assert(proximitySampleCount == 0);
    rssiObservations.push_back(proximityObservation(65535, -52, 1100));
    movementStep(1110); assert(proximitySampleCount == 1);
    rssiObservations.push_back(proximityObservation(65535, -99, 1120));
    rssiObservations.push_back(proximityObservation(0, -67, 1121));
    movementStep(1130); assert(proximitySampleCount == 2 && checkStartedAt == 1010);
    assert(proximitySamples[0].rssi == -52); // Retry neither replaces nor adds a sample.
    startProximityCheck(1140); // Repeated start cannot extend an active operation.
    assert(checkStartedAt == 1010 && proximitySampleCount == 2);
    rssiObservations.push_back(proximityObservation(65535, -20, 1141));
    rssiObservations.push_back(proximityObservation(0, -21, 1142));
    movementStep(1150); assert(proximitySampleCount == 2);
    rssiObservations.push_back(proximityObservation(1, -54, 1160));
    movementStep(1170); assertProximityReset();
    assert(occurrences(Serial.log, "PROXIMITY CHECK | SAMPLE") == 3);
    assert(Serial.log.find("COMPLETE | samples=3 | median=-54 dBm") != std::string::npos);
    rssiObservations.push_back(proximityObservation(2, -40, 1180));
    movementStep(1190); assertProximityReset();
    assert(occurrences(Serial.log, "PROXIMITY CHECK | COMPLETE") == 1);

    const int8_t cases[][4] = {{-67,-54,-52,-54}, {-52,-54,-67,-54}, {-52,-67,-54,-54},
                              {-54,-52,-67,-54}, {-90,-53,-53,-53}, {-128,-127,-1,-127}};
    for (const auto& values : cases)
    {
        freshApp(); settleForCheck(2000);
        for (unsigned i = 0; i < 3; ++i)
            rssiObservations.push_back(proximityObservation(i, values[i], 2010 + i));
        const auto reads = rssiReads;
        movementStep(2020);
        assert(rssiReads == reads + 2 && proximitySampleCount == 2 && rssiObservations.size() == 1);
        assert(Serial.log.find("PROXIMITY CHECK | COMPLETE") == std::string::npos);
        movementStep(2030); assertProximityReset();
        assert(Serial.log.find("median=" + std::to_string(values[3]) + " dBm") != std::string::npos);
    }
    puts("PASS: SETTLED starts once, strict capture-time freshness, message-ID dedup/wrap, signed median and two-observation drain");
}

void testProximityTimeout()
{
    for (uint32_t start : {2000U, UINT32_MAX - 500U})
    for (unsigned count = 0; count < 3; ++count)
    {
        freshApp(); settleForCheck(start);
        rssiObservations.push_back(proximityObservation(99, -10, start - 1));
        movementStep(uint32_t(start + 10)); assert(proximitySampleCount == 0);
        if (count > 0)
        {
            rssiObservations.push_back(proximityObservation(1, -50, uint32_t(start + 100)));
            movementStep(uint32_t(start + 100)); assert(proximitySampleCount == 1);
        }
        if (count > 1)
            rssiObservations.push_back(proximityObservation(2, -60, uint32_t(start + CHECK_TIMEOUT_MS - 1)));
        movementStep(uint32_t(start + CHECK_TIMEOUT_MS - 1));
        assert(proximityUpdateState == ProximityUpdateState::CHECKING && proximitySampleCount == count);
        assert(checkStartedAt == start && Serial.log.find("PROXIMITY CHECK | TIMEOUT") == std::string::npos);
        // Even a third frame captured before deadline cannot complete if drained at expiry.
        rssiObservations.push_back(proximityObservation(3, -55, uint32_t(start + CHECK_TIMEOUT_MS - 1)));
        movementStep(uint32_t(start + CHECK_TIMEOUT_MS)); assertProximityReset();
        assert(Serial.log.find("TIMEOUT | samples=" + std::to_string(count)) != std::string::npos);
        for (unsigned i = 0; i < 10; ++i) loop();
        assert(occurrences(Serial.log, "PROXIMITY CHECK | TIMEOUT") == 1);
        assert(Serial.log.find("PROXIMITY CHECK | COMPLETE") == std::string::npos);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
    }
    // Successful capture and completion across millis rollover too.
    freshApp(); const uint32_t start = UINT32_MAX - 10; settleForCheck(start);
    for (unsigned i = 0; i < 3; ++i)
        rssiObservations.push_back(proximityObservation(i, -50 - i, uint32_t(start + 20 + i)));
    movementStep(uint32_t(start + 30)); movementStep(uint32_t(start + 40)); assertProximityReset();
    assert(Serial.log.find("median=-51 dBm") != std::string::npos);
    puts("PASS: absolute 12s timeout, partial progress cannot extend, boundary precedence and rollover freshness/completion");
}

void testProximityCancellation()
{
    for (uint32_t age : {100U, CHECK_TIMEOUT_MS})
    {
        freshApp(); settleForCheck(2000);
        rssiObservations.push_back(proximityObservation(1, -50, 2020)); movementStep(2020);
        rssiObservations.push_back(proximityObservation(2, -60, 2000 + age - 1));
        rssiObservations.push_back(proximityObservation(3, -55, 2000 + age - 1));
        movementStep(2000 + age, MotionEvent::Activity); assertProximityReset();
        assert(movementState == MovementState::MOVING);
        assert(occurrences(Serial.log, "CANCELLED | reason=MOVEMENT") == 1);
        assert(Serial.log.find("PROXIMITY CHECK | TIMEOUT") == std::string::npos);
        assert(Serial.log.find("PROXIMITY CHECK | COMPLETE") == std::string::npos);
        const uint32_t restart = 2000 + age + SETTLE_MS + 20;
        movementStep(restart - SETTLE_MS, MotionEvent::Inactivity); movementStep(restart);
        assert(checkStartedAt == restart && proximitySampleCount == 0);
        for (unsigned i = 1; i <= 3; ++i) // Same IDs are valid again in a new check.
            rssiObservations.push_back(proximityObservation(i, -50 - i, restart + 10));
        movementStep(restart + 20); movementStep(restart + 30); assertProximityReset();
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 2);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | COMPLETE") == 1);
    }
    freshApp(); settleForCheck(2000); notePeerUnreachable(); loop(); assertProximityReset();
    assert(peerState() == PeerState::OFFLINE && localState() == LocalState::ACTIVE);
    assert(occurrences(Serial.log, "CANCELLED | reason=PEER_OFFLINE") == 1);
    for (auto state : {LocalState::SLEEP_NEGOTIATING, LocalState::SLEEPING, LocalState::WAKING})
    {
        freshApp();
        if (state == LocalState::SLEEPING || state == LocalState::WAKING)
        {
            completedAwaitingCallbacks(false);
            if (state == LocalState::WAKING) injectActivity(hostNow);
        }
        else { loop(); if (state == LocalState::SLEEP_NEGOTIATING) requestSleepForTest(); }
        proximityUpdateState = ProximityUpdateState::CHECKING; checkStartedAt = hostNow;
        proximitySampleCount = 1; proximitySamples[0] = {1, -60};
        loop(); assertProximityReset(); assert(localState() == state);
        assert(occurrences(Serial.log, "CANCELLED | reason=NOT_ACTIVE") == 1);
    }
    for (bool prepareFails : {false, true})
    {
        freshApp(); settleForCheck(2000);
        motionPrepareOk = !prepareFails;
        failedSleepEntryForTest(); assertProximityReset();
        assert(motionPreparations == 1 && motionCancels == 1 && physicalSleeps == 0);
        assert(coordinatedAttempts == (prepareFails ? 0U : 1U));
        assert(localState() == LocalState::ACTIVE && Serial.log.find("CANCELLED | reason=SLEEP") != std::string::npos);
    }
    freshApp(); completedAwaitingCallbacks(false);
    proximityUpdateState = ProximityUpdateState::CHECKING; checkStartedAt = hostNow;
    proximitySampleCount = 1; proximitySamples[0] = {1, -60};
    mockedTxInFlight = 0; loop(); assertProximityReset(); assert(physicalSleeps == 1);
    freshApp(); settleForCheck(2000); protocolReady = false; loop(); assertProximityReset();
    puts("PASS: Activity beats timeout/queued completion, fresh restart, offline/non-ACTIVE/sleep/runtime cancellations");
}

void testProximityIsolation()
{
    std::vector<Protocol::Message> baselineWire;
    std::string baselineState;
    for (bool measuring : {false, true})
    {
        freshKnownApp(); ackWaitStart = 0; pendingMessage = {};
        if (measuring) settleForCheck(2000);
        hostNow = 2020; suppressPeriodicForTest = false; nextEventTime = 2050;
        std::string state;
        for (uint32_t now : {2050U, 2070U, 2100U, 2110U, 2120U, 2200U, 6200U, 6500U, 6800U, 7100U})
        {
            hostNow = now;
            if (now == 2070) receive(incoming(Type::Ack, pendingMessage.messageId));
            if (now == 2100 || now == 2110)
                receive(proximityObservation(42, -50, now).message); // New EVENT then duplicate.
            if (measuring && (now == 2100 || now == 2110 || now == 2120))
                rssiObservations.push_back(proximityObservation(now, -50 - (now % 3), now));
            loop();
            state += std::to_string(nextMessageId) + ":" + std::to_string(waitingForAck) + ":" +
                std::to_string(retryCount) + ":" + std::to_string(nextEventTime) + ":" +
                std::to_string(controlCount) + ":" + std::to_string(lastPeerEventId) + ":" +
                std::to_string(static_cast<int>(peerState())) + ":" + std::to_string(static_cast<int>(localState())) + ";";
            assert(!transaction().active && wakeEvents.empty() && physicalSleeps == 0);
        }
        if (!measuring) { baselineState = state; baselineWire = wire; }
        else
        {
            assert(state == baselineState && wire.size() == baselineWire.size());
            for (size_t i = 0; i < wire.size(); ++i)
                assert(memcmp(&wire[i], &baselineWire[i], sizeof(Protocol::Message)) == 0);
            assert(occurrences(Serial.log, "PROXIMITY CHECK | COMPLETE") == 1);
        }
    }
    puts("PASS: completed measurement preserves normal EVENT/ACK/dedup/retry, heartbeat schedule, message IDs and power/peer state");
}

void testProximityProbes()
{
    for (auto transport : {Transport::ESP_NOW, Transport::CC1101})
    for (uint32_t start : {uint32_t(2000), UINT32_MAX - 100})
    {
        freshApp(); selectedTransport = transport; settleForCheck(start);
        const auto first = wire.back();
        assert(first.version == Protocol::VERSION && first.type == Type::ProximityProbe &&
               first.sender == LOCAL_DEVICE && first.event == Protocol::EventType::None && first.ackForMessageId == 0);
        movementStep(start + 499); assert(wire.size() == 1);
        movementStep(start + 500); assert(wire.size() == 2 && probeMessageId != first.messageId);
        auto reply = proximityObservation(100, -55, start + 501);
        reply.message.type = Type::ProximityProbeReply; reply.message.event = Protocol::EventType::None;
        reply.message.ackForMessageId = first.messageId;
        sampleProximity(reply, start + 502); assert(proximitySampleCount == 0);
        reply.message.ackForMessageId = probeMessageId;
        for (unsigned fault = 0; fault < 6; ++fault)
        {
            auto bad = reply;
            if (fault == 0) bad.message.sender = LOCAL_DEVICE;
            if (fault == 1) bad.message.event = Protocol::EventType::Heartbeat;
            if (fault == 2)
            {
                if (transport == Transport::CC1101) bad.message.type = Type::Ack;
                else ++bad.message.ackForMessageId; // ESP-NOW may use ordinary traffic, but replies must correlate.
            }
            if (fault == 3) bad.receivedAt = start;
            if (fault == 4) bad.receivedAt = start + 499;
            if (fault == 5) bad.message.version = 0;
            sampleProximity(bad, start + 502); assert(proximitySampleCount == 0 && probeOutstanding);
        }
        // Observer first, normal callback later: no callback-side correlation required.
        sampleProximity(reply, start + 502); assert(proximitySampleCount == 1 && !probeOutstanding);
        const auto sent = wire.size();
        handleReceivedData(reinterpret_cast<const uint8_t*>(&reply.message), sizeof(reply.message)); assert(wire.size() == sent);
        assert(peerState() == PeerState::UNKNOWN && !haveLastPeerEvent && !waitingForAck);
        movementStep(start + 510); assert(probeOutstanding);
        movementStep(start + 511, MotionEvent::Activity); assertProximityReset();
        sampleProximity(reply, start + 512); assertProximityReset();
        settleForCheck(start + 2000);
        reply.receivedAt = start + 2001;
        sampleProximity(reply, start + 2002); assert(proximitySampleCount == 0);
        movementStep(start + 2000 + CHECK_TIMEOUT_MS); assertProximityReset();
    }
    freshApp(); selectedTransport = Transport::CC1101; radioAccepts = false; settleForCheck(2000);
    assert(!probeOutstanding && probeTimerActive && wire.size() == 1);
    for (uint32_t now = 2010; now < 2500; now += 10) movementStep(now);
    assert(wire.size() == 1);
    movementStep(2500); assert(wire.size() == 2 && wire[0].messageId != wire[1].messageId);
    protocolReady = false; loop(); assertProximityReset();

    for (auto state : {LocalState::ACTIVE, LocalState::SLEEP_NEGOTIATING,
                       LocalState::SLEEPING, LocalState::WAKING})
    {
        freshApp(); selectedTransport = Transport::CC1101;
        if (state == LocalState::SLEEPING || state == LocalState::WAKING)
        {
            completedAwaitingCallbacks(false);
            if (state == LocalState::WAKING) injectActivity(hostNow);
        }
        else if (state != LocalState::ACTIVE) { loop(); if (state == LocalState::SLEEP_NEGOTIATING) requestSleepForTest(); }
        assert(localState() == state);
        selectedTransport = Transport::CC1101;
        const auto peer = peerState(); const auto sent = wire.size(); const auto id = nextMessageId;
        const auto pending = pendingMessage; const auto waiting = waitingForAck; const auto retries = retryCount;
        const auto route = pendingTransport;
        auto probe = incoming(Type::ProximityProbe, 0, 432);
        handleReceivedData(reinterpret_cast<const uint8_t*>(&probe), sizeof(probe), Transport::CC1101);
        auto reply = incoming(Type::ProximityProbeReply, 432, 433);
        handleReceivedData(reinterpret_cast<const uint8_t*>(&reply), sizeof(reply), Transport::CC1101);
        assert(wire.size() == sent && nextMessageId == id);
        handleReceivedData(reinterpret_cast<const uint8_t*>(&probe), sizeof(probe));
        const bool allowed = state == LocalState::ACTIVE;
        assert(wire.size() == sent + unsigned(allowed) && nextMessageId == uint16_t(id + unsigned(allowed)));
        if (allowed)
        {
            const auto& response = wire.back();
            assert(response.type == Type::ProximityProbeReply && response.ackForMessageId == 432 &&
                   response.messageId == id && response.sender == LOCAL_DEVICE &&
                   response.event == Protocol::EventType::None && response.version == Protocol::VERSION);
        }
        assert(localState() == state && peerState() == peer && !haveLastPeerEvent);
        assert(waitingForAck == waiting && retryCount == retries && memcmp(&pending, &pendingMessage, 8) == 0);
        assert(selectedTransport == Transport::CC1101 && pendingTransport == route && ccWire.empty());
    }
    puts("PASS: bounded ESP-NOW probes, rejection pacing, rollover, stale/malformed/duplicate correlation, cancellation and awake-only replies");
}

void completeClassifiedMeasurement(int8_t median)
{
    serviceAutomaticTransportSelection(); // Apply any previous completed measurement before starting this one.
    const auto prior = proximityClassification;
    const auto logs = occurrences(Serial.log, "PROXIMITY | median=");
    const auto selected = selectedTransport, pendingRoute = pendingTransport;
    const auto pending = pendingMessage;
    const auto waiting = waitingForAck; const auto retries = retryCount;
    const auto ackStart = ackWaitStart, nextEvent = nextEventTime;
    const auto local = localState(); const auto peer = peerState();
    settleForCheck(hostNow + 2000);
    classificationSample(500, -100);
    assert(proximitySampleCount == 1 && proximityClassification == prior);
    classificationSample(501, median);
    assert(proximitySampleCount == 2 && proximityClassification == prior);
    assert(occurrences(Serial.log, "PROXIMITY | median=") == logs);
    classificationSample(502, -40); // Classification must use the median, not this last raw sample.
    assertProximityReset();
    assert(automaticSelectionPending); // Application selection is deliberately deferred until the next loop.
    assert(occurrences(Serial.log, "PROXIMITY | median=") == logs + 1);
    const std::string label = proximityClassification == ProximityClassification::FAR ? "FAR" : "CLOSE";
    assert(Serial.log.find("PROXIMITY | median=" + std::to_string(median) + " dBm | " + label) != std::string::npos);
    assert(selectedTransport == selected && pendingTransport == pendingRoute);
    assert(memcmp(&pendingMessage, &pending, sizeof(pending)) == 0 && waitingForAck == waiting && retryCount == retries);
    const auto expectedNext = prior == ProximityClassification::UNKNOWN && !waiting ?
        hostNow - 10 + automaticHeartbeatIntervalMs() : nextEvent;
    assert(ackWaitStart == ackStart && nextEventTime == expectedNext && localState() == local && peerState() == peer);
}

void testProximityClassification()
{
    using Class = ProximityClassification;
    struct Case { Class prior; int8_t median; Class expected; };
    const Case cases[] = {
        {Class::UNKNOWN, -79, Class::CLOSE}, {Class::UNKNOWN, -80, Class::FAR},
        {Class::CLOSE, -79, Class::CLOSE}, {Class::CLOSE, -80, Class::FAR}, {Class::CLOSE, -81, Class::FAR},
        {Class::FAR, -76, Class::FAR}, {Class::FAR, -75, Class::CLOSE}, {Class::FAR, -74, Class::CLOSE},
        {Class::CLOSE, -78, Class::CLOSE}, {Class::CLOSE, -77, Class::CLOSE}, {Class::CLOSE, -76, Class::CLOSE},
        {Class::FAR, -79, Class::FAR}, {Class::FAR, -78, Class::FAR}, {Class::FAR, -77, Class::FAR},
        {Class::CLOSE, -67, Class::CLOSE}, {Class::CLOSE, -68, Class::CLOSE}
    };
    for (auto transport : {Transport::ESP_NOW, Transport::CC1101})
    {
        for (const auto& test : cases)
        {
            freshApp(); selectedTransport = transport;
            assert(proximityClassification == Class::UNKNOWN);
            // Raw observations outside CHECKING do not invent an initial distance.
            rssiObservations.push_back(proximityObservation(99, -100, hostNow));
            loop(); assert(proximityClassification == Class::UNKNOWN && !automaticSelectionPending);
            if (test.prior != Class::UNKNOWN)
                completeClassifiedMeasurement(test.prior == Class::FAR ? -85 : -50);
            assert(proximityClassification == test.prior);
            selectTransportForTest(transport); // Exercise each sampling mode independently of prior policy.
            completeClassifiedMeasurement(test.median);
            assert(proximityClassification == test.expected);
        }
        for (auto prior : {Class::UNKNOWN, Class::CLOSE, Class::FAR})
        for (unsigned samples = 0; samples < 3; ++samples)
        for (bool timeout : {false, true})
        {
            freshApp(); selectedTransport = transport;
            if (prior != Class::UNKNOWN) completeClassifiedMeasurement(prior == Class::FAR ? -85 : -50);
            selectTransportForTest(transport); // Consume prior policy; failures must not request new policy.
            const auto logs = occurrences(Serial.log, "PROXIMITY | median=");
            if (timeout && samples == 0) radioAccepts = false; // Failed probes also retain the prior state.
            settleForCheck(hostNow + 2000);
            const uint32_t began = checkStartedAt;
            for (unsigned i = 0; i < samples; ++i)
            {
                classificationSample(600 + i, prior == Class::FAR ? -40 : -100);
                assert(proximitySampleCount == i + 1 && proximityClassification == prior);
                assert(!automaticSelectionPending);
            }
            if (timeout) movementStep(began + CHECK_TIMEOUT_MS);
            else movementStep(hostNow + 10, MotionEvent::Activity);
            assertProximityReset();
            assert(proximityClassification == prior && occurrences(Serial.log, "PROXIMITY | median=") == logs);
            assert(!automaticSelectionPending && selectedTransport == transport);
        }
    }
    // A reboot starts UNKNOWN even when RTC history is restored.
    freshApp(); completeClassifiedMeasurement(-85); saveRtcHistory();
    protocolReady = false; injectedBoot.deep = true; setup();
    assert(rtcRestored && proximityClassification == Class::UNKNOWN);
    assert(selectedTransport == Transport::ESP_NOW && !automaticSelectionPending);
    puts("PASS: UNKNOWN initialization, conservative CLOSE/FAR boundaries, full hysteresis band and median-only updates on both transports");
    puts("PASS: raw/partial/failed/cancelled/timeout checks retain classification, transport/reliability/power isolation, RAM-only reboot reset");
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    runCase(testRssiDiagnostics);
    runCase(testProximitySamples);
    runCase(testProximityTimeout);
    runCase(testProximityCancellation);
    runCase(testProximityIsolation);
    runCase(testProximityProbes);
    runCase(testProximityClassification);
    finishSuite("proximity_measurement");
}
