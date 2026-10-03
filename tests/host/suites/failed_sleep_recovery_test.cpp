#include "../fixtures/scenario_helpers.h"

Protocol::Message failPhysicalSleep(bool participant, Transport route, uint32_t at = 1000)
{
    completedAwaitingCallbacks(participant);
    const auto finalControl = pendingMessage;
    proximityClassification = ProximityClassification::CLOSE;
    selectedTransport = route;
    hostNow = at; armResult = CC1101SleepArm::Result::NotInRx;
    mockedTxInFlight = 0; loop();
    assert(coordinatedAttempts == 1 && physicalSleeps == 0 && motionCancels == 1);
    assert(localState() == LocalState::ACTIVE && peerState() == PeerState::SLEEPING);
    assert(!waitingForAck && !transaction().active && !sleepDrainWaiting && !automaticSleepArmed);
    assert(cooldownLeftMs(hostNow) > 0 && proximityUpdateState == ProximityUpdateState::READY);
    return finalControl;
}

Protocol::Message postSleepEvent()
{
    suppressPeriodicForTest = false; nextEventTime = hostNow; loop();
    suppressPeriodicForTest = true;
    assert(waitingForAck && pendingMessage.type == Type::Event);
    return pendingMessage;
}

void postSleepActivity()
{
    motionPendingEvent = MotionEvent::Activity; loop();
    assert(localState() == LocalState::ACTIVE && movementState == MovementState::MOVING);
    assert(proximityUpdateState == ProximityUpdateState::READY); // Let the genuine movement settle first.
    motionPendingEvent = MotionEvent::Inactivity; loop();
    assert(movementState == MovementState::WAITING);
    hostNow = settleStartedAt + SETTLE_MS; loop();
    assert(movementState == MovementState::READY && proximityUpdateState == ProximityUpdateState::CHECKING);
}

void testFailedSleepRecoveryEvidence()
{
    for (bool participant : {false, true})
    for (auto route : {Transport::ESP_NOW, Transport::CC1101})
    for (uint16_t eventId : {uint16_t(0xFFFF), uint16_t(0)})
    {
        const auto closed = failPhysicalSleep(participant, route, UINT32_MAX - 100);
        const auto activity = lastMeaningfulActivity;
        const auto cooldown = cooldownLeftMs(hostNow);
        // Delayed negotiation packets and arbitrary new/duplicate application
        // EVENTs (including user intent) cannot establish post-sleep freshness.
        for (auto type : {Type::SleepReady, Type::SleepCommit, Type::SleepAck, Type::SleepCancel})
            receive(incoming(type, closed.ackForMessageId, 21));
        receive(incoming(Type::Ack, closed.messageId));
        auto peerEvent = proximityObservation(800, -50, hostNow).message;
        receiveVia(peerEvent, route); receiveVia(peerEvent, route);
        peerEvent.messageId++; peerEvent.event = Protocol::EventType::UserHeartbeat;
        receiveVia(peerEvent, route);
        assert(peerState() == PeerState::SLEEPING && localState() == LocalState::ACTIVE);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 0);
        hostNow = 100; // Failure and transaction start straddle millis() rollover.
        nextMessageId = eventId; const auto event = postSleepEvent();
        assert(event.messageId == eventId && pendingTransport == route);
        const auto other = route == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW;
        receiveVia(incoming(Type::Ack, event.messageId), other);
        receiveVia(incoming(Type::Ack, uint16_t(event.messageId + 1)), route);
        auto malformed = incoming(Type::Ack, event.messageId); malformed.event = Protocol::EventType::Heartbeat;
        receiveVia(malformed, route);
        malformed = incoming(Type::Ack, event.messageId); malformed.sender = LOCAL_DEVICE;
        receiveVia(malformed, route);
        assert(waitingForAck && peerState() == PeerState::SLEEPING);
        hostNow = ackWaitStart + ACK_TIMEOUT_MS; loop(); // Retry retains its transaction's post-abort eligibility.
        assert(retryCount == 1 && memcmp(&pendingMessage, &event, sizeof(event)) == 0 && pendingTransport == route);
        mockedTxInFlight = 1; // ACK can precede the final transport callback.
        receiveVia(incoming(Type::Ack, event.messageId), route);
        assert(!waitingForAck && peerState() == PeerState::ONLINE);
        assert(sleepRecoveryCheckPending());
        assert(localState() == LocalState::ACTIVE && selectedTransport == route);
        assert(lastMeaningfulActivity == activity && !automaticSleepArmed && cooldownLeftMs(hostNow) < cooldown);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 0);
        for (unsigned i = 0; i < 20; ++i) receive(peerEvent);
        hostNow += CHECK_TIMEOUT_MS + 100; loop(); // Drain deferral has no running measurement timeout.
        assert(proximityUpdateState == ProximityUpdateState::READY && localState() == LocalState::ACTIVE);
        assert(sleepRecoveryCheckPending());
        mockedTxInFlight = 0; loop();
        assert(proximityUpdateState == ProximityUpdateState::CHECKING && !sleepRecoveryCheckPending());
        assert(movementState == MovementState::READY && lastMeaningfulActivity == activity && !automaticSleepArmed);
        const auto began = checkStartedAt;
        // Keep a real EVENT live through completion; selection cannot retarget it.
        const auto active = postSleepEvent();
        classificationSample(810, -52); classificationSample(811, -54); classificationSample(812, -53);
        assert(proximityClassification == ProximityClassification::CLOSE && proximityUpdateState == ProximityUpdateState::READY);
        loop(); assert(waitingForAck && selectedTransport == route && pendingTransport == route);
        assert(memcmp(&pendingMessage, &active, sizeof(active)) == 0);
        mockedTxInFlight = 1;
        receiveVia(incoming(Type::Ack, active.messageId), route);
        assert(selectedTransport == route); // Physical callback drain still governs selection.
        mockedTxInFlight = 0; loop();
        assert(selectedTransport == Transport::ESP_NOW && !automaticSelectionPending);
        for (unsigned i = 0; i < 20; ++i) receive(peerEvent);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | COMPLETE") == 1 && hostNow - began < CHECK_TIMEOUT_MS);
    }
    puts("PASS: failed physical sleep, both roles/radios, exact post-abort EVENT ACK only, ID/time rollover, ACTIVE/cooldown preservation, stationary recovery after drain, CLOSE and immutable transactions/drained selection");
}

void testFailedSleepRecoveryPreAbort()
{
    for (bool participant : {false, true})
    for (auto route : {Transport::ESP_NOW, Transport::CC1101})
    {
        completedAwaitingCallbacks(participant);
        proximityClassification = ProximityClassification::CLOSE; selectedTransport = route;
        // Adversarial arm-boundary injection: start a real transaction BEFORE
        // failure notification. This makes the final drain guard reject entry.
        afterArm = [] { startHeartbeatEvent(); };
        mockedTxInFlight = 0; loop(); afterArm = nullptr;
        assert(coordinatedAttempts == 1 && physicalSleeps == 0 && localState() == LocalState::ACTIVE);
        const auto old = pendingMessage;
        hostNow = ackWaitStart + ACK_TIMEOUT_MS; loop();
        assert(retryCount == 1 && pendingTransport == route && memcmp(&pendingMessage, &old, sizeof(old)) == 0);
        receiveVia(incoming(Type::Ack, old.messageId), route);
        assert(!waitingForAck && peerState() == PeerState::SLEEPING);
        const auto fresh = postSleepEvent();
        receiveVia(incoming(Type::Ack, old.messageId), route);
        assert(waitingForAck && peerState() == PeerState::SLEEPING);
        receiveVia(incoming(Type::Ack, fresh.messageId), route);
        assert(peerState() == PeerState::ONLINE && localState() == LocalState::ACTIVE);
    }
    puts("PASS: retry/matched ACK of a pre-abort EVENT cannot recover; only a newly begun post-abort transaction qualifies");
}

void testFailedSleepRecoveryScheduling()
{
    for (bool participant : {false, true})
    for (bool timeout : {false, true})
    {
        failPhysicalSleep(participant, Transport::CC1101);
        const auto event = postSleepEvent();
        awakeAckBusy = true; // Keep recovery pending until the button owns the outbox.
        receiveVia(incoming(Type::Ack, event.messageId), Transport::CC1101);
        // An actual button press is activity, but its outbox has priority over measurement.
        awakeAckBusy = true; buttonLevel = LOW; loop(); hostNow += BUTTON_DEBOUNCE_MS; loop();
        assert(localState() == LocalState::ACTIVE && buttonHeartbeatPending);
        assert(proximityUpdateState == ProximityUpdateState::READY);
        buttonLevel = HIGH; loop(); hostNow += BUTTON_DEBOUNCE_MS; loop();
        awakeAckBusy = false; loop();
        assert(waitingForAck && pendingMessage.event == Protocol::EventType::UserHeartbeat);
        assert(proximityUpdateState == ProximityUpdateState::READY && sleepRecoveryCheckPending());
        receiveVia(incoming(Type::Ack, pendingMessage.messageId), Transport::CC1101);
        assert(proximityUpdateState == ProximityUpdateState::CHECKING && !sleepRecoveryCheckPending());
        const auto started = checkStartedAt;
        if (timeout)
        {
            classificationSample(820, -53); classificationSample(821, -52);
            hostNow = started + CHECK_TIMEOUT_MS; loop();
            assert(proximityClassification == ProximityClassification::CLOSE && selectedTransport == Transport::CC1101);
            assert(occurrences(Serial.log, "PROXIMITY CHECK | TIMEOUT") == 1);
        }
        else
        {
            classificationSample(820, -53); classificationSample(821, -52); classificationSample(822, -54); loop();
            assert(selectedTransport == Transport::ESP_NOW);
        }
        for (unsigned i = 0; i < 30; ++i) receive(proximityObservation(830+i, -53, hostNow).message);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1 && proximityUpdateState == ProximityUpdateState::READY);
    }
    // Recovery arriving during a motion-started measurement must join that check.
    failPhysicalSleep(false, Transport::CC1101);
    postSleepActivity(); const auto event = postSleepEvent();
    const auto began = checkStartedAt;
    classificationSample(840, -53);
    receiveVia(incoming(Type::Ack, event.messageId), Transport::CC1101);
    assert(peerState() == PeerState::ONLINE && checkStartedAt == began && proximitySampleCount == 1);
    classificationSample(841, -52); classificationSample(842, -54); loop();
    assert(selectedTransport == Transport::ESP_NOW && occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
    puts("PASS: failed-sleep recovery retains one request through button priority, coalesces a running motion check, and consumes completion/timeout without ONLINE restarts");
}

void testFailedSleepRecoverySuperseded()
{
    for (bool participant : {false, true})
    for (bool recovered : {false, true})
    for (bool successful : {false, true})
    {
        failPhysicalSleep(participant, Transport::CC1101);
        const auto old = postSleepEvent();
        if (recovered)
        {
            movementState = MovementState::MOVING; // Keep one unconsumed request until superseded.
            receiveVia(incoming(Type::Ack, old.messageId), Transport::CC1101);
            assert(sleepRecoveryCheckPending());
        }
        else for (unsigned i = 0; i < 3; ++i) { hostNow = ackWaitStart + ACK_TIMEOUT_MS; loop(); }
        hostNow += 3000; loop();
        armResult = CC1101SleepArm::Result::Ready;
        resetMovement(); // New eligible negotiation wins before deferred service.
        if (participant) receive(incoming(Type::SleepRequest, 200));
        else requestSleepForTest();
        const auto newSleepId = transaction().sleepId;
        assert(transaction().active && localState() == LocalState::SLEEP_NEGOTIATING);
        receiveVia(incoming(Type::Ack, old.messageId), Transport::CC1101);
        assert(peerState() == PeerState::SLEEP_PENDING);
        if (!successful)
        {
            receive(incoming(Type::SleepCancel, newSleepId));
            // A genuine button edge updates activity without a movement-triggered check.
            buttonLevel = LOW; loop(); hostNow += BUTTON_DEBOUNCE_MS; loop();
            assert(localState() == LocalState::ACTIVE && waitingForAck);
            receiveVia(incoming(Type::Ack, pendingMessage.messageId), pendingTransport);
            assert(proximityUpdateState == ProximityUpdateState::READY);
            assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 0);
        }
        else
        {
            if (participant)
            {
                receive(incoming(Type::SleepCommit, newSleepId, 201));
                receive(incoming(Type::Ack, pendingMessage.messageId));
            }
            else
            {
                receive(incoming(Type::SleepReady, newSleepId));
                receive(incoming(Type::SleepAck, newSleepId));
            }
            assert(physicalSleeps == 1 && localState() == LocalState::SLEEPING);
            receiveVia(incoming(Type::Ack, old.messageId), Transport::CC1101);
            receive(proximityObservation(850, -53, hostNow).message);
            assert(peerState() == PeerState::SLEEPING && proximityUpdateState == ProximityUpdateState::READY);
            assert(physicalSleeps == 1 && occurrences(Serial.log, "PROXIMITY CHECK | START") == 0);
        }
    }
    puts("PASS: a new negotiation supersedes pending recovery/evidence; late ACKs cannot revive it or undo successful physical sleep");
}

void testFailedSleepRecoveryFallbackTimeout()
{
    for (bool participant : {false, true})
    for (auto classification : {ProximityClassification::CLOSE, ProximityClassification::FAR})
    {
        failPhysicalSleep(participant, Transport::ESP_NOW);
        proximityClassification = classification;
        postSleepEvent(); mockedTxInFlight = 1;
        for (unsigned i = 0; i < 3; ++i) { hostNow = ackWaitStart + ACK_TIMEOUT_MS; loop(); }
        assert(espNowFallbackPending && selectedTransport == Transport::ESP_NOW && peerState() == PeerState::SLEEPING);
        const auto event = postSleepEvent(); receive(incoming(Type::Ack, event.messageId));
        assert(peerState() == PeerState::ONLINE && localState() == LocalState::ACTIVE && espNowFallbackPending);
        assert(sleepRecoveryCheckPending() && proximityUpdateState == ProximityUpdateState::READY);
        mockedTxInFlight = 0; loop();
        assert(!sleepRecoveryCheckPending() && proximityUpdateState == ProximityUpdateState::CHECKING);
        assert(selectedTransport == Transport::ESP_NOW && espNowFallbackPending);
        mockedTxInFlight = 1; // Selection still waits for callbacks after timeout.
        classificationSample(860, -53); classificationSample(861, -52);
        hostNow = checkStartedAt + CHECK_TIMEOUT_MS; loop();
        assert(proximityUpdateState == ProximityUpdateState::READY && proximityClassification == classification);
        assert(espNowFallbackPending && selectedTransport == Transport::ESP_NOW && !automaticSelectionPending);
        mockedTxInFlight = 0; loop();
        assert(selectedTransport == Transport::CC1101 && !espNowFallbackPending);
        for (unsigned i = 0; i < 20; ++i) receive(proximityObservation(870+i, -53, hostNow).message);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | TIMEOUT") == 1);
    }
    puts("PASS: failed-sleep recovery timeout preserves prior CLOSE/FAR and real pending ESP-NOW fallback, including callback drain and no ONLINE restart");
}

void testFailedSleepRecoveryDrains()
{
    for (bool participant : {false, true})
    for (auto route : {Transport::ESP_NOW, Transport::CC1101})
    for (unsigned guard = 0; guard < 13; ++guard)
    {
        failPhysicalSleep(participant, route);
        const auto event = postSleepEvent();
        const auto ack = incoming(Type::Ack, event.messageId);
        // Process evidence without servicing the deferred request yet.
        handleReceivedData(reinterpret_cast<const uint8_t*>(&ack), sizeof(ack), route);
        assert(peerState() == PeerState::ONLINE && sleepRecoveryCheckPending());
        assert(localState() == LocalState::ACTIVE && movementState == MovementState::READY);
        const auto activity = lastMeaningfulActivity;
        const auto savedQueue = receiveQueue;
        switch (guard)
        {
            case 0: startHeartbeatEvent(); break;
            case 1: controlCount = 1; break;
            case 2: mockedTxInFlight = 1; break;
            case 3: mockedRxActive = true; break;
            case 4: assert(xQueueSend(receiveQueue, &ack, 0) == pdPASS); break;
            case 5: deferredWakePending = true; break;
            case 6: awakeAckBusy = true; break;
            case 7: protocolReady = false; break;
            case 8: sleepDrainWaiting = true; break;
            case 9: movementState = MovementState::MOVING; break;
            case 10: movementState = MovementState::WAITING; break;
            case 11: buttonHeartbeatPending = true; break;
            case 12: receiveQueue = nullptr; break;
        }
        for (unsigned i = 0; i < 3; ++i)
        {
            hostNow += CHECK_TIMEOUT_MS;
            serviceProximityProbe(hostNow);
            assert(sleepRecoveryCheckPending());
            assertProximityReset();
            assert(selectedTransport == route && lastMeaningfulActivity == activity && !automaticSleepArmed);
        }
        switch (guard)
        {
            case 0:
            {
                const auto receipt = incoming(Type::Ack, pendingMessage.messageId);
                handleReceivedData(reinterpret_cast<const uint8_t*>(&receipt), sizeof(receipt), route);
                break;
            }
            case 1: controlCount = 0; break;
            case 2: mockedTxInFlight = 0; break;
            case 3: mockedRxActive = false; break;
            case 4: receiveQueue->items.clear(); break;
            case 5: deferredWakePending = false; break;
            case 6: awakeAckBusy = false; break;
            case 7: protocolReady = true; break;
            case 8: sleepDrainWaiting = false; break;
            case 9: case 10: resetMovement(); break;
            case 11: buttonHeartbeatPending = false; break;
            case 12: receiveQueue = savedQueue; break;
        }
        serviceProximityProbe(hostNow);
        assert(!sleepRecoveryCheckPending() && proximityUpdateState == ProximityUpdateState::CHECKING);
        assert(checkStartedAt == hostNow && selectedTransport == route);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        hostNow += CHECK_TIMEOUT_MS; serviceProximityProbe(hostNow);
        assertProximityReset();
        serviceProximityProbe(hostNow + 1);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
    }
    puts("PASS: failed-entry recovery keeps one untimed request through transaction/control/callback/RX/wake/runtime/movement/button blockers; drain starts one bounded check without activity or forced radio selection");
}

void testFailedSleepRecoveryMovement()
{
    for (bool participant : {false, true})
    for (auto route : {Transport::ESP_NOW, Transport::CC1101})
    for (uint32_t start : {1000U, UINT32_MAX - 1000})
    {
        failPhysicalSleep(participant, route, start);
        const auto event = postSleepEvent();
        movementStep(start + 100, MotionEvent::Activity);
        receiveVia(incoming(Type::Ack, event.messageId), route);
        assert(sleepRecoveryCheckPending() && movementState == MovementState::MOVING);
        movementStep(start + 200, MotionEvent::Inactivity);
        movementStep(start + 200 + SETTLE_MS - 1);
        assert(sleepRecoveryCheckPending() && proximityUpdateState == ProximityUpdateState::READY);
        movementStep(start + 200 + SETTLE_MS, MotionEvent::Activity); // Activity wins at expiry.
        const auto activity = lastMeaningfulActivity;
        assert(movementState == MovementState::MOVING && sleepRecoveryCheckPending());
        movementStep(start + 1400, MotionEvent::Inactivity);
        mockedTxInFlight = 1;
        movementStep(start + 1400 + SETTLE_MS);
        assert(movementState == MovementState::READY && sleepRecoveryCheckPending());
        assert(proximityUpdateState == ProximityUpdateState::READY);
        mockedTxInFlight = 0; loop();
        assert(proximityUpdateState == ProximityUpdateState::CHECKING && !sleepRecoveryCheckPending());
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        assert(lastMeaningfulActivity == activity && automaticSleepArmed && selectedTransport == route);
    }
    puts("PASS: recovery respects real movement, repeated activity at settlement, final callback drain and millis rollover on both radios/roles");
}

void testFailedSleepRecoveryOfflineFallback()
{
    for (bool participant : {false, true})
    {
        failPhysicalSleep(participant, Transport::ESP_NOW);
        const auto recovered = postSleepEvent();
        mockedTxInFlight = 1;
        receive(incoming(Type::Ack, recovered.messageId));
        assert(sleepRecoveryCheckPending() && peerState() == PeerState::ONLINE);
        postSleepEvent();
        for (unsigned i = 0; i <= MAX_RETRIES; ++i) { hostNow = ackWaitStart + ACK_TIMEOUT_MS; loop(); }
        assert(peerState() == PeerState::OFFLINE && espNowFallbackPending && sleepRecoveryCheckPending());
        assert(selectedTransport == Transport::ESP_NOW);
        mockedTxInFlight = 0; loop();
        assert(selectedTransport == Transport::CC1101 && !espNowFallbackPending);
        assert(sleepRecoveryCheckPending() && proximityUpdateState == ProximityUpdateState::READY);
        const auto returned = postSleepEvent();
        receiveVia(incoming(Type::Ack, returned.messageId), Transport::CC1101);
        assert(peerState() == PeerState::ONLINE && !sleepRecoveryCheckPending());
        assert(proximityUpdateState == ProximityUpdateState::CHECKING && selectedTransport == Transport::CC1101);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        completePeerReturnMeasurement(-50);
        assert(selectedTransport == Transport::ESP_NOW && proximityClassification == ProximityClassification::CLOSE);
        assert(!automaticSleepArmed && localState() == LocalState::ACTIVE);
    }
    puts("PASS: a later OFFLINE event failure can still apply drained fallback while recovery is pending; returning ACK consumes one measured recovery, without movement");
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    runCase(testFailedSleepRecoveryEvidence);
    runCase(testFailedSleepRecoveryPreAbort);
    runCase(testFailedSleepRecoveryScheduling);
    runCase(testFailedSleepRecoverySuperseded);
    runCase(testFailedSleepRecoveryFallbackTimeout);
    runCase(testFailedSleepRecoveryDrains);
    runCase(testFailedSleepRecoveryMovement);
    runCase(testFailedSleepRecoveryOfflineFallback);
    finishSuite("failed_sleep_recovery");
}
