#include "../fixtures/scenario_helpers.h"

void testFsm()
{
    // ACTIVE alone is not participant admission. The loop supplies eligibility.
    freshPower();
    PowerManager::handleControl(control(Type::SleepRequest, 25), 0, false);
    assert(localState() == LocalState::ACTIVE && !transaction().active);
    assert(intents.size() == 1 && intents.back().type == Type::SleepCancel);

    // Coordinator: packet receipt is not the sleep agreement.
    freshPower();
    assert(localState() == LocalState::ACTIVE);
    assert(requestSleep(27, 0));
    assert(transaction().role == SleepRole::COORDINATOR && transaction().phase == SleepPhase::WAIT_READY);
    assert(intents.back().type == Type::SleepRequest && intents.back().id == 27);
    notePeerSeen(); assert(peerState() == PeerState::SLEEP_PENDING);
    handleControl(control(Type::SleepAck, 27), 100); // Correct ID, wrong phase.
    assert(transaction().phase == SleepPhase::WAIT_READY);
    handleControl(control(Type::SleepReady, 27), 500);
    assert(transaction().phase == SleepPhase::WAIT_ACK && intents.back().type == Type::SleepCommit);
    auto phaseDeadline = transaction().phaseDeadline;
    handleControl(control(Type::SleepReady, 27), 600);
    assert(transaction().phaseDeadline == phaseDeadline && transaction().hardDeadline == 5000);
    handleControl(control(Type::SleepAck, 27), 700);
    assert(localState() == LocalState::SLEEPING && peerState() == PeerState::SLEEPING && !transaction().active);
    notePeerSeen(); notePeerUnreachable(); assert(peerState() == PeerState::SLEEPING);
    auto effects = occurrences(Serial.log, "HANDSHAKE_COMPLETE");
    handleControl(control(Type::SleepAck, 27), 710);
    assert(occurrences(Serial.log, "HANDSHAKE_COMPLETE") == effects);

    // Participant and duplicate REQUEST/COMMIT: one transition, repeatable response.
    freshPower();
    auto request = control(Type::SleepRequest, 20);
    auto commit = control(Type::SleepCommit, 20, 21);
    handleControl(request, 100);
    assert(transaction().role == SleepRole::PARTICIPANT && transaction().phase == SleepPhase::WAIT_COMMIT);
    auto hardDeadline = transaction().hardDeadline;
    phaseDeadline = transaction().phaseDeadline;
    handleControl(request, 200);
    assert(intents.size() == 2 && intents.back().type == Type::SleepReady);
    assert(transaction().hardDeadline == hardDeadline && transaction().phaseDeadline == phaseDeadline);
    handleControl(commit, 300); handleControl(commit, 310);
    assert(transaction().active && intents.back().type == Type::SleepAck);
    controlSent(Type::SleepAck, 20, 320);
    assert(localState() == LocalState::SLEEPING && !transaction().active);
    effects = occurrences(Serial.log, "HANDSHAKE_COMPLETE");
    auto replies = intents.size();
    handleControl(commit, 330); // Exact accepted packet, receipt replay only.
    assert(intents.size() == replies + 1 && intents.back().type == Type::SleepAck);
    assert(occurrences(Serial.log, "HANDSHAKE_COMPLETE") == effects);
    commit.messageId = 99; handleControl(commit, 340);
    assert(intents.size() == replies + 1); // Same closed sleepId alone is insufficient.
    injectActivity(400); update(650);
    handleControl(request, 651); assert(!transaction().active); // Closed request stays closed.

    // Stale IDs, wrong sender, wrong role, malformed request cannot advance/cancel.
    freshPower(); requestSleep(30, 0);
    for (auto type : {Type::SleepReady, Type::SleepCommit, Type::SleepAck, Type::SleepCancel})
    {
        handleControl(control(type, 29), 10);
        assert(transaction().active && transaction().sleepId == 30 && transaction().phase == SleepPhase::WAIT_READY);
    }
    handleControl(control(Type::SleepReady, 30, 90, Device::Bubu), 10);
    handleControl(control(Type::SleepCommit, 30), 10);
    assert(transaction().phase == SleepPhase::WAIT_READY);
    freshPower(); auto malformed = control(Type::SleepRequest, 40); malformed.messageId = 41;
    handleControl(malformed, 0); assert(!transaction().active);

    // Activity closes state first; late control cannot put the device to sleep.
    freshPower(); handleControl(control(Type::SleepRequest, 40), 0);
    injectActivity(100); assert(localState() == LocalState::ACTIVE && !transaction().active);
    assert(intents.back().type == Type::SleepCancel && cooldownLeftMs(100) == 3000);
    handleControl(control(Type::SleepCommit, 40), 110);
    assert(localState() == LocalState::ACTIVE && !transaction().active);
    assert(!requestSleep(99, 200));
    update(3100); handleControl(control(Type::SleepRequest, 40), 3101); assert(!transaction().active);
    handleControl(control(Type::SleepRequest, 41), 3102); assert(transaction().sleepId == 41);
    handleControl(control(Type::SleepCancel, 41), 3200);
    assert(localState() == LocalState::ACTIVE && !transaction().active && cooldownLeftMs(3200) == 3000);

    // Phase and absolute hard bounds. Duplicate READY cannot extend either bound.
    freshPower(); requestSleep(27, 0); update(3000);
    assert(!transaction().active && Serial.log.find("PHASE_TIMEOUT") != std::string::npos);
    freshPower(); handleControl(control(Type::SleepRequest, 27), 0); update(3000);
    assert(!transaction().active && localState() == LocalState::ACTIVE && peerState() == PeerState::UNKNOWN);
    freshPower(); requestSleep(27, 0);
    handleControl(control(Type::SleepReady, 27), 2999);
    handleControl(control(Type::SleepReady, 27), 4999);
    assert(transaction().hardDeadline == 5000); update(5000);
    assert(!transaction().active && Serial.log.find("HARD_TIMEOUT") != std::string::npos);
    auto afterTimeout = Serial.log;
    for (uint32_t now = 5001; now < 20000; ++now) update(now);
    assert(!transaction().active && localState() == LocalState::ACTIVE && Serial.log == afterTimeout);

    // Simultaneous requests, including equal numeric IDs. Losing the collision
    // cannot extend the hard limit or send a CANCEL that kills the winning ID.
    for (uint16_t peerId : {uint16_t(20), uint16_t(44)})
    {
        freshPower(Device::Bubu); requestSleep(20, 0);
        handleControl(control(Type::SleepRequest, peerId, 0, Device::Dudu), 100);
        assert(transaction().role == SleepRole::COORDINATOR && transaction().sleepId == 20 && intents.size() == 1);
        freshPower(Device::Dudu); requestSleep(peerId, 0);
        handleControl(control(Type::SleepRequest, 20, 0, Device::Bubu), 100);
        assert(transaction().role == SleepRole::PARTICIPANT && transaction().sleepId == 20);
        assert(transaction().hardDeadline == 5000 && intents.size() == 2 && intents.back().type == Type::SleepReady);
        handleControl(control(Type::SleepCommit, 20, 25, Device::Bubu), 200);
        controlSent(Type::SleepAck, 20, 210); assert(localState() == LocalState::SLEEPING);
    }
    // Tie-break never takes over an already committed coordinator transaction.
    freshPower(Device::Dudu); requestSleep(44, 0);
    handleControl(control(Type::SleepReady, 44, 90, Device::Bubu), 10);
    handleControl(control(Type::SleepRequest, 20, 0, Device::Bubu), 20);
    assert(transaction().role == SleepRole::COORDINATOR && transaction().sleepId == 44);

    // Different failure knowledge before and after COMMIT.
    freshPower(); requestSleep(27, 0); controlFailed(Type::SleepRequest, 27, 900);
    assert(peerState() == PeerState::OFFLINE && localState() == LocalState::ACTIVE);
    freshPower(); requestSleep(27, 0); handleControl(control(Type::SleepReady, 27), 10);
    controlFailed(Type::SleepCommit, 27, 900); assert(peerState() == PeerState::UNKNOWN && !transaction().active);
    freshPower(); handleControl(control(Type::SleepRequest, 27), 0);
    handleControl(control(Type::SleepCommit, 27), 10); controlSent(Type::SleepAck, 27, 10);
    controlFailed(Type::SleepAck, 27, 910);
    assert(peerState() == PeerState::UNKNOWN && localState() == LocalState::SLEEPING);
    freshPower(); acceptIntent = false; assert(!requestSleep(27, 0));
    assert(!transaction().active && cooldownLeftMs(0) == 3000);

    // uint32 millis rollover, including a zero hard deadline.
    freshPower(); const uint32_t start = UINT32_MAX - 4999;
    requestSleep(27, start);
    handleControl(control(Type::SleepReady, 27), start + 2999);
    update(UINT32_MAX); assert(transaction().active); update(0);
    assert(!transaction().active && cooldownLeftMs(0) == 3000);
    update(3000); assert(cooldownLeftMs(3000) == 0);
    // Request IDs use serial arithmetic, independent of the clock rollover.
    freshPower(); handleControl(control(Type::SleepRequest, 65530), 0);
    injectActivity(10); update(3010);
    handleControl(control(Type::SleepRequest, 5), 3011); assert(transaction().sleepId == 5);
}

void testDelayedCollision()
{
    // Replay both sides of the same timed hardware exchange, each against its
    // peer's scheduled packets. Both start as coordinators before any REQUEST
    // is delivered. Use the real loop/outbox and the harness-injected 1000ms queue delay.
    freshApp(); deferControlsForTest = true;
    const uint16_t ownId = LOCAL_DEVICE == Device::Bubu ? 40 : 33;
    hostNow = LOCAL_DEVICE == Device::Bubu ? 100 : 0;
    nextMessageId = ownId;

    assert(PowerManager::requestSleep(nextMessageId++, hostNow));
    assert(transaction().role == SleepRole::COORDINATOR);
    const uint32_t hard = transaction().hardDeadline;

    if (LOCAL_DEVICE == Device::Bubu)
    {
        // Dudu's delayed REQUEST arrives while Bubu's REQUEST is still queued.
        hostNow = 1000; receive(incoming(Type::SleepRequest, 33));
        assert(transaction().role == SleepRole::COORDINATOR && transaction().sleepId == 40);
        hostNow = 1100; loop(); assert(countWire(Type::SleepRequest) == 1);
        receive(incoming(Type::Ack, 40));
        hostNow = 2110; receive(incoming(Type::SleepReady, 40, 35));
        assert(transaction().phase == SleepPhase::WAIT_ACK && countWire(Type::SleepCommit) == 0);
        hostNow = 3110; loop(); assert(countWire(Type::SleepCommit) == 1);
        receive(incoming(Type::Ack, pendingMessage.messageId));
        assert(transaction().hardDeadline == hard);
        hostNow = 4120; receive(incoming(Type::SleepAck, 40, 37));
    }
    else
    {
        hostNow = 1000; loop(); assert(countWire(Type::SleepRequest) == 1);
        receive(incoming(Type::Ack, 33));
        hostNow = 1100; receive(incoming(Type::SleepRequest, 40));
        assert(transaction().role == SleepRole::PARTICIPANT && transaction().sleepId == 40);
        assert(transaction().hardDeadline == hard); // Losing ID 33 cannot extend 5000ms.
        const uint32_t oldWaitCommitDeadline = transaction().phaseDeadline; // 4100ms
        hostNow = 2100; loop(); assert(countWire(Type::SleepReady) == 1);
        receive(incoming(Type::Ack, pendingMessage.messageId));
        hostNow = 3110; receive(incoming(Type::SleepCommit, 40, 43));
        assert(transaction().phase == SleepPhase::WAIT_SLEEP_ACK_TX);
        assert(transaction().phaseDeadline == 6110 && transaction().hardDeadline == hard);
        assert(countWire(Type::SleepAck) == 0); // Still in the delayed outbox.
        hostNow = oldWaitCommitDeadline; loop();
        // Regression: old code cancelled here, ten milliseconds before ACK TX.
        assert(transaction().active && countWire(Type::SleepCancel) == 0);
        assert(transaction().hardDeadline == hard);
        hostNow = 4110; loop(); assert(countWire(Type::SleepAck) == 1);
    }
    assert(hostNow < hard && localState() == LocalState::SLEEPING && !transaction().active);
    assert(peerState() == PeerState::SLEEPING && countWire(Type::SleepCancel) == 0);
    assert(occurrences(Serial.log, "HANDSHAKE_COMPLETE") == 1);
    if (LOCAL_DEVICE == Device::Dudu)
    {
        assert(waitingForAck && occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
        assert(armAttempts == 0);
        receive(incoming(Type::Ack, pendingMessage.messageId));
    }
    const char* role = LOCAL_DEVICE == Device::Bubu ? "COORDINATOR" : "PARTICIPANT";
    assert(Serial.log.find(std::string("SLEEP EXECUTION READY | sleepId=40 | role=") + role) != std::string::npos);
    for (int i = 0; i < 100; ++i) loop();
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(coordinatedAttempts == 1 && physicalSleeps == 1);
    assert(armAttempts == 1);
}

void testFinalSendBounds()
{
    // READY is already delivered. Delay final SLEEP_ACK using the harness callback
    // delay; COMMIT must create one new phase budget, not a new transaction.
    const auto prepare = [](uint32_t commitAt)
    {
        freshApp(); loop(); receive(incoming(Type::SleepRequest, 20));
        receive(incoming(Type::Ack, pendingMessage.messageId));
        deferControlsForTest = true;
        const uint32_t hard = transaction().hardDeadline;
        hostNow = commitAt; receive(incoming(Type::SleepCommit, 20, 21));
        assert(transaction().phase == SleepPhase::WAIT_SLEEP_ACK_TX && transaction().active);
        assert(transaction().phaseDeadline == commitAt + 3000 && transaction().hardDeadline == hard);
        assert(controlCount == 1 && countWire(Type::SleepAck) == 0);
        assert(controlStillNeeded(Type::SleepAck, 20) && !controlStillNeeded(Type::SleepReady, 20));
        assert(std::string(toString(transaction().phase)) == "WAIT_SLEEP_ACK_TX");
    };

    prepare(500);
    const uint32_t phase = transaction().phaseDeadline;
    const uint32_t hard = transaction().hardDeadline;
    const auto queued = controlQueue[0];
    hostNow = 700; receive(incoming(Type::SleepCommit, 20, 21));
    assert(transaction().phaseDeadline == phase && transaction().hardDeadline == hard);
    assert(controlCount == 1 && controlQueue[0].notBefore == queued.notBefore);
    assert(controlQueue[0].message.messageId == queued.message.messageId);
    assert(occurrences(Serial.log, "WAIT_COMMIT -> WAIT_SLEEP_ACK_TX") == 1);

    // Rejected submissions and retransmissions retain both absolute deadlines.
    radioAccepts = false; hostNow = 1500; loop();
    assert(transaction().active && waitingForAck && retryCount == 0);
    const auto firstAttempt = ackWaitStart;
    hostNow = 1600; receive(incoming(Type::SleepCommit, 20, 21));
    assert(ackWaitStart == firstAttempt && transaction().phaseDeadline == phase);
    hostNow = 1800; loop();
    assert(retryCount == 1 && transaction().phaseDeadline == phase && transaction().hardDeadline == hard);
    radioAccepts = true; hostNow = 2100; loop();
    assert(localState() == LocalState::SLEEPING && !transaction().active);
    assert(countWire(Type::SleepAck) == 3 && occurrences(Serial.log, "HANDSHAKE_COMPLETE") == 1);
    for (const auto& packet : wire)
        if (packet.type == Type::SleepAck) assert(packet.messageId == queued.message.messageId);

    // Model a stalled/unserviced transport queue. Expiry must purge the unsent
    // ACK before loop can submit it, even though it is now eligible for TX.
    prepare(500);
    const uint32_t finalPhaseDeadline = transaction().phaseDeadline;
    assert(finalPhaseDeadline < transaction().hardDeadline);
    PowerManager::update(finalPhaseDeadline - 1); assert(transaction().active);
    hostNow = finalPhaseDeadline; loop();
    assert(!transaction().active && localState() == LocalState::ACTIVE && controlCount == 0);
    assert(countWire(Type::SleepAck) == 0 && countWire(Type::SleepCancel) == 1);
    assert(Serial.log.find("PHASE_TIMEOUT") != std::string::npos && cooldownLeftMs(finalPhaseDeadline) == 3000);
    PowerManager::controlSent(Type::SleepAck, 20, finalPhaseDeadline + 1);
    assert(localState() == LocalState::ACTIVE); // Late notification cannot complete.
    SleepDecision decision{};
    assert(!takeSleepDecision(decision) && occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);

    // A late but valid COMMIT gets a fresh phase deadline beyond the hard cap.
    // The unchanged hard deadline still cancels first, before any ACK is sent.
    prepare(2500);
    const uint32_t finalHardDeadline = transaction().hardDeadline;
    assert(transaction().phaseDeadline > finalHardDeadline);
    hostNow = 3010; loop(); assert(transaction().active); // Old WAIT_COMMIT limit.
    hostNow = finalHardDeadline; loop();
    assert(!transaction().active && localState() == LocalState::ACTIVE && controlCount == 0);
    assert(countWire(Type::SleepAck) == 0 && Serial.log.find("HARD_TIMEOUT") != std::string::npos);
    assert(!takeSleepDecision(decision) && occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);

    for (bool remoteCancel : {false, true})
    {
        prepare(500);
        hostNow = 800;
        if (remoteCancel) receive(incoming(Type::SleepCancel, 20));
        else activityForTest();
        const auto expected = LocalState::ACTIVE;
        assert(!transaction().active && localState() == expected && controlCount == 0);
        receive(incoming(Type::SleepCommit, 20, 21)); // Stale after cancellation.
        hostNow = 5000; loop();
        assert(localState() == expected && countWire(Type::SleepAck) == 0);
        assert(!takeSleepDecision(decision) && occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
        assert(armAttempts == 0);
    }
}

void testSleepDecisionInterface()
{
    SleepDecision decision{};
    for (bool participant : {false, true})
    {
        freshPower();
        assert(!takeSleepDecision(decision));
        if (participant)
        {
            handleControl(control(Type::SleepRequest, 27), 0);
            handleControl(control(Type::SleepCommit, 27, 28), 10);
            assert(!takeSleepDecision(decision)); // Queued is not submitted.
            controlSent(Type::SleepAck, 27, 20);
        }
        else
        {
            requestSleep(27, 0);
            handleControl(control(Type::SleepReady, 27), 10);
            assert(!takeSleepDecision(decision));
            handleControl(control(Type::SleepAck, 27), 20);
        }
        assert(takeSleepDecision(decision));
        assert(decision.sleepId == 27);
        assert(decision.role == (participant ? SleepRole::PARTICIPANT : SleepRole::COORDINATOR));
        for (int i = 0; i < 10; ++i)
        {
            handleControl(control(participant ? Type::SleepCommit : Type::SleepAck, 27, 28), 30 + i);
            controlSent(Type::SleepAck, 27, 30 + i);
            assert(!takeSleepDecision(decision));
        }
    }

    // Stale controls and cancellation never create a decision.
    freshPower();
    for (auto type : {Type::SleepReady, Type::SleepCommit, Type::SleepAck, Type::SleepCancel})
    {
        handleControl(control(type, 99), 0);
        assert(!takeSleepDecision(decision));
    }
    requestSleep(27, 0); injectActivity(10);
    handleControl(control(Type::SleepAck, 27), 20);
    assert(!takeSleepDecision(decision));

    // A pending decision cannot survive activity wake or initialization.
    for (bool reinitialize : {false, true})
    {
        freshPower(); requestSleep(27, 0);
        handleControl(control(Type::SleepReady, 27), 10);
        handleControl(control(Type::SleepAck, 27), 20);
        if (reinitialize) PowerManager::begin(Device::Bubu, captureIntent);
        else { injectActivity(30); update(280); }
        assert(!takeSleepDecision(decision));
        // The latch is per completion, not a once-per-boot flag.
        assert(requestSleep(28, 300));
        handleControl(control(Type::SleepReady, 28), 310);
        handleControl(control(Type::SleepAck, 28), 320);
        assert(takeSleepDecision(decision) && decision.sleepId == 28);
        assert(!takeSleepDecision(decision));
    }
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    runCase(testFsm);
    runCase(testDelayedCollision);
    runCase(testFinalSendBounds);
    runCase(testSleepDecisionInterface);
    finishSuite("sleep_fsm");
}
