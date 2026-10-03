#include "../fixtures/scenario_helpers.h"

void simulateHistoryRestart()
{
    // Recreate startup defaults, NOT setup(): no radio reset, no real reboot.
    // RTC storage is deliberately left intact across this simulated restart.
    freshApp(); protocolReady = false;
    pendingMessage = {}; ackWaitStart = 0; nextEventTime = 0;
    for (auto& control : controlQueue) control = {};
    assert(restoreRtcHistory());
    assert(!transaction().active && transaction().role == SleepRole::NONE &&
           transaction().phase == SleepPhase::NONE);
    assert(transaction().startedAt == 0 && transaction().phaseDeadline == 0 &&
           transaction().hardDeadline == 0 && cooldownLeftMs(hostNow) == 0);
    assert(localState() == LocalState::ACTIVE && peerState() == PeerState::UNKNOWN);
    SleepDecision decision{};
    assert(!takeSleepDecision(decision));
    assert(!waitingForAck && retryCount == 0 && controlCount == 0 && ackWaitStart == 0);
    assert(pendingMessage.messageId == 0 && nextEventTime == 0 && receiveQueue->items.empty());
    assert(armInitializations == 0 && armAttempts == 0 && wire.empty());
    RtcState::History consumed{};
    assert(!RtcState::load(consumed));
    assert(!restoreRtcHistory()); // Cannot replay the old ID allocator snapshot.
    protocolReady = true;
}

void testRtcHistoryRestart()
{
    // Retain EVENT history and the next unused ID, including wrap through zero.
    freshApp(); nextMessageId = 0xFFFF;
    haveLastPeerEvent = true; lastPeerEventId = 0xFFFF;
    saveRtcHistory(); simulateHistoryRestart();
    assert(nextMessageId == 0xFFFF && haveLastPeerEvent && lastPeerEventId == 0xFFFF);
    proximityClassification = ProximityClassification::CLOSE; // Isolate ID wrap from initial UNKNOWN probes.
    startHeartbeatEvent(); assert(pendingMessage.messageId == 0xFFFF && nextMessageId == 0);
    receive(incoming(Type::Ack, 0xFFFF));
    startHeartbeatEvent(); assert(pendingMessage.messageId == 0 && nextMessageId == 1);
    receive(incoming(Type::Ack, 0));
    Protocol::Message event{1, Type::Event, 0xFFFF, PEER_DEVICE, Protocol::EventType::Heartbeat, 0};
    loop(); requestSleepForTest();
    receive(event);
    assert(transaction().active && occurrences(Serial.log, "RX DUPLICATE") == 1);
    assert(occurrences(Serial.log, "RX NEW EVENT") == 0 && wire.back().ackForMessageId == 0xFFFF);
    event.messageId = 0; receive(event);
    assert(transaction().active && localState() == LocalState::SLEEP_NEGOTIATING && lastPeerEventId == 0);
    assert(occurrences(Serial.log, "RX NEW EVENT") == 1);

    // Snapshot while transaction, pending retry, TX outbox and RX queue contain
    // live work. Only history is encoded; none of that work survives restart.
    freshApp(); startHeartbeatEvent(); hostNow = ackWaitStart + 300; loop();
    assert(waitingForAck && retryCount == 1);

    assert(PowerManager::requestSleep(nextMessageId++, hostNow));
    queueReceivedData(reinterpret_cast<const uint8_t*>(&event), sizeof(event));
    assert(transaction().active && controlCount == 1 && !receiveQueue->items.empty());
    const auto nextId = nextMessageId;
    const auto activeId = transaction().sleepId;
    const auto runtimeBefore = transaction();
    assert(!PowerManager::restoreHistory({true, 55})); // Refuse live FSM import.
    assert(transaction().sleepId == runtimeBefore.sleepId && transaction().active);
    saveRtcHistory();
    assert(!restoreRtcHistory()); // Refuse live application import.
    RtcState::History stillValid{}; assert(RtcState::load(stillValid));
    simulateHistoryRestart(); assert(nextMessageId == nextId);
    receive(incoming(Type::SleepReady, activeId)); receive(incoming(Type::SleepAck, activeId));
    assert(!transaction().active && armAttempts == 0);

    // Completed participant: save before consuming SleepDecision. Persist the
    // REQUEST watermark, not the decision or completed COMMIT replay authority.
    freshApp(); loop(); receive(incoming(Type::SleepRequest, 0xFFFF));
    const auto commit = incoming(Type::SleepCommit, 0xFFFF, 21);
    PowerManager::handleControl(commit, hostNow, false); sendNextControl();
    assert(localState() == LocalState::SLEEPING && waitingForAck);
    assert(sleepDecisionPending && completed.valid); // Ensure test source really is live.
    saveRtcHistory(); simulateHistoryRestart();
    const auto history = PowerManager::exportHistory();
    assert(history.havePeerRequest && history.newestPeerRequest == 0xFFFF);
    loop();
    receive(incoming(Type::SleepRequest, 0xFFFF));
    receive(incoming(Type::SleepRequest, 0xFFFE));
    receive(commit); receive(incoming(Type::SleepAck, 0xFFFF));
    assert(!transaction().active && countWire(Type::SleepReady) == 0 && countWire(Type::SleepAck) == 0);
    assert(armAttempts == 0 && localState() == LocalState::ACTIVE);
    receive(incoming(Type::SleepRequest, 0)); // Existing half-range arithmetic accepts rollover.
    assert(transaction().active && transaction().sleepId == 0);
    activityForTest(); assert(cooldownLeftMs(hostNow) > 0);
    saveRtcHistory(); simulateHistoryRestart(); // No old cooldown or timestamps.
    loop(); receive(incoming(Type::SleepRequest, 0));
    assert(!transaction().active); // Cancellation's watermark is retained too.
    receive(incoming(Type::SleepRequest, 1)); assert(transaction().active);

    freshApp(); protocolReady = false; nextMessageId = 77;
    RtcState::invalidate(); assert(!restoreRtcHistory());
    assert(nextMessageId == 77 && !PowerManager::exportHistory().havePeerRequest);
    assert(!transaction().active && armInitializations == 0);
    puts("PASS: RTC application restart, EVENT dedup/ID wrap, REQUEST freshness, no live transaction/decision/queue/retry/deadline/replay restore");
}

void testBootRouting()
{
    // Cold boot discards even a valid old snapshot and runs normal CC1101 init.
    freshApp(); saveRtcHistory(); protocolReady = false;
    delete receiveQueue; receiveQueue = nullptr;
    setup();
    RtcState::History history{};
    assert(armInitializations == 1 && wakeRecoveries == 0 && !RtcState::load(history));
    assert(hostPixel().begins == 1 && hostPixel().shown == 0 && !led.busy());
    assert(Serial.log.find("BOOT | COLD") != std::string::npos);

    for (bool duplicate : {false, true}) for (bool validRtc : {false, true})
    {
        freshApp();
        RtcState::save({123, duplicate, 70, {true, 40}});
        if (!validRtc) RtcState::invalidate();
        // Model C++ startup defaults, then exercise the actual setup routing.
        protocolReady = false; pendingMessage = {}; ackWaitStart = 0;
        injectedBoot.deep = true; injectedBoot.cause = CC1101WakeRecovery::Cause::Gpio;
        injectedBoot.gpioMask = 1ULL << 4; injectedBoot.gdoAtBoot = 1;
        injectWakePacket = true;
        injectedPacket = {1, Type::Event, 70, PEER_DEVICE, Protocol::EventType::Heartbeat, 0};
        delete receiveQueue; receiveQueue = nullptr;
        setup();
        assert(armInitializations == 0 && wakeRecoveries == 1 && protocolReady);
        assert(rtcRestored == validRtc && !RtcState::load(history));
        assert(wire.empty()); // Wake receipt uses CC1101, never ESP-NOW.
        assert(!transaction().active && transaction().role == SleepRole::NONE && transaction().phase == SleepPhase::NONE);
        SleepDecision decision{}; assert(!takeSleepDecision(decision));
        assert(!waitingForAck && retryCount == 0 && controlCount == 0 && receiveQueue->items.empty());
        assert(localState() == LocalState::ACTIVE && cooldownLeftMs(hostNow) == 0);
        assert(wakeReport.processed == (validRtc && !duplicate));
        assert(wakeReport.duplicate == (validRtc && duplicate));
        assert(hostPixel().begins == 1 && hostPixel().shown == 0);
        assert(!led.busy() && led.requests == 0); // Retained delivery is ACKed while local distance is UNKNOWN.
        assert(nextMessageId == (validRtc ? 124 : 1));
        if (validRtc) assert(PowerManager::exportHistory().newestPeerRequest == 40);
        for (int i = 0; i < 20; ++i) loop();
        typeSerialForTest("p"); assert(wakeRecoveries == 1 && armInitializations == 0);
    }
    assert(Serial.log.find("Sleep handshake bench") == std::string::npos);
    assert(Serial.log.find("Power tests:") == std::string::npos);
    puts("PASS: cold/deep startup routing, RTC before wake EVENT dedup, fresh runtime, one recovery, no command banner");
}

void testPeerWakeRequestGuards()
{
    freshApp(); protocolReady = false; wakePeerForTest();
    assert(wakeEvents.empty() && nextMessageId == 1);
    freshApp(); startHeartbeatEvent(); const auto id = nextMessageId; wakePeerForTest();
    assert(wakeEvents.empty() && nextMessageId == id);
    freshApp(); deferControls(); loop(); requestSleepForTest(); wakePeerForTest();
    assert(wakeEvents.empty());
    freshApp(); loop(); requestSleepForTest(); receive(incoming(Type::Ack, pendingMessage.messageId));
    assert(!waitingForAck && transaction().active); wakePeerForTest(); assert(wakeEvents.empty());
    // A queued control blocks peer wake even without an active FSM transaction.
    freshApp(); assert(queueSleepControl(Type::SleepReady, 40)); wakePeerForTest(); assert(wakeEvents.empty());
    freshApp(); queueReceivedData(reinterpret_cast<const uint8_t*>(&injectedPacket), sizeof(injectedPacket));
    wakePeerForTest(); assert(wakeEvents.empty());
    freshApp(); nextMessageId = 0xFFFF;
    wakePeerForTest(); assert(wakeEvents.size() == 1 && nextMessageId == 0);
    assert(wakeEvents[0].messageId == 0xFFFF && wakeEvents[0].type == Type::Event &&
           wakeEvents[0].version == Protocol::VERSION && wakeEvents[0].sender == LOCAL_DEVICE &&
           wakeEvents[0].event == Protocol::EventType::Heartbeat && wakeEvents[0].ackForMessageId == 0);
    assert(Serial.log.find("GIVE_UP | retries=2") != std::string::npos);
    for (unsigned i = 0; i < 20; ++i) loop();
    assert(wakeEvents.size() == 1 && nextMessageId == 0 && wire.empty() && !transaction().active);
    wakePeerForTest(); assert(wakeEvents.size() == 2 && wakeEvents[1].messageId == 0 && nextMessageId == 1);
    // Existing ESP-NOW EVENT path still uses that same allocator afterwards.
    startHeartbeatEvent(); assert(pendingMessage.messageId == 1 && nextMessageId == 2);
    puts("PASS: peer wake helper runtime/transport/FSM guards, one allocator increment, rollover, no automatic wake TX");
}

void testMotionInitialization()
{
    for (bool deep : {false, true}) for (bool ok : {false, true})
    for (auto event : {MotionEvent::None, MotionEvent::Activity, MotionEvent::Inactivity})
    for (int level : {0, 1})
    {
        freshApp(); protocolReady = false;
        injectedBoot.deep = deep;
        injectedBoot.cause = CC1101WakeRecovery::Cause::Gpio;
        RtcState::save({123, false, 0, {false, 0}});
        injectWakePacket = deep;
        injectedPacket = {1, Type::Event, 70, PEER_DEVICE, Protocol::EventType::Heartbeat, 0};
        motionInitOk = ok; motionStartup = event; motionIntLevel = level;
        movementState = MovementState::WAITING; settleStartedAt = 123;
        proximityUpdateState = ProximityUpdateState::CHECKING; checkStartedAt = 123;
        proximitySampleCount = 1; proximitySamples[0] = {7, -60};
        delete receiveQueue; receiveQueue = nullptr;
        selectedTransport = pendingTransport = Transport::CC1101;
        setup();
        assert(selectedTransport == Transport::ESP_NOW && pendingTransport == Transport::ESP_NOW);
        assert(movementState == MovementState::READY && settleStartedAt == 0);
        assert(proximityUpdateState == ProximityUpdateState::CHECKING); // Cold and deep startup both bootstrap once.
        assert(proximitySampleCount == 0 && checkStartedAt == hostNow);
        for (const auto& sample : proximitySamples) assert(sample.messageId == 0 && sample.rssi == 0);
        assert(motionInitializations == 1 && protocolReady);
        assert(localState() == LocalState::ACTIVE && !transaction().active);
        assert(peerState() == (deep ? PeerState::ONLINE : PeerState::UNKNOWN));
        if (deep) assert(wakeReport.processed && wakeReport.ackSent && nextMessageId == 124);
        const std::string expected = ok ? "MOTION INIT | OK (DEVID=0xE5)" : "MOTION INIT | FAILED (DEVID/config check)";
        assert(Serial.log.find(expected) != std::string::npos);
        assert(Serial.log.find("GPIO3 INT1=" + std::to_string(level)) != std::string::npos);
        assert(Serial.log.find(ok ? "startup=" + std::to_string(static_cast<int>(event)) :
                                   "startup=UNAVAILABLE | continuing") != std::string::npos);
        for (int i = 0; i < 20; ++i) loop();
        assert(motionInitializations == 1 && physicalSleeps == 0 && wakeEvents.empty());
        startHeartbeatEvent(); receive(incoming(Type::Ack, pendingMessage.messageId));
        assert(!waitingForAck && peerState() == PeerState::ONLINE);
    }
    puts("PASS: Motion initialized once after CC1101 boot/recovery, startup/pin diagnostics, failure isolation, no motion policy");
}

void testMotionPeerWake()
{
    for (auto result : {CC1101WakeTx::Result::Acked, CC1101WakeTx::Result::AckTimeout,
                        CC1101WakeTx::Result::RadioUnavailable})
    for (uint64_t mask : {0ULL, 8ULL, 16ULL, 24ULL})
    for (bool deep : {false, true})
    {
        freshApp(); protocolReady = false;
        delete receiveQueue; receiveQueue = nullptr;
        injectedBoot.deep = deep; injectedBoot.gpioMask = mask;
        injectedBoot.cause = mask ? CC1101WakeRecovery::Cause::Gpio : CC1101WakeRecovery::Cause::Timer;
        injectWakePacket = deep && (mask & 16);
        injectedPacket = {1, Type::Event, 70, PEER_DEVICE, Protocol::EventType::Heartbeat, 0};
        RtcState::save({0xFFFF, false, 0, {false, 0}});
        wakeTxResult = result;
        setup();
        const bool automaticWake = deep && mask == 8;
        assert(protocolReady && motionInitializations == 1 && localState() == LocalState::ACTIVE);
        assert(wakeEvents.size() == (automaticWake ? 1U : 0U));
        assert(occurrences(Serial.log, "MOTION PEER WAKE | one-shot request") == (automaticWake ? 1U : 0U));
        if (automaticWake)
        {
            const auto& packet = wakeEvents.front();
            assert(packet.messageId == 0xFFFF && packet.version == Protocol::VERSION);
            assert(packet.sender == LOCAL_DEVICE && packet.type == Type::Event);
            assert(packet.event == Protocol::EventType::Heartbeat && packet.ackForMessageId == 0);
            assert(!haveLastPeerEvent); // No local application EVENT executed for motion.
            assert(nextMessageId == 0); // Exactly one allocation, including rollover.
            assert(Serial.log.find(result == CC1101WakeTx::Result::Acked ? "| OK |" : "| GIVE_UP |") != std::string::npos);
        }
        else if (injectWakePacket)
            assert(wakeReport.processed && wakeReport.ackSent && lastPeerEventId == 70);
        const auto idAfterBoot = nextMessageId;
        const bool initialCheck = proximityUpdateState == ProximityUpdateState::CHECKING;
        for (unsigned i = 0; i < 500; ++i) loop();
        const unsigned probes = initialCheck ? 10 : 0; // One bounded probe every 500ms during the 5s window.
        assert(wakeEvents.size() == (automaticWake ? 1U : 0U) && nextMessageId == uint16_t(idAfterBoot + probes));
        assert(localState() == LocalState::ACTIVE && !waitingForAck && wire.size() == probes);
        assert(countWire(Type::ProximityProbe) == probes && led.requests == 0);
    }
    // Incomplete runtime setup cannot send; a guard refusal is also one-shot,
    // never converted into an unbounded deferred request after the queue drains.
    for (bool startupFailure : {false, true})
    {
        freshApp(); protocolReady = false;
        delete receiveQueue; receiveQueue = nullptr;
        injectedBoot.deep = true; injectedBoot.cause = CC1101WakeRecovery::Cause::Gpio;
        injectedBoot.gpioMask = 8;
        RtcState::save({123, false, 0, {false, 0}});
        if (startupFailure) radioStarts = false;
        else atRadioStart = [] {
            const auto ack = incoming(Type::Ack, 99);
            queueReceivedData(reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
        };
        setup();
        assert(wakeEvents.empty() && nextMessageId == 123);
        if (!startupFailure) assert(Serial.log.find("CC1101 WAKE TX | REFUSED") != std::string::npos);
        for (unsigned i = 0; i < 100; ++i) loop();
        const unsigned probes = startupFailure ? 0 : 2;
        assert(wakeEvents.empty() && nextMessageId == 123 + probes && localState() == LocalState::ACTIVE);
        assert(wire.size() == probes && countWire(Type::ProximityProbe) == probes && led.requests == 0);
    }
    puts("PASS: pure-motion one-shot after safe startup; radio/both/timer/cold suppressed; ACK/timeout/unavailable outcomes");
    puts("PASS: one allocator increment/rollover, no local delivery, no loop retrigger, startup failure/guard refusal bounded");
}

void testRetainedUserAnimation()
{
    for (bool duplicate : {false, true}) for (bool validRtc : {false, true}) for (bool runtimeOk : {false, true})
    {
        freshApp(); protocolReady = false; delete receiveQueue; receiveQueue = nullptr;
        RtcState::save({100, duplicate, 70, {false, 0}});
        if (!validRtc) RtcState::invalidate();
        injectedBoot.deep = true; injectedBoot.cause = CC1101WakeRecovery::Cause::Gpio;
        injectedBoot.gpioMask = 0x10; injectWakePacket = true; radioStarts = runtimeOk;
        injectedPacket = {Protocol::VERSION, Type::Event, 70, PEER_DEVICE, Protocol::EventType::UserHeartbeat, 0};
        setup();
        const unsigned newUser = validRtc && !duplicate;
        assert(wakeReport.processed == bool(newUser) && led.userRequests == newUser && !retainedUserAnimationPending);
        assert(occurrences(Serial.log, "PARTNER LED | DEFERRED USER HEARTBEAT | id=70") == newUser);
        assert(occurrences(Serial.log, "PARTNER LED | USER HEARTBEAT | id=70") == newUser);
        assert(occurrences(Serial.log, "RX NEW EVENT") == newUser);
        assert(buttonEventPackets() == 0 && wakeEvents.empty());
        if (!newUser) continue;
        const auto allocator = nextMessageId;
        const auto began = hostNow;
        assert(allocator == 101); // Receipt only; deferred animation allocates no EVENT/ID.
        ledStep(began, 0); ledStep(began + 130, 180);
        if (!runtimeOk)
        {
            ledStep(began + 515, 255); ledStep(began + 700, 0, false);
            assert(led.userRequests == 1 && nextMessageId == allocator);
            continue; // LED delivery survives a later runtime failure; there is no live receive service.
        }
        receiveVia(injectedPacket, Transport::CC1101);
        assert(led.userRequests == 1 && occurrences(Serial.log, "RX NEW EVENT") == 1);
        ledStep(began + 515, 255); ledStep(began + 700, 0, false);
        receiveVia(injectedPacket, Transport::CC1101);
        assert(led.userRequests == 1 && !led.busy()); // ACK/retries do not replay delivery or animation.
        proximityClassification = ProximityClassification::CLOSE;
        const Protocol::Message background{Protocol::VERSION, Type::Event, 71, PEER_DEVICE, Protocol::EventType::Heartbeat, 0};
        receiveVia(background, Transport::CC1101);
        assert(led.userRequests == 1 && led.requests == 2);
        ledStep(hostNow, 0); ledStep(hostNow + 130, 180); // Current CLOSE background resumes normally.
    }
    // Both directions at once: recover/animate incoming user traffic before one local wake episode.
    for (uint64_t mask : {0x30ULL, 0x38ULL})
    {
        setupButtonWakeTest(mask, HIGH, true);
        assert(wakeReport.processed && wakeReport.ackSent && led.userRequests == 1 && buttonWakeIntentHeld);
        const auto start = hostNow; loop();
        assert(wakeEvents.empty() && buttonWakeIntentHeld && led.userHeartbeatActive());
        hostNow = start + 130; loop(); assert(hostPixel().shown == uint32_t(180) << 16);
        hostNow = start + 515; loop(); assert(hostPixel().shown == uint32_t(255) << 16);
        hostNow = start + 700; loop();
        assert(!led.userHeartbeatActive() && wakeEvents.size() == 1 && buttonEventPackets() == 1);
        assert(pendingMessage.messageId != wakeEvents.front().messageId && led.userRequests == 1);
        assert(!buttonWakeIntentHeld && !retainedUserAnimationPending);
        const auto own = pendingMessage;
        receiveVia(injectedPacket, Transport::CC1101); receiveVia(incoming(Type::Ack, own.messageId), Transport::CC1101);
        assert(led.userRequests == 1 && wakeEvents.size() == 1 && !waitingForAck);
    }
    puts("PASS: new retained UserHeartbeat owns one deferred visual before ACK, consumes after led.begin even on later startup failure, no duplicate/RTC-invalid replay or ID allocation, real pulse/CLOSE resume; simultaneous radio+button traffic animates then hands off once");
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    runCase(testRtcHistoryRestart);
    runCase(testBootRouting);
    runCase(testPeerWakeRequestGuards);
    runCase(testMotionInitialization);
    runCase(testMotionPeerWake);
    runCase(testRetainedUserAnimation);
    finishSuite("boot_wake");
}
