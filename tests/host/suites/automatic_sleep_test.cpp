#include "../fixtures/scenario_helpers.h"

void testAutomaticSleepClock()
{
    static_assert(AUTOMATIC_SLEEP_INACTIVITY_MS == 35000, "product inactivity duration changed");
    for (bool deep : {false, true})
    for (bool runtimeOk : {false, true})
    {
        freshApp(); protocolReady = false; delete receiveQueue; receiveQueue = nullptr;
        injectedBoot.deep = deep; radioStarts = runtimeOk; hostNow = 123;
        setup();
        assert(localState() == LocalState::ACTIVE && automaticSleepArmed == runtimeOk);
        if (!runtimeOk)
        {
            hostNow += 100000; loop();
            assert(!transaction().active && !automaticSleepArmed);
            continue;
        }
        const auto began = hostNow;
        assert(lastMeaningfulActivity == began && uint32_t(hostNow - lastMeaningfulActivity) == 0);
        hostNow = began + CHECK_TIMEOUT_MS; loop(); // Existing startup probe timeout completes normally.
        hostNow = began + 34999; loop();
        assert(localState() == LocalState::ACTIVE && automaticSleepArmed && countWire(Type::SleepRequest) == 0);
        hostNow = began + 35000; loop();
        assert(transaction().active && transaction().startedAt == began + 35000);
        assert(transaction().role == SleepRole::COORDINATOR && !automaticSleepArmed);
        assert(countWire(Type::SleepRequest) == 1 && lastMeaningfulActivity == began);
    }
    for (uint32_t start : {0U, UINT32_MAX - 31000})
    {
        freshAutomaticRuntime(start);
        movementStep(start + 30000, MotionEvent::Activity);
        assert(lastMeaningfulActivity == start + 30000 && automaticSleepArmed);
        movementStep(start + 33000, MotionEvent::Inactivity);
        assert(lastMeaningfulActivity == start + 30000 && movementState == MovementState::WAITING);
        movementStep(start + 34000);
        assert(proximityUpdateState == ProximityUpdateState::CHECKING);
        movementStep(start + 46000); // Measurement ends without changing the activity clock.
        movementStep(start + 64999);
        assert(!transaction().active && automaticSleepArmed && lastMeaningfulActivity == start + 30000);
        movementStep(start + 65000);
        assert(transaction().active && transaction().startedAt == start + 65000 && !automaticSleepArmed);
    }
    puts("PASS: cold/deep runtime inactivity starts at zero; 34999ms stays ACTIVE, 35000ms starts once; Activity at 30s moves eligibility to 65s, Inactivity/settlement/proximity do not restart it, including rollover");
}

void testActiveActivityBookkeeping()
{
    for (uint32_t start : {0U, UINT32_MAX - 500})
    {
        freshAutomaticRuntime(start); automaticSleepArmed = false;
        movementStep(start + 100, MotionEvent::Inactivity);
        assert(localState() == LocalState::ACTIVE && !automaticSleepArmed && lastMeaningfulActivity == start);
        movementStep(start + 200, MotionEvent::Activity);
        assert(localState() == LocalState::ACTIVE && automaticSleepArmed && lastMeaningfulActivity == start + 200);
        automaticSleepArmed = false;
        movementStep(start + 300, MotionEvent::Activity);
        assert(localState() == LocalState::ACTIVE && automaticSleepArmed && lastMeaningfulActivity == start + 300);
        automaticSleepArmed = false;
        buttonLevel = LOW; hostNow = start + 600; loop();
        hostNow = start + 630; loop();
        assert(localState() == LocalState::ACTIVE && automaticSleepArmed && lastMeaningfulActivity == start + 630);
        assert(Serial.log.find("POWER: ACTIVE ->") == std::string::npos);
        assert(!transaction().active && countWire(Type::SleepRequest) == 0);
    }
    puts("PASS: stationary, repeated motion and button activity stay ACTIVE; only meaningful activity refreshes the timer and rearms, even with no state transition and across rollover");
}

void testAutomaticSleepBackgroundTraffic()
{
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshAutomaticRuntime(); proximityClassification = ProximityClassification::CLOSE;
        selectedTransport = radio; suppressPeriodicForTest = false; nextEventTime = 1000;
        uint16_t peerId = 10000, firstEvent = 0;
        bool sawRetry = false;
        for (uint32_t now = 0; now <= 35000; now += 50)
        {
            hostNow = now;
            if (now % 2500 == 0)
            {
                const Protocol::Message heartbeat{Protocol::VERSION, Type::Event, peerId++, PEER_DEVICE,
                                                   Protocol::EventType::Heartbeat, 0};
                queueReceivedData(reinterpret_cast<const uint8_t*>(&heartbeat), sizeof(heartbeat));
            }
            if (waitingForAck && pendingMessage.type == Type::Event)
            {
                if (!firstEvent) firstEvent = pendingMessage.messageId;
                if (pendingMessage.messageId != firstEvent || retryCount != 0)
                {
                    sawRetry = sawRetry || retryCount != 0;
                    const auto ack = incoming(Type::Ack, pendingMessage.messageId);
                    if (pendingTransport == Transport::CC1101) ccIncoming.push_back(ack);
                    else queueReceivedData(reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
                }
            }
            if (now == 10000)
            {
                const auto probe = incoming(Type::ProximityProbe, 0, peerId++);
                queueReceivedData(reinterpret_cast<const uint8_t*>(&probe), sizeof(probe));
                auto observation = proximityObservation(peerId++, -50, now);
                rssiObservations.push_back(observation);
            }
            loop();
            assert(lastMeaningfulActivity == 0);
            if (now < 35000) assert(automaticSleepArmed && !transaction().active);
        }
        assert(sawRetry && !automaticSleepArmed && transaction().startedAt == 35000);
        assert(countWire(Type::SleepRequest) == 1 && countWire(Type::ProximityProbeReply) == 1);
        assert(led.requests > 10 && !displayFrames.empty());
        const auto sleep = transaction();
        const auto next = nextMessageId;
        const Protocol::Message heartbeat{Protocol::VERSION, Type::Event, peerId++, PEER_DEVICE,
                                           Protocol::EventType::Heartbeat, 0};
        receiveVia(heartbeat, radio); receiveVia(heartbeat, radio);
        assert(transaction().active && transaction().sleepId == sleep.sleepId && peerState() == PeerState::SLEEP_PENDING);
        assert(transaction().phaseDeadline == sleep.phaseDeadline && transaction().hardDeadline == sleep.hardDeadline);
        assert(lastMeaningfulActivity == 0 && !automaticSleepArmed && countWire(Type::SleepCancel) == 0);
        assert(nextMessageId == uint16_t(next + 2)); // Normal receipts for new + duplicate EVENT.
    }
    freshAutomaticRuntime(); hostNow = 1000;
    startProximityCheck(hostNow); loop();
    classificationSample(800, -85); classificationSample(801, -85); classificationSample(802, -85);
    loop();
    assert(proximityClassification == ProximityClassification::FAR && selectedTransport == Transport::CC1101);
    assert(lastMeaningfulActivity == 0 && automaticSleepArmed && !displayFrames.empty());
    puts("PASS: periodic local/remote Heartbeats, LED, duplicates/ACKs/retries, both radios, probe/RSSI/classification/transport/OLED work do not reset inactivity; Heartbeats preserve accepted negotiation and peer sleep intent");
}

void testAutomaticSleepDeferral()
{
    for (unsigned busy = 0; busy < 14; ++busy)
    {
        freshAutomaticRuntime(); hostNow = 35000;
        QueueHandle_t savedQueue = receiveQueue;
        switch (busy)
        {
            case 0: protocolReady = false; break;
            case 1: motionReady = false; break;
            case 2: movementState = MovementState::MOVING; break;
            case 3: movementState = MovementState::WAITING; settleStartedAt = hostNow; break;
            case 4: startProximityCheck(hostNow); break;
            case 5: startHeartbeatEvent(); break;
            case 6: controlCount = 1; break;
            case 7: awakeAckBusy = true; break;
            case 8: mockedTxInFlight = 1; break;
            case 9: mockedRxActive = true; break;
            case 10:
            {
                const auto packet = incoming(Type::ProximityProbe, 0, 99);
                queueReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)); break;
            }
            case 11: assert(requestSleep(nextMessageId++, hostNow)); break;
            case 12:
                assert(requestSleep(nextMessageId++, hostNow));
                PowerManager::injectActivity(hostNow); break;
            case 13: receiveQueue = nullptr; break;
        }
        const auto id = nextMessageId;
        const auto requests = countWire(Type::SleepRequest);
        for (unsigned i = 0; i < 100; ++i) serviceAutomaticSleep();
        assert(automaticSleepArmed && lastMeaningfulActivity == 0 && nextMessageId == id);
        assert(countWire(Type::SleepRequest) == requests);
        switch (busy)
        {
            case 0: protocolReady = true; break;
            case 1: motionReady = true; break;
            case 2: case 3: resetMovement(); break;
            case 4: resetProximityCheck(); break;
            case 5: handleAck(incoming(Type::Ack, pendingMessage.messageId), pendingTransport); break;
            case 6: controlCount = 0; break;
            case 7: awakeAckBusy = false; break;
            case 8: mockedTxInFlight = 0; break;
            case 9: mockedRxActive = false; break;
            case 10:
            {
                Protocol::Message packet{};
                assert(xQueueReceive(receiveQueue, &packet, 0) == pdPASS);
                handleReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)); break;
            }
            case 11: PowerManager::injectActivity(hostNow); // Exercise the guard, without creating a physical Activity.
                // fall through
            case 12: hostNow += 3000; PowerManager::update(hostNow); break;
            case 13: receiveQueue = savedQueue; break;
        }
        serviceAutomaticSleep(); sendNextControl();
        assert(transaction().active && !automaticSleepArmed && transaction().role == SleepRole::COORDINATOR);
        assert(countWire(Type::SleepRequest) == requests + 1);
        for (unsigned i = 0; i < 100; ++i) serviceAutomaticSleep();
        assert(countWire(Type::SleepRequest) == requests + 1);
    }
    puts("PASS: runtime/sensor, MOVING/WAITING/proximity, pending ACK/control, CC1101, ESP-NOW TX/callback/RX, active transaction and cooldown defer without consuming an ID or automatic opportunity; drain permits exactly one attempt");
}

void testAutomaticSleepFailures()
{
    for (unsigned outcome = 0; outcome < 5; ++outcome)
    for (auto peer : {PeerState::UNKNOWN, PeerState::ONLINE, PeerState::OFFLINE})
    {
        freshAutomaticRuntime();
        if (peer == PeerState::ONLINE) notePeerSeen();
        if (peer == PeerState::OFFLINE) notePeerUnreachable();
        hostNow = 35000; loop();
        const auto request = pendingMessage;
        assert(!automaticSleepArmed && transaction().active && request.type == Type::SleepRequest);
        if (outcome == 0)
        {
            for (uint32_t elapsed : {300U, 600U, 900U}) { hostNow = 35000 + elapsed; loop(); }
            assert(countWire(Type::SleepRequest) == 3 && peerState() == PeerState::OFFLINE);
            for (const auto& packet : wire) if (packet.type == Type::SleepRequest)
                assert(memcmp(&packet, &request, sizeof(packet)) == 0);
        }
        else if (outcome == 1) receive(incoming(Type::SleepCancel, request.messageId));
        else
        {
            receive(incoming(Type::Ack, request.messageId));
            if (outcome == 2) { hostNow = 38000; loop(); }
            else
            {
                hostNow = 37999; receive(incoming(Type::SleepReady, request.messageId));
                receive(incoming(Type::Ack, pendingMessage.messageId));
                if (outcome == 3) { hostNow = 40000; loop(); }
                else { entryFails = true; receive(incoming(Type::SleepAck, request.messageId)); }
            }
        }
        assert(!transaction().active && localState() == LocalState::ACTIVE && !automaticSleepArmed);
        assert(cooldownLeftMs(hostNow) > 0 && physicalSleeps == 0 && lastMeaningfulActivity == 0);
        const auto requests = countWire(Type::SleepRequest);
        for (unsigned i = 0; i < 100; ++i) { hostNow += 1000; loop(); }
        assert(!transaction().active && !automaticSleepArmed && countWire(Type::SleepRequest) == requests);
        assert(cooldownLeftMs(hostNow) == 0);
        const auto activity = hostNow;
        movementStep(activity, MotionEvent::Activity);
        assert(automaticSleepArmed && lastMeaningfulActivity == activity && localState() == LocalState::ACTIVE);
        movementStep(activity + 3000, MotionEvent::Inactivity);
        movementStep(activity + 4000); movementStep(activity + 16000);
        movementStep(activity + 34999); assert(!transaction().active);
        movementStep(activity + 35000);
        assert(transaction().active && !automaticSleepArmed && countWire(Type::SleepRequest) == requests + 1);
    }
    puts("PASS: UNKNOWN/OFFLINE/ONLINE attempts remain bounded by identical retries, refusal, phase/hard deadline or physical failure; cooldown never rearms, 100 later loops do not retry, new Activity permits one attempt 35s later");
}

void testAutomaticSleepAdmission()
{
    static_assert(AUTOMATIC_SLEEP_PEER_GRACE_MS == 3000, "participant inactivity grace changed");
    // Stationary ACTIVE cannot waive the recipient's own inactivity requirement.
    freshAutomaticRuntime(); loop(); hostNow = 31999;
    receive(incoming(Type::SleepRequest, 90));
    assert(localState() == LocalState::ACTIVE && !transaction().active && automaticSleepArmed);
    assert(countWire(Type::SleepCancel) == 1 && countWire(Type::SleepReady) == 0);
    freshAutomaticRuntime(); hostNow = 31999;
    receive(incoming(Type::SleepRequest, 100));
    assert(!transaction().active && localState() == LocalState::ACTIVE && automaticSleepArmed);
    assert(countWire(Type::Ack) == 1 && countWire(Type::SleepCancel) == 1 && countWire(Type::SleepReady) == 0);
    hostNow = 32000; holdEspReceipts = true;
    receive(incoming(Type::SleepRequest, 101));
    assert(transaction().active && transaction().role == SleepRole::PARTICIPANT && !automaticSleepArmed);
    assert(transaction().startedAt == 32000 && mockedTxInFlight == 1 && countWire(Type::SleepReady) == 1);
    const auto hard = transaction().hardDeadline;
    receive(incoming(Type::SleepRequest, 101)); // Own receipt/control TX cannot block an accepted transaction.
    receive(incoming(Type::SleepCommit, 101, 102));
    assert(localState() == LocalState::SLEEPING && countWire(Type::SleepAck) == 1 && physicalSleeps == 0);
    assert(lastMeaningfulActivity == 0 && !automaticSleepArmed && hard == 37000);
    receive(incoming(Type::Ack, pendingMessage.messageId));
    holdEspReceipts = false; mockedTxInFlight = 0; loop();
    assert(physicalSleeps == 1);

    for (unsigned busy = 0; busy < 12; ++busy)
    {
        freshAutomaticRuntime(); hostNow = 32000;
        if (busy == 0) motionReady = false;
        if (busy == 1) movementState = MovementState::MOVING;
        if (busy == 2) movementState = MovementState::WAITING;
        if (busy == 3) startProximityCheck(hostNow);
        if (busy == 4) startHeartbeatEvent();
        if (busy == 5) awakeAckBusy = true;
        if (busy == 6) mockedTxInFlight = 1;
        if (busy == 7) mockedRxActive = true;
        if (busy == 8)
        {
            const auto queued = incoming(Type::ProximityProbe, 0, 99);
            queueReceivedData(reinterpret_cast<const uint8_t*>(&queued), sizeof(queued));
        }
        if (busy == 9) controlCount = 1;
        if (busy == 10)
        {
            assert(requestSleep(nextMessageId++, hostNow));
            PowerManager::injectActivity(hostNow);
        }
        if (busy == 11) protocolReady = false;
        const auto request = incoming(Type::SleepRequest, 100);
        handleReceivedData(reinterpret_cast<const uint8_t*>(&request), sizeof(request));
        assert(!transaction().active && automaticSleepArmed && countWire(Type::SleepReady) == 0);
        // Runtime failure cannot enqueue/send a CANCEL; receipt behavior is unchanged.
        assert(countWire(Type::SleepCancel) == (busy == 10 ? 2U : busy == 11 ? 0U : 1U));
        assert(countWire(Type::Ack) == 1);
    }
    // Stale requests cannot gain admission when the inactivity boundary passes.
    freshAutomaticRuntime(); hostNow = 31999; receive(incoming(Type::SleepRequest, 100));
    automaticSleepArmed = false; hostNow = 32000; receive(incoming(Type::SleepRequest, 100));
    assert(localState() == LocalState::ACTIVE && !transaction().active);
    // Grace admission consumes an armed opportunity, but never rearms a spent one
    // or rewrites the activity clock, including across millis rollover.
    for (uint32_t start : {0U, UINT32_MAX - 31000})
    for (uint32_t age : {32000U, 33000U, 34999U})
    for (bool armed : {false, true})
    {
        freshAutomaticRuntime(start); automaticSleepArmed = armed; hostNow = start + age;
        receive(incoming(Type::SleepRequest, 100));
        assert(transaction().active && transaction().role == SleepRole::PARTICIPANT);
        assert(lastMeaningfulActivity == start && !automaticSleepArmed && countWire(Type::SleepRequest) == 0);
        receive(incoming(Type::SleepCancel, 100, 101));
        const auto cancelledAt = hostNow - 10;
        assert(cooldownLeftMs(cancelledAt) == 3000 && !automaticSleepArmed);
        hostNow = cancelledAt + 3000;
        for (unsigned i = 0; i < 100; ++i) loop();
        assert(!transaction().active && lastMeaningfulActivity == start && !automaticSleepArmed);
        assert(countWire(Type::SleepRequest) == 0);
    }
    // Recent real movement still refuses, even after settling and proximity finish.
    freshAutomaticRuntime(); movementStep(30000, MotionEvent::Activity);
    movementStep(33000, MotionEvent::Inactivity); movementStep(34000); movementStep(46000);
    assert(movementState == MovementState::READY && proximityUpdateState != ProximityUpdateState::CHECKING);
    assert(lastMeaningfulActivity == 30000 && sleepTransportBlockedReason() == nullptr);
    receive(incoming(Type::SleepRequest, 100));
    assert(!transaction().active && automaticSleepArmed && lastMeaningfulActivity == 30000);
    assert(countWire(Type::SleepCancel) == 1 && countWire(Type::SleepReady) == 0);
    puts("PASS: fresh peer REQUEST refuses at 31999ms, accepts at 32000ms only with all existing guards; grace never changes activity time/rearms/retries; recent Motion refuses, stale/duplicate/COMMIT handling is unchanged");
}

void testAutomaticSleepStaggeredClocks()
{
    // This harness has one runtime. Replay each endpoint's prefix to exchange
    // its actual emitted packets; both device builds exercise both time roles.
    const auto deliver = [](Protocol::Message message, uint32_t at) {
        message.sender = PEER_DEVICE;
        hostNow = at; receive(message);
    };
    for (uint32_t offset : {0U, 500U, 1000U, 2999U, 3000U, 4000U})
    {
        const bool reverse = offset > 3000;
        const uint32_t coordinatorAt = reverse ? offset + 35000 : 35000;
        const auto earlier = [] { freshAutomaticRuntime(); nextMessageId = 40; };
        const auto later = [offset] { freshAutomaticRuntime(offset); nextMessageId = 100; };
        Protocol::Message refusedRequest{}, refusal{};
        if (reverse)
        {
            earlier(); hostNow = 35000; loop(); refusedRequest = pendingMessage;
            assert(refusedRequest.type == Type::SleepRequest && !automaticSleepArmed);
            later(); deliver(refusedRequest, 35000); refusal = wire.back();
            assert(refusal.type == Type::SleepCancel && refusal.ackForMessageId == refusedRequest.messageId);
            assert(!transaction().active && automaticSleepArmed && lastMeaningfulActivity == offset);
            assert(countWire(Type::SleepReady) == 0 && cooldownLeftMs(hostNow) == 0);
        }
        const auto startCoordinator = [&] {
            if (reverse)
            {
                later(); deliver(refusedRequest, 35000);
                assert(!transaction().active && automaticSleepArmed);
            }
            else earlier();
            hostNow = coordinatorAt - 1; loop();
            assert(countWire(Type::SleepRequest) == 0 && !transaction().active && automaticSleepArmed);
            hostNow = coordinatorAt; loop();
            assert(transaction().role == SleepRole::COORDINATOR && !automaticSleepArmed);
            assert(transaction().startedAt == coordinatorAt && pendingMessage.type == Type::SleepRequest);
            assert(countWire(Type::SleepRequest) == 1 && lastMeaningfulActivity == (reverse ? offset : 0));
        };
        startCoordinator(); const auto request = pendingMessage;
        const auto startParticipant = [&] {
            if (reverse)
            {
                earlier(); hostNow = 35000; loop();
                deliver(refusal, 35010);
                assert(!transaction().active && !automaticSleepArmed && cooldownLeftMs(35010) == 3000);
                hostNow = 38009; loop(); assert(cooldownLeftMs(38009) == 1);
                hostNow = 38010; loop(); assert(cooldownLeftMs(38010) == 0);
                // Expiry and repeated loop service cannot retry the earlier attempt.
                while (hostNow < coordinatorAt) loop();
                assert(countWire(Type::SleepRequest) == 1 && !transaction().active && !automaticSleepArmed);
            }
            else later();
            deliver(request, coordinatorAt);
            assert(transaction().active && transaction().role == SleepRole::PARTICIPANT);
            assert(transaction().startedAt == coordinatorAt && !automaticSleepArmed);
            assert(lastMeaningfulActivity == (reverse ? 0 : offset));
            assert(countWire(Type::SleepRequest) == (reverse ? 1U : 0U));
            assert(countWire(Type::SleepReady) == 1 && pendingMessage.type == Type::SleepReady);
        };
        startParticipant(); const auto ready = pendingMessage;
        const auto advanceCoordinator = [&] {
            startCoordinator(); assert(memcmp(&pendingMessage, &request, sizeof(request)) == 0);
            deliver(ready, coordinatorAt + 10);
            assert(transaction().phase == SleepPhase::WAIT_ACK && pendingMessage.type == Type::SleepCommit);
        };
        advanceCoordinator(); const auto commit = pendingMessage;
        const auto advanceParticipant = [&] {
            startParticipant(); assert(memcmp(&pendingMessage, &ready, sizeof(ready)) == 0);
            deliver(commit, coordinatorAt + 20);
            assert(localState() == LocalState::SLEEPING && pendingMessage.type == Type::SleepAck);
            assert(physicalSleeps == 0);
        };
        advanceParticipant(); const auto sleepAck = pendingMessage;
        advanceCoordinator(); assert(memcmp(&pendingMessage, &commit, sizeof(commit)) == 0);
        deliver(sleepAck, coordinatorAt + 30); const auto receipt = wire.back();
        assert(receipt.type == Type::Ack && receipt.ackForMessageId == sleepAck.messageId);
        for (unsigned i = 0; i < 100; ++i) loop();
        assert(physicalSleeps == 1 && countWire(Type::SleepRequest) == 1 && !automaticSleepArmed);
        assert(countWire(Type::SleepCommit) == 1 && lastMeaningfulActivity == (reverse ? offset : 0));
        advanceParticipant(); assert(memcmp(&pendingMessage, &sleepAck, sizeof(sleepAck)) == 0);
        deliver(receipt, coordinatorAt + 40);
        for (unsigned i = 0; i < 100; ++i) loop();
        assert(physicalSleeps == 1 && !automaticSleepArmed && lastMeaningfulActivity == (reverse ? 0 : offset));
        assert(countWire(Type::SleepRequest) == (reverse ? 1U : 0U) && countWire(Type::SleepAck) == 1);
    }
    puts("PASS: offsets 0/500/1000/2999/3000ms complete both endpoints on the first request; 4000ms refuses then reverses after the unchanged cooldown, without rearming or a second automatic attempt by either endpoint");
}

void testAutomaticSleepMotionPriority()
{
    for (unsigned phase = 0; phase < 6; ++phase)
    {
        freshAutomaticRuntime(); hostNow = 35000;
        const bool participant = phase == 2 || phase == 3 || phase == 5;
        if (participant) receive(incoming(Type::SleepRequest, 100));
        else loop();
        const auto id = transaction().sleepId;
        if (phase == 1 || phase == 4) receive(incoming(Type::SleepReady, id));
        if (phase == 3 || phase == 5)
        {
            receive(incoming(Type::Ack, pendingMessage.messageId));
            deferControlsForTest = phase == 3;
            if (phase == 5) mockedTxInFlight = 1;
            receive(incoming(Type::SleepCommit, id, 101));
        }
        if (phase == 4)
        {
            mockedTxInFlight = 1;
            receive(incoming(Type::SleepAck, id));
        }
        const bool committed = phase >= 4;
        assert(localState() == (committed ? LocalState::SLEEPING : LocalState::SLEEP_NEGOTIATING));
        if (phase == 3) assert(transaction().phase == SleepPhase::WAIT_SLEEP_ACK_TX);
        const auto activity = hostNow;
        movementStep(activity, MotionEvent::Activity);
        assert(!transaction().active && automaticSleepArmed && lastMeaningfulActivity == activity);
        assert(localState() == (committed ? LocalState::WAKING : LocalState::ACTIVE));
        assert(countWire(Type::SleepCancel) == (committed ? 0U : 1U));
        assert(physicalSleeps == 0 && motionPreparations == 0);
        SleepDecision decision{}; assert(!takeSleepDecision(decision));
        if (committed) { hostNow = activity + 250; loop(); assert(localState() == LocalState::ACTIVE); }
    }
    for (bool late : {false, true})
    {
        freshAutomaticRuntime(); hostNow = 35000; loop(); const auto id = transaction().sleepId;
        receive(incoming(Type::SleepReady, id));
        if (late) afterAwakeService = [] { motionPendingEvent = MotionEvent::Activity; afterAwakeService = nullptr; };
        else motionPendingEvent = MotionEvent::Activity;
        receive(incoming(Type::SleepAck, id));
        assert(physicalSleeps == 0 && motionPreparations == 0 && automaticSleepArmed);
        assert(localState() == (late ? LocalState::WAKING : LocalState::ACTIVE));
        assert(countWire(Type::SleepCancel) == (late ? 0U : 1U));
    }
    puts("PASS: real Motion cancels WAIT_READY/WAIT_COMMIT/WAIT_ACK/WAIT_SLEEP_ACK_TX, revokes committed drain decisions through existing WAKING semantics, and wins both before queued SLEEP_ACK and after radio service just before physical execution");
}

void testAutomaticSleepCollision()
{
    freshAutomaticRuntime(); hostNow = 35000;
    nextMessageId = LOCAL_DEVICE == Device::Bubu ? 40 : 33;
    loop(); const auto hard = transaction().hardDeadline;
    const auto peerId = LOCAL_DEVICE == Device::Bubu ? 33 : 40;
    receive(incoming(Type::SleepRequest, peerId));
    assert(transaction().hardDeadline == hard && !automaticSleepArmed && countWire(Type::SleepCancel) == 0);
    assert(transaction().sleepId == 40);
    if (LOCAL_DEVICE == Device::Bubu)
    {
        assert(transaction().role == SleepRole::COORDINATOR);
        receive(incoming(Type::SleepReady, 40)); receive(incoming(Type::SleepAck, 40));
    }
    else
    {
        assert(transaction().role == SleepRole::PARTICIPANT);
        receive(incoming(Type::SleepCommit, 40, 41)); receive(incoming(Type::Ack, pendingMessage.messageId));
    }
    assert(physicalSleeps == 1);
    freshAutomaticRuntime(); loop(); requestSleepForTest();
    assert(transaction().active && countWire(Type::SleepRequest) == 1 && !automaticSleepArmed);
    activityForTest(); assert(localState() == LocalState::ACTIVE && automaticSleepArmed);
    assert(lastMeaningfulActivity == hostNow - 10);
    puts("PASS: simultaneous automatic coordinators retain DeviceId arbitration and hard limit, both roles reach physical entry; local activity rearms admission");
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    runCase(testAutomaticSleepClock);
    runCase(testActiveActivityBookkeeping);
    runCase(testAutomaticSleepBackgroundTraffic);
    runCase(testAutomaticSleepDeferral);
    runCase(testAutomaticSleepFailures);
    runCase(testAutomaticSleepAdmission);
    runCase(testAutomaticSleepStaggeredClocks);
    runCase(testAutomaticSleepMotionPriority);
    runCase(testAutomaticSleepCollision);
    finishSuite("automatic_sleep");
}
