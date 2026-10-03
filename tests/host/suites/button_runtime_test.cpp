#include "../fixtures/scenario_helpers.h"

void buttonStep(uint32_t now, int level)
{
    hostNow = now; buttonLevel = level; loop();
}

void buttonPress(uint32_t at)
{
    buttonStep(at, LOW); buttonStep(at + 30, LOW);
}

void buttonRelease(uint32_t at)
{
    buttonStep(at, HIGH); buttonStep(at + 30, HIGH);
}

void testButtonDebounce()
{
    for (uint32_t at : {uint32_t(1000), UINT32_MAX - 20})
    {
        freshKnownApp(); const auto activity = lastMeaningfulActivity;
        buttonStep(at - 100, HIGH);
        buttonStep(at, LOW); buttonStep(at + 5, HIGH);
        buttonStep(at + 10, LOW); buttonStep(at + 20, HIGH);
        buttonStep(at + 25, LOW); buttonStep(at + 54, LOW);
        assert(buttonEventPackets() == 0 && led.requests == 0 && !buttonHeartbeatPending);
        assert(lastMeaningfulActivity == activity && !automaticSleepArmed);
        buttonStep(at + 55, LOW);
        assert(buttonEventPackets() == 1 && led.requests == 0 && !buttonHeartbeatPending);
        assert(lastMeaningfulActivity == uint32_t(at + 55) && automaticSleepArmed);
        assert(occurrences(Serial.log, "BUTTON | PRESSED") == 1);
        hostNow = at + 65; receive(incoming(Type::Ack, pendingMessage.messageId));
        for (uint32_t elapsed = 100; elapsed <= 2200; elapsed += 10) buttonStep(at + elapsed, LOW);
        assert(buttonEventPackets() == 1 && lastMeaningfulActivity == uint32_t(at + 55));
        // Release bounce cannot rearm the next press until HIGH is stable too.
        buttonStep(at + 2300, HIGH); buttonStep(at + 2310, LOW);
        buttonStep(at + 2320, HIGH); buttonStep(at + 2349, HIGH);
        assert(buttonStablePressed);
        buttonStep(at + 2350, HIGH); assert(!buttonStablePressed);
        assert(lastMeaningfulActivity == uint32_t(at + 55) && buttonEventPackets() == 1);
        buttonPress(at + 2360);
        assert(buttonEventPackets() == 2 && led.requests == 0);
        assert(lastMeaningfulActivity == uint32_t(at + 2390));
        assert(occurrences(Serial.log, "BUTTON | PRESSED") == 2);
        const auto now = hostNow;
        for (unsigned i = 0; i < 100; ++i) serviceButton();
        assert(hostNow == now); // Debounce contains no blocking delay.
    }
    for (bool deep : {false, true}) for (int level : {HIGH, LOW})
    {
        freshApp(); protocolReady = false; delete receiveQueue; receiveQueue = nullptr;
        injectedBoot.deep = deep; buttonLevel = level; setup();
        assert(buttonConfigurations == 1 && BUTTON_PIN == 5 && !buttonHeartbeatPending);
        const auto at = buttonChangedAt;
        buttonStep(at + 29, level); assert(!buttonHeartbeatPending);
        buttonStep(at + 30, level);
        assert(!buttonHeartbeatPending);
        assert(occurrences(Serial.log, "BUTTON | PRESSED") == unsigned(level == LOW));
        assert(buttonEventPackets() == unsigned(level == LOW) && led.requests == 0);
        if (level == LOW) assert(waitingForAck && pendingTransport == Transport::CC1101);
        assert(proximityClassification == ProximityClassification::UNKNOWN);
    }
    puts("PASS: button GPIO5 INPUT_PULLUP, HIGH release/LOW press, stable 30ms edges, bounce/hold/release, rollover and cold/deep startup");
}

void testButtonDeepSleepWakeIntent()
{
    for (uint64_t mask : {0x20ULL, 0x30ULL}) for (int level : {HIGH, LOW})
    {
        freshApp(); protocolReady = false; delete receiveQueue; receiveQueue = nullptr;
        injectedBoot.deep = true; injectedBoot.cause = CC1101WakeRecovery::Cause::Gpio;
        injectedBoot.gpioMask = mask; buttonLevel = level; setup();
        // HIGH already at startup represents a press released before beginButton.
        assert(bootInfo.gpioMask == mask && buttonHeartbeatPending && buttonWakeIntentHeld);
        assert(buttonStablePressed && buttonRawPressed == (level == LOW));
        assert(wakeRecoveries == 1 && armInitializations == 0 && wakeEvents.empty());
        assert(occurrences(Serial.log, "BUTTON WAKE | DETECTED | GPIO5") == 1);
        assert(occurrences(Serial.log, "one UserHeartbeat intent preserved") == 1);
        const auto at = buttonChangedAt;
        const auto id = nextMessageId;
        awakeAckBusy = true; // Preserve intent while retained/runtime work drains.
        for (unsigned elapsed = 0; elapsed < 1000; elapsed += 10) buttonStep(at + elapsed, level);
        assert(buttonHeartbeatPending && buttonWakeIntentHeld && buttonEventPackets() == 0);
        assert(occurrences(Serial.log, "BUTTON | PRESSED") == 0 && nextMessageId == id);
        assert(occurrences(Serial.log, "BUTTON HEARTBEAT | SEND") == 0 && wakeEvents.empty());
        assert(occurrences(Serial.log, "BUTTON | RELEASED") == unsigned(level == HIGH));
        assert(buttonStablePressed == (level == LOW));

        // Held wake: release bounce does not rearm; only 30ms continuously HIGH does.
        if (level == LOW)
        {
            buttonStep(at + 1000, HIGH); buttonStep(at + 1010, LOW);
            buttonStep(at + 1020, HIGH); buttonStep(at + 1049, HIGH);
            assert(buttonStablePressed && occurrences(Serial.log, "BUTTON | RELEASED") == 0);
            buttonStep(at + 1050, HIGH);
            assert(!buttonStablePressed && occurrences(Serial.log, "BUTTON | RELEASED") == 1);
        }
        buttonStep(at + 1100, LOW); buttonStep(at + 1129, LOW);
        assert(occurrences(Serial.log, "BUTTON | PRESSED") == 0);
        buttonStep(at + 1130, LOW);
        assert(occurrences(Serial.log, "BUTTON | PRESSED") == 1);
        // Repress is debounced activity, coalesced into the same held one-slot intent.
        assert(buttonHeartbeatPending && buttonWakeIntentHeld && buttonEventPackets() == 0);
        assert(occurrences(Serial.log, "one UserHeartbeat intent preserved") == 1 && wakeEvents.empty());
        buttonRelease(at + 1200);
        assert(std::string(sleepEntryBlockedReason()) == "BUTTON_WAKE_INTENT_HELD");
        awakeAckBusy = false; wakeTxResult = CC1101WakeTx::Result::Acked;
        hostNow = at + 1300; loop();
        assert(!buttonHeartbeatPending && !buttonWakeIntentHeld && buttonEventPackets() == 1);
        assert(wakeEvents.size() == 1 && pendingMessage.event == Protocol::EventType::UserHeartbeat);
        assert(pendingMessage.messageId != wakeEvents.front().messageId && pendingTransport == Transport::CC1101);
        receiveVia(incoming(Type::Ack, pendingMessage.messageId), Transport::CC1101);
        hostNow = lastMeaningfulActivity + 35000;
        serviceProximityCheck(hostNow); // Allow the existing peer-return check to finish its bounded timeout.
        assert(productSleepEligible(hostNow) && sleepEntryBlockedReason() == nullptr);
        for (unsigned i = 0; i < 100; ++i) serviceButtonHeartbeat();
        assert(wakeEvents.size() == 1); // No repeated peer-wake episode after handoff.
    }
    // A mask without GPIO wake evidence must not manufacture a wake-origin intent.
    for (auto cause : {CC1101WakeRecovery::Cause::Cold, CC1101WakeRecovery::Cause::Timer})
    {
        freshApp(); protocolReady = false; injectedBoot.deep = cause != CC1101WakeRecovery::Cause::Cold;
        injectedBoot.cause = cause; injectedBoot.gpioMask = 0x20; setup();
        assert(!buttonHeartbeatPending && !buttonWakeIntentHeld);
        assert(occurrences(Serial.log, "BUTTON WAKE |") == 0);
        buttonPress(buttonChangedAt + 100);
        assert(buttonEventPackets() == 1 && !buttonHeartbeatPending); // Ordinary awake path still works.
    }

    freshAutomaticRuntime(); buttonHeartbeatPending = true; hostNow = 35000;
    assert(!productSleepEligible(hostNow));
    assert(std::string(sleepEntryBlockedReason()) == "BUTTON_HEARTBEAT_PENDING");
    serviceButtonHeartbeat(); // Sleep-only guard must not deadlock the ordinary send path.
    assert(!buttonHeartbeatPending && waitingForAck && buttonEventPackets() == 1);
    for (bool wakeHeld : {false, true})
    {
        completedAwaitingCallbacks(false); mockedTxInFlight = 0;
        buttonHeartbeatPending = true; buttonWakeIntentHeld = wakeHeld;
        loop();
        assert(coordinatedAttempts == 0 && physicalSleeps == 0 && buttonHeartbeatPending);
        assert(buttonWakeIntentHeld == wakeHeld && sleepDrainWaiting);
    }
    for (bool duringFrame : {false, true})
    {
        completedAwaitingCallbacks(false); mockedTxInFlight = 0;
        if (duringFrame) duringSleepFrame = [] { buttonLevel = LOW; buttonStablePressed = true; };
        else afterArm = [] { buttonLevel = LOW; buttonStablePressed = true; };
        loop();
        assert(coordinatedAttempts == 1 && physicalSleeps == 0 && motionCancels == 1);
        assert(std::string(sleepEntryBlockedReason()) == "BUTTON_LOW");
        assert(occurrences(Serial.log, "BUTTON SLEEP | BLOCKED | reason=BUTTON_LOW") == 1);
    }
    puts("PASS: GPIO5-only/coincident GPIO4+5 wake latch preserves one intent after early release; stable release/repress, held duplicate suppression, drain then one result-aware handoff, fresh UserHeartbeat ID, normal sleep eligibility and final guards");
}

void testButtonWakeHandoff()
{
    for (uint64_t mask : {0x20ULL, 0x28ULL, 0x30ULL, 0x38ULL}) for (int level : {HIGH, LOW})
    {
        setupButtonWakeTest(mask, level);
        const auto activity = lastMeaningfulActivity;
        assert(automaticSleepArmed && activity == buttonChangedAt && wakeEvents.empty());
        assert(buttonHeartbeatPending && buttonWakeIntentHeld && led.requests == 0);
        suppressPeriodicForTest = false; nextEventTime = hostNow;
        movementState = MovementState::MOVING; // Button handoff cannot depend on settling/classification.
        wakeTxElapsedMs = 900; loop();
        assert(wakeEvents.size() == 1 && buttonEventPackets() == 1 && !buttonWakeIntentHeld);
        assert(!buttonHeartbeatPending && !buttonWakeRetryOnPress && waitingForAck);
        assert(wakeEvents[0].event == Protocol::EventType::Heartbeat && wakeEvents[0].messageId == 100);
        assert(pendingMessage.event == Protocol::EventType::UserHeartbeat && pendingMessage.messageId == 101);
        assert(pendingTransport == Transport::CC1101 && proximityClassification == ProximityClassification::UNKNOWN);
        assert(led.requests == 0 && occurrences(Serial.log, "BUTTON | PRESSED") == 0);
        assert(occurrences(Serial.log, "BUTTON WAKE | PEER_WAKE | one bounded episode") == 1);
        assert(occurrences(Serial.log, "MOTION PEER WAKE |") == 0); // GPIO3+5 coalesce into the button episode.
        const auto user = pendingMessage;
        const auto sentAt = ackWaitStart;
        receive(incoming(Type::Ack, user.messageId)); // Wrong radio cannot complete the application packet.
        assert(waitingForAck && memcmp(&pendingMessage, &user, sizeof(user)) == 0);
        hostNow = sentAt + 300; loop(); hostNow = sentAt + 600; loop();
        assert(retryCount == 2 && buttonEventPackets() == 3 && wakeEvents.size() == 1);
        for (const auto& packet : ccWire) if (packet.type == Type::Event)
            assert(memcmp(&packet, &user, sizeof(user)) == 0);
        receiveVia(incoming(Type::Ack, user.messageId), Transport::CC1101);
        assert(!waitingForAck && led.requests == 0 && lastMeaningfulActivity == activity);
        suppressPeriodicForTest = true;
        for (unsigned i = 0; i < 100; ++i) serviceButtonHeartbeat();
        assert(wakeEvents.size() == 1 && occurrences(Serial.log, "BUTTON | PRESSED") == 0);
        buttonRelease(hostNow + 50); resetMovement();
        hostNow = activity + 34999; serviceProximityCheck(hostNow);
        assert(!productSleepEligible(hostNow));
        hostNow = activity + 35000;
        assert(productSleepEligible(hostNow) && sleepEntryBlockedReason() == nullptr);
        loop(); const auto sleepId = transaction().sleepId;
        assert(countWire(Type::SleepRequest) == 1);
        receive(incoming(Type::SleepReady, sleepId)); receive(incoming(Type::SleepAck, sleepId));
        assert(physicalSleeps == 1 && wakeEvents.size() == 1); // Full existing coordinator path is available again.
    }
    // A frame arriving after the wake result is returned still drains before the user packet.
    setupButtonWakeTest();
    duringWakeTx = [] {
        ccIncoming.push_back({Protocol::VERSION, Type::Event, 70, PEER_DEVICE, Protocol::EventType::Heartbeat, 0});
    };
    loop();
    assert(wakeEvents.size() == 1 && !buttonWakeIntentHeld && buttonHeartbeatPending && !waitingForAck);
    assert(!ccIncoming.empty() && buttonEventPackets() == 0);
    duringWakeTx = nullptr; loop();
    assert(ccIncoming.empty() && lastPeerEventId == 70 && buttonEventPackets() == 1 && wakeEvents.size() == 1);
    puts("PASS: GPIO5/3+5/4+5/3+4+5 wake, activity timer, one ACK+RX_READY handoff, UNKNOWN CC1101/fresh ID, immutable application retries/no sender pulse, coincident drain and later coordinated sleep");
}

void testButtonWakeFailures()
{
    using Result = CC1101WakeTx::Result;
    for (auto result : {Result::Acked, Result::AckTimeout, Result::RadioUnavailable,
                        Result::Busy, Result::TxFailed, Result::InvalidAck})
    for (bool ready : {false, true})
    {
        if (result == Result::Acked && ready) continue;
        setupButtonWakeTest(0x20, LOW); wakeTxResult = result; wakeTxRxReady = ready;
        loop();
        assert(wakeEvents.size() == 1 && !buttonWakeIntentHeld && !buttonHeartbeatPending);
        assert(buttonWakeRetryOnPress && !waitingForAck && buttonEventPackets() == 0);
        assert(occurrences(Serial.log, "BUTTON WAKE | HANDOFF") == 0);
        assert(occurrences(Serial.log, "BUTTON WAKE | GIVE_UP") == 1);
        if (result == Result::Acked) assert(Serial.log.find("reason=LOCAL_RX_NOT_READY") != std::string::npos);
        for (unsigned i = 0; i < 200; ++i) loop();
        assert(wakeEvents.size() == 1 && occurrences(Serial.log, "BUTTON | PRESSED") == 0);
        assert(occurrences(Serial.log, "BUTTON WAKE | GIVE_UP") == 1);
        buttonRelease(hostNow + 50);
        hostNow = lastMeaningfulActivity + 35000;
        assert(productSleepEligible(hostNow) && sleepEntryBlockedReason() == nullptr);
        // Only a new stable press starts another episode; no loop-based retry.
        wakeTxResult = Result::Acked; wakeTxRxReady = true;
        buttonPress(hostNow + 50);
        assert(wakeEvents.size() == 2 && waitingForAck && buttonEventPackets() == 1);
        assert(!buttonWakeRetryOnPress && !buttonWakeIntentHeld && !buttonHeartbeatPending);
        assert(pendingMessage.messageId != wakeEvents.back().messageId);
    }
    setupButtonWakeTest(); mockedTxInFlight = 1;
    hostNow = buttonWakeHeldAt + 2999; loop();
    assert(buttonWakeIntentHeld && wakeEvents.empty());
    hostNow = buttonWakeHeldAt + 3000; loop();
    assert(!buttonWakeIntentHeld && !buttonHeartbeatPending && wakeEvents.empty());
    assert(Serial.log.find("reason=DRAIN_TIMEOUT") != std::string::npos);
    mockedTxInFlight = 0;
    for (unsigned i = 0; i < 100; ++i) loop();
    assert(wakeEvents.empty());

    // Exhausting the local wake wait never clears or mutates an unrelated application outbox.
    setupButtonWakeTest(); startHeartbeatEvent(); const auto inFlight = pendingMessage;
    hostNow = buttonWakeHeldAt + 3000; serviceButtonHeartbeat();
    assert(waitingForAck && memcmp(&pendingMessage, &inFlight, sizeof(inFlight)) == 0);
    assert(!buttonWakeIntentHeld && !buttonHeartbeatPending && wakeEvents.empty());
    setupButtonWakeTest(); awakeRuntimeStopped = true; loop();
    assert(!buttonWakeIntentHeld && !buttonHeartbeatPending && wakeEvents.empty());
    assert(Serial.log.find("reason=CC1101_RUNTIME_STOPPED") != std::string::npos);
    setupButtonWakeTest(0x20, HIGH, false, false, false);
    assert(!protocolReady && !buttonWakeIntentHeld && !buttonHeartbeatPending && wakeEvents.empty());
    for (unsigned i = 0; i < 100; ++i) loop();
    assert(occurrences(Serial.log, "BUTTON WAKE | GIVE_UP") == 1);
    assert(Serial.log.find("reason=RUNTIME_INIT_FAILED") != std::string::npos);
    // millis rollover does not extend the admission deadline.
    freshApp(); hostNow = UINT32_MAX - 1000; bootInfo.deep = true;
    bootInfo.cause = CC1101WakeRecovery::Cause::Gpio; bootInfo.gpioMask = 0x20; beginButton();
    mockedTxInFlight = 1; hostNow = buttonWakeHeldAt + 3000; serviceButtonHeartbeat();
    assert(!buttonWakeIntentHeld && !buttonHeartbeatPending);
    puts("PASS: ACK and readiness fail independently; all terminal wake results clear intent without delivery/new EVENT, stable release+new press retries, 3s drain bound/rollover, stopped/runtime-init cutoff and immutable foreign outbox");
}

void testButtonActivityAndSleep()
{
    freshKnownApp(); loop(); buttonPress(1000);
    assert(localState() == LocalState::ACTIVE && lastMeaningfulActivity == 1030 && automaticSleepArmed);
    hostNow = 1050; receive(incoming(Type::Ack, pendingMessage.messageId));
    buttonStep(1030 + 34999, LOW); assert(countWire(Type::SleepRequest) == 0);
    buttonStep(1030 + 35000, LOW);
    assert(countWire(Type::SleepRequest) == 0 && automaticSleepArmed);
    assert(lastMeaningfulActivity == 1030); // Held LOW vetoes sleep without becoming repeated activity.
    assert(occurrences(Serial.log, "BUTTON SLEEP | BLOCKED | reason=BUTTON_LOW") == 1);
    buttonRelease(1030 + 35010);
    assert(countWire(Type::SleepRequest) == 1 && !automaticSleepArmed);
    for (bool participant : {false, true})
    {
        freshKnownApp(); loop();
        if (participant) receive(incoming(Type::SleepRequest, 20)); else requestSleepForTest();
        assert(transaction().active);
        buttonPress(100);
        assert(localState() == LocalState::ACTIVE && !transaction().active);
        assert(lastMeaningfulActivity == 130 && automaticSleepArmed);
        assert(countWire(Type::SleepCancel) == 1 && cooldownLeftMs(130) == 3000);
        assert(buttonEventPackets() == 1 && led.requests == 0);

        completedAwaitingCallbacks(participant); proximityClassification = ProximityClassification::CLOSE;
        const auto at = hostNow + 100; buttonPress(at);
        assert(localState() == LocalState::WAKING && !transaction().active);
        assert(buttonHeartbeatPending && buttonEventPackets() == 0 && physicalSleeps == 0);
        assert(lastMeaningfulActivity == at + 30 && automaticSleepArmed);
        SleepDecision decision{}; assert(!takeSleepDecision(decision));
        assert(countWire(Type::SleepCancel) == 0); // Committed sleep is revoked via existing WAKING semantics.
        mockedTxInFlight = 0; buttonStep(at + 280, LOW);
        assert(localState() == LocalState::ACTIVE && buttonEventPackets() == 1 && !buttonHeartbeatPending);
    }

    freshAutomaticRuntime(); buttonStep(34999, LOW); buttonStep(35000, LOW); buttonStep(35028, LOW);
    assert(countWire(Type::SleepRequest) == 0 && lastMeaningfulActivity == 0 && automaticSleepArmed);
    buttonStep(35029, LOW);
    assert(lastMeaningfulActivity == 35029 && !buttonHeartbeatPending && physicalSleeps == 0);
    assert(buttonEventPackets() == 1 && pendingTransport == Transport::CC1101);
    assert(countWire(Type::SleepRequest) == 0);
    freshAutomaticRuntime(); buttonStep(34999, LOW); buttonStep(35000, HIGH);
    assert(countWire(Type::SleepRequest) == 1 && lastMeaningfulActivity == 0);
    assert(!buttonHeartbeatPending && occurrences(Serial.log, "BUTTON | PRESSED") == 0);

    // The same temporary guard applies to fresh peer admission, not just our own initiation.
    freshAutomaticRuntime(); buttonStep(31999, LOW);
    hostNow = 32000; receive(incoming(Type::SleepRequest, 20)); assert(!transaction().active);
    buttonLevel = HIGH; hostNow = 32010; receive(incoming(Type::SleepRequest, 21));
    assert(transaction().active && transaction().role == SleepRole::PARTICIPANT);
    for (bool valid : {false, true})
    {
        completedAwaitingCallbacks(false); mockedTxInFlight = 0;
        const auto at = hostNow;
        afterAwakeService = [] { buttonLevel = LOW; afterAwakeService = nullptr; };
        loop();
        assert(localState() == LocalState::SLEEPING && physicalSleeps == 0 && coordinatedAttempts == 0);
        assert(sleepDrainWaiting && !buttonHeartbeatPending);
        buttonStep(at + 30, valid ? LOW : HIGH);
        assert(physicalSleeps == unsigned(!valid));
        if (valid) assert(localState() == LocalState::WAKING && lastMeaningfulActivity == at + 30);
    }
    // Also catch a LOW that arrives after decision consumption, during radio arm or final OLED rendering.
    for (bool duringFrame : {false, true})
    {
        completedAwaitingCallbacks(false); mockedTxInFlight = 0;
        if (duringFrame) duringSleepFrame = [] { buttonLevel = LOW; };
        else afterArm = [] { buttonLevel = LOW; };
        loop(); assert(physicalSleeps == 0 && coordinatedAttempts == 1 && motionCancels == 1);
        const auto at = hostNow; buttonStep(at, LOW); buttonStep(at + 30, LOW);
        assert(lastMeaningfulActivity == at + 30 && automaticSleepArmed && !buttonHeartbeatPending);
        assert(buttonEventPackets() == 1 && pendingTransport == Transport::CC1101);
    }
    puts("PASS: button uses existing activity/rearm/cancel/revoke semantics; candidate defers local/peer sleep and late physical entry, then resolves without changing the 35s policy");
}

void testButtonPendingGuards()
{
    for (unsigned guard = 0; guard < 10; ++guard)
    {
        freshKnownApp(); buttonLevel = LOW; hostNow = 100; serviceButton(); hostNow = 130; serviceButton();
        assert(buttonHeartbeatPending && lastMeaningfulActivity == 130);
        const auto queue = receiveQueue;
        switch (guard)
        {
            case 0: protocolReady = false; break;
            case 1: startHeartbeatEvent(); break;
            case 2: controlCount = 1; break;
            case 3: assert(requestSleep(nextMessageId++, hostNow)); controlCount = 0; break;
            case 4: sleepDrainWaiting = true; break;
            case 5: awakeAckBusy = true; break;
            case 6: mockedTxInFlight = 1; break;
            case 7: mockedRxActive = true; break;
            case 8: receiveQueue = nullptr; break;
            case 9: { const auto packet = incoming(Type::Ack, 99);
                queueReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)); break; }
        }
        const auto pending = pendingMessage;
        const auto id = nextMessageId; const auto events = buttonEventPackets(), leds = size_t(led.requests);
        for (unsigned i = 0; i < 100; ++i) serviceButtonHeartbeat();
        assert(buttonHeartbeatPending && buttonEventPackets() == events && led.requests == leds);
        assert(nextMessageId == id && memcmp(&pendingMessage, &pending, sizeof(pending)) == 0);
        switch (guard)
        {
            case 0: protocolReady = true; break;
            case 1: waitingForAck = false; break;
            case 2: controlCount = 0; break;
            case 3: injectActivity(hostNow); discardObsoleteControls(); controlCount = 0; break;
            case 4: sleepDrainWaiting = false; break;
            case 5: awakeAckBusy = false; break;
            case 6: mockedTxInFlight = 0; break;
            case 7: mockedRxActive = false; break;
            case 8: receiveQueue = queue; break;
            case 9: receiveQueue->items.clear(); break;
        }
        serviceButtonHeartbeat();
        assert(!buttonHeartbeatPending && buttonEventPackets() == events + 1 && led.requests == leds);
        for (unsigned i = 0; i < 100; ++i) serviceButtonHeartbeat();
        assert(buttonEventPackets() == events + 1);
    }
    freshKnownApp(); buttonPress(100); const auto first = pendingMessage;
    buttonRelease(150); buttonPress(190); buttonRelease(230); buttonPress(270);
    assert(buttonHeartbeatPending && buttonEventPackets() == 1 && led.requests == 0);
    assert(memcmp(&pendingMessage, &first, sizeof(first)) == 0 && ackWaitStart == 130 && retryCount == 0);
    assert(lastMeaningfulActivity == 300 && occurrences(Serial.log, "BUTTON | PRESSED") == 3);
    assert(occurrences(Serial.log, "BUTTON HEARTBEAT | PENDING") == 2); // Third press coalesces, but is still activity.
    hostNow = 310; receive(incoming(Type::Ack, first.messageId));
    assert(!buttonHeartbeatPending && buttonEventPackets() == 2 && pendingMessage.messageId != first.messageId);
    hostNow = 320; receive(incoming(Type::Ack, pendingMessage.messageId));
    buttonStep(5000, LOW);
    assert(buttonEventPackets() == 2 && led.requests == 0 && lastMeaningfulActivity == 300);
    puts("PASS: one pending button slot, coalescing, immutable in-flight EVENT, one follow-up after drain and all shared runtime/transport guards");
}

void testButtonTransportsAndPriority()
{
    for (auto route : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshKnownApp(); selectedTransport = route; nextMessageId = 0xFFFF; buttonPress(100);
        const auto original = pendingMessage;
        assert(pendingTransport == route && waitingForAck && nextMessageId == 0);
        assert(original.version == Protocol::VERSION && original.type == Type::Event &&
               original.event == Protocol::EventType::UserHeartbeat && original.sender == LOCAL_DEVICE &&
               original.messageId == 0xFFFF && original.ackForMessageId == 0);
        const auto other = route == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW;
        hostNow = 140; receiveVia(incoming(Type::Ack, original.messageId), other);
        assert(waitingForAck && ackWaitStart == 130);
        buttonStep(429, LOW); assert(buttonEventPackets() == 1 && retryCount == 0);
        buttonStep(430, LOW); assert(buttonEventPackets() == 2 && retryCount == 1);
        buttonStep(730, LOW); assert(buttonEventPackets() == 3 && retryCount == 2);
        const auto& sent = route == Transport::ESP_NOW ? wire : ccWire;
        assert(sent.size() == 3);
        for (const auto& packet : sent) assert(memcmp(&packet, &original, sizeof(packet)) == 0);
        assert(led.requests == 0 && lastMeaningfulActivity == 130);
        buttonStep(1030, LOW);
        assert(!waitingForAck && buttonEventPackets() == 3 && selectedTransport == Transport::CC1101);
        assert(occurrences(Serial.log, "APP TRANSPORT FALLBACK |") == unsigned(route == Transport::ESP_NOW));
        buttonRelease(1050); buttonPress(1100);
        assert(pendingTransport == Transport::CC1101 && pendingMessage.messageId == 0 && led.requests == 0);
        hostNow = 1150; receiveVia(incoming(Type::Ack, pendingMessage.messageId), Transport::CC1101);
        assert(!waitingForAck && nextEventTime == 3650 && lastMeaningfulActivity == 1130);

        // Replay the actual button packet at the receiving peer, including a duplicate/retry.
        auto received = original; received.sender = PEER_DEVICE;
        freshKnownApp(); selectedTransport = route;
        const auto activity = lastMeaningfulActivity;
        receiveVia(received, route); receiveVia(received, route);
        const auto& receipts = route == Transport::ESP_NOW ? wire : ccWire;
        assert(receipts.size() == 2 && led.requests == 1 && led.userRequests == 1 && buttonEventPackets() == 0);
        for (const auto& ack : receipts) assert(ack.type == Type::Ack && ack.ackForMessageId == received.messageId);
        assert(lastMeaningfulActivity == activity && !automaticSleepArmed && !buttonHeartbeatPending);
    }
    freshKnownApp(); suppressPeriodicForTest = false; nextEventTime = 130; buttonPress(100);
    assert(buttonEventPackets() == 1 && occurrences(Serial.log, "BUTTON HEARTBEAT | SEND") == 1);
    hostNow = 150; receive(incoming(Type::Ack, pendingMessage.messageId));
    assert(nextEventTime == 2650);
    buttonStep(2649, LOW); assert(buttonEventPackets() == 1);
    buttonStep(2650, LOW); assert(buttonEventPackets() == 2 && led.requests == 1 && led.userRequests == 0);
    assert(lastMeaningfulActivity == 130 && occurrences(Serial.log, "BUTTON HEARTBEAT | SEND") == 1);
    freshKnownApp(); suppressPeriodicForTest = false; nextEventTime = 130; awakeAckBusy = true;
    buttonPress(100); assert(buttonHeartbeatPending && buttonEventPackets() == 0);
    awakeAckBusy = false; buttonStep(150, LOW);
    assert(!buttonHeartbeatPending && buttonEventPackets() == 1 && led.requests == 0);
    puts("PASS: button uses both normal radios, same-packet 300ms/two retries, wrong-radio rejection, ESP-NOW fallback, peer dedup/LED and priority with one shared periodic cadence");
}

void testButtonUnknown()
{
    for (auto initialRoute : {Transport::ESP_NOW, Transport::CC1101})
    for (auto movement : {MovementState::READY, MovementState::MOVING, MovementState::WAITING})
    for (auto classification : {ProximityClassification::UNKNOWN, ProximityClassification::CLOSE, ProximityClassification::FAR})
    {
        freshApp(); selectedTransport = initialRoute; proximityClassification = classification;
        movementState = movement; settleStartedAt = 100;
        startProximityCheck(0);
        // Model an actual probe in flight: the intent cancels measurement immediately,
        // but may only send after its callback drains, without replacing any packet.
        serviceProximityProbe(1); const auto probes = countWire(Type::ProximityProbe);
        mockedTxInFlight = 1;
        suppressPeriodicForTest = false; nextEventTime = 130;
        buttonPress(100);
        assert(buttonHeartbeatPending && buttonEventPackets() == 0);
        assert(proximityClassification == classification && selectedTransport == initialRoute);
        assert(proximityUpdateState == ProximityUpdateState::READY && !probeOutstanding);
        assert(movementState == movement && lastMeaningfulActivity == 130);
        const auto id = nextMessageId;
        startProximityCheck(140); serviceProximityProbe(140);
        assert(proximityUpdateState == ProximityUpdateState::READY && nextMessageId == id);
        // Incoming measurement requests also yield; neither reply nor periodic EVENT wins.
        receive(incoming(Type::ProximityProbe, 0, 900));
        assert(countWire(Type::ProximityProbeReply) == 0 && buttonEventPackets() == 0);
        mockedTxInFlight = 0; buttonStep(150, LOW);
        const auto expected = classification == ProximityClassification::UNKNOWN ? Transport::CC1101 : initialRoute;
        assert(!buttonHeartbeatPending && waitingForAck && pendingTransport == expected);
        assert(buttonEventPackets() == 1 && ackWaitStart == 150 && selectedTransport == initialRoute);
        assert(proximityClassification == classification && movementState == movement);
        assert(countWire(Type::ProximityProbe) == probes && led.requests == 0);
        const auto original = pendingMessage;
        receiveVia(incoming(Type::Ack, original.messageId), expected == Transport::CC1101 ? Transport::ESP_NOW : Transport::CC1101);
        assert(waitingForAck && ackWaitStart == 150);
        buttonStep(450, LOW); buttonStep(750, LOW);
        assert(waitingForAck && retryCount == 2 && buttonEventPackets() == 3);
        assert(memcmp(&pendingMessage, &original, sizeof(original)) == 0 && pendingTransport == expected);
        hostNow = 760; receiveVia(incoming(Type::Ack, original.messageId), expected);
        assert(!waitingForAck && proximityClassification == classification && lastMeaningfulActivity == 130);
    }
    for (auto route : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshApp(); const auto activity = lastMeaningfulActivity;
        auto event = proximityObservation(60, -50, hostNow).message;
        event.event = Protocol::EventType::UserHeartbeat;
        for (uint16_t id : {60, 61, 62})
        {
            event.messageId = id; receiveVia(event, route);
            assert(led.requests == unsigned(id - 59) && led.busy());
            const auto start = hostNow;
            ledStep(start, 0); ledStep(start + 65, 90);
            // Same-radio and cross-radio retries must ACK without another invocation.
            receiveVia(event, route);
            receiveVia(event, route == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW);
            assert(led.requests == unsigned(id - 59));
            ledStep(start + 130, 180); ledStep(start + 515, 255); ledStep(start + 700, 0, false);
            assert(occurrences(Serial.log, "PARTNER LED | USER HEARTBEAT | id=" + std::to_string(id)) == 1);
            hostNow = start + 800;
        }
        assert(proximityClassification == ProximityClassification::UNKNOWN && lastMeaningfulActivity == activity);
        assert(buttonEventPackets() == 0 && !buttonHeartbeatPending);
        const auto requests = led.requests;
        receiveVia(incoming(Type::Ack, 999), route); assert(led.requests == requests);
    }
    // Submission failure uses the same bounded retry episode, never a new ID or recovery loop.
    freshApp(); ccAccepts = false; buttonPress(100); const auto original = pendingMessage;
    assert(waitingForAck && !buttonHeartbeatPending && ccSubmitAttempts.size() == 1);
    buttonStep(430, LOW); buttonStep(730, LOW); buttonStep(1030, LOW);
    assert(!waitingForAck && ccSubmitAttempts.size() == 3 && buttonEventPackets() == 0);
    for (const auto& packet : ccSubmitAttempts) assert(memcmp(&packet, &original, sizeof(original)) == 0);
    assert(proximityClassification == ProximityClassification::UNKNOWN);
    puts("PASS: button preempts checks/probes/periodic traffic in READY/MOVING/WAITING; UNKNOWN uses CC1101 without classification, callback drain and bounded immutable retries preserved; remote 60-62 each invoke and render partner pulses once, duplicates/ACKs never replay");
}

void testButtonLedOwnership()
{
    using Event = Protocol::EventType;
    static_assert(sizeof(Protocol::Message) == 8, "user intent changed packet size");
    static_assert(unsigned(Event::Heartbeat) == 1 && unsigned(Event::UserHeartbeat) == 2, "wire enum changed");
    for (auto route : {Transport::ESP_NOW, Transport::CC1101})
    for (auto classification : {ProximityClassification::CLOSE, ProximityClassification::FAR})
    {
        // Sender: a button cannot restart its existing background pulse. FAR's
        // visual clock continues independently of the button's receipt ACK.
        freshKnownApp(classification); notePeerSeen(); selectedTransport = route;
        suppressPeriodicForTest = false; nextEventTime = 0; loop();
        assert(led.requests == 1 && led.userRequests == 0 && pendingMessage.event == Event::Heartbeat);
        hostNow = 10; receiveVia(incoming(Type::Ack, pendingMessage.messageId), route);
        hostNow = 75; loop(); assert(hostPixel().shown == uint32_t(90) << 16);
        buttonPress(100);
        assert(pendingMessage.event == Event::UserHeartbeat && ackWaitStart == 130);
        assert(led.requests == 1 && led.userRequests == 0 && !led.userHeartbeatActive());
        hostNow = 140; receiveVia(incoming(Type::Ack, pendingMessage.messageId), route);
        assert(hostPixel().shown == uint32_t(180) << 16 && led.requests == 1);
        hostNow = 525; loop(); assert(hostPixel().shown == uint32_t(255) << 16);
        hostNow = 710; loop(); assert(!led.busy());
        if (classification == ProximityClassification::FAR)
        {
            hostNow = 6000; loop();
            assert(led.requests == 2 && led.userRequests == 0 && !waitingForAck);
        }
        hostNow = nextEventTime; loop();
        assert(led.requests == 2 && led.userRequests == 0 && pendingMessage.event == Event::Heartbeat);

        // Receiver: both its scheduled local output and ordinary peer background
        // requests yield while packets/ACKs keep flowing. No user EVENT is echoed.
        freshKnownApp(classification); notePeerSeen(); selectedTransport = route;
        suppressPeriodicForTest = false; nextEventTime = 0; loop();
        hostNow = 10; receiveVia(incoming(Type::Ack, pendingMessage.messageId), route);
        Protocol::Message user{Protocol::VERSION, Type::Event, 70, PEER_DEVICE, Event::UserHeartbeat, 0};
        hostNow = 75; receiveVia(user, route);
        assert(buttonEventPackets() == 1 && led.userRequests == 1 && led.userHeartbeatActive());
        assert(proximityClassification == classification && selectedTransport == route);
        const auto start = hostNow; ledStep(start, 0); ledStep(start + 65, 90);
        nextEventTime = start + 115; hostNow = nextEventTime; loop();
        assert(pendingMessage.event == Event::Heartbeat && waitingForAck && led.userRequests == 1);
        hostNow = start + 130; receiveVia(incoming(Type::Ack, pendingMessage.messageId), route);
        assert(hostPixel().shown == uint32_t(180) << 16);
        auto background = user; background.messageId = 71; background.event = Event::Heartbeat;
        hostNow = start + 195; receiveVia(background, route);
        assert(hostPixel().shown == uint32_t(90) << 16 && led.userRequests == 1);
        hostNow = start + 230; receiveVia(user, route); // Retry after an interleaved background ID.
        receiveVia(user, route == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW);
        assert(led.userRequests == 1 && recentUserEventCount == 1);
        ledStep(start + 260, 0); ledStep(start + 515, 255); ledStep(start + 700, 0, false);
        assert(!led.userHeartbeatActive() && buttonEventPackets() == 2); // Only the two scheduled background EVENTs.
        if (classification == ProximityClassification::FAR)
        {
            const auto before = led.requests;
            hostNow = 5999; loop(); assert(!led.busy() && led.requests == before);
            hostNow = 6000; loop(); assert(led.busy() && led.requests == before + 1);
            hostNow = nextEventTime - 1; loop(); assert(led.requests == before + 1);
        }
        else { hostNow = nextEventTime - 1; loop(); assert(!led.busy()); }
        hostNow = nextEventTime; loop(); assert(led.busy() && !led.userHeartbeatActive());
        assert(pendingMessage.event == Event::Heartbeat && led.userRequests == 1);

        // Current eligibility, not the state at user receipt, controls resumption.
        for (auto distance : {ProximityClassification::CLOSE, ProximityClassification::FAR, ProximityClassification::UNKNOWN})
        for (bool negotiate : {false, true})
        {
            freshKnownApp(classification); notePeerSeen(); selectedTransport = route;
            led.requestHeartbeat(); ledStep(0, 0); hostNow = 65; receiveVia(user, route);
            const auto pulse = hostNow; ledStep(pulse, 0);
            if (negotiate) { loop(); requestSleepForTest(); assert(transaction().active); }
            proximityClassification = distance;
            ledStep(pulse + 515, 255); ledStep(pulse + 700, 0, false);
            const auto requests = led.requests;
            hostNow = pulse + 710; receiveVia(background, route);
            const bool allowed = distance != ProximityClassification::UNKNOWN && !negotiate;
            assert(led.requests == requests + unsigned(allowed) && led.busy() == allowed);
            assert(led.userRequests == 1 && !led.userHeartbeatActive());
            assert(proximityClassification == distance && selectedTransport == route);
        }

        // Communications, retries, debounced input and motion keep running under
        // user ownership. A different user request uses the one-slot restart policy.
        freshKnownApp(classification); notePeerSeen(); selectedTransport = route;
        receiveVia(user, route); const auto pulse = hostNow; ledStep(pulse, 0);
        buttonPress(100); const auto own = pendingMessage;
        assert(lastMeaningfulActivity == 130 && own.event == Event::UserHeartbeat && led.userRequests == 1);
        const auto services = awakeServices, polls = motionEventPolls;
        movementStep(250, MotionEvent::Activity);
        assert(awakeServices > services && motionEventPolls > polls && movementState == MovementState::MOVING);
        hostNow = 430; loop(); assert(retryCount == 1 && ackWaitStart == 430 && led.userRequests == 1);
        assert(memcmp(&pendingMessage, &own, sizeof(own)) == 0);
        auto newer = user; newer.messageId = 72;
        hostNow = 450; receiveVia(newer, route); const auto restarted = hostNow;
        assert(led.userRequests == 2 && recentUserEventCount == 2);
        ledStep(restarted, 0); ledStep(restarted + 65, 90);
        hostNow = restarted + 100; receiveVia(user, route); receiveVia(newer, route);
        assert(led.userRequests == 2); // Old and latest user retries cannot extend the newer pulse.
        hostNow = restarted + 130; receiveVia(incoming(Type::Ack, own.messageId), route);
        assert(!waitingForAck && led.userRequests == 2 && hostPixel().shown == uint32_t(180) << 16);
        ledStep(restarted + 515, 255); ledStep(restarted + 700, 0, false);
        receiveVia(user, route); receiveVia(newer, route);
        assert(!led.busy() && led.userRequests == 2); // Late duplicates cannot enqueue a new pulse.
        assert(lastMeaningfulActivity == 250 && recentUserEventCount <= RX_QUEUE_LENGTH);
    }
    // Wrap the bounded recent-user cache without expanding storage or changing IDs.
    freshApp();
    for (uint16_t id = 0; id < 20; ++id)
        receive({Protocol::VERSION, Type::Event, id, PEER_DEVICE, Event::UserHeartbeat, 0});
    assert(recentUserEventCount == RX_QUEUE_LENGTH && led.userRequests == 20);
    receive({Protocol::VERSION, Type::Event, 15, PEER_DEVICE, Event::UserHeartbeat, 0});
    assert(led.userRequests == 20);
    hostNow = USER_EVENT_HISTORY_MS + 200;
    receive({Protocol::VERSION, Type::Event, 15, PEER_DEVICE, Event::UserHeartbeat, 0});
    assert(led.userRequests == 21); // Old cache entries cannot suppress allocator reuse.
    for (uint32_t start : {0U, UINT32_MAX - 300U})
    {
        freshApp(); hostNow = start;
        const Protocol::Message user{Protocol::VERSION, Type::Event, 0xFFFF, PEER_DEVICE, Event::UserHeartbeat, 0};
        receive(user);
        receive({Protocol::VERSION, Type::Event, 0, PEER_DEVICE, Event::Heartbeat, 0});
        hostNow = start + 900; receive(user);
        assert(led.userRequests == 1 && recentUserEventCount == 1); // ID/time rollover, interleaved retry.
    }
    puts("PASS: explicit eight-byte user EVENT; button adds no sender pulse and CLOSE/FAR continue; receiver priority survives local/remote background traffic; current CLOSE/FAR/power eligibility controls resume; ACKs/retries/duplicates have no pulse or echo; input/motion/radio run, distinct users restart one slot, eight-ID cache stays bounded");
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    runCase(testButtonDebounce);
    runCase(testButtonDeepSleepWakeIntent);
    runCase(testButtonActivityAndSleep);
    runCase(testButtonPendingGuards);
    runCase(testButtonWakeHandoff);
    runCase(testButtonWakeFailures);
    runCase(testButtonTransportsAndPriority);
    runCase(testButtonUnknown);
    runCase(testButtonLedOwnership);
    finishSuite("button_runtime");
}
