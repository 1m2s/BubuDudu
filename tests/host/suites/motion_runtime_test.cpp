#include "../fixtures/scenario_helpers.h"

void testAwakeMotionDiagnostics()
{
    for (auto event : {MotionEvent::Activity, MotionEvent::Inactivity})
    for (bool negotiating : {false, true}) for (bool sensorOk : {false, true})
    {
        freshApp();
        if (negotiating) { loop(); requestSleepForTest(); }
        motionInitOk = sensorOk;
        const auto local = localState();
        const auto peer = peerState();
        const auto sleep = transaction();
        const auto id = nextMessageId;
        const auto heartbeatAt = nextEventTime;
        const auto sent = wire.size();
        const auto queued = controlCount;
        const auto pending = pendingMessage;
        const auto retries = retryCount;
        const bool waiting = waitingForAck;
        const auto polls = motionEventPolls;
        const auto activityAt = hostNow;
        Serial.log.clear();
        motionPendingEvent = event;
        for (unsigned i = 0; i < 5; ++i) loop();
        assert(motionEventPolls == polls + 5);
        assert(occurrences(Serial.log, "MOTION AWAKE |") == (sensorOk ? 1U : 0U));
        if (sensorOk)
            assert(Serial.log.find(event == MotionEvent::Activity ? "MOTION AWAKE | MOVING" :
                                  "MOTION AWAKE | INACTIVITY") != std::string::npos);
        if (negotiating && sensorOk && event == MotionEvent::Activity)
        {
            assert(localState() == LocalState::ACTIVE && peerState() == PeerState::ONLINE);
            assert(!transaction().active && !waitingForAck && controlCount == 0);
            assert(nextMessageId == uint16_t(id + 1) && countWire(Type::SleepCancel) == 1);
            assert(cooldownLeftMs(hostNow) == 3000 - uint32_t(hostNow - activityAt));
            assert(automaticSleepArmed && lastMeaningfulActivity == activityAt);
            assert(nextEventTime == heartbeatAt && physicalSleeps == 0 && motionPreparations == 0);
            continue;
        }
        assert(localState() == local && peerState() == peer);
        assert(transaction().active == sleep.active && transaction().sleepId == sleep.sleepId);
        assert(transaction().role == sleep.role && transaction().phase == sleep.phase);
        assert(transaction().startedAt == sleep.startedAt && transaction().phaseDeadline == sleep.phaseDeadline);
        assert(transaction().hardDeadline == sleep.hardDeadline && cooldownLeftMs(hostNow) == 0);
        assert(nextMessageId == id && nextEventTime == heartbeatAt && suppressPeriodicForTest);
        assert(wire.size() == sent && wakeEvents.empty() && controlCount == queued);
        assert(waitingForAck == waiting && retryCount == retries);
        assert(memcmp(&pendingMessage, &pending, sizeof(pending)) == 0);
        assert(motionPreparations == 0 && motionCancels == 0 && physicalSleeps == 0 && armAttempts == 0);
        assert(!haveLastPeerEvent);
    }

    // A normally scheduled heartbeat still runs, identically with or without motion.
    for (auto event : {MotionEvent::None, MotionEvent::Activity, MotionEvent::Inactivity})
    {
        freshKnownApp(); suppressPeriodicForTest = false; nextEventTime = 10;
        motionPendingEvent = event;
        loop(); // Not due yet: movement must not pull the schedule forward.
        assert(wire.empty() && nextEventTime == 10 && nextMessageId == 1);
        motionPendingEvent = event;
        loop();
        assert(wire.size() == 1 && wire[0].type == Type::Event && wire[0].messageId == 1);
        assert(waitingForAck && nextMessageId == 2 && nextEventTime == 10 && !suppressPeriodicForTest);
        assert(localState() == LocalState::ACTIVE && peerState() == PeerState::UNKNOWN && wakeEvents.empty());
    }
    freshApp(); protocolReady = false; motionPendingEvent = MotionEvent::Activity;
    loop();
    assert(motionEventPolls == 0 && Serial.log.find("MOTION AWAKE |") == std::string::npos);
    puts("PASS: real Motion Activity cancels negotiation and rearms inactivity; Inactivity/unavailable sensor preserve power/transport, heartbeat schedule unchanged");
}

void testMovementSettle()
{
    freshApp();
    assert(movementState == MovementState::READY && settleStartedAt == 0);
    movementStep(0, MotionEvent::Inactivity);
    assert(movementState == MovementState::READY);
    for (uint32_t cycle = 0; cycle < 3; ++cycle)
    {
        const uint32_t base = 100 + cycle * 5000;
        movementStep(base, MotionEvent::Activity);
        assert(movementState == MovementState::MOVING);
        movementStep(base + 10, MotionEvent::Activity);
        assert(movementState == MovementState::MOVING);
        movementStep(base + 20);
        assert(movementState == MovementState::MOVING);
        movementStep(base + 30, MotionEvent::Inactivity);
        assert(movementState == MovementState::WAITING && settleStartedAt == base + 30);
        movementStep(base + 1000, MotionEvent::Inactivity);
        assert(settleStartedAt == base + 30);
        movementStep(base + 30 + SETTLE_MS - 1);
        assert(movementState == MovementState::WAITING);
        assert(occurrences(Serial.log, "MOVEMENT | SETTLED") == cycle);
        movementStep(base + 30 + SETTLE_MS);
        assert(movementState == MovementState::READY && settleStartedAt == 0);
        movementStep(base + 30 + SETTLE_MS + 10, MotionEvent::Inactivity);
        for (unsigned i = 0; i < 5; ++i) loop();
        assert(occurrences(Serial.log, "MOVEMENT | SETTLED") == cycle + 1);
    }
    assert(occurrences(Serial.log, "MOVEMENT | MOVING") == 3);
    assert(occurrences(Serial.log, "MOVEMENT | WAITING") == 3);

    for (uint32_t offset : {SETTLE_MS - 1, SETTLE_MS, SETTLE_MS + 100})
    {
        freshApp(); movementStep(0, MotionEvent::Activity);
        movementStep(10, MotionEvent::Inactivity);
        movementStep(10 + offset, MotionEvent::Activity);
        assert(movementState == MovementState::MOVING && settleStartedAt == 0);
        movementStep(5000);
        assert(movementState == MovementState::MOVING);
        assert(occurrences(Serial.log, "MOVEMENT | SETTLED") == 0);
        movementStep(5010, MotionEvent::Inactivity);
        movementStep(5010 + SETTLE_MS + 1); // First update after the boundary.
        assert(movementState == MovementState::READY);
        assert(occurrences(Serial.log, "MOVEMENT | SETTLED") == 1);
    }

    const uint32_t start = UINT32_MAX - SETTLE_MS / 2;
    freshApp(); movementStep(start - 100, MotionEvent::Activity);
    movementStep(start, MotionEvent::Inactivity);
    movementStep(uint32_t(start + SETTLE_MS - 1));
    assert(movementState == MovementState::WAITING && settleStartedAt == start);
    movementStep(uint32_t(start + SETTLE_MS));
    assert(movementState == MovementState::READY);
    movementStep(uint32_t(start + SETTLE_MS + 100));
    assert(occurrences(Serial.log, "MOVEMENT | SETTLED") == 1);
    puts("PASS: movement transitions, repeated inactivity, one-shot settle, activity wins, exact/late expiry and rollover");
}

void testMovementBoundaries()
{
    for (auto state : {LocalState::SLEEP_NEGOTIATING,
                       LocalState::SLEEPING, LocalState::WAKING})
    for (auto stale : {MovementState::MOVING, MovementState::WAITING})
    for (auto event : {MotionEvent::None, MotionEvent::Activity, MotionEvent::Inactivity})
    {
        freshApp();
        if (state == LocalState::SLEEPING || state == LocalState::WAKING)
        {
            completedAwaitingCallbacks(false);
            if (state == LocalState::WAKING) injectActivity(hostNow);
        }
        else
        {
            loop();
            if (state == LocalState::SLEEP_NEGOTIATING) requestSleepForTest();
        }
        assert(localState() == state);
        movementState = stale;
        settleStartedAt = hostNow - SETTLE_MS; // Would expire if not suppressed.
        Serial.log.clear();
        movementStep(hostNow, event);
        const bool resumes = event == MotionEvent::Activity &&
            (state == LocalState::SLEEP_NEGOTIATING);
        const auto expected = resumes ? LocalState::ACTIVE :
            event == MotionEvent::Activity && state == LocalState::SLEEPING ? LocalState::WAKING : state;
        assert(localState() == expected);
        assert(movementState == (resumes ? MovementState::MOVING : MovementState::READY) && settleStartedAt == 0);
        assert((Serial.log.find("MOVEMENT | MOVING") != std::string::npos) == resumes);
        assert(occurrences(Serial.log, "MOTION AWAKE |") == (event == MotionEvent::None ? 0U : 1U));
    }

    // WAKING can become ACTIVE at the beginning of this very iteration.
    completedAwaitingCallbacks(false); injectActivity(hostNow);
    movementState = MovementState::WAITING; settleStartedAt = hostNow - SETTLE_MS;
    hostNow += 250; Serial.log.clear(); loop();
    assert(localState() == LocalState::ACTIVE && movementState == MovementState::READY);
    assert(Serial.log.find("MOVEMENT | SETTLED") == std::string::npos);

    for (bool preparationFails : {false, true})
    {
        freshApp(); movementStep(0, MotionEvent::Activity);
        movementStep(10, MotionEvent::Inactivity);
        motionPrepareOk = !preparationFails; entryFails = !preparationFails;
        failedSleepEntryForTest();
        assert(motionPreparations == 1 && motionCancels == 1 && physicalSleeps == 0);
        assert(localState() == LocalState::ACTIVE && movementState == MovementState::READY);
        assert(settleStartedAt == 0);
        movementStep(10 + SETTLE_MS + 1);
        assert(Serial.log.find("MOVEMENT | SETTLED") == std::string::npos);
    }
    freshApp(); motionInitOk = false;
    movementStep(0, MotionEvent::Activity); movementStep(10, MotionEvent::Inactivity);
    movementStep(10 + SETTLE_MS);
    assert(movementState == MovementState::READY && Serial.log.find("MOVEMENT |") == std::string::npos);
    movementState = MovementState::WAITING; settleStartedAt = 10;
    protocolReady = false; const auto polls = motionEventPolls;
    loop();
    assert(movementState == MovementState::READY && settleStartedAt == 0 && motionEventPolls == polls);
    puts("PASS: movement ACTIVE-only, raw events still serviced, failed sensor/runtime, boot and aborted-sleep reset");
}

void testMovementIsolation()
{
    // Compare identical transport timelines with/without an entire movement cycle.
    for (bool heartbeats : {false, true})
    {
        std::vector<Protocol::Message> baselinePackets;
        std::string baselineState;
        for (bool moving : {false, true})
        {
            freshKnownApp(); suppressPeriodicForTest = !heartbeats; nextEventTime = 50;
            ackWaitStart = 0; pendingMessage = {}; // Identical initial transport history for both runs.
            std::string states;
            for (uint32_t time : {0U, 10U, 50U, 350U, 650U, 950U, SETTLE_MS + 10U, SETTLE_MS + 20U})
            {
                const auto event = !moving ? MotionEvent::None : time == 0 ? MotionEvent::Activity :
                                   time == 10 ? MotionEvent::Inactivity : MotionEvent::None;
                movementStep(time, event);
                const auto& tx = transaction();
                // Include packet/retry, scheduling, semantic state and all sleep deadlines.
                states += std::to_string(nextMessageId) + ":" + std::to_string(waitingForAck) + ":" +
                    std::to_string(retryCount) + ":" + std::to_string(ackWaitStart) + ":" +
                    std::to_string(nextEventTime) + ":" + std::to_string(controlCount) + ":" +
                    std::to_string(static_cast<int>(localState())) + ":" +
                    std::to_string(static_cast<int>(peerState())) + ":" + std::to_string(tx.active) + ":" +
                    std::to_string(tx.sleepId) + ":" + std::to_string(static_cast<int>(tx.role)) + ":" +
                    std::to_string(static_cast<int>(tx.phase)) + ":" + std::to_string(tx.startedAt) + ":" +
                    std::to_string(tx.phaseDeadline) + ":" + std::to_string(tx.hardDeadline) + ";";
                assert(wakeEvents.empty() && motionPreparations == 0 && physicalSleeps == 0);
            }
            if (!moving) { baselinePackets = wire; baselineState = states; }
            else
            {
                assert(states == baselineState && wire.size() == baselinePackets.size());
                for (size_t i = 0; i < wire.size(); ++i)
                    assert(memcmp(&wire[i], &baselinePackets[i], sizeof(Protocol::Message)) == 0);
                assert(occurrences(Serial.log, "MOVEMENT | SETTLED") == 1);
            }
        }
    }
    puts("PASS: full movement/settle cycle preserves heartbeat timing, packet IDs/bytes, ACK retries, power/peer state and sleep deadlines");
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    runCase(testAwakeMotionDiagnostics);
    runCase(testMovementSettle);
    runCase(testMovementBoundaries);
    runCase(testMovementIsolation);
    finishSuite("motion_runtime");
}
