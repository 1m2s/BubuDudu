#include "../fixtures/scenario_helpers.h"

void testExecutionDrain()
{
    // Final ACK retries block physical sleep. Exhaustion now aborts execution
    // instead of physically sleeping without the participant receipt.
    freshApp(); loop(); receive(incoming(Type::SleepRequest, 20));
    receive(incoming(Type::SleepCommit, 20, 21));
    const auto began = ackWaitStart;
    const auto finalId = pendingMessage.messageId;
    for (uint32_t elapsed : {299U, 300U, 600U, 899U})
    {
        hostNow = began + elapsed; loop();
        assert(waitingForAck && occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
        assert(armAttempts == 0);
    }
    hostNow = began + 900; loop();
    assert(!waitingForAck && controlCount == 0 && peerState() == PeerState::UNKNOWN);
    assert(countWire(Type::SleepAck) == 3);
    for (const auto& packet : wire)
        if (packet.type == Type::SleepAck) assert(packet.messageId == finalId);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(coordinatedAttempts == 0);
    assert(armAttempts == 0);
    for (int i = 0; i < 100; ++i) loop();
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(coordinatedAttempts == 0);
    assert(armAttempts == 0);

    assert(localState() == LocalState::ACTIVE && physicalSleeps == 0 && coordinatedAttempts == 0);

    // Receipt followed by duplicate COMMIT in the same RX batch can leave a
    // required replay queued but not yet eligible for TX. Queue also must drain.
    freshApp(); loop(); receive(incoming(Type::SleepRequest, 20));
    const auto commit = incoming(Type::SleepCommit, 20, 21);
    receive(commit); deferControlsForTest = true;
    const auto receipt = incoming(Type::Ack, pendingMessage.messageId);
    for (const auto& packet : {receipt, commit})
        queueReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet));
    loop();
    assert(!waitingForAck && controlCount == 1);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);
    hostNow = controlQueue[0].notBefore; loop();
    assert(waitingForAck && controlCount == 0);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);
    receive(incoming(Type::Ack, pendingMessage.messageId));
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(coordinatedAttempts == 1 && physicalSleeps == 1);
    assert(armAttempts == 1);

    // Activity after semantic completion but before transport drain revokes
    // the unconsumed decision, even if a late receipt/COMMIT follows.
    freshApp(); loop(); receive(incoming(Type::SleepRequest, 20));
    receive(commit); const auto pendingId = pendingMessage.messageId;
    assert(localState() == LocalState::SLEEPING && waitingForAck);
    activityForTest();
    receive(incoming(Type::Ack, pendingId)); receive(commit);
    for (int i = 0; i < 100; ++i) loop();
    assert(localState() == LocalState::ACTIVE);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);
}

void testArmFailure()
{
    for (auto failure : {CC1101SleepArm::Result::RadioUnavailable,
                         CC1101SleepArm::Result::WrongConfig,
                         CC1101SleepArm::Result::NotInRx,
                         CC1101SleepArm::Result::GdoHigh,
                         CC1101SleepArm::Result::RxPending,
                         CC1101SleepArm::Result::RxOverflow})
    {
        for (bool participant : {false, true})
        {
            freshApp(); armResult = failure; loop();
            uint16_t id = 20;
            if (participant)
            {
                receive(incoming(Type::SleepRequest, id));
                receive(incoming(Type::SleepCommit, id, 21));
                assert(armAttempts == 0);
                receive(incoming(Type::Ack, pendingMessage.messageId));
            }
            else
            {
                requestSleepForTest(); id = transaction().sleepId;
                receive(incoming(Type::SleepReady, id));
                receive(incoming(Type::SleepAck, id));
            }
            assert(armAttempts == 1 && !transaction().active);
            assert(Serial.log.find("CC1101 SLEEP ARM | FAILED") != std::string::npos);
            const auto requests = countWire(Type::SleepRequest);
            receive(incoming(participant ? Type::SleepCommit : Type::SleepAck, id, 21));
            receive(incoming(Type::SleepAck, uint16_t(id - 1))); // Stale.
            if (waitingForAck) receive(incoming(Type::Ack, pendingMessage.messageId));
            for (int i = 0; i < 1000; ++i) loop();
            assert(armAttempts == 1 && countWire(Type::SleepRequest) == requests);
            assert(localState() == LocalState::ACTIVE && peerState() == PeerState::SLEEPING);
            assert(!transaction().active && cooldownLeftMs(hostNow) == 0);
            assert(coordinatedAttempts == 1 && physicalSleeps == 0);
            activityForTest(); hostNow += 250; loop();
            assert(localState() == LocalState::ACTIVE);
            // ESP-NOW remains usable after either role's arm failure.
            startHeartbeatEvent(); receive(incoming(Type::Ack, pendingMessage.messageId));
            assert(!waitingForAck && armAttempts == 1);
        }
    }
}

void testCoordinatedExecution()
{
    for (bool participant : {false, true})
    {
        completedAwaitingCallbacks(participant);
        for (int i = 0; i < 20; ++i) loop();
        assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
        assert(coordinatedAttempts == 0);
        mockedTxInFlight = 0; loop();
        assert(coordinatedAttempts == 1 && physicalSleeps == 1 && armAttempts == 1);
        SleepDecision decision{}; assert(!takeSleepDecision(decision));
        for (int i = 0; i < 20; ++i) loop();
        assert(coordinatedAttempts == 1);
    }
    // Callbacks finishing FIRST is insufficient: final packet receipt is required.
    freshApp(); loop(); receive(incoming(Type::SleepRequest, 20));
    receive(incoming(Type::SleepCommit, 20, 21));
    assert(mockedTxInFlight == 0 && waitingForAck); loop();
    assert(physicalSleeps == 0);
    receive(incoming(Type::Ack, pendingMessage.messageId));
    assert(physicalSleeps == 1);

    completedAwaitingCallbacks(false); mockedTxInFlight = 0; mockedRxActive = true;
    loop(); assert(coordinatedAttempts == 0);
    mockedRxActive = false;
    const auto packet = incoming(Type::Ack, 123);
    // One message beyond the loop's RX batch must keep the decision pending.
    delete receiveQueue; receiveQueue = xQueueCreate(RX_QUEUE_LENGTH + 1, sizeof(packet));
    for (unsigned i = 0; i < RX_QUEUE_LENGTH + 1; ++i)
        queueReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet));
    loop(); assert(coordinatedAttempts == 0 && uxQueueMessagesWaiting(receiveQueue) == 1);
    loop(); assert(physicalSleeps == 1);

    // A lost callback cannot strand semantic SLEEPING or reset the wait budget.
    completedAwaitingCallbacks(false);
    hostNow = sleepDrainStarted + SLEEP_DRAIN_TIMEOUT_MS; loop();
    assert(localState() == LocalState::ACTIVE && peerState() == PeerState::SLEEPING);
    assert(coordinatedAttempts == 0 && cooldownLeftMs(hostNow) > 0);
    mockedTxInFlight = 0;
    for (int i = 0; i < 500; ++i) loop();
    assert(coordinatedAttempts == 0 && !transaction().active);

    // Traffic appearing inside arm inspection aborts once, without RTC save.
    for (unsigned fault = 0; fault < 4; ++fault)
    {
        completedAwaitingCallbacks(true); mockedTxInFlight = 0;
        RtcState::invalidate();
        if (fault == 0) afterArm = [] { mockedTxInFlight = 1; };
        if (fault == 1) afterArm = [] {
            const auto queued = incoming(Type::Ack, 123);
            queueReceivedData(reinterpret_cast<const uint8_t*>(&queued), sizeof(queued));
        };
        if (fault == 2) afterArm = [] { mockedRxActive = true; };
        if (fault == 3) entryFails = true; // Actual GDO/setup/return cases tested in wake suite.
        loop();
        RtcState::History history{};
        assert(!RtcState::load(history) && physicalSleeps == 0 && coordinatedAttempts == 1);
        assert(localState() == LocalState::ACTIVE && peerState() == PeerState::SLEEPING);
        assert(!transaction().active && cooldownLeftMs(hostNow) > 0);
        mockedTxInFlight = 0; mockedRxActive = false;
        for (int i = 0; i < 500; ++i) loop();
        assert(coordinatedAttempts == 1 && physicalSleeps == 0);
    }
    // Rejected final receipt must not turn into physical sleep with zero callbacks.
    freshApp(); loop(); requestSleepForTest(); const auto id = transaction().sleepId;
    receive(incoming(Type::SleepReady, id)); radioAccepts = false;
    receive(incoming(Type::SleepAck, id));
    assert(localState() == LocalState::ACTIVE && peerState() == PeerState::SLEEPING && physicalSleeps == 0);

    // The failure notification cannot cancel an ACTIVE/negotiating FSM.
    freshPower(); notifySleepExecutionFailed(0);
    assert(localState() == LocalState::ACTIVE && cooldownLeftMs(0) == 0);
    requestSleep(27, 0); notifySleepExecutionFailed(10);
    assert(transaction().active && localState() == LocalState::SLEEP_NEGOTIATING);
    handleControl(control(Type::SleepReady, 27), 10);
    handleControl(control(Type::SleepAck, 27), 20);
    notifySleepExecutionFailed(21);
    SleepDecision decision{};
    assert(!takeSleepDecision(decision) && !transaction().active && localState() == LocalState::ACTIVE);
    assert(peerState() == PeerState::SLEEPING && cooldownLeftMs(21) == 3000);
    notifySleepExecutionFailed(100);
    assert(cooldownLeftMs(100) == 2921); // Duplicate notification cannot renew cooldown.
    assert(!requestSleep(28, 22));
    update(3021); assert(!transaction().active && !takeSleepDecision(decision));
    // Automatic execution waits for outstanding radio callbacks.
    completedAwaitingCallbacks(false); loop(); assert(coordinatedAttempts == 0);
    mockedTxInFlight = 0; loop(); assert(coordinatedAttempts == 1 && physicalSleeps == 1);

    // Timer wake restores history only, with the actual setup routing and new
    // runtime defaults. UNKNOWN stays silent while one bootstrap check starts.
    freshApp();
    RtcState::save({123, true, 70, {true, 40}});
    protocolReady = false; pendingMessage = {}; ackWaitStart = 0;
    suppressPeriodicForTest = false;
    injectedBoot.deep = true; injectedBoot.cause = CC1101WakeRecovery::Cause::Timer;
    delete receiveQueue; receiveQueue = nullptr;
    setup();
    RtcState::History history{};
    assert(rtcRestored && !RtcState::load(history) && nextMessageId == 123);
    assert(haveLastPeerEvent && lastPeerEventId == 70 && PowerManager::exportHistory().newestPeerRequest == 40);
    assert(localState() == LocalState::ACTIVE && !transaction().active && !takeSleepDecision(decision));
    assert(transaction().phaseDeadline == 0 && transaction().hardDeadline == 0);
    assert(!waitingForAck && controlCount == 0 && retryCount == 0 && !sleepDrainWaiting);
    assert(mockedTxInFlight == 0 && receiveQueue->items.empty() && physicalSleeps == 0);
    const auto bootstrapStartedAt = checkStartedAt;
    hostNow = nextEventTime; loop();
    assert(!waitingForAck && wire.size() == 1 && countWire(Type::ProximityProbe) == 1 && led.requests == 0);
    receive({Protocol::VERSION, Type::Event, 71, PEER_DEVICE, Protocol::EventType::Heartbeat, 0});
    assert(!waitingForAck && peerState() == PeerState::ONLINE);
    assert(proximityUpdateState == ProximityUpdateState::CHECKING && led.requests == 0);
    assert(checkStartedAt == bootstrapStartedAt && occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
    puts("PASS: coordinator/participant callback drain, receipt gate, one physical attempt, RX batch/callback gates");
    puts("PASS: bounded drain timeout, busy-after-arm abort, failed-entry ACTIVE/cooldown/peer preservation, no retry");
    puts("PASS: timer reboot restores history only, fresh drain/retry/queue/deadline state, ESP-NOW peer rediscovery");
}

void testMotionSleepEntry()
{
    // Failed sensor/arm and final GPIO race all return via the existing
    // one-shot execution-failed path, with no automatic rearm or peer TX.
    for (unsigned failure = 0; failure < 4; ++failure)
    {
        completedAwaitingCallbacks(false);
        mockedTxInFlight = 0;
        if (failure == 0) motionInitOk = false;
        if (failure == 1) motionPrepareOk = false;
        if (failure == 2) motionIntLevel = 1;
        if (failure == 3) afterArm = [] { motionIntLevel = 1; };
        loop();
        assert(physicalSleeps == 0 && motionPreparations == 1 && motionCancels == 1);
        assert(localState() == LocalState::ACTIVE && peerState() != PeerState::OFFLINE);
        for (unsigned i = 0; i < 20; ++i) loop();
        assert(motionPreparations == 1 && wakeEvents.empty());
    }
    completedAwaitingCallbacks(false); mockedTxInFlight = 0; motionPrepareOk = false;
    loop();
    assert(coordinatedAttempts == 0 && motionCancels == 1 && localState() == LocalState::ACTIVE);

    // All deep sources restore RTC and inspect retained CC before Motion.
    for (uint64_t mask : {0ULL, 8ULL, 16ULL, 24ULL})
    {
        freshApp(); protocolReady = false;
        delete receiveQueue; receiveQueue = nullptr;
        injectedBoot.deep = true; injectedBoot.gpioMask = mask;
        injectedBoot.cause = mask ? CC1101WakeRecovery::Cause::Gpio : CC1101WakeRecovery::Cause::Timer;
        injectWakePacket = (mask & 16) != 0;
        injectedPacket = {1, Type::Event, 70, PEER_DEVICE, Protocol::EventType::Heartbeat, 0};
        RtcState::save({123, false, 0, {false, 0}});
        setup();
        assert(rtcRestored && protocolReady && motionInitializations == 1);
        assert(wakeRecoveries == 1 && armInitializations == 0);
        assert(wakeReport.processed == injectWakePacket && wakeReport.ackSent == injectWakePacket);
        assert(localState() == LocalState::ACTIVE && !transaction().active);
        assert(wakeEvents.size() == (mask == 8 ? 1U : 0U));
        assert(nextMessageId == ((injectWakePacket || mask == 8) ? 124 : 123));
    }
    puts("PASS: GPIO3/GPIO4/both/timer startup ordering; Motion arm failure and GPIO race abort once; only pure motion startup requests peer wake");
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    runCase(testExecutionDrain);
    runCase(testArmFailure);
    runCase(testCoordinatedExecution);
    runCase(testMotionSleepEntry);
    finishSuite("sleep_execution");
}
