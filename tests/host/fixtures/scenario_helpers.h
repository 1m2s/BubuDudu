#pragma once
#include "runtime_fixture.h"

void completedAwaitingCallbacks(bool participant)
{
    freshApp(); loop();
    if (participant)
    {
        receive(incoming(Type::SleepRequest, 20));
        receive(incoming(Type::SleepCommit, 20, 21));
        assert(waitingForAck && localState() == LocalState::SLEEPING);
        mockedTxInFlight = 1;
        receive(incoming(Type::Ack, pendingMessage.messageId));
    }
    else
    {
        requestSleepForTest(); const auto id = transaction().sleepId;
        receive(incoming(Type::SleepReady, id));
        mockedTxInFlight = 1; // Includes final fire-and-forget receipt ACK.
        receive(incoming(Type::SleepAck, id));
    }
    assert(!waitingForAck && !transaction().active && controlCount == 0);
    assert(armAttempts == 0 && coordinatedAttempts == 0 && physicalSleeps == 0);
    assert(localState() == LocalState::SLEEPING);
}

void movementStep(uint32_t now, MotionEvent event = MotionEvent::None)
{
    hostNow = now;
    motionPendingEvent = event;
    const auto polls = motionEventPolls;
    loop();
    assert(motionEventPolls == polls + 1); // Exactly one sensor read per ready loop.
}

ESPNowRadio::RssiObservation proximityObservation(uint16_t id, int8_t rssi, uint32_t captured)
{
    ESPNowRadio::RssiObservation observation{};
    observation.message = {Protocol::VERSION, Type::Event, id, PEER_DEVICE, Protocol::EventType::Heartbeat, 0};
    observation.rssi = rssi; observation.receivedAt = captured;
    return observation;
}

void settleForCheck(uint32_t settledAt)
{
    movementStep(uint32_t(settledAt - SETTLE_MS - 10), MotionEvent::Activity);
    movementStep(uint32_t(settledAt - SETTLE_MS), MotionEvent::Inactivity);
    movementStep(settledAt);
    assert(movementState == MovementState::READY && proximityUpdateState == ProximityUpdateState::CHECKING);
    assert(checkStartedAt == settledAt && proximitySampleCount == 0);
}

void assertProximityReset()
{
    assert(proximityUpdateState == ProximityUpdateState::READY && checkStartedAt == 0 && proximitySampleCount == 0);
    for (const auto& sample : proximitySamples) assert(sample.messageId == 0 && sample.rssi == 0);
    assert(!probeOutstanding && !probeTimerActive && probeMessageId == 0 && probeStartedAt == 0);
}

void receiveVia(const Protocol::Message& packet, Transport transport)
{
    if (transport == Transport::ESP_NOW) receive(packet);
    else { ccIncoming.push_back(packet); loop(); }
}

void classificationSample(uint16_t id, int8_t rssi)
{
    movementStep(hostNow + 10); // Allows the next bounded probe after an accepted sample.
    const uint32_t captured = hostNow + 1;
    auto observation = proximityObservation(id, rssi, captured);
    if (selectedTransport == Transport::CC1101 || proximityClassification == ProximityClassification::UNKNOWN)
    {
        assert(probeOutstanding);
        observation.message.type = Type::ProximityProbeReply;
        observation.message.event = Protocol::EventType::None;
        observation.message.ackForMessageId = probeMessageId;
    }
    rssiObservations.push_back(observation);
    movementStep(captured);
}

void completePolicyMeasurement(int8_t median)
{
    const auto route = selectedTransport;
    startProximityCheck(hostNow);
    classificationSample(700, -100);
    classificationSample(701, median);
    classificationSample(702, -40);
    assertProximityReset();
    assert(automaticSelectionPending && selectedTransport == route);
}

Protocol::Message requestFallbackFromEvent()
{
    assert(selectedTransport == Transport::ESP_NOW && !waitingForAck);
    const auto classification = proximityClassification;
    startHeartbeatEvent();
    const auto failed = pendingMessage;
    mockedTxInFlight = 1; // Keep semantic exhaustion separate from actual transport drain.
    for (unsigned attempt = 0; attempt <= MAX_RETRIES; ++attempt)
    {
        const auto began = ackWaitStart;
        hostNow = began + ACK_TIMEOUT_MS - 1; loop();
        assert(waitingForAck && retryCount == attempt && ackWaitStart == began && !espNowFallbackPending);
        hostNow = began + ACK_TIMEOUT_MS; loop();
        assert(selectedTransport == Transport::ESP_NOW && pendingTransport == Transport::ESP_NOW);
        assert(memcmp(&pendingMessage, &failed, sizeof(failed)) == 0);
        if (attempt < MAX_RETRIES)
            assert(waitingForAck && retryCount == attempt + 1 && ackWaitStart == began + ACK_TIMEOUT_MS && !espNowFallbackPending);
    }
    unsigned attempts = 0;
    for (const auto& packet : wire)
        if (packet.type == Type::Event && packet.messageId == failed.messageId)
        { ++attempts; assert(memcmp(&packet, &failed, sizeof(failed)) == 0); }
    assert(attempts == 3 && ccWire.empty());
    assert(!waitingForAck && retryCount == 0 && espNowFallbackPending && !automaticSelectionPending);
    assert(peerState() == PeerState::OFFLINE && proximityClassification == classification);
    assert(Serial.log.find("APP TRANSPORT FALLBACK") == std::string::npos);
    return failed;
}

void completePeerReturnMeasurement(int8_t median)
{
    assert(proximityUpdateState == ProximityUpdateState::CHECKING && selectedTransport == Transport::CC1101);
    const auto began = checkStartedAt;
    const auto starts = occurrences(Serial.log, "PROXIMITY CHECK | START");
    const auto prior = proximityClassification;
    classificationSample(850, -100);
    assert(proximitySampleCount == 1 && proximityClassification == prior);
    assert(displayFrames.back().peer == "ONLINE" && displayFrames.back().distance == "CHECKING");
    assert(displayFrames.back().radio == "CC1101");
    classificationSample(851, median);
    assert(proximitySampleCount == 2 && proximityClassification == prior && checkStartedAt == began);
    classificationSample(852, -40);
    assertProximityReset();
    assert(automaticSelectionPending && selectedTransport == Transport::CC1101);
    assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == starts);
    assert(Serial.log.find("MOVEMENT |") == std::string::npos);
    loop(); // Existing automatic policy consumes the completed median.
    assert(proximityClassification == (median <= ENTER_FAR_DBM ? ProximityClassification::FAR : ProximityClassification::CLOSE));
    assert(selectedTransport == (median <= ENTER_FAR_DBM ? Transport::CC1101 : Transport::ESP_NOW));
}

std::string displayObservedRuntime()
{
    const auto& sleep = transaction();
    const uint64_t values[]{
        nextMessageId, uint64_t(pendingTransport), waitingForAck, retryCount, ackWaitStart, nextEventTime,
        uint64_t(selectedTransport), uint64_t(proximityClassification), uint64_t(proximityUpdateState),
        automaticSelectionPending, espNowFallbackPending, uint64_t(peerState()), uint64_t(localState()),
        sleep.active, sleep.sleepId, uint64_t(sleep.role), uint64_t(sleep.phase), sleep.startedAt,
        sleep.phaseDeadline, sleep.hardDeadline, controlCount, sleepDrainWaiting, sleepDrainStarted,
        probeOutstanding, probeMessageId, probeTimerActive, probeStartedAt, checkStartedAt, proximitySampleCount,
        uint64_t(movementState), settleStartedAt, motionEventPolls, motionPreparations, motionCancels,
        wire.size(), ccWire.size(), wakeEvents.size(), physicalSleeps, millis()
    };
    std::string result(reinterpret_cast<const char*>(&pendingMessage), sizeof(pendingMessage));
    result.append(reinterpret_cast<const char*>(values), sizeof(values));
    return result;
}

void ledStep(uint32_t now, uint8_t red, bool busy = true)
{
    hostNow = now;
    const auto before = displayObservedRuntime();
    const auto shows = hostPixel().shows;
    led.update(now);
    assert(displayObservedRuntime() == before); // Includes fake millis: a hidden delay would fail this.
    assert(hostPixel().shows <= shows + 1);
    assert(hostPixel().shown == uint32_t(red) << 16 && led.busy() == busy);
    const auto after = hostPixel().shows;
    led.update(now); // Same brightness/idle must not resend the pixel.
    assert(hostPixel().shows == after);
}

void freshAutomaticRuntime(uint32_t now = 0)
{
    freshApp(); hostNow = now;
    lastMeaningfulActivity = now;
    automaticSleepArmed = true;
}

size_t buttonEventPackets()
{
    size_t count = countWire(Type::Event);
    for (const auto& packet : ccWire) if (packet.type == Type::Event) ++count;
    return count;
}

void setupButtonWakeTest(uint64_t mask = 0x20, int level = HIGH,
                         bool retainedUser = false, bool duplicate = false, bool runtimeOk = true)
{
    freshApp(); protocolReady = false; delete receiveQueue; receiveQueue = nullptr;
    RtcState::save({100, duplicate, 70, {false, 0}});
    injectedBoot.deep = true; injectedBoot.cause = CC1101WakeRecovery::Cause::Gpio;
    injectedBoot.gpioMask = mask; buttonLevel = level;
    injectWakePacket = retainedUser;
    injectedPacket = {Protocol::VERSION, Type::Event, 70, PEER_DEVICE, Protocol::EventType::UserHeartbeat, 0};
    wakeTxResult = CC1101WakeTx::Result::Acked; radioStarts = runtimeOk;
    setup();
    assert(rtcRestored && wakeRecoveries == 1 && armInitializations == 0);
}
