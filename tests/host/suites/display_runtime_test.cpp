#include "../fixtures/scenario_helpers.h"

void testDisplayStartup()
{
    for (bool deep : {false, true}) for (bool displayOk : {false, true})
    for (bool motionOk : {false, true})
    {
        freshApp(); protocolReady = false;
        delete receiveQueue; receiveQueue = nullptr;
        injectedBoot.deep = deep;
        RtcState::save({123, false, 0, {false, 0}});
        injectWakePacket = deep;
        injectedPacket = {1, Type::Event, 70, PEER_DEVICE, Protocol::EventType::Heartbeat, 0};
        motionInitOk = motionOk; displayInitOk = displayOk;
        displayedStatus = {"OFFLINE", "FAR", "CC1101", "ACTIVE", "MOVING"}; // Prior boot must not suppress the first draw.
        atRadioStart = [] { assert(motionInitializations == 1 && displayInitializations == 1); };
        setup();
        assert(protocolReady && motionInitializations == 1 && displayInitializations == 1);
#ifdef DEVICE_DUDU
        assert(i2cHealthReports == 1);
#else
        assert(i2cHealthReports == 0);
#endif
        assert(displayReady == displayOk && displayedStatus.peer == nullptr && displayFrames.empty());
        if (deep) assert(wakeReport.processed && wakeReport.ackSent);
        loop();
        assert(displayFrames.empty()); // Startup probes preserve the existing probe-busy OLED guard.
        classificationSample(800, -50); // A correlated reply provides an idle boundary, still CHECKING.
        assert(displayFrames.size() == (displayOk ? 1U : 0U));
        if (displayOk)
        {
            const auto& frame = displayFrames.back();
            assert(frame.device == DEVICE_NAME && frame.peer == (deep ? "ONLINE" : "UNKNOWN"));
            assert(frame.distance == "CHECKING" && frame.radio == "ESP-NOW" && frame.state == "ACTIVE");
            assert(frame.motion == (motionOk ? "STILL" : "N/A") && motionReady == motionOk);
        }
        for (unsigned i = 0; i < 100; ++i) loop();
        assert(displayInitializations == 1 && displayFrames.size() == (displayOk ? 1U : 0U));
        startHeartbeatEvent(); receive(incoming(Type::Ack, pendingMessage.messageId));
        assert(!waitingForAck && peerState() == PeerState::ONLINE); // Display/Motion init failure cannot gate radio startup.
    }
    puts("PASS: retained wake recovery -> Motion -> Display -> radio startup; initial draw, once-only init, no unchanged redraws, init failure isolation");
}

void testDisplayTransitions()
{
    freshApp(); loop();
    assert(displayFrames.size() == 1 && displayFrames.back().peer == "UNKNOWN");
    notePeerSeen(); loop();
    assert(displayFrames.size() == 2 && displayFrames.back().peer == "ONLINE");
    notePeerUnreachable(); loop();
    assert(displayFrames.size() == 3 && displayFrames.back().peer == "OFFLINE");
    notePeerSeen(); startProximityCheck(hostNow); loop();
    assert(displayFrames.size() == 3); // UNKNOWN now has an outstanding dedicated probe.
    assert(proximityClassification == ProximityClassification::UNKNOWN);
    classificationSample(800, -100);
    assert(displayFrames.size() == 4 && displayFrames.back().distance == "CHECKING");
    classificationSample(801, -50);
    assert(displayFrames.size() == 4 && proximitySampleCount == 2);
    classificationSample(802, -40);
    assert(displayFrames.size() == 5 && displayFrames.back().distance == "CLOSE");
    assert(proximityClassification == ProximityClassification::CLOSE);
    for (unsigned i = 0; i < 100; ++i) loop();
    assert(displayFrames.size() == 5);
    completePolicyMeasurement(-85); loop();
    assert(displayFrames.back().distance == "FAR" && displayFrames.back().radio == "CC1101");
    loop(); assert(displayFrames.back().state == "ACTIVE");
    const auto count = displayFrames.size();
    for (unsigned i = 0; i < 100; ++i) loop();
    assert(displayFrames.size() == count);

    assert(strcmp(displayPeerName(PeerState::SLEEP_PENDING), "SLEEP") == 0);
    assert(strcmp(displayPeerName(PeerState::SLEEPING), "SLEEP") == 0);
    assert(strcmp(displayStateName(LocalState::SLEEP_NEGOTIATING), "SLEEP NEG") == 0);
    assert(strcmp(displayStateName(LocalState::SLEEPING), "SLEEP") == 0);
    assert(strcmp(displayStateName(LocalState::WAKING), "WAKING") == 0);
    freshApp(); startProximityCheck(hostNow); serviceDisplayStatus();
    proximityClassification = ProximityClassification::FAR; // Hidden by CHECKING: same visible text.
    serviceDisplayStatus(); assert(displayFrames.size() == 1 && displayFrames.back().distance == "CHECKING");
    puts("PASS: OLED peer, UNKNOWN/CHECKING/CLOSE/FAR, selected radio and ACTIVE labels; compact sleep labels, visible-text change detection");
}

void isolatedDisplayService()
{
    const auto before = displayObservedRuntime();
    const auto requests = led.requests, userRequests = led.userRequests;
    const auto activityAt = lastMeaningfulActivity;
    const bool sleepArmed = automaticSleepArmed;
    serviceDisplayStatus();
    assert(displayObservedRuntime() == before);
    assert(led.requests == requests && led.userRequests == userRequests);
    assert(lastMeaningfulActivity == activityAt && automaticSleepArmed == sleepArmed);
}

void testDisplayDiagnostic()
{
    freshApp(); notePeerUnreachable(); isolatedDisplayService();
    assert(displayFrames.size() == 1 && displayFrames.back().peer == "OFFLINE");
    notePeerSeen(); proximityClassification = ProximityClassification::CLOSE;
    awakeAckBusy = awakeRuntimeStopped = true;
    for (hostNow = 1; hostNow < 1000; ++hostNow) isolatedDisplayService();
#ifdef DEVICE_DUDU
    assert(Serial.writes == 1 && Serial.capacityChecks == 1);
    assert(Serial.log.find("cache=-/-/-/-/- why=ATTEMPTED cc_stop=0 attempted=1 last_ms=0") != std::string::npos);
#else
    assert(Serial.writes == 0 && Serial.capacityChecks == 0);
#endif
    isolatedDisplayService(); // t=1000: the changed frame was attempted at t=1 despite STOPPED/busy.
    assert(displayFrames.size() == 2);
#ifdef DEVICE_DUDU
    assert(Serial.log.find("OLED DUDU ms=1000 power=ACTIVE/ONLINE prox=CLOSE/READY "
        "want=ONLINE/CLOSE/ESP-NOW/ACTIVE/STILL cache=ONLINE/CLOSE/ESP-NOW/ACTIVE/STILL "
        "why=UNCHANGED cc_stop=1 attempted=2 last_ms=1\n") != std::string::npos);
#endif
    hostNow = 1100; isolatedDisplayService();
    hostNow = 1200; movementState = MovementState::MOVING; isolatedDisplayService();
    hostNow = 1800; movementState = MovementState::READY; isolatedDisplayService();
    assert(displayFrames.size() == 4);
    hostNow = 2000; isolatedDisplayService();
#ifdef DEVICE_DUDU
    assert(Serial.writes == 3 && displayDiagnostic.attempts == 4);
    assert(Serial.log.find("why=UNCHANGED cc_stop=1 attempted=4 last_ms=1800\n") != std::string::npos);
#endif
    // The production loop still consumes packets/ACKs with the OLED blocked.
    awakeRuntimeStopped = false; hostNow = 3000;
    const auto event = proximityObservation(800, -50, hostNow).message;
    queueReceivedData(reinterpret_cast<const uint8_t*>(&event), sizeof(event));
    loop();
    assert(wire.size() == 1 && countWire(Type::Ack) == 1 && led.requests == 1);
    assert(peerState() == PeerState::ONLINE && displayFrames.size() == 4);
#ifdef DEVICE_DUDU
    assert(Serial.log.find("why=CC1101_RUNTIME_BUSY cc_stop=0 attempted=4 last_ms=1800\n") != std::string::npos);
    assert(Serial.writes == 4);
    awakeAckBusy = false; proximityUpdateState = ProximityUpdateState::CHECKING; probeOutstanding = true;
    hostNow = 4000; isolatedDisplayService();
    assert(Serial.log.find("prox=CLOSE/CHECKING want=ONLINE/CHECKING/ESP-NOW/ACTIVE/STILL "
        "cache=ONLINE/CLOSE/ESP-NOW/ACTIVE/STILL why=PROBE_PENDING") != std::string::npos);
    assert(displayFrames.size() == 4 && Serial.writes == 5);
#else
    assert(Serial.log.find("OLED ") == std::string::npos && Serial.writes == 0 && Serial.capacityChecks == 0);
#endif

#ifdef DEVICE_DUDU
    freshKnownApp(); hostNow = UINT32_MAX - 499; isolatedDisplayService();
    assert(Serial.writes == 1);
    hostNow = UINT32_MAX; isolatedDisplayService();
    hostNow = 0; isolatedDisplayService();
    hostNow = 499; isolatedDisplayService();
    assert(Serial.writes == 1);
    hostNow = 500; isolatedDisplayService();
    assert(Serial.writes == 2 && displayFrames.size() == 1);
    assert(Serial.log.find("OLED DUDU ms=500 ") != std::string::npos);

    // Congested serial drops a whole sample, without queuing/retrying it or
    // losing intervening frame-attempt history. Even unavailable OLEDs report.
    freshKnownApp(); Serial.writeCapacity = 0; isolatedDisplayService();
    assert(Serial.writes == 0 && Serial.capacityChecks == 1 && displayFrames.size() == 1);
    hostNow = 100; movementState = MovementState::MOVING; isolatedDisplayService();
    Serial.writeCapacity = 256;
    hostNow = 999; isolatedDisplayService(); assert(Serial.capacityChecks == 1);
    displayReady = false; hostNow = 1000; isolatedDisplayService();
    assert(Serial.writes == 1 && Serial.capacityChecks == 2 && displayFrames.size() == 2);
    assert(Serial.log.find("why=DISPLAY_NOT_READY cc_stop=0 attempted=2 last_ms=100\n") != std::string::npos);

    // Logging neither rearms sleep nor runs during negotiation.
    freshKnownApp();
    isolatedDisplayService(); assert(Serial.log.find("power=ACTIVE/") != std::string::npos);
    requestSleep(nextMessageId++, hostNow);
    hostNow = 1000; isolatedDisplayService();
    assert(Serial.writes == 1 && !automaticSleepArmed);
#endif
    printf("PASS %s: OLED observation during blocked refresh/real-loop RX, intervening attempts, unchanged application state, bounded serial and rollover; Bubu emits no diagnostic\n", DEVICE_NAME);
}

void testDisplayMotion()
{
    freshKnownApp(); loop(); // Isolate motion redraws from UNKNOWN's dedicated probe wait.
    assert(displayFrames.size() == 1 && displayFrames.back().motion == "STILL");
    const auto unchanged = displayFrames.back();
    movementStep(100, MotionEvent::Activity);
    assert(displayFrames.size() == 2 && displayFrames.back().motion == "MOVING");
    for (unsigned i = 0; i < 100; ++i) isolatedDisplayService();
    movementStep(200, MotionEvent::Activity);
    assert(displayFrames.size() == 2);
    movementStep(300, MotionEvent::Inactivity);
    assert(displayFrames.size() == 3 && displayFrames.back().motion == "SETTLING");
    assert(displayFrames.back().peer == unchanged.peer && displayFrames.back().distance == unchanged.distance);
    assert(displayFrames.back().radio == unchanged.radio && displayFrames.back().state == unchanged.state);
    assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 0);
    movementStep(400, MotionEvent::Inactivity);
    movementStep(1299);
    for (unsigned i = 0; i < 100; ++i) isolatedDisplayService();
    assert(displayFrames.size() == 3 && settleStartedAt == 300);
    movementStep(1300); // Existing 1000ms settlement, including its one proximity trigger.
    assert(movementState == MovementState::READY && proximityUpdateState == ProximityUpdateState::CHECKING);
    assert(displayFrames.size() == 4 && displayFrames.back().motion == "STILL");
    assert(displayFrames.back().distance == "CHECKING" && occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
    for (unsigned i = 0; i < 100; ++i) isolatedDisplayService();
    assert(displayFrames.size() == 4);

    // READY must also redraw when motion is the ONLY visible field that changes.
    cancelProximityCheck("TEST");
    movementState = MovementState::WAITING; isolatedDisplayService();
    const auto beforeReady = displayFrames.size();
    movementState = MovementState::READY; isolatedDisplayService();
    assert(displayFrames.size() == beforeReady + 1 && displayFrames.back().motion == "STILL");
    motionReady = false; isolatedDisplayService();
    const auto unavailableFrames = displayFrames.size();
    assert(displayFrames.back().motion == "N/A");
    for (auto state : {MovementState::READY, MovementState::MOVING, MovementState::WAITING})
    {
        movementState = state;
        for (unsigned i = 0; i < 100; ++i) isolatedDisplayService();
        assert(displayFrames.size() == unavailableFrames && displayFrames.back().motion == "N/A");
    }
    puts("PASS: OLED motion STILL -> MOVING -> SETTLING -> STILL follows existing movement/1000ms proximity settlement; motion-only changes redraw once, unchanged/N/A text does not redraw or mutate runtime");
}

void testDisplayGuards()
{
    for (bool stopped : {false, true})
    for (unsigned guard = 0; guard < 13; ++guard)
    {
        if (stopped && guard == 5) continue; // STOPPED/busy alone is covered as eligible below.
        freshApp(); isolatedDisplayService();
        assert(displayFrames.size() == 1);
        awakeAckBusy = awakeRuntimeStopped = stopped;
        const auto oldSnapshot = displayedStatus;
        const auto savedQueue = receiveQueue;
        const auto packet = incoming(Type::Ack, 999);
        switch (guard)
        {
            case 0: protocolReady = false; break;
            case 1: startHeartbeatEvent(); break;
            case 2: controlCount = 1; break;
            case 3: requestSleep(nextMessageId++, hostNow); break;
            case 4: sleepDrainWaiting = true; break;
            case 5: awakeAckBusy = true; break;
            case 6: mockedTxInFlight = 1; break;
            case 7: mockedRxActive = true; break;
            case 8: receiveQueue = nullptr; break;
            case 9: queueReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)); break;
            case 10: probeOutstanding = true; break;
            case 11: displayReady = false; break;
            case 12: deferredWakePending = true; break;
        }
        notePeerUnreachable(); proximityClassification = ProximityClassification::FAR;
        movementState = MovementState::MOVING;
        isolatedDisplayService();
        // Several visible changes while busy must coalesce, without acknowledging a draw.
        proximityClassification = ProximityClassification::CLOSE;
        movementState = MovementState::WAITING;
        const auto log = Serial.log;
        for (unsigned i = 0; i < 100; ++i) isolatedDisplayService();
        assert(displayFrames.size() == 1 && Serial.log == log);
        assert(displayedStatus.peer == oldSnapshot.peer && displayedStatus.distance == oldSnapshot.distance);
        assert(displayedStatus.radio == oldSnapshot.radio && displayedStatus.state == oldSnapshot.state);
        assert(displayedStatus.motion == oldSnapshot.motion);
#ifdef DEVICE_DUDU
        const char* reasons[]{"RUNTIME_NOT_READY", "ACK_PENDING", "CONTROL_QUEUED", "TRANSACTION_ACTIVE",
            "SLEEP_DRAIN", "CC1101_RUNTIME_BUSY", "TX_IN_FLIGHT", "RX_CALLBACK_ACTIVE", "RX_QUEUE_MISSING",
            "RX_QUEUED", "PROBE_PENDING", "DISPLAY_NOT_READY", "CC1101_WAKE_EVENTS_PENDING"};
        hostNow += 1000;
        isolatedDisplayService();
        if (guard == 3) assert(Serial.log == log); // Negotiation is outside normal awake logging.
        else assert(Serial.log.find(std::string("why=") + reasons[guard]) != std::string::npos);
        assert(displayFrames.size() == 1);
#endif
        switch (guard)
        {
            case 0: protocolReady = true; break;
            case 1: handleAck(incoming(Type::Ack, pendingMessage.messageId), Transport::ESP_NOW); break;
            case 2: controlCount = 0; break;
            case 3: injectActivity(hostNow); discardObsoleteControls(); break;
            case 4: sleepDrainWaiting = false; break;
            case 5: awakeAckBusy = false; break;
            case 6: mockedTxInFlight = 0; break;
            case 7: mockedRxActive = false; break;
            case 8: receiveQueue = savedQueue; break;
            case 9: { Protocol::Message out{}; assert(xQueueReceive(receiveQueue, &out, 0) == pdPASS); break; }
            case 10: probeOutstanding = false; break;
            case 11: displayReady = true; break;
            case 12: deferredWakePending = false; break;
        }
        isolatedDisplayService();
        assert(displayFrames.size() == 2 && displayFrames.back().distance == "CLOSE");
        assert(displayFrames.back().peer == displayPeerName(peerState()));
        assert(displayFrames.back().motion == "SETTLING");
        for (unsigned i = 0; i < 100; ++i) isolatedDisplayService();
        assert(displayFrames.size() == 2);
    }
    // A real CC1101-mode probe wait remains protected after its TX callback drains.
    freshApp(); selectedTransport = Transport::CC1101; loop();
    startProximityCheck(hostNow); loop();
    assert(probeOutstanding && displayFrames.size() == 1);
    classificationSample(810, -100); classificationSample(811, -50); classificationSample(812, -40);
    assert(displayFrames.back().distance == "CLOSE");
    loop(); assert(displayFrames.back().radio == "ESP-NOW");
    puts("PASS: every OLED blocker, including deferred work and blockers after CC1101, still defers with STOPPED/busy; cache coalescing, probe wait and runtime isolation retained");
}

void testDisplayStoppedCc1101()
{
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshKnownApp(); notePeerSeen(); selectedTransport = radio;
        awakeAckBusy = awakeRuntimeStopped = true;
        isolatedDisplayService();
        assert(displayFrames.size() == 1 && displayFrames.back().peer == "ONLINE");
        assert(displayFrames.back().distance == "CLOSE" && displayFrames.back().radio == transportName(radio));
#ifdef DEVICE_DUDU
        assert(Serial.log.find("cache=-/-/-/-/- why=ATTEMPTED cc_stop=1 attempted=1 last_ms=0\n") != std::string::npos);
#endif
        hostNow = 1000; notePeerUnreachable(); proximityClassification = ProximityClassification::FAR;
        isolatedDisplayService();
        assert(displayFrames.size() == 2 && displayFrames.back().peer == "OFFLINE" && displayFrames.back().distance == "FAR");
        hostNow = 1200; movementState = MovementState::MOVING; isolatedDisplayService();
        assert(displayFrames.size() == 3 && displayFrames.back().motion == "MOVING");
        for (unsigned i = 0; i < 100; ++i) isolatedDisplayService();
        assert(displayFrames.size() == 3);
        hostNow = 1800; movementState = MovementState::WAITING; isolatedDisplayService();
        assert(displayFrames.size() == 4 && displayFrames.back().motion == "SETTLING");
        hostNow = 2000; movementState = MovementState::READY; isolatedDisplayService();
        assert(displayFrames.size() == 5 && displayFrames.back().state == "ACTIVE" && displayFrames.back().motion == "STILL");
        assert(awakeAckBusy && awakeRuntimeStopped && selectedTransport == radio);
#ifdef DEVICE_DUDU
        assert(Serial.writes == 3 && Serial.capacityChecks == 3 && displayDiagnostic.attempts == 5);
        assert(Serial.log.find("why=ATTEMPTED cc_stop=1 attempted=5 last_ms=2000\n") != std::string::npos);
#else
        assert(Serial.writes == 0 && Serial.capacityChecks == 0);
#endif
    }

    // Display eligibility must not relax any consumer of the strict guard.
    freshKnownApp(); awakeAckBusy = awakeRuntimeStopped = true;
    assert(std::string(sleepTransportBlockedReason()) == "CC1101_RUNTIME_BUSY");
    assert(std::string(sleepEntryBlockedReason()) == "CC1101_RUNTIME_BUSY");
    assert(!productSleepEligible(hostNow));
    automaticSleepArmed = true; serviceAutomaticSleep();
    assert(automaticSleepArmed && !transaction().active && localState() == LocalState::ACTIVE);
    proximityClassification = ProximityClassification::FAR;
    automaticSelectionPending = true; serviceAutomaticTransportSelection();
    assert(automaticSelectionPending && selectedTransport == Transport::ESP_NOW);
    espNowFallbackPending = true; serviceAutomaticTransportSelection();
    assert(espNowFallbackPending && automaticSelectionPending && selectedTransport == Transport::ESP_NOW);
    buttonHeartbeatPending = true; serviceButtonHeartbeat();
    assert(buttonHeartbeatPending && wire.empty() && ccWire.empty() && led.requests == 0);

    // A completed sleep decision still waits and times out on STOPPED/busy.
    completedAwaitingCallbacks(false); mockedTxInFlight = 0;
    awakeAckBusy = awakeRuntimeStopped = true;
    const auto frames = displayFrames.size();
    isolatedDisplayService(); assert(displayFrames.size() == frames);
    loop(); assert(sleepDrainWaiting && coordinatedAttempts == 0 && physicalSleeps == 0);
    hostNow = sleepDrainStarted + SLEEP_DRAIN_TIMEOUT_MS; loop();
    assert(localState() == LocalState::ACTIVE && physicalSleeps == 0 && coordinatedAttempts == 0);
    assert(Serial.log.find("DRAIN_TIMEOUT/CC1101_RUNTIME_BUSY") != std::string::npos);

    // The power-state guard independently protects SLEEPING and WAKING even
    // after the stopped radio's busy indication is ignored for display.
    completedAwaitingCallbacks(false); mockedTxInFlight = 0;
    awakeAckBusy = awakeRuntimeStopped = true;
    const auto sleepingFrames = displayFrames.size();
    sleepDrainWaiting = false; isolatedDisplayService();
    assert(displayFrames.size() == sleepingFrames);
    injectActivity(hostNow); assert(localState() == LocalState::WAKING);
    isolatedDisplayService(); assert(displayFrames.size() == sleepingFrames);

    // Recheck the strict guard both after arming and after the final OLED frame.
    for (bool afterFrame : {false, true})
    {
        completedAwaitingCallbacks(false); mockedTxInFlight = 0;
        if (afterFrame) duringSleepFrame = [] { awakeAckBusy = awakeRuntimeStopped = true; };
        else afterArm = [] { awakeAckBusy = awakeRuntimeStopped = true; };
        loop();
        assert(coordinatedAttempts == 1 && physicalSleeps == 0 && localState() == LocalState::ACTIVE);
        assert(deepSleepFrames.size() == (afterFrame ? 1U : 0U));
    }
    puts("PASS: STOPPED/busy permits initial and changed OLED frames on both transports, retains caching and Dudu-only accounting; sleep admission/drain/final entry, button, selection and fallback remain strict");
}

void testDisplaySleep()
{
    freshApp(); loop();
    assert(deepSleepFrames.empty());
    const auto idleFrames = displayFrames.size();
    requestSleepForTest();
    for (unsigned i = 0; i < 10; ++i) loop();
    assert(localState() == LocalState::SLEEP_NEGOTIATING && displayFrames.size() == idleFrames);
    assert(deepSleepFrames.empty());
    for (bool wakeInstead : {false, true})
    {
        completedAwaitingCallbacks(false);
        const auto frames = displayFrames.size();
        for (unsigned i = 0; i < 20; ++i) loop();
        assert(localState() == LocalState::SLEEPING && displayFrames.size() == frames);
        assert(deepSleepFrames.empty());
        mockedTxInFlight = 0;
        isolatedDisplayService();
        assert(displayFrames.size() == frames && sleepTransportBlockedReason() == nullptr);
        if (wakeInstead)
        {
            injectActivity(hostNow); loop();
            assert(localState() == LocalState::WAKING && displayFrames.size() == frames);
            hostNow += 250; loop();
            assert(localState() == LocalState::ACTIVE && displayFrames.size() == frames + 1);
            assert(displayFrames.back().state == "ACTIVE");
            assert(deepSleepFrames.empty());
        }
        else
        {
            loop(); assert(physicalSleeps == 1 && displayFrames.size() == frames);
            for (unsigned i = 0; i < 100; ++i) loop();
            assert(deepSleepFrames.size() == 1 && deepSleepFrames.back() == DEVICE_NAME && lastDisplayWasDeepSleep);
#ifdef DEVICE_DUDU
            assert(displayDiagnostic.attempts == displayFrames.size() + deepSleepFrames.size());
#endif
        }
    }
    for (unsigned failure = 0; failure < 7; ++failure)
    {
        completedAwaitingCallbacks(false); mockedTxInFlight = 0;
        if (failure == 0) motionPrepareOk = false;
        if (failure == 1) armResult = CC1101SleepArm::Result::RadioUnavailable;
        if (failure == 2) afterArm = [] { motionIntLevel = 1; };
        if (failure == 3) entryFails = true;
        if (failure == 4) returnAfterSleepFrame = true;
        if (failure == 5) duringSleepFrame = [] { motionIntLevel = 1; };
        if (failure == 6) duringSleepFrame = [] { mockedTxInFlight = 1; };
        loop();
        assert(physicalSleeps == 0 && localState() == LocalState::ACTIVE);
        assert(deepSleepFrames.size() == (failure >= 4 ? 1U : 0U));
        mockedTxInFlight = 0; motionIntLevel = 0; loop();
        assert(!lastDisplayWasDeepSleep && displayFrames.back().state == "ACTIVE");
        assert(motionCancels == 1);
#ifdef DEVICE_DUDU
        assert(displayDiagnostic.attempts == displayFrames.size() + deepSleepFrames.size());
#endif
    }
    // Failed/unavailable OLED cannot veto physical entry.
    completedAwaitingCallbacks(false); mockedTxInFlight = 0; displayReady = false; loop();
    assert(physicalSleeps == 1 && deepSleepFrames.empty());
    puts("PASS: no final OLED during ACTIVE/negotiation/semantic SLEEPING/drain or failed Motion/radio/setup; physical entry draws once, post-frame abort restores awake display, no added delay, unavailable OLED does not gate sleep");
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    runCase(testDisplayStartup);
    runCase(testDisplayTransitions);
    runCase(testDisplayDiagnostic);
    runCase(testDisplayMotion);
    runCase(testDisplayGuards);
    runCase(testDisplayStoppedCc1101);
    runCase(testDisplaySleep);
    finishSuite("display_runtime");
}
