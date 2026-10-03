#pragma once
// Hardware substitutes and observations shared by the production-loop suites.
uint32_t hostNow = 0;
HostSerial Serial;
std::vector<Protocol::Message> wire;
bool radioAccepts = true;
unsigned armAttempts = 0;
unsigned armInitializations = 0;
CC1101SleepArm::Result armResult = CC1101SleepArm::Result::Ready;
namespace CC1101SleepArm
{
    Result begin() { ++armInitializations; return Result::Ready; }
    Report prepareForSleep()
    {
        // Real loop must consume only after transport drain, never in callback.
        assert(!waitingForAck && controlCount == 0);
        assert(PowerManager::localState() == PowerManager::LocalState::SLEEPING);
        ++armAttempts;
        Report report;
        report.result = armResult;
        return report;
    }
    const char* toString(Result result)
    {
        return result == Result::Ready ? "READY" : "RADIO_UNAVAILABLE";
    }
}
unsigned wakeRecoveries = 0;
unsigned coordinatedAttempts = 0, physicalSleeps = 0, mockedTxInFlight = 0;
bool mockedRxActive = false;
bool holdEspReceipts = false;
void (*afterAwakeService)() = nullptr;
void (*afterArm)() = nullptr;
bool entryFails = false;
unsigned motionInitializations = 0;
bool motionInitOk = true, motionPrepareOk = true;
unsigned motionPreparations = 0, motionCancels = 0;
int motionIntLevel = 0;
int buttonLevel = HIGH;
unsigned buttonConfigurations = 0;
MotionEvent motionStartup = MotionEvent::None;
MotionEvent motionPendingEvent = MotionEvent::None;
unsigned motionEventPolls = 0;
unsigned displayInitializations = 0;
unsigned i2cHealthReports = 0;
bool displayInitOk = true;
struct DisplayFrame { std::string device, peer, distance, radio, state, motion; };
std::vector<DisplayFrame> displayFrames;
std::vector<std::string> deepSleepFrames;
bool lastDisplayWasDeepSleep = false, returnAfterSleepFrame = false;
void (*duringSleepFrame)() = nullptr;
unsigned ledBeginsAtBoot = 0, ledShowsAtBoot = 0;
bool Motion::begin(uint8_t sda, uint8_t scl, uint8_t intPin)
{
    assert(sda == 0 && scl == 1 && intPin == 3);
    assert(!protocolReady);
    assert(bootInfo.deep ? wakeRecoveries == 1 : armInitializations == 1);
    assert(displayInitializations == 0);
    assert(hostPixel().begins == ledBeginsAtBoot);
    ++motionInitializations;
    interruptPin = intPin;
    startupEvent = motionStartup;
    return motionInitOk;
}
uint8_t Motion::getInterruptPin() const { return interruptPin; }
MotionEvent Motion::getStartupEvent() const { assert(motionInitOk); return startupEvent; }
#ifdef DEVICE_DUDU
void Motion::reportStartupI2cHealth(uint8_t) const
{
    assert(!protocolReady && motionInitializations == 1 && displayInitializations == 1);
    assert(hostPixel().begins == ledBeginsAtBoot);
    assert(Serial.log.find(motionInitOk ? "MOTION INIT | OK" : "MOTION INIT | FAILED") != std::string::npos);
    assert(i2cHealthReports++ == 0); // Setup only, never periodic.
}
#endif
int digitalRead(int pin)
{
    if (pin == BUTTON_PIN) return buttonLevel;
    assert(pin == 3); return motionIntLevel;
}
void pinMode(int pin, int mode)
{
    assert(pin == 5 && mode == INPUT_PULLUP);
    ++buttonConfigurations;
}
bool Motion::prepareForSleep() { ++motionPreparations; return motionInitOk && motionPrepareOk; }
bool Motion::cancelSleepPreparation() { ++motionCancels; return motionInitOk; }
MotionEvent Motion::getEvent()
{
    ++motionEventPolls;
    const auto event = motionPendingEvent;
    motionPendingEvent = MotionEvent::None;
    return motionInitOk ? event : MotionEvent::None;
}
std::vector<Protocol::Message> wakeEvents;
CC1101WakeTx::Result wakeTxResult = CC1101WakeTx::Result::AckTimeout;
bool wakeTxRxReady = true;
bool deferredWakePending = false;
uint32_t wakeTxElapsedMs = 0;
void (*duringWakeTx)() = nullptr;
bool radioStarts = true;
void (*atRadioStart)() = nullptr;
namespace CC1101WakeTx
{
    Report send(const Protocol::Message& event, Protocol::DeviceId peer)
    {
        assert(protocolReady && !waitingForAck && controlCount == 0 && !PowerManager::transaction().active);
        assert(receiveQueue->items.empty() && peer == PEER_DEVICE);
        if (Serial.log.find("MOTION PEER WAKE | one-shot request") != std::string::npos)
        {
            assert(wakeRecoveries == 1 && armInitializations == 0 && motionInitializations == 1);
            assert(Serial.log.find("ESP-NOW startup successful.") != std::string::npos);
            assert(PowerManager::localState() == PowerManager::LocalState::ACTIVE);
        }
        wakeEvents.push_back(event);
        if (duringWakeTx) duringWakeTx();
        hostNow += wakeTxElapsedMs;
        Report report;
        report.result = wakeTxResult; report.attempts = wakeTxResult == Result::RadioUnavailable ? 0 : 3; report.rxReady = wakeTxRxReady;
        return report;
    }
    const char* toString(Result) { return "ACK_TIMEOUT"; }
    bool deferredPending() { return deferredWakePending; }
    bool takeDeferredEvent(Protocol::Message&) { return false; }
}
CC1101WakeRecovery::BootInfo injectedBoot;
bool injectWakePacket = false;
bool awakeAckBusy = false, awakeRuntimeStopped = false;
unsigned awakeServices = 0;
std::vector<Protocol::Message> ccWire;
std::vector<Protocol::Message> ccSubmitAttempts;
std::deque<Protocol::Message> ccIncoming;
bool ccAccepts = true, ccHoldTx = false;
Protocol::Message injectedPacket{};
namespace CC1101WakeRecovery
{
    BootInfo captureBoot()
    {
        assert(!protocolReady);
        ledBeginsAtBoot = hostPixel().begins; ledShowsAtBoot = hostPixel().shows;
        return injectedBoot;
    }
    Report recover(bool restored, Protocol::DeviceId peer, EventHandler handler)
    {
        ++wakeRecoveries;
        assert(armInitializations == 0 && !protocolReady);
        assert(motionInitializations == 0 && displayInitializations == 0);
        assert(hostPixel().begins == ledBeginsAtBoot && hostPixel().shows == ledShowsAtBoot);
        assert(!PowerManager::transaction().active);
        Report report;
        if (injectWakePacket && restored)
        {
            assert(injectedPacket.sender == peer);
            const auto ack = handler(injectedPacket, report.processed);
            if (injectedPacket.event == Protocol::EventType::UserHeartbeat)
            {
                assert(retainedUserAnimationPending == report.processed && led.userRequests == 0);
                if (report.processed) assert(retainedUserAnimationId == injectedPacket.messageId);
            }
            assert(ack.ackForMessageId == injectedPacket.messageId && ack.type == Protocol::MessageType::Ack);
            assert(ack.messageId == uint16_t(nextMessageId - 1));
            report.packetRecovered = true; report.duplicate = !report.processed; report.ackSent = true;
        }
        return report;
    }
    void serviceAwake(Protocol::DeviceId peer, ReceiveHandler handler)
    {
        assert(protocolReady && peer == PEER_DEVICE);
        ++awakeServices;
        if (!awakeAckBusy && !ccIncoming.empty())
        {
            const auto packet = ccIncoming.front(); ccIncoming.pop_front();
            handler(packet);
        }
        if (afterAwakeService) afterAwakeService();
    }
    bool awakeBusy() { return awakeAckBusy || !ccIncoming.empty(); }
    bool awakeStopped() { return awakeRuntimeStopped; }
    SubmitResult submitAwake(const Protocol::Message& packet)
    {
        ccSubmitAttempts.push_back(packet);
        if (awakeBusy()) return SubmitResult::Busy;
        if (!ccAccepts) return SubmitResult::Failed;
        ccWire.push_back(packet);
        if (ccHoldTx) awakeAckBusy = true;
        return SubmitResult::Accepted;
    }
    void printReport(const BootInfo&, bool, const Report&) {}
    void enterDeepSleep(void (*save)(), const char* (*guard)(), void (*beforeSleep)())
    {
        assert(!led.busy() && hostPixel().shown == 0);
        assert(beforeSleep != nullptr);
        ++coordinatedAttempts;
        const auto arm = CC1101SleepArm::prepareForSleep();
        if (arm.result != CC1101SleepArm::Result::Ready)
        {
            Serial.println("CC1101 SLEEP ARM | FAILED");
            return;
        }
        if (afterArm) afterArm();
        if (guard() || entryFails) return;
        save();
        const auto beforeFrame = hostNow;
        beforeSleep();
        assert(hostNow == beforeFrame);
        if (guard() || returnAfterSleepFrame) return;
        ++physicalSleeps;
        throw PhysicalSleepEntered{};
    }
}
std::deque<ESPNowRadio::RssiObservation> rssiObservations;
unsigned rssiReads = 0;
bool refillRssi = false;
namespace ESPNowRadio
{
    bool takeRssiObservation(RssiObservation& out)
    {
        ++rssiReads;
        if (rssiObservations.empty()) return false;
        out = rssiObservations.front(); rssiObservations.pop_front();
        if (refillRssi) rssiObservations.push_back(out);
        return true;
    }
    unsigned txInFlight() { return mockedTxInFlight; }
    bool receiveCallbackActive() { return mockedRxActive; }
    bool begin(ReceiveHandler) { if (atRadioStart) atRadioStart(); return radioStarts; }
    bool send(const uint8_t* data, size_t length)
    {
        assert(length == 8);
        Protocol::Message message;
        memcpy(&message, data, length);
        wire.push_back(message);
        if (holdEspReceipts && message.type == Protocol::MessageType::Ack) mockedTxInFlight = 1;
        return radioAccepts;
    }
}

// No real U8g2/I2C implementation is linked into the FSM harness.
Display::Display() = default;
bool Display::begin()
{
    (void)oled;
    assert(!protocolReady && motionInitializations == 1 && displayInitializations == 0);
    assert(hostPixel().begins == ledBeginsAtBoot);
    assert(bootInfo.deep ? wakeRecoveries == 1 : armInitializations == 1);
    ++displayInitializations;
    return displayInitOk;
}
void Display::showStatus(const char* device, const char* peer, const char* distance,
                         const char* radio, const char* state, const char* motion)
{
    assert(displayReady && protocolReady && !waitingForAck && controlCount == 0);
    assert(!PowerManager::transaction().active && !sleepDrainWaiting);
    assert(!CC1101WakeRecovery::awakeBusy() || CC1101WakeRecovery::awakeStopped());
    assert(!CC1101WakeTx::deferredPending() && mockedTxInFlight == 0 && !mockedRxActive);
    assert(receiveQueue && receiveQueue->items.empty() && !probeOutstanding);
    assert(PowerManager::automaticHeartbeatAllowed());
    displayFrames.push_back({device, peer, distance, radio, state, motion});
    lastDisplayWasDeepSleep = false;
}
void Display::showDeepSleep(const char* device)
{
    assert(displayReady && sleepEntryBlockedReason() == nullptr);
    assert(motionPreparations > 0 && motionPrepareOk && armAttempts > 0);
    assert(armResult == CC1101SleepArm::Result::Ready && physicalSleeps == 0);
    deepSleepFrames.push_back(device);
    lastDisplayWasDeepSleep = true;
    if (duringSleepFrame) duringSleepFrame();
}
