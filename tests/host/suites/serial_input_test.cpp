#include "../fixtures/scenario_helpers.h"

std::string serialInertnessRun(unsigned scenario, char input)
{
    freshKnownApp(); hostNow = 100; pendingMessage = {}; ackWaitStart = 0;
    switch (scenario)
    {
        case 0: proximityClassification = ProximityClassification::UNKNOWN; break;
        case 1: nextEventTime = hostNow; break; // Due CLOSE heartbeat must not be paused.
        case 2:
            proximityClassification = ProximityClassification::FAR;
            selectedTransport = Transport::CC1101; nextEventTime = hostNow; break;
        case 3: motionPendingEvent = MotionEvent::Inactivity; break;
        case 4: startHeartbeatEvent(); break; // Unchanged ACK/retry/fallback budget.
        case 5: // Power deadline still advances without consuming serial input.

            assert(PowerManager::requestSleep(nextMessageId++, hostNow)); break;
        case 6: completedAwaitingCallbacks(false); break;
        case 7: motionPendingEvent = MotionEvent::Activity; break;
        case 8:
            startProximityCheck(hostNow);
            assert(proximityUpdateState == ProximityUpdateState::CHECKING); break;
        case 9:
            bootInfo.deep = true; bootInfo.cause = CC1101WakeRecovery::Cause::Gpio;
            bootInfo.gpioMask = 0x20; awakeAckBusy = true; beginButton(); break;
        case 10: buttonLevel = LOW; break; // Physical press still delivers one intent.
        case 11:
            completedAwaitingCallbacks(false); PowerManager::injectActivity(hostNow);
            protocolReady = false; break;
    }
    if (input) Serial.input.push_back(input);
    const auto start = hostNow;
    std::string trace;
    const auto record = [&](unsigned long value) { trace += std::to_string(value) + ","; };
    const auto packet = [&](const Protocol::Message& value) {
        trace.append(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    for (uint32_t elapsed : {0U, 30U, 300U, 600U, 900U, 5100U})
    {
        hostNow = start + elapsed;
        if (scenario == 6 && elapsed == 600) mockedTxInFlight = 0;
        try { firmwareLoop(); } catch (const PhysicalSleepEntered&) {}
        record(hostNow); record(static_cast<unsigned>(localState())); record(static_cast<unsigned>(peerState()));
        record(static_cast<unsigned>(selectedTransport)); record(static_cast<unsigned>(pendingTransport));
        record(static_cast<unsigned>(movementState)); record(static_cast<unsigned>(proximityClassification));
        record(static_cast<unsigned>(proximityUpdateState)); record(proximitySampleCount); record(probeOutstanding);
        record(waitingForAck); record(retryCount); record(ackWaitStart); record(nextMessageId); record(nextEventTime);
        record(controlCount); record(transaction().active); record(transaction().sleepId);
        record(static_cast<unsigned>(transaction().phase)); record(transaction().phaseDeadline);
        record(transaction().hardDeadline); record(cooldownLeftMs(hostNow));
        record(lastMeaningfulActivity); record(automaticSleepArmed); record(sleepDrainWaiting);
        record(buttonHeartbeatPending); record(buttonWakeIntentHeld); record(buttonStablePressed);
        record(automaticSelectionPending); record(espNowFallbackPending); record(haveLastPeerEvent); record(lastPeerEventId);
        record(led.requests); record(led.userRequests); record(hostPixel().shown);
        record(motionEventPolls); record(coordinatedAttempts); record(physicalSleeps); record(armAttempts);
        packet(pendingMessage);
        for (size_t i = 0; i < controlCount; ++i) { packet(controlQueue[i].message); record(controlQueue[i].notBefore); }
        record(wire.size()); for (const auto& value : wire) packet(value);
        record(ccWire.size()); for (const auto& value : ccWire) packet(value);
        record(wakeEvents.size()); for (const auto& value : wakeEvents) packet(value);
    }
    assert(Serial.input.size() == (input ? 1U : 0U));
    assert(Serial.log.find("Power tests:") == std::string::npos);
    assert(Serial.log.find("Sleep handshake bench") == std::string::npos);
    if (scenario == 5) assert(!transaction().active && localState() == LocalState::ACTIVE);
    if (scenario == 11) assert(localState() == LocalState::ACTIVE);
    return trace + Serial.log;
}

void testSerialInputIsInert()
{
    for (unsigned scenario = 0; scenario < 12; ++scenario)
    {
        const auto baseline = serialInertnessRun(scenario, 0);
        for (char input : std::string("ecpxwisahd?\r\n"))
        {
            const auto actual = serialInertnessRun(scenario, input);
            if (actual != baseline)
                fprintf(stderr, "Serial inertness mismatch: scenario=%u input=%u\n", scenario, unsigned(input));
            assert(actual == baseline);
        }
    }
    puts("PASS: all former serial commands/line endings leave real-loop state, radio packets, IDs, retries, power deadlines, animation and physical button/motion behavior identical; input is never consumed");
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    runCase(testSerialInputIsInert);
    finishSuite("serial_input");
}
