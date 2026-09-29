// Exercise the actual production FSM and loop with deterministic time/radio
// substitutes. No PlatformIO, Wi-Fi, hardware, or additional test framework.
#include "Arduino.h"
#include "../../src/PowerManager.cpp"
#include "../../src/RtcState.cpp"
// MCU attributes/GPIO and the existing Motion driver are substituted on host.
#define IRAM_ATTR
int digitalRead(int pin);
#include "LED.h"
// Count application requests while executing the unchanged production LED state machine.
struct ObservedLED : LED
{
    unsigned requests = 0;
    void requestHeartbeat() { ++requests; LED::requestHeartbeat(); }
};
#define LED ObservedLED
#define loop firmwareLoop
#include "../../src/main.cpp"
#undef loop
#undef LED
#include "../../src/LED.cpp"
struct PhysicalSleepEntered {};
void loop() { try { firmwareLoop(); } catch (const PhysicalSleepEntered&) {} }

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
unsigned wakeRecoveries = 0, benchSleepCalls = 0;
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
MotionEvent motionStartup = MotionEvent::None;
MotionEvent motionPendingEvent = MotionEvent::None;
unsigned motionEventPolls = 0;
unsigned displayInitializations = 0;
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
int digitalRead(int pin) { assert(pin == 3); return motionIntLevel; }
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
        Report report;
        report.result = wakeTxResult; report.attempts = wakeTxResult == Result::RadioUnavailable ? 0 : 3; report.rxReady = true;
        return report;
    }
    const char* toString(Result) { return "ACK_TIMEOUT"; }
}
CC1101WakeRecovery::BootInfo injectedBoot;
bool injectWakePacket = false;
bool awakeAckBusy = false;
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
    void enterDeepSleep(void (*save)(), const char* (*guard)(), bool coordinated, void (*beforeSleep)())
    {
        assert(!led.busy() && hostPixel().shown == 0);
        assert(beforeSleep != nullptr);
        if (!coordinated) { ++benchSleepCalls; return; }
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
    assert(!CC1101WakeRecovery::awakeBusy() && mockedTxInFlight == 0 && !mockedRxActive);
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

using Type = Protocol::MessageType;
using Device = Protocol::DeviceId;
using namespace PowerManager;
// Pure FSM tests supply admission explicitly, as the production loop now does.
// Their forceIdle() fixtures represent an already eligible participant.
void handleControl(const Protocol::Message& message, uint32_t now)
{
    PowerManager::handleControl(message, now, localState() == LocalState::IDLE);
}
struct Intent { Type type; uint16_t id; };
std::vector<Intent> intents;
bool acceptIntent = true;
bool captureIntent(Type type, uint16_t id)
{
    intents.push_back({type, id});
    return acceptIntent;
}
Protocol::Message control(Type type, uint16_t id, uint16_t packetId = 90, Device from = Device::Dudu)
{
    return {Protocol::VERSION, type, type == Type::SleepRequest ? id : packetId,
            from, Protocol::EventType::None, id};
}
void freshPower(Device device = Device::Bubu)
{
    Serial.log.clear();
    intents.clear();
    acceptIntent = true;
    PowerManager::begin(device, captureIntent);
}
size_t occurrences(const std::string& text, const std::string& needle)
{
    size_t count = 0, pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) { ++count; pos += needle.size(); }
    return count;
}
void testFsm()
{
    // Coordinator: packet receipt is not the sleep agreement.
    freshPower();
    assert(!requestSleep(27, 0));
    forceIdle(0); assert(requestSleep(27, 0));
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
    freshPower(); forceIdle(0);
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
    injectActivity(400); update(650); forceIdle(650);
    handleControl(request, 651); assert(!transaction().active); // Closed request stays closed.

    // Stale IDs, wrong sender, wrong role, malformed request cannot advance/cancel.
    freshPower(); forceIdle(0); requestSleep(30, 0);
    for (auto type : {Type::SleepReady, Type::SleepCommit, Type::SleepAck, Type::SleepCancel})
    {
        handleControl(control(type, 29), 10);
        assert(transaction().active && transaction().sleepId == 30 && transaction().phase == SleepPhase::WAIT_READY);
    }
    handleControl(control(Type::SleepReady, 30, 90, Device::Bubu), 10);
    handleControl(control(Type::SleepCommit, 30), 10);
    assert(transaction().phase == SleepPhase::WAIT_READY);
    freshPower(); forceIdle(0); auto malformed = control(Type::SleepRequest, 40); malformed.messageId = 41;
    handleControl(malformed, 0); assert(!transaction().active);

    // Activity closes state first; late control cannot put the device to sleep.
    freshPower(); forceIdle(0); handleControl(control(Type::SleepRequest, 40), 0);
    injectActivity(100); assert(localState() == LocalState::ACTIVE && !transaction().active);
    assert(intents.back().type == Type::SleepCancel && cooldownLeftMs(100) == 3000);
    handleControl(control(Type::SleepCommit, 40), 110);
    assert(localState() == LocalState::ACTIVE && !transaction().active);
    forceIdle(200); assert(!requestSleep(99, 200));
    update(3100); handleControl(control(Type::SleepRequest, 40), 3101); assert(!transaction().active);
    handleControl(control(Type::SleepRequest, 41), 3102); assert(transaction().sleepId == 41);
    handleControl(control(Type::SleepCancel, 41), 3200);
    assert(localState() == LocalState::IDLE && !transaction().active && cooldownLeftMs(3200) == 3000);

    // Phase and absolute hard bounds. Duplicate READY cannot extend either bound.
    freshPower(); forceIdle(0); requestSleep(27, 0); update(3000);
    assert(!transaction().active && Serial.log.find("PHASE_TIMEOUT") != std::string::npos);
    freshPower(); forceIdle(0); handleControl(control(Type::SleepRequest, 27), 0); update(3000);
    assert(!transaction().active && localState() == LocalState::IDLE && peerState() == PeerState::UNKNOWN);
    freshPower(); forceIdle(0); requestSleep(27, 0);
    handleControl(control(Type::SleepReady, 27), 2999);
    handleControl(control(Type::SleepReady, 27), 4999);
    assert(transaction().hardDeadline == 5000); update(5000);
    assert(!transaction().active && Serial.log.find("HARD_TIMEOUT") != std::string::npos);
    auto afterTimeout = Serial.log;
    for (uint32_t now = 5001; now < 20000; ++now) update(now);
    assert(!transaction().active && localState() == LocalState::IDLE && Serial.log == afterTimeout);

    // Simultaneous requests, including equal numeric IDs. Losing the collision
    // cannot extend the hard limit or send a CANCEL that kills the winning ID.
    for (uint16_t peerId : {uint16_t(20), uint16_t(44)})
    {
        freshPower(Device::Bubu); forceIdle(0); requestSleep(20, 0);
        handleControl(control(Type::SleepRequest, peerId, 0, Device::Dudu), 100);
        assert(transaction().role == SleepRole::COORDINATOR && transaction().sleepId == 20 && intents.size() == 1);
        freshPower(Device::Dudu); forceIdle(0); requestSleep(peerId, 0);
        handleControl(control(Type::SleepRequest, 20, 0, Device::Bubu), 100);
        assert(transaction().role == SleepRole::PARTICIPANT && transaction().sleepId == 20);
        assert(transaction().hardDeadline == 5000 && intents.size() == 2 && intents.back().type == Type::SleepReady);
        handleControl(control(Type::SleepCommit, 20, 25, Device::Bubu), 200);
        controlSent(Type::SleepAck, 20, 210); assert(localState() == LocalState::SLEEPING);
    }
    // Tie-break never takes over an already committed coordinator transaction.
    freshPower(Device::Dudu); forceIdle(0); requestSleep(44, 0);
    handleControl(control(Type::SleepReady, 44, 90, Device::Bubu), 10);
    handleControl(control(Type::SleepRequest, 20, 0, Device::Bubu), 20);
    assert(transaction().role == SleepRole::COORDINATOR && transaction().sleepId == 44);

    // Different failure knowledge before and after COMMIT.
    freshPower(); forceIdle(0); requestSleep(27, 0); controlFailed(Type::SleepRequest, 27, 900);
    assert(peerState() == PeerState::OFFLINE && localState() == LocalState::IDLE);
    freshPower(); forceIdle(0); requestSleep(27, 0); handleControl(control(Type::SleepReady, 27), 10);
    controlFailed(Type::SleepCommit, 27, 900); assert(peerState() == PeerState::UNKNOWN && !transaction().active);
    freshPower(); forceIdle(0); handleControl(control(Type::SleepRequest, 27), 0);
    handleControl(control(Type::SleepCommit, 27), 10); controlSent(Type::SleepAck, 27, 10);
    controlFailed(Type::SleepAck, 27, 910);
    assert(peerState() == PeerState::UNKNOWN && localState() == LocalState::SLEEPING);
    freshPower(); forceIdle(0); acceptIntent = false; assert(!requestSleep(27, 0));
    assert(!transaction().active && cooldownLeftMs(0) == 3000);

    // uint32 millis rollover, including a zero hard deadline.
    freshPower(); const uint32_t start = UINT32_MAX - 4999;
    forceIdle(start); requestSleep(27, start);
    handleControl(control(Type::SleepReady, 27), start + 2999);
    update(UINT32_MAX); assert(transaction().active); update(0);
    assert(!transaction().active && cooldownLeftMs(0) == 3000);
    update(3000); assert(cooldownLeftMs(3000) == 0);
    // Request IDs use serial arithmetic, independent of the clock rollover.
    freshPower(); forceIdle(0); handleControl(control(Type::SleepRequest, 65530), 0);
    injectActivity(10); update(3010); forceIdle(3010);
    handleControl(control(Type::SleepRequest, 5), 3011); assert(transaction().sleepId == 5);
}

void freshApp()
{
    motionReady = displayReady = true; displayedStatus = {}; // Model an initialized awake runtime.
    displayInitializations = 0; displayInitOk = true; displayFrames.clear();
    deepSleepFrames.clear(); lastDisplayWasDeepSleep = returnAfterSleepFrame = false; duringSleepFrame = nullptr;
    selectedTransport = pendingTransport = Transport::ESP_NOW;
    ccWire.clear(); ccSubmitAttempts.clear(); ccIncoming.clear(); ccAccepts = true; ccHoldTx = false;
    resetProximityCheck();
    proximityClassification = ProximityClassification::UNKNOWN;
    automaticSelectionPending = false;
    espNowFallbackPending = false;
    rssiObservations.clear(); rssiReads = 0; refillRssi = false;
    resetMovement();
    if (receiveQueue) delete receiveQueue;
    receiveQueue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(Protocol::Message));
    protocolReady = true; waitingForAck = false; retryCount = 0; nextMessageId = 1;
    haveLastPeerEvent = false; lastPeerEventId = 0; testAckAlreadyDropped = false;
    nextEventTime = 100000; controlCount = 0; pauseAutomaticHeartbeats = true; delayControlsForTest = false;
    hostNow = 0; wire.clear(); radioAccepts = true; Serial.log.clear(); Serial.input.clear();
    armAttempts = 0; armResult = CC1101SleepArm::Result::Ready;
    armInitializations = 0; wakeRecoveries = 0; benchSleepCalls = 0;
    injectedBoot = {}; injectWakePacket = false;
    awakeAckBusy = false; awakeServices = 0;
    wakeEvents.clear(); wakeTxResult = CC1101WakeTx::Result::AckTimeout;
    radioStarts = true; atRadioStart = nullptr;
    coordinatedAttempts = physicalSleeps = mockedTxInFlight = 0;
    mockedRxActive = entryFails = false; afterArm = nullptr;
    holdEspReceipts = false; afterAwakeService = nullptr;
    sleepDrainWaiting = false; sleepDrainStarted = 0;
    motionInitializations = 0; motionInitOk = motionPrepareOk = true;
    motionPreparations = motionCancels = 0;
    motionIntLevel = 0; motionStartup = MotionEvent::None;
    motionPendingEvent = MotionEvent::None; motionEventPolls = 0;
    PowerManager::begin(LOCAL_DEVICE, queueSleepControl);
    // Existing protocol tests model an old, stationary runtime with automatic
    // initiation already consumed. Dedicated policy tests arm a fresh episode.
    lastMeaningfulActivity = uint32_t(0 - AUTOMATIC_SLEEP_INACTIVITY_MS);
    automaticSleepArmed = false;
    hostPixel() = {}; led.begin(); hostPixel() = {}; // Fresh awake LED, with startup instrumentation reset.
    led.requests = 0;
    ledBeginsAtBoot = ledShowsAtBoot = 0;
    hostPixel().onBegin = [] {
        assert(!protocolReady && motionInitializations == 1 && displayInitializations == 1);
        assert(bootInfo.deep ? wakeRecoveries == 1 : armInitializations == 1);
    };
}
void freshKnownApp(ProximityClassification classification = ProximityClassification::CLOSE)
{
    freshApp(); proximityClassification = classification;
}
void command(char c) { Serial.input.push_back(c); loop(); }
void receive(const Protocol::Message& message)
{
    queueReceivedData(reinterpret_cast<const uint8_t*>(&message), sizeof(message));
    loop();
}
Protocol::Message incoming(Type type, uint16_t id, uint16_t packet = 90)
{
    return control(type, id, packet, PEER_DEVICE);
}
size_t countWire(Type type)
{
    size_t count = 0;
    for (const auto& message : wire) if (message.type == type) ++count;
    return count;
}
void testTransport()
{
    // Receipt ACK never substitutes for SLEEP_ACK; an out-of-order READY can
    // nevertheless retire REQUEST whose receipt ACK was lost.
    freshApp(); command('i'); command('s'); const auto request = pendingMessage;
    assert(request.type == Type::SleepRequest && request.messageId == request.ackForMessageId);
    receive(incoming(Type::Ack, request.messageId));
    assert(transaction().active && transaction().phase == SleepPhase::WAIT_READY);
    receive(incoming(Type::SleepReady, request.messageId));
    assert(pendingMessage.type == Type::SleepCommit && waitingForAck);
    const auto commit = pendingMessage;
    receive(incoming(Type::Ack, commit.messageId)); assert(transaction().active);
    receive(incoming(Type::SleepAck, request.messageId));
    assert(localState() == LocalState::SLEEPING && peerState() == PeerState::SLEEPING);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(benchSleepCalls == 0 && coordinatedAttempts == 1 && physicalSleeps == 1);
    assert(armAttempts == 1);
    assert(Serial.log.find("sleepId=" + std::to_string(request.messageId) + " | role=COORDINATOR") != std::string::npos);
    receive(incoming(Type::SleepAck, request.messageId));
    for (int i = 0; i < 100; ++i) loop();
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(benchSleepCalls == 0 && coordinatedAttempts == 1 && physicalSleeps == 1);
    assert(armAttempts == 1);

    // Missing peer: exactly three sends of the same REQUEST ID, then no loop.
    freshApp(); command('i'); command('s'); auto began = ackWaitStart; auto id = pendingMessage.messageId;
    hostNow = began + 299; loop(); assert(countWire(Type::SleepRequest) == 1);
    hostNow = began + 300; loop(); assert(retryCount == 1);
    hostNow = began + 600; loop(); assert(retryCount == 2);
    hostNow = began + 900; loop();
    assert(!waitingForAck && !transaction().active && peerState() == PeerState::OFFLINE);
    for (const auto& message : wire) if (message.type == Type::SleepRequest) assert(message.messageId == id);
    for (int i = 0; i < 1000; ++i) loop(); assert(countWire(Type::SleepRequest) == 3);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);

    // Duplicates coalesce with in-flight responses, never restart their budget.
    freshApp(); command('i'); const auto peerRequest = incoming(Type::SleepRequest, 20);
    receive(peerRequest); assert(pendingMessage.type == Type::SleepReady);
    began = ackWaitStart; id = pendingMessage.messageId;
    receive(peerRequest);
    assert(ackWaitStart == began && pendingMessage.messageId == id && countWire(Type::SleepReady) == 1);
    hostNow = began + 300; loop(); assert(retryCount == 1 && countWire(Type::SleepReady) == 2);
    auto peerCommit = incoming(Type::SleepCommit, 20, 21);
    receive(peerCommit); // Retires READY, sends SLEEP_ACK, and only then closes.
    assert(localState() == LocalState::SLEEPING && pendingMessage.type == Type::SleepAck);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);
    const auto finalAck = pendingMessage;
    auto transitions = occurrences(Serial.log, "HANDSHAKE_COMPLETE");
    began = ackWaitStart; receive(peerCommit);
    assert(ackWaitStart == began && countWire(Type::SleepAck) == 1);
    hostNow = began + 300; loop(); assert(countWire(Type::SleepAck) == 2 && pendingMessage.messageId == finalAck.messageId);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);
    receive(incoming(Type::Ack, finalAck.messageId)); assert(!waitingForAck && peerState() == PeerState::SLEEPING);
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(benchSleepCalls == 0 && coordinatedAttempts == 1 && physicalSleeps == 1);
    assert(armAttempts == 1);
    assert(Serial.log.find("sleepId=20 | role=PARTICIPANT") != std::string::npos);
    receive(peerCommit); assert(countWire(Type::SleepAck) == 3);
    assert(occurrences(Serial.log, "HANDSHAKE_COMPLETE") == transitions);
    receive(incoming(Type::Ack, pendingMessage.messageId));
    for (int i = 0; i < 100; ++i) loop();
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 1);
    assert(benchSleepCalls == 0 && coordinatedAttempts == 1 && physicalSleeps == 1);
    assert(armAttempts == 1);

    // Real loop collision retires the losing request, even with equal IDs.
    freshApp(); command('i'); command('s'); id = pendingMessage.messageId;
    receive(incoming(Type::SleepRequest, id));
    if (LOCAL_DEVICE == Device::Bubu)
        assert(transaction().role == SleepRole::COORDINATOR && pendingMessage.type == Type::SleepRequest);
    else
        assert(transaction().role == SleepRole::PARTICIPANT && pendingMessage.type == Type::SleepReady);
    assert(countWire(Type::SleepCancel) == 0);

    // Callback still copies only: state changes occur when loop drains the queue.
    freshApp(); command('i'); auto message = incoming(Type::SleepRequest, 30);
    queueReceivedData(reinterpret_cast<const uint8_t*>(&message), sizeof(message));
    assert(!transaction().active && wire.empty()); loop(); assert(transaction().active);
    // Background Heartbeat and its duplicate preserve negotiation; real local Activity cancels.
    Protocol::Message event{1, Type::Event, 70, PEER_DEVICE, Protocol::EventType::Heartbeat, 0};
    receive(event); assert(localState() == LocalState::SLEEP_NEGOTIATING && transaction().active && countWire(Type::SleepCancel) == 0);
    receive(event); assert(countWire(Type::SleepCancel) == 0 && Serial.log.find("RX DUPLICATE") != std::string::npos);
    motionPendingEvent = MotionEvent::Activity; loop();
    assert(localState() == LocalState::ACTIVE && !transaction().active && countWire(Type::SleepCancel) == 1);
    receive(incoming(Type::SleepCommit, 30)); assert(localState() == LocalState::ACTIVE);

    // Cancel removes a queued unsent COMMIT and an old in-flight REQUEST.
    freshApp(); command('d'); command('i'); command('s');
    assert(controlCount == 1 && !waitingForAck); command('a');
    assert(controlCount == 0 && localState() == LocalState::ACTIVE);
    for (int i = 0; i < 600; ++i) loop(); assert(countWire(Type::SleepRequest) == 0);
    freshApp(); command('i'); command('s'); id = pendingMessage.messageId; command('d');
    receive(incoming(Type::SleepReady, id)); assert(controlCount == 1 && !waitingForAck);
    command('a'); for (int i = 0; i < 600; ++i) loop(); assert(countWire(Type::SleepCommit) == 0);

    // Existing EVENT retry budget/ID/ACK matching remains unchanged.
    freshApp(); startHeartbeatEvent(); id = pendingMessage.messageId; began = ackWaitStart;
    receive(incoming(Type::Ack, uint16_t(id + 1))); assert(waitingForAck);
    hostNow = began + 300; loop(); hostNow = began + 600; loop();
    assert(countWire(Type::Event) == 3 && retryCount == 2);
    for (const auto& packet : wire) if (packet.type == Type::Event) assert(packet.messageId == id);
    receive(incoming(Type::Ack, id)); assert(!waitingForAck);
    // Automatic EVENTs are suppressed in negotiation and simulated SLEEPING.
    freshApp(); pauseAutomaticHeartbeats = false; nextEventTime = 0;
    PowerManager::forceIdle(0); PowerManager::requestSleep(nextMessageId++, 0); loop();
    assert(countWire(Type::Event) == 0);
    id = transaction().sleepId; receive(incoming(Type::SleepReady, id)); receive(incoming(Type::SleepAck, id));
    for (int i = 0; i < 600; ++i) loop(); assert(countWire(Type::Event) == 0);
    // A rejected first send cannot close the participant before a later accepted retry.
    freshApp(); command('i'); receive(incoming(Type::SleepRequest, 20)); radioAccepts = false;
    receive(incoming(Type::SleepCommit, 20, 21)); assert(transaction().active);
    radioAccepts = true; hostNow = ackWaitStart + 300; loop(); assert(localState() == LocalState::SLEEPING);
    // Even a delayed direct retry checks the absolute deadline before sending.
    freshApp(); command('i'); command('s'); id = transaction().sleepId;
    receive(incoming(Type::SleepReady, id)); auto commits = countWire(Type::SleepCommit);
    hostNow = transaction().hardDeadline; transmitPendingMessage(true);
    assert(countWire(Type::SleepCommit) == commits && !transaction().active && !waitingForAck);
    assert(peerState() == PeerState::UNKNOWN);
    receive(incoming(Type::Ack, wire.back().messageId)); // Late CANCEL receipt, not sleep evidence.
    assert(peerState() == PeerState::UNKNOWN);
    // Losing all receipts for the final SLEEP_ACK is bounded and reported UNKNOWN.
    freshApp(); command('i'); receive(incoming(Type::SleepRequest, 20));
    receive(incoming(Type::SleepCommit, 20, 21)); began = ackWaitStart;
    hostNow = began + 300; loop(); hostNow = began + 600; loop(); hostNow = began + 900; loop();
    assert(countWire(Type::SleepAck) == 3 && !waitingForAck && !transaction().active);
    assert(localState() == LocalState::IDLE && peerState() == PeerState::UNKNOWN);
}

void testDelayedCollision()
{
    // Replay both sides of the same timed hardware exchange, each against its
    // peer's scheduled packets. Both start as coordinators before any REQUEST
    // is delivered. Use the real loop/outbox and the unchanged d=1000ms delay.
    freshApp(); delayControlsForTest = true;
    const uint16_t ownId = LOCAL_DEVICE == Device::Bubu ? 40 : 33;
    hostNow = LOCAL_DEVICE == Device::Bubu ? 100 : 0;
    nextMessageId = ownId;
    PowerManager::forceIdle(hostNow);
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
    assert(benchSleepCalls == 0 && coordinatedAttempts == 1 && physicalSleeps == 1);
    assert(armAttempts == 1);
}

void testFinalSendBounds()
{
    // READY is already delivered. Delay final SLEEP_ACK using the actual bench
    // setting; COMMIT must create one new phase budget, not a new transaction.
    const auto prepare = [](uint32_t commitAt)
    {
        freshApp(); command('i'); receive(incoming(Type::SleepRequest, 20));
        receive(incoming(Type::Ack, pendingMessage.messageId));
        delayControlsForTest = true;
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
    assert(!transaction().active && localState() == LocalState::IDLE && controlCount == 0);
    assert(countWire(Type::SleepAck) == 0 && countWire(Type::SleepCancel) == 1);
    assert(Serial.log.find("PHASE_TIMEOUT") != std::string::npos && cooldownLeftMs(finalPhaseDeadline) == 3000);
    PowerManager::controlSent(Type::SleepAck, 20, finalPhaseDeadline + 1);
    assert(localState() == LocalState::IDLE); // Late notification cannot complete.
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
    assert(!transaction().active && localState() == LocalState::IDLE && controlCount == 0);
    assert(countWire(Type::SleepAck) == 0 && Serial.log.find("HARD_TIMEOUT") != std::string::npos);
    assert(!takeSleepDecision(decision) && occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(armAttempts == 0);

    for (bool remoteCancel : {false, true})
    {
        prepare(500);
        hostNow = 800;
        if (remoteCancel) receive(incoming(Type::SleepCancel, 20));
        else command('a');
        const auto expected = remoteCancel ? LocalState::IDLE : LocalState::ACTIVE;
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
        freshPower(); forceIdle(0);
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
    freshPower(); forceIdle(0);
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
        freshPower(); forceIdle(0); requestSleep(27, 0);
        handleControl(control(Type::SleepReady, 27), 10);
        handleControl(control(Type::SleepAck, 27), 20);
        if (reinitialize) PowerManager::begin(Device::Bubu, captureIntent);
        else { injectActivity(30); update(280); }
        assert(!takeSleepDecision(decision));
        // The latch is per completion, not a once-per-boot flag.
        forceIdle(300); assert(requestSleep(28, 300));
        handleControl(control(Type::SleepReady, 28), 310);
        handleControl(control(Type::SleepAck, 28), 320);
        assert(takeSleepDecision(decision) && decision.sleepId == 28);
        assert(!takeSleepDecision(decision));
    }
}

void testExecutionDrain()
{
    // Final ACK retries block physical sleep. Exhaustion now aborts execution
    // instead of physically sleeping without the participant receipt.
    freshApp(); command('i'); receive(incoming(Type::SleepRequest, 20));
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
    assert(benchSleepCalls == 0);
    assert(armAttempts == 0);
    for (int i = 0; i < 100; ++i) loop();
    assert(occurrences(Serial.log, "SLEEP EXECUTION READY |") == 0);
    assert(benchSleepCalls == 0);
    assert(armAttempts == 0);

    assert(localState() == LocalState::IDLE && physicalSleeps == 0 && coordinatedAttempts == 0);

    // Receipt followed by duplicate COMMIT in the same RX batch can leave a
    // required replay queued but not yet eligible for TX. Queue also must drain.
    freshApp(); command('i'); receive(incoming(Type::SleepRequest, 20));
    const auto commit = incoming(Type::SleepCommit, 20, 21);
    receive(commit); delayControlsForTest = true;
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
    assert(benchSleepCalls == 0 && coordinatedAttempts == 1 && physicalSleeps == 1);
    assert(armAttempts == 1);

    // Activity after semantic completion but before transport drain revokes
    // the unconsumed decision, even if a late receipt/COMMIT follows.
    freshApp(); command('i'); receive(incoming(Type::SleepRequest, 20));
    receive(commit); const auto pendingId = pendingMessage.messageId;
    assert(localState() == LocalState::SLEEPING && waitingForAck);
    command('a');
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
            freshApp(); armResult = failure; command('i');
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
                command('s'); id = transaction().sleepId;
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
            assert(localState() == LocalState::IDLE && peerState() == PeerState::SLEEPING);
            assert(!transaction().active && cooldownLeftMs(hostNow) == 0);
            assert(coordinatedAttempts == 1 && physicalSleeps == 0);
            command('p'); // Serial command remains usable after failure.
            command('a'); hostNow += 250; loop();
            assert(localState() == LocalState::ACTIVE);
            // ESP-NOW remains usable after either role's arm failure.
            startHeartbeatEvent(); receive(incoming(Type::Ack, pendingMessage.messageId));
            assert(!waitingForAck && armAttempts == 1);
        }
    }
}

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
    command('i'); command('s');
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
    PowerManager::forceIdle(hostNow);
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
    freshApp(); command('i'); receive(incoming(Type::SleepRequest, 0xFFFF));
    const auto commit = incoming(Type::SleepCommit, 0xFFFF, 21);
    PowerManager::handleControl(commit, hostNow, false); sendNextControl();
    assert(localState() == LocalState::SLEEPING && waitingForAck);
    assert(sleepDecisionPending && completed.valid); // Ensure test source really is live.
    saveRtcHistory(); simulateHistoryRestart();
    const auto history = PowerManager::exportHistory();
    assert(history.havePeerRequest && history.newestPeerRequest == 0xFFFF);
    command('i');
    receive(incoming(Type::SleepRequest, 0xFFFF));
    receive(incoming(Type::SleepRequest, 0xFFFE));
    receive(commit); receive(incoming(Type::SleepAck, 0xFFFF));
    assert(!transaction().active && countWire(Type::SleepReady) == 0 && countWire(Type::SleepAck) == 0);
    assert(armAttempts == 0 && localState() == LocalState::IDLE);
    receive(incoming(Type::SleepRequest, 0)); // Existing half-range arithmetic accepts rollover.
    assert(transaction().active && transaction().sleepId == 0);
    command('a'); assert(cooldownLeftMs(hostNow) > 0);
    saveRtcHistory(); simulateHistoryRestart(); // No old cooldown or timestamps.
    command('i'); receive(incoming(Type::SleepRequest, 0));
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
        command('p'); assert(wakeRecoveries == 1 && armInitializations == 0);
    }
    // x cannot interrupt ACK/retry, an unsent control, received work or negotiation.
    freshApp(); startHeartbeatEvent(); command('x'); assert(benchSleepCalls == 0);
    freshApp(); command('d'); command('i'); command('s'); command('x'); assert(benchSleepCalls == 0);
    freshApp(); command('i'); command('s'); receive(incoming(Type::Ack, pendingMessage.messageId));
    command('x'); assert(benchSleepCalls == 0); // Waiting READY, transport already drained.
    freshApp(); queueReceivedData(reinterpret_cast<const uint8_t*>(&injectedPacket), sizeof(injectedPacket));
    command('x'); assert(benchSleepCalls == 0);
    freshApp(); command('x');
    assert(benchSleepCalls == 1 && !transaction().active && countWire(Type::SleepRequest) == 0);
    assert(localState() == LocalState::ACTIVE && peerState() == PeerState::UNKNOWN);
    puts("PASS: cold/deep startup routing, RTC before wake EVENT dedup, fresh runtime, one recovery, x transport/FSM guards");
}

void testManualWakeTx()
{
    freshApp(); protocolReady = false; command('w');
    assert(wakeEvents.empty() && nextMessageId == 1);
    freshApp(); startHeartbeatEvent(); const auto id = nextMessageId; command('w');
    assert(wakeEvents.empty() && nextMessageId == id);
    freshApp(); command('d'); command('i'); command('s'); command('w');
    assert(wakeEvents.empty());
    freshApp(); command('i'); command('s'); receive(incoming(Type::Ack, pendingMessage.messageId));
    assert(!waitingForAck && transaction().active); command('w'); assert(wakeEvents.empty());
    // A queued control blocks w even without an active FSM transaction.
    freshApp(); assert(queueSleepControl(Type::SleepReady, 40)); command('w'); assert(wakeEvents.empty());
    freshApp(); queueReceivedData(reinterpret_cast<const uint8_t*>(&injectedPacket), sizeof(injectedPacket));
    command('w'); assert(wakeEvents.empty());
    freshApp(); nextMessageId = 0xFFFF;
    command('w'); assert(wakeEvents.size() == 1 && nextMessageId == 0);
    assert(wakeEvents[0].messageId == 0xFFFF && wakeEvents[0].type == Type::Event &&
           wakeEvents[0].version == Protocol::VERSION && wakeEvents[0].sender == LOCAL_DEVICE &&
           wakeEvents[0].event == Protocol::EventType::Heartbeat && wakeEvents[0].ackForMessageId == 0);
    assert(Serial.log.find("GIVE_UP | retries=2") != std::string::npos);
    for (unsigned i = 0; i < 20; ++i) loop();
    assert(wakeEvents.size() == 1 && nextMessageId == 0 && wire.empty() && !transaction().active);
    command('w'); assert(wakeEvents.size() == 2 && wakeEvents[1].messageId == 0 && nextMessageId == 1);
    // Existing ESP-NOW EVENT path still uses that same allocator afterwards.
    startHeartbeatEvent(); assert(pendingMessage.messageId == 1 && nextMessageId == 2);
    puts("PASS: manual w runtime/transport/FSM guards, one allocator increment, rollover, no automatic wake TX");
}

void completedAwaitingCallbacks(bool participant)
{
    freshApp(); command('i');
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
        command('s'); const auto id = transaction().sleepId;
        receive(incoming(Type::SleepReady, id));
        mockedTxInFlight = 1; // Includes final fire-and-forget receipt ACK.
        receive(incoming(Type::SleepAck, id));
    }
    assert(!waitingForAck && !transaction().active && controlCount == 0);
    assert(armAttempts == 0 && coordinatedAttempts == 0 && physicalSleeps == 0);
    assert(localState() == LocalState::SLEEPING);
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
    freshApp(); command('i'); receive(incoming(Type::SleepRequest, 20));
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
    assert(localState() == LocalState::IDLE && peerState() == PeerState::SLEEPING);
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
        assert(localState() == LocalState::IDLE && peerState() == PeerState::SLEEPING);
        assert(!transaction().active && cooldownLeftMs(hostNow) > 0);
        mockedTxInFlight = 0; mockedRxActive = false;
        for (int i = 0; i < 500; ++i) loop();
        assert(coordinatedAttempts == 1 && physicalSleeps == 0);
    }
    // Rejected final receipt must not turn into physical sleep with zero callbacks.
    freshApp(); command('i'); command('s'); const auto id = transaction().sleepId;
    receive(incoming(Type::SleepReady, id)); radioAccepts = false;
    receive(incoming(Type::SleepAck, id));
    assert(localState() == LocalState::IDLE && peerState() == PeerState::SLEEPING && physicalSleeps == 0);

    // The failure notification cannot cancel an ACTIVE/negotiating FSM.
    freshPower(); notifySleepExecutionFailed(0);
    assert(localState() == LocalState::ACTIVE && cooldownLeftMs(0) == 0);
    forceIdle(0); requestSleep(27, 0); notifySleepExecutionFailed(10);
    assert(transaction().active && localState() == LocalState::SLEEP_NEGOTIATING);
    handleControl(control(Type::SleepReady, 27), 10);
    handleControl(control(Type::SleepAck, 27), 20);
    notifySleepExecutionFailed(21);
    SleepDecision decision{};
    assert(!takeSleepDecision(decision) && !transaction().active && localState() == LocalState::IDLE);
    assert(peerState() == PeerState::SLEEPING && cooldownLeftMs(21) == 3000);
    notifySleepExecutionFailed(100);
    assert(cooldownLeftMs(100) == 2921); // Duplicate notification cannot renew cooldown.
    assert(!requestSleep(28, 22));
    update(3021); assert(!transaction().active && !takeSleepDecision(decision));
    // Manual x uses the same strengthened callback gate. w remains independent.
    freshApp(); mockedTxInFlight = 1; command('x'); assert(benchSleepCalls == 0);
    mockedTxInFlight = 0; command('x'); assert(benchSleepCalls == 1);

    // Timer wake restores history only, with the actual setup routing and new
    // runtime defaults. UNKNOWN stays silent while one bootstrap check starts.
    freshApp();
    RtcState::save({123, true, 70, {true, 40}});
    protocolReady = false; pendingMessage = {}; ackWaitStart = 0;
    pauseAutomaticHeartbeats = false;
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
    puts("PASS: bounded drain timeout, busy-after-arm abort, failed-entry IDLE/cooldown/peer preservation, no retry");
    puts("PASS: timer reboot restores history only, fresh drain/retry/queue/deadline state, ESP-NOW peer rediscovery");
}

void testAwakeWakeService()
{
    freshApp();
    awakeAckBusy = true;
    assert(std::string(sleepTransportBlockedReason()) == "CC1101_RUNTIME_BUSY");
    command('w'); assert(wakeEvents.empty());
    command('x'); assert(benchSleepCalls == 0);
    // The radio's in-flight ACK must not prevent ordinary ESP-NOW receipt work.
    startHeartbeatEvent();
    const auto id = pendingMessage.messageId;
    receive(incoming(Type::Ack, id));
    assert(!waitingForAck && peerState() == PeerState::ONLINE && awakeServices >= 3);
    awakeAckBusy = false;
    assert(sleepTransportBlockedReason() == nullptr);
    command('w'); assert(wakeEvents.size() == 1);
    completedAwaitingCallbacks(false);
    mockedTxInFlight = 0; awakeAckBusy = true;
    loop(); assert(physicalSleeps == 0 && coordinatedAttempts == 0);
    awakeAckBusy = false;
    loop(); assert(physicalSleeps == 1 && coordinatedAttempts == 1);
    freshApp(); protocolReady = false;
    loop(); assert(awakeServices == 0);
    puts("PASS: awake CC1101 service stays in loop, ACK blocks competing sleep/TX, ESP-NOW still drains");
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
        assert(localState() == LocalState::IDLE && peerState() != PeerState::OFFLINE);
        for (unsigned i = 0; i < 20; ++i) loop();
        assert(motionPreparations == 1 && wakeEvents.empty());
    }
    freshApp(); motionPrepareOk = false;
    command('x');
    assert(benchSleepCalls == 0 && motionCancels == 1 && localState() == LocalState::ACTIVE);

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

void testAwakeMotionDiagnostics()
{
    for (auto event : {MotionEvent::Activity, MotionEvent::Inactivity})
    for (bool negotiating : {false, true}) for (bool sensorOk : {false, true})
    {
        freshApp();
        if (negotiating) { command('i'); command('s'); }
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
        assert(nextMessageId == id && nextEventTime == heartbeatAt && pauseAutomaticHeartbeats);
        assert(wire.size() == sent && wakeEvents.empty() && controlCount == queued);
        assert(waitingForAck == waiting && retryCount == retries);
        assert(memcmp(&pendingMessage, &pending, sizeof(pending)) == 0);
        assert(motionPreparations == 0 && motionCancels == 0 && physicalSleeps == 0 && armAttempts == 0);
        assert(!haveLastPeerEvent);
    }

    // A normally scheduled heartbeat still runs, identically with or without motion.
    for (auto event : {MotionEvent::None, MotionEvent::Activity, MotionEvent::Inactivity})
    {
        freshKnownApp(); pauseAutomaticHeartbeats = false; nextEventTime = 10;
        motionPendingEvent = event;
        loop(); // Not due yet: movement must not pull the schedule forward.
        assert(wire.empty() && nextEventTime == 10 && nextMessageId == 1);
        motionPendingEvent = event;
        loop();
        assert(wire.size() == 1 && wire[0].type == Type::Event && wire[0].messageId == 1);
        assert(waitingForAck && nextMessageId == 2 && nextEventTime == 10 && !pauseAutomaticHeartbeats);
        assert(localState() == LocalState::ACTIVE && peerState() == PeerState::UNKNOWN && wakeEvents.empty());
    }
    freshApp(); protocolReady = false; motionPendingEvent = MotionEvent::Activity;
    loop();
    assert(motionEventPolls == 0 && Serial.log.find("MOTION AWAKE |") == std::string::npos);
    puts("PASS: real Motion Activity cancels negotiation and rearms inactivity; Inactivity/unavailable sensor preserve power/transport, heartbeat schedule unchanged");
}

void movementStep(uint32_t now, MotionEvent event = MotionEvent::None)
{
    hostNow = now;
    motionPendingEvent = event;
    const auto polls = motionEventPolls;
    loop();
    assert(motionEventPolls == polls + 1); // Exactly one sensor read per ready loop.
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
    for (auto state : {LocalState::IDLE, LocalState::SLEEP_NEGOTIATING,
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
            command('i');
            if (state == LocalState::SLEEP_NEGOTIATING) command('s');
        }
        assert(localState() == state);
        movementState = stale;
        settleStartedAt = hostNow - SETTLE_MS; // Would expire if not suppressed.
        Serial.log.clear();
        movementStep(hostNow, event);
        const bool resumes = event == MotionEvent::Activity &&
            (state == LocalState::IDLE || state == LocalState::SLEEP_NEGOTIATING);
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
        command('x');
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
            freshKnownApp(); pauseAutomaticHeartbeats = !heartbeats; nextEventTime = 50;
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

void testRssiDiagnostics()
{
    freshApp();
    ESPNowRadio::RssiObservation observation{};
    observation.message = {Protocol::VERSION, Type::Event, 42, PEER_DEVICE, Protocol::EventType::Heartbeat, 0};
    observation.rssi = -61; observation.receivedAt = UINT32_MAX - 5;
    rssiObservations.push_back(observation); refillRssi = true;
    hostNow = 20;
    loop();
    assert(rssiReads == 2 && rssiObservations.size() == 1); // Continuous producer cannot extend the batch.
    assert(occurrences(Serial.log, "ESPNOW RSSI |") == 2);
    assert(Serial.log.find("rssi=-61 dBm | age=26 ms") != std::string::npos);
    assert(nextMessageId == 1 && wire.empty() && !haveLastPeerEvent && wakeEvents.empty());
    assert(localState() == LocalState::ACTIVE && peerState() == PeerState::UNKNOWN);

    // Same traffic timeline with and without observations, including duplicate
    // EVENT delivery, matched ACK, and an exhausted 300 ms / two-retry episode.
    std::vector<Protocol::Message> baselineWire;
    std::string baselineState;
    for (bool diagnostics : {false, true})
    {
        freshApp(); ackWaitStart = 0; pendingMessage = {};
        if (diagnostics) { rssiObservations.push_back(observation); refillRssi = true; }
        std::string states;
        for (unsigned step = 0; step < 7; ++step)
        {
            hostNow = step * 300;
            if (step == 0) receive(observation.message);
            if (step == 1) receive(observation.message);
            if (step == 2)
            {
                startHeartbeatEvent();
                receive(incoming(Type::Ack, pendingMessage.messageId));
            }
            if (step == 3) startHeartbeatEvent();
            loop();
            states += std::to_string(nextMessageId) + ":" + std::to_string(waitingForAck) + ":" +
                std::to_string(retryCount) + ":" + std::to_string(nextEventTime) + ":" +
                std::to_string(controlCount) + ":" + std::to_string(lastPeerEventId) + ":" +
                std::to_string(static_cast<int>(peerState())) + ":" +
                std::to_string(static_cast<int>(localState())) + ";";
            PowerManager::SleepDecision decision{};
            assert(!transaction().active && !PowerManager::takeSleepDecision(decision));
            assert(wakeEvents.empty() && physicalSleeps == 0 && movementState == MovementState::READY);
        }
        assert(!waitingForAck && lastPeerEventId == 42 && haveLastPeerEvent);
        if (!diagnostics) { baselineState = states; baselineWire = wire; }
        else
        {
            assert(states == baselineState && wire.size() == baselineWire.size());
            for (size_t i = 0; i < wire.size(); ++i)
                assert(memcmp(&wire[i], &baselineWire[i], sizeof(Protocol::Message)) == 0);
        }
    }
    freshApp(); command('i'); command('s');
    const auto sleep = transaction();
    const auto id = nextMessageId, pendingId = pendingMessage.messageId;
    const auto sent = wire.size(), queued = controlCount;
    const auto peer = peerState();
    auto controlObservation = observation;
    controlObservation.message = incoming(Type::SleepCancel, sleep.sleepId);
    rssiObservations.push_back(controlObservation);
    loop();
    assert(localState() == LocalState::SLEEP_NEGOTIATING && peerState() == peer);
    assert(transaction().active && transaction().sleepId == sleep.sleepId && transaction().phase == sleep.phase);
    assert(transaction().phaseDeadline == sleep.phaseDeadline && transaction().hardDeadline == sleep.hardDeadline);
    assert(nextMessageId == id && waitingForAck && pendingMessage.messageId == pendingId && retryCount == 0);
    assert(wire.size() == sent && controlCount == queued);
    completedAwaitingCallbacks(false);
    rssiObservations.assign(4, observation);
    mockedTxInFlight = 0;
    const auto reads = rssiReads;
    loop();
    assert(physicalSleeps == 1 && rssiObservations.size() == 4 && rssiReads == reads);
    freshApp(); protocolReady = false;
    rssiObservations.push_back(observation); loop();
    assert(rssiReads == 0 && rssiObservations.size() == 1);
    puts("PASS: RSSI diagnostics bounded, no EVENT execution/ACK/IDs/power effects, retries unchanged, backlog does not defer sleep");
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

void testProximitySamples()
{
    freshApp(); assertProximityReset();
    movementStep(0, MotionEvent::Activity); movementStep(10, MotionEvent::Inactivity);
    // A prior partial batch must be cleared when a new operation starts.
    proximitySampleCount = 2; proximitySamples[0] = {40, -30}; proximitySamples[1] = {41, -31};
    rssiObservations.push_back(proximityObservation(1, -10, 1009));
    rssiObservations.push_back(proximityObservation(2, -11, 1010));
    movementStep(1010); // Strictly older AND equal-timestamp queued samples excluded.
    assert(proximityUpdateState == ProximityUpdateState::CHECKING && checkStartedAt == 1010 && proximitySampleCount == 0);
    for (const auto& sample : proximitySamples) assert(sample.messageId == 0 && sample.rssi == 0);
    assert(rssiObservations.empty());
    for (unsigned i = 0; i < 5; ++i) loop();
    assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
    auto wrongPeer = proximityObservation(9, -20, 1090);
    wrongPeer.message.sender = LOCAL_DEVICE;
    rssiObservations.push_back(wrongPeer);
    rssiObservations.push_back(proximityObservation(10, -20, 1200)); // Future timestamp.
    movementStep(1090); assert(proximitySampleCount == 0);
    rssiObservations.push_back(proximityObservation(65535, -52, 1100));
    movementStep(1110); assert(proximitySampleCount == 1);
    rssiObservations.push_back(proximityObservation(65535, -99, 1120));
    rssiObservations.push_back(proximityObservation(0, -67, 1121));
    movementStep(1130); assert(proximitySampleCount == 2 && checkStartedAt == 1010);
    assert(proximitySamples[0].rssi == -52); // Retry neither replaces nor adds a sample.
    startProximityCheck(1140); // Repeated start cannot extend an active operation.
    assert(checkStartedAt == 1010 && proximitySampleCount == 2);
    rssiObservations.push_back(proximityObservation(65535, -20, 1141));
    rssiObservations.push_back(proximityObservation(0, -21, 1142));
    movementStep(1150); assert(proximitySampleCount == 2);
    rssiObservations.push_back(proximityObservation(1, -54, 1160));
    movementStep(1170); assertProximityReset();
    assert(occurrences(Serial.log, "PROXIMITY CHECK | SAMPLE") == 3);
    assert(Serial.log.find("COMPLETE | samples=3 | median=-54 dBm") != std::string::npos);
    rssiObservations.push_back(proximityObservation(2, -40, 1180));
    movementStep(1190); assertProximityReset();
    assert(occurrences(Serial.log, "PROXIMITY CHECK | COMPLETE") == 1);

    const int8_t cases[][4] = {{-67,-54,-52,-54}, {-52,-54,-67,-54}, {-52,-67,-54,-54},
                              {-54,-52,-67,-54}, {-90,-53,-53,-53}, {-128,-127,-1,-127}};
    for (const auto& values : cases)
    {
        freshApp(); settleForCheck(2000);
        for (unsigned i = 0; i < 3; ++i)
            rssiObservations.push_back(proximityObservation(i, values[i], 2010 + i));
        const auto reads = rssiReads;
        movementStep(2020);
        assert(rssiReads == reads + 2 && proximitySampleCount == 2 && rssiObservations.size() == 1);
        assert(Serial.log.find("PROXIMITY CHECK | COMPLETE") == std::string::npos);
        movementStep(2030); assertProximityReset();
        assert(Serial.log.find("median=" + std::to_string(values[3]) + " dBm") != std::string::npos);
    }
    puts("PASS: SETTLED starts once, strict capture-time freshness, message-ID dedup/wrap, signed median and two-observation drain");
}

void testProximityTimeout()
{
    for (uint32_t start : {2000U, UINT32_MAX - 500U})
    for (unsigned count = 0; count < 3; ++count)
    {
        freshApp(); settleForCheck(start);
        rssiObservations.push_back(proximityObservation(99, -10, start - 1));
        movementStep(uint32_t(start + 10)); assert(proximitySampleCount == 0);
        if (count > 0)
        {
            rssiObservations.push_back(proximityObservation(1, -50, uint32_t(start + 100)));
            movementStep(uint32_t(start + 100)); assert(proximitySampleCount == 1);
        }
        if (count > 1)
            rssiObservations.push_back(proximityObservation(2, -60, uint32_t(start + CHECK_TIMEOUT_MS - 1)));
        movementStep(uint32_t(start + CHECK_TIMEOUT_MS - 1));
        assert(proximityUpdateState == ProximityUpdateState::CHECKING && proximitySampleCount == count);
        assert(checkStartedAt == start && Serial.log.find("PROXIMITY CHECK | TIMEOUT") == std::string::npos);
        // Even a third frame captured before deadline cannot complete if drained at expiry.
        rssiObservations.push_back(proximityObservation(3, -55, uint32_t(start + CHECK_TIMEOUT_MS - 1)));
        movementStep(uint32_t(start + CHECK_TIMEOUT_MS)); assertProximityReset();
        assert(Serial.log.find("TIMEOUT | samples=" + std::to_string(count)) != std::string::npos);
        for (unsigned i = 0; i < 10; ++i) loop();
        assert(occurrences(Serial.log, "PROXIMITY CHECK | TIMEOUT") == 1);
        assert(Serial.log.find("PROXIMITY CHECK | COMPLETE") == std::string::npos);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
    }
    // Successful capture and completion across millis rollover too.
    freshApp(); const uint32_t start = UINT32_MAX - 10; settleForCheck(start);
    for (unsigned i = 0; i < 3; ++i)
        rssiObservations.push_back(proximityObservation(i, -50 - i, uint32_t(start + 20 + i)));
    movementStep(uint32_t(start + 30)); movementStep(uint32_t(start + 40)); assertProximityReset();
    assert(Serial.log.find("median=-51 dBm") != std::string::npos);
    puts("PASS: absolute 12s timeout, partial progress cannot extend, boundary precedence and rollover freshness/completion");
}

void testProximityCancellation()
{
    for (uint32_t age : {100U, CHECK_TIMEOUT_MS})
    {
        freshApp(); settleForCheck(2000);
        rssiObservations.push_back(proximityObservation(1, -50, 2020)); movementStep(2020);
        rssiObservations.push_back(proximityObservation(2, -60, 2000 + age - 1));
        rssiObservations.push_back(proximityObservation(3, -55, 2000 + age - 1));
        movementStep(2000 + age, MotionEvent::Activity); assertProximityReset();
        assert(movementState == MovementState::MOVING);
        assert(occurrences(Serial.log, "CANCELLED | reason=MOVEMENT") == 1);
        assert(Serial.log.find("PROXIMITY CHECK | TIMEOUT") == std::string::npos);
        assert(Serial.log.find("PROXIMITY CHECK | COMPLETE") == std::string::npos);
        const uint32_t restart = 2000 + age + SETTLE_MS + 20;
        movementStep(restart - SETTLE_MS, MotionEvent::Inactivity); movementStep(restart);
        assert(checkStartedAt == restart && proximitySampleCount == 0);
        for (unsigned i = 1; i <= 3; ++i) // Same IDs are valid again in a new check.
            rssiObservations.push_back(proximityObservation(i, -50 - i, restart + 10));
        movementStep(restart + 20); movementStep(restart + 30); assertProximityReset();
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 2);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | COMPLETE") == 1);
    }
    freshApp(); settleForCheck(2000); notePeerUnreachable(); loop(); assertProximityReset();
    assert(peerState() == PeerState::OFFLINE && localState() == LocalState::ACTIVE);
    assert(occurrences(Serial.log, "CANCELLED | reason=PEER_OFFLINE") == 1);
    for (auto state : {LocalState::IDLE, LocalState::SLEEP_NEGOTIATING, LocalState::SLEEPING, LocalState::WAKING})
    {
        freshApp();
        if (state == LocalState::SLEEPING || state == LocalState::WAKING)
        {
            completedAwaitingCallbacks(false);
            if (state == LocalState::WAKING) injectActivity(hostNow);
        }
        else { command('i'); if (state == LocalState::SLEEP_NEGOTIATING) command('s'); }
        proximityUpdateState = ProximityUpdateState::CHECKING; checkStartedAt = hostNow;
        proximitySampleCount = 1; proximitySamples[0] = {1, -60};
        loop(); assertProximityReset(); assert(localState() == state);
        assert(occurrences(Serial.log, "CANCELLED | reason=NOT_ACTIVE") == 1);
    }
    for (bool prepareFails : {false, true})
    {
        freshApp(); settleForCheck(2000);
        motionPrepareOk = !prepareFails;
        command('x'); assertProximityReset();
        assert(motionPreparations == 1 && motionCancels == 1 && physicalSleeps == 0);
        assert(benchSleepCalls == (prepareFails ? 0U : 1U)); // Bench mock returns without sleeping.
        assert(localState() == LocalState::ACTIVE && Serial.log.find("CANCELLED | reason=SLEEP") != std::string::npos);
    }
    freshApp(); completedAwaitingCallbacks(false);
    proximityUpdateState = ProximityUpdateState::CHECKING; checkStartedAt = hostNow;
    proximitySampleCount = 1; proximitySamples[0] = {1, -60};
    mockedTxInFlight = 0; loop(); assertProximityReset(); assert(physicalSleeps == 1);
    freshApp(); settleForCheck(2000); protocolReady = false; loop(); assertProximityReset();
    puts("PASS: Activity beats timeout/queued completion, fresh restart, offline/non-ACTIVE/sleep/runtime cancellations");
}

void testProximityIsolation()
{
    std::vector<Protocol::Message> baselineWire;
    std::string baselineState;
    for (bool measuring : {false, true})
    {
        freshKnownApp(); ackWaitStart = 0; pendingMessage = {};
        if (measuring) settleForCheck(2000);
        hostNow = 2020; pauseAutomaticHeartbeats = false; nextEventTime = 2050;
        std::string state;
        for (uint32_t now : {2050U, 2070U, 2100U, 2110U, 2120U, 2200U, 6200U, 6500U, 6800U, 7100U})
        {
            hostNow = now;
            if (now == 2070) receive(incoming(Type::Ack, pendingMessage.messageId));
            if (now == 2100 || now == 2110)
                receive(proximityObservation(42, -50, now).message); // New EVENT then duplicate.
            if (measuring && (now == 2100 || now == 2110 || now == 2120))
                rssiObservations.push_back(proximityObservation(now, -50 - (now % 3), now));
            loop();
            state += std::to_string(nextMessageId) + ":" + std::to_string(waitingForAck) + ":" +
                std::to_string(retryCount) + ":" + std::to_string(nextEventTime) + ":" +
                std::to_string(controlCount) + ":" + std::to_string(lastPeerEventId) + ":" +
                std::to_string(static_cast<int>(peerState())) + ":" + std::to_string(static_cast<int>(localState())) + ";";
            assert(!transaction().active && wakeEvents.empty() && physicalSleeps == 0);
        }
        if (!measuring) { baselineState = state; baselineWire = wire; }
        else
        {
            assert(state == baselineState && wire.size() == baselineWire.size());
            for (size_t i = 0; i < wire.size(); ++i)
                assert(memcmp(&wire[i], &baselineWire[i], sizeof(Protocol::Message)) == 0);
            assert(occurrences(Serial.log, "PROXIMITY CHECK | COMPLETE") == 1);
        }
    }
    puts("PASS: completed measurement preserves normal EVENT/ACK/dedup/retry, heartbeat schedule, message IDs and power/peer state");
}


void receiveVia(const Protocol::Message& packet, Transport transport)
{
    if (transport == Transport::ESP_NOW) receive(packet);
    else { ccIncoming.push_back(packet); loop(); }
}

void testApplicationTransports()
{
    freshKnownApp(); assert(selectedTransport == Transport::ESP_NOW);
    command('c'); assert(selectedTransport == Transport::CC1101);
    command('e'); assert(selectedTransport == Transport::ESP_NOW);
    command('p'); assert(Serial.log.find("selected=ESP-NOW pending=NONE") != std::string::npos);
    for (unsigned guard = 0; guard < 6; ++guard)
    {
        freshKnownApp();
        if (guard == 0) startHeartbeatEvent();
        if (guard == 1) { command('i'); command('s'); }
        if (guard == 2) awakeAckBusy = true;
        if (guard == 3) mockedTxInFlight = 1;
        if (guard == 4) protocolReady = false;
        if (guard == 5) completedAwaitingCallbacks(false);
        command('c'); assert(selectedTransport == Transport::ESP_NOW);
        assert(Serial.log.find("APP TRANSPORT | REFUSED") != std::string::npos);
    }
    for (auto transport : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshKnownApp(); selectedTransport = transport;
        startHeartbeatEvent(); const auto original = pendingMessage;
        assert(waitingForAck && pendingTransport == transport);
        auto& sent = transport == Transport::ESP_NOW ? wire : ccWire;
        assert(sent.size() == 1 && memcmp(&sent[0], &original, 8) == 0);
        command(transport == Transport::ESP_NOW ? 'c' : 'e');
        assert(selectedTransport == transport && waitingForAck);
        const auto other = transport == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW;
        receiveVia(incoming(Type::Ack, original.messageId), other);
        assert(waitingForAck && peerState() == PeerState::UNKNOWN);
        auto wrongPeer = incoming(Type::Ack, original.messageId); wrongPeer.sender = LOCAL_DEVICE;
        receiveVia(wrongPeer, transport); assert(waitingForAck);
        receiveVia(incoming(Type::Ack, original.messageId + 1), transport); assert(waitingForAck);
        hostNow = ackWaitStart + 300;
        receiveVia(incoming(Type::Ack, original.messageId), transport);
        assert(!waitingForAck && retryCount == 0 && sent.size() == 1); // ACK beats exact deadline.
        assert(peerState() == PeerState::ONLINE);
        assert(Serial.log.find(std::string("ACK MATCHED | message=") + std::to_string(original.messageId) +
                               " | via=" + transportName(transport)) != std::string::npos);

        // One shared retry machine; even a forced selector change cannot reroute
        // an in-flight packet (the public command already refuses that change).
        freshKnownApp(); selectedTransport = transport; startHeartbeatEvent();
        const auto retryPacket = pendingMessage;
        selectedTransport = other;
        for (unsigned retry = 1; retry <= 2; ++retry)
        { hostNow = ackWaitStart + 300; loop(); assert(retryCount == retry && pendingTransport == transport); }
        auto& retries = transport == Transport::ESP_NOW ? wire : ccWire;
        assert(retries.size() == 3);
        for (const auto& packet : retries) assert(memcmp(&packet, &retryPacket, 8) == 0);
        hostNow = ackWaitStart + 300; loop();
        assert(!waitingForAck && peerState() == PeerState::OFFLINE);
        for (unsigned i = 0; i < 50; ++i) loop();
        assert(retries.size() == 3);
    }
    for (auto first : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshKnownApp(); notePeerSeen(); // Isolate cross-radio duplicate receipts from first-contact probes.
        const auto other = first == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW;
        selectedTransport = other;
        const auto event = proximityObservation(77, -50, 0).message;
        receiveVia(event, first); receiveVia(event, other);
        assert(occurrences(Serial.log, "RX NEW EVENT") == 1 && occurrences(Serial.log, "RX DUPLICATE") == 1);
        assert(wire.size() == 1 && ccWire.size() == 1);
        assert(wire[0].type == Type::Ack && ccWire[0].type == Type::Ack);
        assert(wire[0].ackForMessageId == 77 && ccWire[0].ackForMessageId == 77);
    }
    // Bidirectional independent EVENTs: receiving/re-ACKing the peer's EVENT
    // cannot replace our own pending packet. First peer receipt is lost.
    freshKnownApp(); command('c'); startHeartbeatEvent(); const auto ours = pendingMessage;
    const auto peerEvent = proximityObservation(99, -50, 0).message;
    receiveVia(peerEvent, Transport::CC1101);
    receiveVia(peerEvent, Transport::CC1101);
    assert(waitingForAck && memcmp(&ours, &pendingMessage, 8) == 0);
    assert(ccWire.size() == 3 && ccWire[1].ackForMessageId == 99 && ccWire[2].ackForMessageId == 99);
    assert(occurrences(Serial.log, "RX NEW EVENT") == 1);
    hostNow = ackWaitStart + 300; loop(); assert(retryCount == 1);
    assert(memcmp(&ccWire.back(), &ours, 8) == 0);
    receiveVia(incoming(Type::Ack, ours.messageId), Transport::CC1101);
    assert(!waitingForAck && retryCount == 0);

    // Receipt TX blocks a due heartbeat, mode change, synchronous w, and sleep.
    freshKnownApp(); command('c'); ccHoldTx = true;
    pauseAutomaticHeartbeats = false; nextEventTime = hostNow;
    receiveVia(peerEvent, Transport::CC1101);
    assert(awakeAckBusy && ccWire.size() == 1 && ccWire[0].type == Type::Ack && !waitingForAck);
    command('e'); command('w'); command('x');
    assert(selectedTransport == Transport::CC1101 && wakeEvents.empty() && benchSleepCalls == 0);
    awakeAckBusy = false; ccHoldTx = false; loop();
    assert(waitingForAck && ccWire.size() == 2 && ccWire.back().type == Type::Event);

    // Every sleep control stays on ESP-NOW, independent of the app selector.
    freshKnownApp(); command('c'); command('i'); command('s');
    const auto request = pendingMessage;
    assert(request.type == Type::SleepRequest && pendingTransport == Transport::ESP_NOW && ccWire.empty());
    receiveVia(incoming(Type::Ack, request.messageId), Transport::CC1101); assert(waitingForAck);
    receive(incoming(Type::Ack, request.messageId));
    receive(incoming(Type::SleepReady, request.messageId));
    assert(pendingMessage.type == Type::SleepCommit && pendingTransport == Transport::ESP_NOW);
    const auto commit = pendingMessage;
    receive(incoming(Type::Ack, commit.messageId));
    receive(incoming(Type::SleepAck, request.messageId));
    assert(physicalSleeps == 1 && ccWire.empty());
    freshKnownApp(); command('c'); command('i'); command('s'); command('a');
    assert(countWire(Type::SleepCancel) == 1 && ccWire.empty());
    freshKnownApp(); command('c'); command('i'); receive(incoming(Type::SleepRequest, 40, 40));
    assert(pendingMessage.type == Type::SleepReady && pendingTransport == Transport::ESP_NOW);
    receive(incoming(Type::Ack, pendingMessage.messageId)); receive(incoming(Type::SleepCommit, 40));
    assert(pendingMessage.type == Type::SleepAck && pendingTransport == Transport::ESP_NOW && ccWire.empty());
    receive(incoming(Type::Ack, pendingMessage.messageId)); assert(physicalSleeps == 1);

    // CC mode keeps normal ESP-NOW RX/ACK alive, but samples only matched probe replies.
    freshKnownApp(); command('c'); settleForCheck(2000);
    assert(wire.size() == 1 && wire[0].type == Type::ProximityProbe);
    receive(peerEvent); assert(wire.size() == 2 && wire.back().type == Type::Ack);
    rssiObservations.push_back(proximityObservation(100, -20, 2010));
    movementStep(2020); assert(proximitySampleCount == 0);
    for (unsigned i = 0; i < 3; ++i)
    {
        movementStep(2030 + 20 * i);
        auto observation = proximityObservation(101 + i, -50 - i, 2031 + 20 * i);
        observation.message.type = Type::ProximityProbeReply;
        observation.message.event = Protocol::EventType::None;
        observation.message.ackForMessageId = probeMessageId;
        rssiObservations.push_back(observation);
        rssiObservations.push_back(observation); // Same reply cannot count twice.
        movementStep(2040 + 20 * i);
    }
    assertProximityReset(); assert(Serial.log.find("median=-51 dBm") != std::string::npos);
    assert(countWire(Type::ProximityProbe) == 3 && !waitingForAck);
    assert(selectedTransport == Transport::CC1101 && ccWire.empty());
    command('c'); // Preserve the bench route after the completed CLOSE measurement.
    command('x'); assert(benchSleepCalls == 1 && motionCancels == 1);
    startHeartbeatEvent(); assert(ccWire.size() == 1); // Aborted bench entry resumes runtime.
    freshKnownApp(); command('c'); ccAccepts = false; startHeartbeatEvent();
    for (unsigned i = 0; i < 3; ++i) { hostNow = ackWaitStart + 300; loop(); }
    assert(!waitingForAck && ccWire.empty() && peerState() == PeerState::OFFLINE);
    puts("PASS: manual transports, route-pinned retries, matching ACK before expiry, cross-radio dedup and receipt priority");
    puts("PASS: simultaneous independent EVENTs, lost ACK/retry/re-ACK, sleep guards/ESP-NOW controls, RSSI/Motion in CC mode");
}

void testProximityProbes()
{
    for (auto transport : {Transport::ESP_NOW, Transport::CC1101})
    for (uint32_t start : {uint32_t(2000), UINT32_MAX - 100})
    {
        freshApp(); selectedTransport = transport; settleForCheck(start);
        const auto first = wire.back();
        assert(first.version == Protocol::VERSION && first.type == Type::ProximityProbe &&
               first.sender == LOCAL_DEVICE && first.event == Protocol::EventType::None && first.ackForMessageId == 0);
        movementStep(start + 499); assert(wire.size() == 1);
        movementStep(start + 500); assert(wire.size() == 2 && probeMessageId != first.messageId);
        auto reply = proximityObservation(100, -55, start + 501);
        reply.message.type = Type::ProximityProbeReply; reply.message.event = Protocol::EventType::None;
        reply.message.ackForMessageId = first.messageId;
        sampleProximity(reply, start + 502); assert(proximitySampleCount == 0);
        reply.message.ackForMessageId = probeMessageId;
        for (unsigned fault = 0; fault < 6; ++fault)
        {
            auto bad = reply;
            if (fault == 0) bad.message.sender = LOCAL_DEVICE;
            if (fault == 1) bad.message.event = Protocol::EventType::Heartbeat;
            if (fault == 2)
            {
                if (transport == Transport::CC1101) bad.message.type = Type::Ack;
                else ++bad.message.ackForMessageId; // ESP-NOW may use ordinary traffic, but replies must correlate.
            }
            if (fault == 3) bad.receivedAt = start;
            if (fault == 4) bad.receivedAt = start + 499;
            if (fault == 5) bad.message.version = 0;
            sampleProximity(bad, start + 502); assert(proximitySampleCount == 0 && probeOutstanding);
        }
        // Observer first, normal callback later: no callback-side correlation required.
        sampleProximity(reply, start + 502); assert(proximitySampleCount == 1 && !probeOutstanding);
        const auto sent = wire.size();
        handleReceivedData(reinterpret_cast<const uint8_t*>(&reply.message), sizeof(reply.message)); assert(wire.size() == sent);
        assert(peerState() == PeerState::UNKNOWN && !haveLastPeerEvent && !waitingForAck);
        movementStep(start + 510); assert(probeOutstanding);
        movementStep(start + 511, MotionEvent::Activity); assertProximityReset();
        sampleProximity(reply, start + 512); assertProximityReset();
        settleForCheck(start + 2000);
        reply.receivedAt = start + 2001;
        sampleProximity(reply, start + 2002); assert(proximitySampleCount == 0);
        movementStep(start + 2000 + CHECK_TIMEOUT_MS); assertProximityReset();
    }
    freshApp(); selectedTransport = Transport::CC1101; radioAccepts = false; settleForCheck(2000);
    assert(!probeOutstanding && probeTimerActive && wire.size() == 1);
    for (uint32_t now = 2010; now < 2500; now += 10) movementStep(now);
    assert(wire.size() == 1);
    movementStep(2500); assert(wire.size() == 2 && wire[0].messageId != wire[1].messageId);
    protocolReady = false; loop(); assertProximityReset();

    for (auto state : {LocalState::ACTIVE, LocalState::IDLE, LocalState::SLEEP_NEGOTIATING,
                       LocalState::SLEEPING, LocalState::WAKING})
    {
        freshApp(); selectedTransport = Transport::CC1101;
        if (state == LocalState::SLEEPING || state == LocalState::WAKING)
        {
            completedAwaitingCallbacks(false);
            if (state == LocalState::WAKING) injectActivity(hostNow);
        }
        else if (state != LocalState::ACTIVE) { command('i'); if (state == LocalState::SLEEP_NEGOTIATING) command('s'); }
        assert(localState() == state);
        selectedTransport = Transport::CC1101;
        const auto peer = peerState(); const auto sent = wire.size(); const auto id = nextMessageId;
        const auto pending = pendingMessage; const auto waiting = waitingForAck; const auto retries = retryCount;
        const auto route = pendingTransport;
        auto probe = incoming(Type::ProximityProbe, 0, 432);
        handleReceivedData(reinterpret_cast<const uint8_t*>(&probe), sizeof(probe), Transport::CC1101);
        auto reply = incoming(Type::ProximityProbeReply, 432, 433);
        handleReceivedData(reinterpret_cast<const uint8_t*>(&reply), sizeof(reply), Transport::CC1101);
        assert(wire.size() == sent && nextMessageId == id);
        handleReceivedData(reinterpret_cast<const uint8_t*>(&probe), sizeof(probe));
        const bool allowed = state == LocalState::ACTIVE || state == LocalState::IDLE;
        assert(wire.size() == sent + unsigned(allowed) && nextMessageId == uint16_t(id + unsigned(allowed)));
        if (allowed)
        {
            const auto& response = wire.back();
            assert(response.type == Type::ProximityProbeReply && response.ackForMessageId == 432 &&
                   response.messageId == id && response.sender == LOCAL_DEVICE &&
                   response.event == Protocol::EventType::None && response.version == Protocol::VERSION);
        }
        assert(localState() == state && peerState() == peer && !haveLastPeerEvent);
        assert(waitingForAck == waiting && retryCount == retries && memcmp(&pending, &pendingMessage, 8) == 0);
        assert(selectedTransport == Transport::CC1101 && pendingTransport == route && ccWire.empty());
    }
    puts("PASS: bounded ESP-NOW probes, rejection pacing, rollover, stale/malformed/duplicate correlation, cancellation and awake-only replies");
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

void completeClassifiedMeasurement(int8_t median)
{
    serviceAutomaticTransportSelection(); // Apply any previous completed measurement before starting this one.
    const auto prior = proximityClassification;
    const auto logs = occurrences(Serial.log, "PROXIMITY | median=");
    const auto selected = selectedTransport, pendingRoute = pendingTransport;
    const auto pending = pendingMessage;
    const auto waiting = waitingForAck; const auto retries = retryCount;
    const auto ackStart = ackWaitStart, nextEvent = nextEventTime;
    const auto local = localState(); const auto peer = peerState();
    settleForCheck(hostNow + 2000);
    classificationSample(500, -100);
    assert(proximitySampleCount == 1 && proximityClassification == prior);
    classificationSample(501, median);
    assert(proximitySampleCount == 2 && proximityClassification == prior);
    assert(occurrences(Serial.log, "PROXIMITY | median=") == logs);
    classificationSample(502, -40); // Classification must use the median, not this last raw sample.
    assertProximityReset();
    assert(automaticSelectionPending); // Application selection is deliberately deferred until the next loop.
    assert(occurrences(Serial.log, "PROXIMITY | median=") == logs + 1);
    const std::string label = proximityClassification == ProximityClassification::FAR ? "FAR" : "CLOSE";
    assert(Serial.log.find("PROXIMITY | median=" + std::to_string(median) + " dBm | " + label) != std::string::npos);
    assert(selectedTransport == selected && pendingTransport == pendingRoute);
    assert(memcmp(&pendingMessage, &pending, sizeof(pending)) == 0 && waitingForAck == waiting && retryCount == retries);
    const auto expectedNext = prior == ProximityClassification::UNKNOWN && !waiting ?
        hostNow - 10 + automaticHeartbeatIntervalMs() : nextEvent;
    assert(ackWaitStart == ackStart && nextEventTime == expectedNext && localState() == local && peerState() == peer);
}

void testProximityClassification()
{
    using Class = ProximityClassification;
    struct Case { Class prior; int8_t median; Class expected; };
    const Case cases[] = {
        {Class::UNKNOWN, -79, Class::CLOSE}, {Class::UNKNOWN, -80, Class::FAR},
        {Class::CLOSE, -79, Class::CLOSE}, {Class::CLOSE, -80, Class::FAR}, {Class::CLOSE, -81, Class::FAR},
        {Class::FAR, -76, Class::FAR}, {Class::FAR, -75, Class::CLOSE}, {Class::FAR, -74, Class::CLOSE},
        {Class::CLOSE, -78, Class::CLOSE}, {Class::CLOSE, -77, Class::CLOSE}, {Class::CLOSE, -76, Class::CLOSE},
        {Class::FAR, -79, Class::FAR}, {Class::FAR, -78, Class::FAR}, {Class::FAR, -77, Class::FAR},
        {Class::CLOSE, -67, Class::CLOSE}, {Class::CLOSE, -68, Class::CLOSE}
    };
    for (auto transport : {Transport::ESP_NOW, Transport::CC1101})
    {
        for (const auto& test : cases)
        {
            freshApp(); selectedTransport = transport;
            assert(proximityClassification == Class::UNKNOWN);
            // Raw observations outside CHECKING do not invent an initial distance.
            rssiObservations.push_back(proximityObservation(99, -100, hostNow));
            loop(); assert(proximityClassification == Class::UNKNOWN && !automaticSelectionPending);
            if (test.prior != Class::UNKNOWN)
                completeClassifiedMeasurement(test.prior == Class::FAR ? -85 : -50);
            assert(proximityClassification == test.prior);
            selectApplicationTransport(transport); // Exercise each sampling mode independently of prior policy.
            completeClassifiedMeasurement(test.median);
            assert(proximityClassification == test.expected);
        }
        for (auto prior : {Class::UNKNOWN, Class::CLOSE, Class::FAR})
        for (unsigned samples = 0; samples < 3; ++samples)
        for (bool timeout : {false, true})
        {
            freshApp(); selectedTransport = transport;
            if (prior != Class::UNKNOWN) completeClassifiedMeasurement(prior == Class::FAR ? -85 : -50);
            selectApplicationTransport(transport); // Consume prior policy; failures must not request new policy.
            const auto logs = occurrences(Serial.log, "PROXIMITY | median=");
            if (timeout && samples == 0) radioAccepts = false; // Failed probes also retain the prior state.
            settleForCheck(hostNow + 2000);
            const uint32_t began = checkStartedAt;
            for (unsigned i = 0; i < samples; ++i)
            {
                classificationSample(600 + i, prior == Class::FAR ? -40 : -100);
                assert(proximitySampleCount == i + 1 && proximityClassification == prior);
                assert(!automaticSelectionPending);
            }
            if (timeout) movementStep(began + CHECK_TIMEOUT_MS);
            else movementStep(hostNow + 10, MotionEvent::Activity);
            assertProximityReset();
            assert(proximityClassification == prior && occurrences(Serial.log, "PROXIMITY | median=") == logs);
            assert(!automaticSelectionPending && selectedTransport == transport);
        }
    }
    // A reboot starts UNKNOWN even when RTC history is restored.
    freshApp(); completeClassifiedMeasurement(-85); saveRtcHistory();
    protocolReady = false; injectedBoot.deep = true; setup();
    assert(rtcRestored && proximityClassification == Class::UNKNOWN);
    assert(selectedTransport == Transport::ESP_NOW && !automaticSelectionPending);
    puts("PASS: UNKNOWN initialization, conservative CLOSE/FAR boundaries, full hysteresis band and median-only updates on both transports");
    puts("PASS: raw/partial/failed/cancelled/timeout checks retain classification, transport/reliability/power isolation, RAM-only reboot reset");
}

// Complete real accepted samples within one application ACK interval. Movement
// settling is covered separately; keeping this short permits live retry tests.
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

void testAutomaticHeartbeatCadence()
{
    static_assert(FIRST_EVENT_DELAY_MS == 1000, "startup heartbeat delay changed");
    using Class = ProximityClassification;
    struct Case { Class classification; uint32_t interval; };
    const Case cases[]{{Class::CLOSE, 2500}, {Class::FAR, 4000}};
    for (const auto& test : cases)
    for (auto transport : {Transport::ESP_NOW, Transport::CC1101})
    for (bool acknowledged : {false, true})
    {
        freshApp(); notePeerSeen(); selectedTransport = transport;
        proximityClassification = test.classification;
        pauseAutomaticHeartbeats = false; nextEventTime = FIRST_EVENT_DELAY_MS;
        hostNow = 999; loop();
        assert(!waitingForAck && wire.empty() && ccWire.empty() && led.requests == 0);
        hostNow = 1000; loop();
        const auto original = pendingMessage;
        assert(waitingForAck && pendingTransport == transport && ackWaitStart == 1000 && led.requests == 1);
        const uint32_t completedAt = acknowledged ? 1100 : 1900;
        if (acknowledged)
        {
            hostNow = completedAt; receiveVia(incoming(Type::Ack, original.messageId), transport);
        }
        else
        {
            for (unsigned retry = 1; retry <= 2; ++retry)
            {
                const uint32_t retryAt = 1000 + retry * 300;
                hostNow = retryAt - 1; loop();
                assert(waitingForAck && retryCount == retry - 1);
                hostNow = retryAt; loop();
                assert(waitingForAck && retryCount == retry && ackWaitStart == retryAt);
                assert(memcmp(&pendingMessage, &original, sizeof(original)) == 0 && led.requests == 1);
            }
            hostNow = completedAt; loop();
            assert(selectedTransport == Transport::CC1101); // Includes real ESP-NOW failure fallback.
        }
        assert(!waitingForAck && retryCount == 0 && led.requests == 1);
        assert(proximityClassification == test.classification && nextEventTime == completedAt + test.interval);
        assert(Serial.log.find("PROXIMITY CHECK | START") == std::string::npos);
        const auto due = nextEventTime;
        hostNow = due - 1; loop();
        assert(!waitingForAck && led.requests == 1 && nextEventTime == due);
        assert(memcmp(&pendingMessage, &original, sizeof(original)) == 0);
        hostNow = due; loop();
        assert(waitingForAck && ackWaitStart == due && led.requests == 2);
        assert(pendingMessage.type == Type::Event && pendingMessage.messageId != original.messageId);
        assert(pendingTransport == selectedTransport);
    }
    puts("PASS: known automatic heartbeat CLOSE=2500ms, FAR=4000ms on either radio after ACK/exhaustion; exact next-EVENT boundary, first delay=1000ms, CLOSE fallback keeps 2500ms, no cadence-driven probes");
}

void testAutomaticSelection()
{
    for (auto initial : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshApp(); command(initial == Transport::ESP_NOW ? 'e' : 'c');
        for (unsigned i = 0; i < 20; ++i) loop();
        assert(proximityClassification == ProximityClassification::UNKNOWN && selectedTransport == initial);
        assert(!automaticSelectionPending && Serial.log.find("APP TRANSPORT AUTO") == std::string::npos);
        automaticSelectionPending = true; // Defensive UNKNOWN service path.
        serviceAutomaticTransportSelection();
        assert(!automaticSelectionPending && selectedTransport == initial);

        const auto target = initial == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW;
        completePolicyMeasurement(target == Transport::CC1101 ? -85 : -50);
        const auto pending = pendingMessage; const auto pendingRoute = pendingTransport;
        const auto ackStart = ackWaitStart; const auto id = nextMessageId;
        // IDLE is also a safe application state, without starting negotiation.
        if (initial == Transport::CC1101) PowerManager::forceIdle(hostNow);
        loop();
        assert(selectedTransport == target && !automaticSelectionPending);
        assert(pendingTransport == pendingRoute && memcmp(&pendingMessage, &pending, sizeof(pending)) == 0);
        assert(!waitingForAck && retryCount == 0 && ackWaitStart == ackStart && nextMessageId == id);
        const std::string expected = target == Transport::CC1101 ?
            "APP TRANSPORT AUTO | selected=CC1101 | proximity=FAR" :
            "APP TRANSPORT AUTO | selected=ESP-NOW | proximity=CLOSE";
        assert(occurrences(Serial.log, expected) == 1);
        for (unsigned i = 0; i < 20; ++i) loop();
        assert(occurrences(Serial.log, "APP TRANSPORT AUTO") == 1);
        pauseAutomaticHeartbeats = false; nextEventTime = hostNow;
        loop(); // Service runs before a due automatic EVENT, which snapshots the applied selection.
        assert(waitingForAck && pendingTransport == target);
        const auto& sent = target == Transport::ESP_NOW ? wire : ccWire;
        assert(sent.back().type == Type::Event && sent.back().messageId == pendingMessage.messageId);
    }
    // Repeated valid measurements matching the applied route clear silently.
    freshApp(); completePolicyMeasurement(-50); loop();
    completePolicyMeasurement(-50); loop();
    assert(!automaticSelectionPending && Serial.log.find("APP TRANSPORT AUTO") == std::string::npos);

    // The newer completed classification replaces the older intent while busy.
    for (auto initial : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshApp(); selectedTransport = initial; mockedTxInFlight = 1;
        completePolicyMeasurement(initial == Transport::ESP_NOW ? -85 : -50);
        completePolicyMeasurement(initial == Transport::ESP_NOW ? -50 : -85);
        mockedTxInFlight = 0; loop();
        assert(selectedTransport == initial && !automaticSelectionPending);
        assert(Serial.log.find("APP TRANSPORT AUTO") == std::string::npos);
    }
    puts("PASS: UNKNOWN no-op, CLOSE/FAR future EVENT routes, one-loop application, latest completed classification and silent matching policy");
}

void testAutomaticSelectionRetries()
{
    for (auto initial : {Transport::ESP_NOW, Transport::CC1101})
    for (bool acknowledged : {false, true})
    for (bool classified : {false, true})
    {
        freshApp(); selectedTransport = initial;
        if (classified)
            proximityClassification = initial == Transport::ESP_NOW ?
                ProximityClassification::CLOSE : ProximityClassification::FAR;
        startHeartbeatEvent();
        const auto original = pendingMessage; const auto began = ackWaitStart;
        const auto scheduled = nextEventTime;
        const auto target = initial == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW;
        completePolicyMeasurement(target == Transport::CC1101 ? -85 : -50);
        assert(proximityClassification == (target == Transport::CC1101 ?
            ProximityClassification::FAR : ProximityClassification::CLOSE));
        assert(nextEventTime == scheduled && led.requests == unsigned(classified));
        assert(waitingForAck && pendingTransport == initial && retryCount == 0 && ackWaitStart == began);
        assert(memcmp(&pendingMessage, &original, sizeof(original)) == 0);
        receiveVia(incoming(Type::Ack, original.messageId), target); // Wrong radio cannot finish the EVENT.
        assert(waitingForAck && automaticSelectionPending && selectedTransport == initial);
        hostNow = began + ACK_TIMEOUT_MS - 1; loop(); assert(retryCount == 0 && ackWaitStart == began);
        for (unsigned retry = 1; retry <= MAX_RETRIES; ++retry)
        {
            const auto due = ackWaitStart + ACK_TIMEOUT_MS;
            hostNow = due; loop();
            assert(retryCount == retry && ackWaitStart == due && pendingTransport == initial);
            assert(selectedTransport == initial && automaticSelectionPending);
            assert(memcmp(&pendingMessage, &original, sizeof(original)) == 0);
            assert(nextEventTime == scheduled && led.requests == unsigned(classified));
        }
        const auto& sent = initial == Transport::ESP_NOW ? wire : ccWire;
        unsigned events = 0;
        for (const auto& packet : sent)
            if (packet.type == Type::Event) { ++events; assert(memcmp(&packet, &original, sizeof(original)) == 0); }
        assert(events == 3);
        const auto& other = initial == Transport::ESP_NOW ? ccWire : wire;
        for (const auto& packet : other) assert(packet.type != Type::Event);

        mockedTxInFlight = 1; // Semantic completion alone is not the drained switch boundary.
        const auto completedAt = acknowledged ? hostNow : ackWaitStart + ACK_TIMEOUT_MS;
        if (acknowledged) receiveVia(incoming(Type::Ack, original.messageId), initial);
        else { hostNow = ackWaitStart + ACK_TIMEOUT_MS; loop(); }
        assert(nextEventTime == completedAt + (target == Transport::CC1101 ? 4000U : 2500U));
        assert(led.requests == unsigned(classified));
        const bool fallback = !acknowledged && initial == Transport::ESP_NOW;
        assert(!waitingForAck && retryCount == 0 && automaticSelectionPending == !fallback && selectedTransport == initial);
        assert(espNowFallbackPending == fallback);
        assert(pendingTransport == initial && memcmp(&pendingMessage, &original, sizeof(original)) == 0);
        assert(peerState() == (acknowledged ? PeerState::ONLINE : PeerState::OFFLINE));
        mockedTxInFlight = 0; pauseAutomaticHeartbeats = false; nextEventTime = hostNow;
        loop();
        assert(selectedTransport == target && pendingTransport == target && waitingForAck);
        assert(pendingMessage.messageId != original.messageId && !automaticSelectionPending);
        assert(occurrences(Serial.log, "APP TRANSPORT AUTO") == (fallback ? 0U : 1U));
        assert(occurrences(Serial.log, "APP TRANSPORT FALLBACK") == (fallback ? 1U : 0U));
    }
    // Failures never manufacture FAR/classifier requests; only ESP-NOW exhaustion falls back.
    for (auto initial : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshApp(); selectedTransport = initial; radioAccepts = ccAccepts = false;
        startHeartbeatEvent();
        for (unsigned i = 0; i <= MAX_RETRIES; ++i) { hostNow = ackWaitStart + ACK_TIMEOUT_MS; loop(); }
        assert(peerState() == PeerState::OFFLINE && !waitingForAck && !automaticSelectionPending);
        assert(proximityClassification == ProximityClassification::UNKNOWN && selectedTransport == Transport::CC1101);
        assert(!espNowFallbackPending);
        assert(occurrences(Serial.log, "APP TRANSPORT FALLBACK") == (initial == Transport::ESP_NOW ? 1U : 0U));
        assert(Serial.log.find("APP TRANSPORT AUTO") == std::string::npos);
    }
    puts("PASS: both pending EVENT routes and exact retries/ACK deadlines survive policy changes; ACK/drain or exhaustion/drain precedes selection");
    puts("PASS: FAR -> CLOSE and CLOSE -> FAR use 2500/4000ms at the next ACK/exhaustion without rescheduling the in-flight EVENT or replaying its LED");
}

void testAutomaticSelectionGuards()
{
    for (bool fallback : {false, true})
    for (unsigned guard = 0; guard < 11; ++guard)
    {
        freshApp();
        if (fallback)
        {
            requestFallbackFromEvent();
            // Real peer evidence restores check eligibility; pending TX still holds fallback.
            receive(proximityObservation(900, -50, hostNow).message);
            mockedTxInFlight = 0;
        }
        else completePolicyMeasurement(-85);
        const auto savedQueue = receiveQueue;
        const auto packet = incoming(Type::Ack, 999);
        switch (guard)
        {
            case 0: protocolReady = false; break;
            case 1: startHeartbeatEvent(); break;
            case 2: controlCount = 1; break;
            case 3: PowerManager::forceIdle(hostNow); PowerManager::requestSleep(nextMessageId++, hostNow); break;
            case 4: sleepDrainWaiting = true; break;
            case 5: awakeAckBusy = true; break;
            case 6: mockedTxInFlight = 1; break;
            case 7: mockedRxActive = true; break;
            case 8: receiveQueue = nullptr; break;
            case 9: queueReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)); break;
            case 10: startProximityCheck(hostNow); break;
        }
        const auto log = Serial.log;
        for (unsigned i = 0; i < 100; ++i) serviceAutomaticTransportSelection();
        assert(automaticSelectionPending == !fallback && espNowFallbackPending == fallback);
        assert(selectedTransport == Transport::ESP_NOW && Serial.log == log);
        switch (guard)
        {
            case 0: protocolReady = true; break;
            case 1: handleAck(incoming(Type::Ack, pendingMessage.messageId), Transport::ESP_NOW); break;
            case 2: controlCount = 0; break;
            case 3: PowerManager::injectActivity(hostNow); discardObsoleteControls(); break;
            case 4: sleepDrainWaiting = false; break;
            case 5: awakeAckBusy = false; break;
            case 6: mockedTxInFlight = 0; break;
            case 7: mockedRxActive = false; break;
            case 8: receiveQueue = savedQueue; break;
            case 9: { Protocol::Message out{}; assert(xQueueReceive(receiveQueue, &out, 0) == pdPASS); break; }
            case 10: cancelProximityCheck("TEST"); break; // Keeps the earlier valid request; creates no new one.
        }
        serviceAutomaticTransportSelection();
        assert(selectedTransport == Transport::CC1101 && !automaticSelectionPending && !espNowFallbackPending);
        for (unsigned i = 0; i < 100; ++i) serviceAutomaticTransportSelection();
        assert(occurrences(Serial.log, fallback ? "APP TRANSPORT FALLBACK" : "APP TRANSPORT AUTO") == 1);
        assert(Serial.log.find("APP TRANSPORT | REFUSED") == std::string::npos);
    }
    puts("PASS: classifier and fallback share every awake/drained/CHECKING guard, wait silently and apply once");
}

void testAutomaticSelectionManual()
{
    for (auto initial : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshApp(); selectedTransport = initial;
        const int8_t median = initial == Transport::ESP_NOW ? -50 : -85;
        completePolicyMeasurement(median);
        const auto manual = initial == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW;
        command(manual == Transport::CC1101 ? 'c' : 'e');
        assert(selectedTransport == manual && !automaticSelectionPending);
        for (unsigned i = 0; i < 20; ++i) loop();
        assert(selectedTransport == manual && Serial.log.find("APP TRANSPORT AUTO") == std::string::npos);
        completePolicyMeasurement(median); // Same label, but fresh evidence overrides the successful manual choice.
        loop(); assert(selectedTransport == initial && !automaticSelectionPending);
        assert(occurrences(Serial.log, "APP TRANSPORT AUTO") == 1);

        freshApp(); selectedTransport = initial;
        completePolicyMeasurement(initial == Transport::ESP_NOW ? -85 : -50);
        startHeartbeatEvent(); const auto event = pendingMessage;
        command(manual == Transport::CC1101 ? 'c' : 'e');
        assert(selectedTransport == initial && automaticSelectionPending && waitingForAck);
        assert(Serial.log.find("APP TRANSPORT | REFUSED") != std::string::npos);
        receiveVia(incoming(Type::Ack, event.messageId), initial);
        assert(selectedTransport == manual && !automaticSelectionPending);
    }
    puts("PASS: successful e/c consumes pending policy until the next completed median; refused e/c preserves it");
}

void testAutomaticSelectionSleep()
{
    // CLOSE/FAR requests from either radio, plus an exhausted ESP-NOW EVENT's fallback.
    for (unsigned request = 0; request < 3; ++request)
    for (bool wakeInstead : {false, true})
    {
        const bool fallback = request == 2;
        const auto initial = request == 1 ? Transport::CC1101 : Transport::ESP_NOW;
        freshApp(); selectedTransport = initial;
        if (fallback) requestFallbackFromEvent();
        else completePolicyMeasurement(initial == Transport::ESP_NOW ? -85 : -50);
        mockedTxInFlight = 1; // Hold the previous TX while requesting sleep.
        command('i'); command('s');
        const auto id = transaction().sleepId;
        const auto phaseDeadline = transaction().phaseDeadline, hardDeadline = transaction().hardDeadline;
        mockedTxInFlight = 0;
        for (unsigned i = 0; i < 10; ++i) loop();
        assert(transaction().phaseDeadline == phaseDeadline && transaction().hardDeadline == hardDeadline);
        assert(localState() == LocalState::SLEEP_NEGOTIATING && selectedTransport == initial);
        assert(automaticSelectionPending == !fallback && espNowFallbackPending == fallback);
        assert(pendingMessage.type == Type::SleepRequest && pendingTransport == Transport::ESP_NOW);
        receive(incoming(Type::Ack, pendingMessage.messageId));
        receive(incoming(Type::SleepReady, id));
        assert(pendingMessage.type == Type::SleepCommit && pendingTransport == Transport::ESP_NOW);
        receive(incoming(Type::Ack, pendingMessage.messageId));
        mockedTxInFlight = 1;
        receive(incoming(Type::SleepAck, id));
        assert(localState() == LocalState::SLEEPING && physicalSleeps == 0);
        assert(automaticSelectionPending == !fallback && espNowFallbackPending == fallback);
        assert(selectedTransport == initial && ccWire.empty());
        assert(Serial.log.find("APP TRANSPORT AUTO") == std::string::npos);
        assert(Serial.log.find("APP TRANSPORT FALLBACK") == std::string::npos);
        mockedTxInFlight = 0;
        if (wakeInstead)
        {
            PowerManager::injectActivity(hostNow);
            loop(); assert(localState() == LocalState::WAKING && selectedTransport == initial);
            assert(automaticSelectionPending == !fallback && espNowFallbackPending == fallback);
            hostNow += 250; loop();
            assert(localState() == LocalState::ACTIVE && selectedTransport != initial && !automaticSelectionPending);
            assert(!espNowFallbackPending);
        }
        else
        {
            assert(sleepTransportBlockedReason() == nullptr); // Pending policy is not a sleep blocker.
            loop(); assert(physicalSleeps == 1 && selectedTransport == initial);
            assert(automaticSelectionPending == !fallback && espNowFallbackPending == fallback);
            protocolReady = false; injectedBoot.deep = true; setup();
            assert(rtcRestored && proximityClassification == ProximityClassification::UNKNOWN);
            assert(selectedTransport == Transport::ESP_NOW && !automaticSelectionPending && !espNowFallbackPending);
        }
    }
    puts("PASS: classifier/fallback wait through negotiation/SLEEPING/WAKING without changing sleep routes/deadlines or blocking physical entry; reboot clears both");
}

void testEspNowFallback()
{
    using Class = ProximityClassification;
    for (auto classification : {Class::UNKNOWN, Class::CLOSE, Class::FAR})
    for (bool accepted : {false, true})
    {
        freshApp();
        if (classification != Class::UNKNOWN)
        {
            completePolicyMeasurement(classification == Class::CLOSE ? -50 : -85);
            command('e'); // Includes the FAR-but-manually-ESP-NOW case.
        }
        radioAccepts = accepted;
        const auto failed = requestFallbackFromEvent();
        const auto nextEvent = nextEventTime, ackStart = ackWaitStart;
        for (unsigned i = 0; i < 100; ++i) loop();
        assert(espNowFallbackPending && selectedTransport == Transport::ESP_NOW);
        assert(Serial.log.find("APP TRANSPORT FALLBACK") == std::string::npos);
        assert(Serial.log.find("APP TRANSPORT | REFUSED") == std::string::npos);
        mockedTxInFlight = 0; loop();
        assert(selectedTransport == Transport::CC1101 && !espNowFallbackPending && !automaticSelectionPending);
        assert(peerState() == PeerState::OFFLINE && proximityClassification == classification);
        assert(!waitingForAck && retryCount == 0 && pendingTransport == Transport::ESP_NOW);
        assert(memcmp(&pendingMessage, &failed, sizeof(failed)) == 0 && ackWaitStart == ackStart && nextEventTime == nextEvent);
        assert(ccWire.empty()); // Applying fallback did not replay the failed EVENT.
        for (unsigned i = 0; i < 20; ++i) loop();
        assert(occurrences(Serial.log, "APP TRANSPORT FALLBACK | selected=CC1101 | reason=ESP_NOW_EVENT_GIVE_UP") == 1);
        startHeartbeatEvent();
        assert(pendingTransport == Transport::CC1101 && pendingMessage.messageId != failed.messageId);
        for (unsigned i = 0; i <= MAX_RETRIES; ++i) { hostNow = ackWaitStart + ACK_TIMEOUT_MS; loop(); }
        assert(!waitingForAck && !espNowFallbackPending && selectedTransport == Transport::CC1101);
        assert(proximityClassification == classification && peerState() == PeerState::OFFLINE);
        assert(occurrences(Serial.log, "APP TRANSPORT FALLBACK") == 1); // No failure ping-pong.
    }
    // A reliable sleep control also uses ESP-NOW, but its exhaustion must not request fallback.
    freshApp(); command('i'); command('s');
    for (unsigned i = 0; i <= MAX_RETRIES; ++i) { hostNow = ackWaitStart + ACK_TIMEOUT_MS; loop(); }
    assert(!espNowFallbackPending && selectedTransport == Transport::ESP_NOW);
    assert(Serial.log.find("APP TRANSPORT FALLBACK") == std::string::npos);
    puts("PASS: UNKNOWN/CLOSE/FAR preserved across ESP-NOW GIVE_UP, identical three attempts, callback drain, one fallback, new CC1101 EVENT only; no CC1101/sleep-control fallback");
}

void testFallbackPrecedenceAndManual()
{
    for (int8_t median : {int8_t(-50), int8_t(-85)})
    {
        freshApp(); requestFallbackFromEvent();
        // OFFLINE otherwise prevents measurements. An actual incoming EVENT restores ONLINE.
        receiveVia(proximityObservation(901, -50, hostNow).message, Transport::CC1101);
        assert(peerState() == PeerState::ONLINE && espNowFallbackPending);
        completePolicyMeasurement(median);
        assert(!espNowFallbackPending && automaticSelectionPending);
        mockedTxInFlight = 0; loop();
        assert(selectedTransport == (median == -50 ? Transport::ESP_NOW : Transport::CC1101));
        assert(!automaticSelectionPending && Serial.log.find("APP TRANSPORT FALLBACK") == std::string::npos);
    }
    freshApp(); completePolicyMeasurement(-85); assert(automaticSelectionPending);
    requestFallbackFromEvent();
    assert(!automaticSelectionPending && espNowFallbackPending && proximityClassification == ProximityClassification::FAR);
    mockedTxInFlight = 0; loop();
    assert(selectedTransport == Transport::CC1101 && occurrences(Serial.log, "APP TRANSPORT FALLBACK") == 1);
    assert(Serial.log.find("APP TRANSPORT AUTO") == std::string::npos);

    for (bool alreadyCc1101 : {false, true})
    {
        freshApp(); completePolicyMeasurement(-50); command('e'); requestFallbackFromEvent();
        automaticSelectionPending = true; // Defensive conflicting flags: fallback wins, without later bouncing to CLOSE's ESP-NOW.
        if (alreadyCc1101) selectedTransport = Transport::CC1101;
        serviceAutomaticTransportSelection(); assert(espNowFallbackPending && automaticSelectionPending);
        mockedTxInFlight = 0; loop(); loop();
        assert(selectedTransport == Transport::CC1101 && !espNowFallbackPending && !automaticSelectionPending);
        assert(occurrences(Serial.log, "APP TRANSPORT FALLBACK") == (alreadyCc1101 ? 0U : 1U));
        assert(Serial.log.find("APP TRANSPORT AUTO") == std::string::npos);
    }
    for (char manual : {'e', 'c'})
    {
        freshApp(); requestFallbackFromEvent(); automaticSelectionPending = true;
        command(manual); // TX still outstanding: refusal must preserve both flags.
        assert(espNowFallbackPending && automaticSelectionPending && selectedTransport == Transport::ESP_NOW);
        assert(Serial.log.find("APP TRANSPORT | REFUSED") != std::string::npos);
        mockedTxInFlight = 0; command(manual);
        assert(!espNowFallbackPending && !automaticSelectionPending);
        assert(selectedTransport == (manual == 'e' ? Transport::ESP_NOW : Transport::CC1101));
        assert(Serial.log.find("APP TRANSPORT FALLBACK") == std::string::npos);
    }
    puts("PASS: fresh valid median supersedes fallback, newer ESP-NOW exhaustion supersedes classifier intent, defensive fallback priority and manual success/refusal semantics");
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

void testFallbackRecovery()
{
    freshKnownApp(); const auto failed = requestFallbackFromEvent();
    mockedTxInFlight = 0; loop();
    assert(selectedTransport == Transport::CC1101 && peerState() == PeerState::OFFLINE);
    assert(proximityClassification == ProximityClassification::CLOSE);
    assert(displayFrames.back().radio == "CC1101" && displayFrames.back().peer == "OFFLINE");
    assert(displayFrames.back().distance == "CLOSE" && pendingTransport == Transport::ESP_NOW);
    pauseAutomaticHeartbeats = false; hostNow = nextEventTime; loop();
    const auto recovery = pendingMessage;
    assert(waitingForAck && pendingTransport == Transport::CC1101 && recovery.messageId != failed.messageId);
    assert(ccWire.size() == 1 && memcmp(&ccWire.back(), &recovery, sizeof(recovery)) == 0);
    receiveVia(incoming(Type::Ack, recovery.messageId), Transport::CC1101);
    assert(!waitingForAck && peerState() == PeerState::ONLINE);
    assert(proximityClassification == ProximityClassification::CLOSE);
    assert(proximityUpdateState == ProximityUpdateState::CHECKING && countWire(Type::ProximityProbe) == 1);
    // The outstanding probe defers OLED updates until a normal safe boundary.
    assert(displayFrames.back().radio == "CC1101" && displayFrames.back().peer == "OFFLINE");
    pauseAutomaticHeartbeats = true;
    completePeerReturnMeasurement(-50); // The matched CC1101 ACK starts this check without movement.
    assert(countWire(Type::ProximityProbe) == 3 && proximityClassification == ProximityClassification::CLOSE);
    assert(selectedTransport == Transport::ESP_NOW && !automaticSelectionPending && !espNowFallbackPending);
    assert(displayFrames.back().radio == "ESP-NOW" && displayFrames.back().distance == "CLOSE");
    startHeartbeatEvent(); assert(pendingTransport == Transport::ESP_NOW);
    receive(incoming(Type::Ack, pendingMessage.messageId)); assert(!waitingForAck && peerState() == PeerState::ONLINE);
    assert(occurrences(Serial.log, "APP TRANSPORT FALLBACK") == 1 && occurrences(Serial.log, "APP TRANSPORT AUTO") == 1);
    puts("PASS: ESP-NOW exhaustion -> CC1101 fallback -> matched CC1101 ACK starts recovery check -> CLOSE -> ESP-NOW EVENT/ACK and OLED recovery, without movement");
}

void testPeerReturnEvents()
{
    for (auto evidenceRadio : {Transport::ESP_NOW, Transport::CC1101})
    for (int8_t median : {int8_t(-50), int8_t(-85)})
    {
        freshApp(); requestFallbackFromEvent(); mockedTxInFlight = 0; loop();
        assert(peerState() == PeerState::OFFLINE && selectedTransport == Transport::CC1101);
        assert(displayFrames.back().peer == "OFFLINE" && displayFrames.back().distance == "UNKNOWN");
        const auto event = proximityObservation(1, -47, hostNow).message;
        receiveVia(event, evidenceRadio); // Real EVENT path, including applicationEvent()'s early ONLINE update.
        assert(peerState() == PeerState::ONLINE && proximityUpdateState == ProximityUpdateState::CHECKING);
        assert(selectedTransport == Transport::CC1101 && proximityClassification == ProximityClassification::UNKNOWN);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1 && countWire(Type::ProximityProbe) == 1);
        // A strong RSSI on the returning EVENT cannot stand in for a correlated probe median.
        rssiObservations.push_back(proximityObservation(1, -47, hostNow)); loop();
        assert(proximitySampleCount == 0 && proximityClassification == ProximityClassification::UNKNOWN);
        completePeerReturnMeasurement(median);
        assert(countWire(Type::ProximityProbe) == 3 && peerState() == PeerState::ONLINE);
        assert(displayFrames.back().peer == "ONLINE");
        assert(displayFrames.back().distance == (median <= ENTER_FAR_DBM ? "FAR" : "CLOSE"));
        assert(displayFrames.back().radio == (median <= ENTER_FAR_DBM ? "CC1101" : "ESP-NOW"));
        startHeartbeatEvent(); assert(pendingTransport == selectedTransport);
        receiveVia(incoming(Type::Ack, pendingMessage.messageId), pendingTransport);
        assert(!waitingForAck && occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
    }
    // Transition detection changes check state only; the receive handler sends no probe itself.
    freshApp(); selectedTransport = Transport::CC1101; notePeerUnreachable();
    const auto event = proximityObservation(20, -47, hostNow).message;
    handleReceivedData(reinterpret_cast<const uint8_t*>(&event), sizeof(event));
    assert(proximityUpdateState == ProximityUpdateState::CHECKING && countWire(Type::ProximityProbe) == 0);
    loop(); assert(countWire(Type::ProximityProbe) == 1);
    puts("PASS: peer return via ESP-NOW/CC1101 EVENT starts one loop-owned probe check; flash scenario, CLOSE/FAR policy, OLED sequence and future EVENT routes without movement");
}

void testPeerReturnIsolation()
{
    freshApp(); selectedTransport = Transport::CC1101; notePeerUnreachable(); startHeartbeatEvent();
    const auto id = pendingMessage.messageId;
    receiveVia(incoming(Type::Ack, id), Transport::ESP_NOW); // Right ID, wrong radio.
    receiveVia(incoming(Type::Ack, id + 1), Transport::CC1101);
    auto malformedAck = incoming(Type::Ack, id); malformedAck.event = Protocol::EventType::Heartbeat;
    receiveVia(malformedAck, Transport::CC1101);
    assert(waitingForAck && peerState() == PeerState::OFFLINE && proximityUpdateState == ProximityUpdateState::READY);
    receiveVia(incoming(Type::Ack, id), Transport::CC1101);
    assert(!waitingForAck && peerState() == PeerState::ONLINE && proximityUpdateState == ProximityUpdateState::CHECKING);
    classificationSample(860, -100);
    const auto began = checkStartedAt;
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        const auto event = proximityObservation(21, -47, hostNow).message;
        receiveVia(event, radio); receiveVia(event, radio); // New/duplicate real evidence while already ONLINE.
        startHeartbeatEvent(); receiveVia(incoming(Type::Ack, pendingMessage.messageId), Transport::CC1101);
        assert(checkStartedAt == began && proximitySampleCount == 1);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
    }
    classificationSample(861, -85); classificationSample(862, -40); loop();
    receiveVia(proximityObservation(22, -47, hostNow).message, Transport::CC1101);
    assert(selectedTransport == Transport::CC1101 && proximityUpdateState == ProximityUpdateState::READY);
    assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1); // No recurring monitor after completion.

    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    for (auto type : {Type::ProximityProbe, Type::ProximityProbeReply, Type::SleepRequest,
                      Type::SleepReady, Type::SleepCommit, Type::SleepAck, Type::SleepCancel, Type::Ack})
    {
        freshApp(); selectedTransport = Transport::CC1101; notePeerUnreachable();
        const auto packet = incoming(type, type == Type::ProximityProbe ? 0 : 23, 23);
        receiveVia(packet, radio);
        assert(proximityUpdateState == ProximityUpdateState::READY && proximityClassification == ProximityClassification::UNKNOWN);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 0 && selectedTransport == Transport::CC1101);
    }
    for (unsigned invalid = 0; invalid < 6; ++invalid)
    {
        freshApp(); selectedTransport = Transport::CC1101; notePeerUnreachable();
        auto packet = proximityObservation(23, -47, hostNow).message;
        if (invalid == 0) ++packet.version;
        if (invalid == 1) packet.sender = LOCAL_DEVICE;
        if (invalid == 2) packet.event = Protocol::EventType::None;
        if (invalid == 3) packet.ackForMessageId = 1;
        if (invalid == 4) packet.type = static_cast<Type>(99);
        handleReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet) - (invalid == 5));
        assert(peerState() == PeerState::OFFLINE && proximityUpdateState == ProximityUpdateState::READY);
        assert(countWire(Type::ProximityProbe) == 0 && occurrences(Serial.log, "PROXIMITY CHECK | START") == 0);
    }
    // First contact starts an initial check; OFFLINE recovery still requires CC1101.
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    for (bool offline : {false, true})
    {
        freshApp(); selectedTransport = radio;
        if (offline) notePeerUnreachable();
        startHeartbeatEvent(); receiveVia(incoming(Type::Ack, pendingMessage.messageId), radio);
        assert((proximityUpdateState == ProximityUpdateState::CHECKING) == (!offline || radio == Transport::CC1101));
    }
    freshApp(); selectedTransport = Transport::CC1101; notePeerUnreachable(); command('i');
    receiveVia(proximityObservation(24, -47, hostNow).message, Transport::CC1101);
    assert(peerState() == PeerState::ONLINE && localState() == LocalState::IDLE);
    assert(proximityUpdateState == ProximityUpdateState::READY); // Existing ACTIVE guard remains authoritative.
    freshApp(); selectedTransport = Transport::CC1101; command('i'); command('s');
    receive(incoming(Type::Ack, pendingMessage.messageId)); // Matching sleep-control receipt is not application evidence.
    assert(localState() == LocalState::SLEEP_NEGOTIATING && proximityUpdateState == ProximityUpdateState::READY);
    puts("PASS: no restart for already-ONLINE EVENTs/ACKs; probes, sleep controls/receipts, wrong/unrelated/malformed ACKs and malformed EVENTs cannot trigger; ACTIVE/CC1101 guards retained");
}

void testPeerReturnTimeout()
{
    for (auto prior : {ProximityClassification::UNKNOWN, ProximityClassification::CLOSE, ProximityClassification::FAR})
    for (unsigned samples : {0U, 2U})
    {
        freshApp();
        if (prior != ProximityClassification::UNKNOWN)
            completePolicyMeasurement(prior == ProximityClassification::CLOSE ? -50 : -85);
        command('c'); notePeerUnreachable();
        receiveVia(proximityObservation(30, -47, hostNow).message, Transport::CC1101);
        const auto began = checkStartedAt;
        const auto starts = occurrences(Serial.log, "PROXIMITY CHECK | START");
        for (unsigned i = 0; i < samples; ++i) classificationSample(870 + i, -40);
        radioAccepts = false; // Remaining probe attempts fail, without manufacturing distance.
        for (uint32_t elapsed = PROBE_REPLY_WAIT_MS; elapsed < CHECK_TIMEOUT_MS; elapsed += PROBE_REPLY_WAIT_MS)
        {
            hostNow = began + elapsed; loop();
            assert(checkStartedAt == began && proximitySampleCount == samples);
        }
        hostNow = began + CHECK_TIMEOUT_MS; loop();
        assertProximityReset();
        assert(selectedTransport == Transport::CC1101 && proximityClassification == prior);
        assert(peerState() == PeerState::ONLINE && !automaticSelectionPending && !espNowFallbackPending);
        const auto probes = countWire(Type::ProximityProbe);
        for (unsigned i = 0; i < 100; ++i) loop();
        receiveVia(proximityObservation(31, -47, hostNow).message, Transport::CC1101);
        assert(countWire(Type::ProximityProbe) == probes && occurrences(Serial.log, "PROXIMITY CHECK | START") == starts);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | TIMEOUT") == 1);
    }
    puts("PASS: peer-return probe failure/partial timeout stays bounded to 12s, preserves UNKNOWN/CLOSE/FAR and CC1101, never repeats while ONLINE");
}

void testInitialProximityContact()
{
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    for (bool acknowledged : {false, true})
    {
        freshApp(); selectedTransport = radio; loop();
        assert(peerState() == PeerState::UNKNOWN && proximityClassification == ProximityClassification::UNKNOWN);
        assertProximityReset();
        assert(displayFrames.back().distance == "UNKNOWN");
        auto evidence = proximityObservation(90, -50, hostNow).message;
        if (acknowledged)
        {
            startHeartbeatEvent();
            evidence = incoming(Type::Ack, pendingMessage.messageId);
        }
        hostNow = 10;
        handleReceivedData(reinterpret_cast<const uint8_t*>(&evidence), sizeof(evidence), radio);
        assert(peerState() == PeerState::ONLINE && proximityUpdateState == ProximityUpdateState::CHECKING);
        assert(checkStartedAt == 10 && proximitySampleCount == 0 && countWire(Type::ProximityProbe) == 0);
        loop(); // UNKNOWN obtains evidence through loop-owned ESP-NOW probes on either application route.
        assert(countWire(Type::ProximityProbe) == 1);
        assert(displayFrames.back().distance == "UNKNOWN"); // Existing probe-busy OLED guard still applies.
        for (uint16_t id : {91, 92})
        {
            hostNow += 10;
            const auto event = proximityObservation(id, -50, hostNow).message;
            receiveVia(event, radio); receiveVia(event, radio);
            startHeartbeatEvent(); receiveVia(incoming(Type::Ack, pendingMessage.messageId), radio);
            assert(checkStartedAt == 10 && proximitySampleCount == 0);
            assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        }
        assert(Serial.log.find("MOVEMENT |") == std::string::npos);
    }
    puts("PASS: UNKNOWN peer/UNKNOWN distance first EVENT or matched ACK starts one check on either radio; no receive-handler probes, movement or ONLINE/duplicate restarts");
}

void testInitialProximityCadence()
{
    // Exercise real outgoing probes and queued replies/RSSI; no heartbeat traffic
    // supplies the samples. Both device identities execute this test.
    for (unsigned trigger = 0; trigger < 3; ++trigger) // Movement, peer contact, cold startup.
    for (int8_t rssi : {int8_t(-50), int8_t(-85)})
    {
        freshApp(); pauseAutomaticHeartbeats = false; nextEventTime = FIRST_EVENT_DELAY_MS;
        uint32_t start = 20000; // The old first-EVENT deadline is already stale.
        if (trigger == 2)
        {
            protocolReady = false; delete receiveQueue; receiveQueue = nullptr;
            setup();
            assert(protocolReady && peerState() == PeerState::UNKNOWN);
            assert(proximityClassification == ProximityClassification::UNKNOWN);
            assert(proximityUpdateState == ProximityUpdateState::CHECKING && wire.empty());
            start = checkStartedAt + 2000; // Service after the original 1000ms EVENT deadline.
            assert(nextEventTime < start && led.requests == 0);
        }
        else if (trigger == 1)
        {
            hostNow = start; notePeerSeen();
            startPeerAvailabilityProximityCheck(PeerState::UNKNOWN);
        }
        else settleForCheck(start); // Existing movement trigger also works before peer ONLINE.
        const auto peer = peerState();
        struct Delivery { uint32_t at; Protocol::Message packet; };
        std::vector<Delivery> deliveries;
        size_t sent = 0;
        uint16_t peerId = 100;
        uint16_t peerProbeId = 0;
        std::vector<uint16_t> peerSamples;
        bool completed = false, sawChecking = false;
        uint32_t completedAt = 0;
        for (uint32_t now = start; now < start + CHECK_TIMEOUT_MS; now += 10)
        {
            hostNow = now;
            if (trigger == 2 && peerSamples.size() < 3)
            {
                // The other UNKNOWN endpoint independently probes too. Run our
                // real responder; each endpoint collects RSSI from replies.
                peerProbeId = peerId++;
                const auto probe = incoming(Type::ProximityProbe, 0, peerProbeId);
                queueReceivedData(reinterpret_cast<const uint8_t*>(&probe), sizeof(probe));
            }
            for (auto it = deliveries.begin(); it != deliveries.end();)
            {
                if (it->at != now) { ++it; continue; }
                ESPNowRadio::RssiObservation observation{};
                observation.message = it->packet; observation.rssi = rssi; observation.receivedAt = now;
                rssiObservations.push_back(observation);
                queueReceivedData(reinterpret_cast<const uint8_t*>(&it->packet), sizeof(it->packet));
                it = deliveries.erase(it);
            }
            loop();
            for (; sent < wire.size(); ++sent)
            {
                const auto& packet = wire[sent];
                assert(packet.event == Protocol::EventType::None);
                if (packet.type == Type::ProximityProbe)
                    deliveries.push_back({now + 10, incoming(Type::ProximityProbeReply, packet.messageId, peerId++)});
                else
                {
                    assert(trigger == 2 && packet.type == Type::ProximityProbeReply);
                    assert(packet.ackForMessageId == peerProbeId);
                    for (const auto id : peerSamples) assert(id != packet.messageId);
                    peerSamples.push_back(packet.messageId);
                }
            }
            assert(!waitingForAck && retryCount == 0 && controlCount == 0 && peerState() == peer);
            assert(led.requests == 0 && !led.busy() && hostPixel().shown == 0 && !haveLastPeerEvent);
            if (!displayFrames.empty() && displayFrames.back().distance == "CHECKING") sawChecking = true;
            if (proximityClassification != ProximityClassification::UNKNOWN)
            {
                completedAt = now;
                assert(uint32_t(now - start) < CHECK_TIMEOUT_MS && sawChecking);
                assertProximityReset();
                assert(occurrences(Serial.log, "PROXIMITY CHECK | SAMPLE") == 3);
                assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
                assert(countWire(Type::ProximityProbe) == 3 && countWire(Type::Event) == 0 && countWire(Type::Ack) == 0);
                completed = true;
                break;
            }
        }
        assert(completed);
        if (trigger == 2)
        {
            assert(peerSamples.size() == 3 && countWire(Type::ProximityProbeReply) == 3);
            assert(peerState() == PeerState::UNKNOWN && Serial.log.find("MOVEMENT |") == std::string::npos);
        }
        const bool far = rssi <= ENTER_FAR_DBM;
        const uint32_t interval = far ? 4000 : 2500;
        assert(proximityClassification == (far ? ProximityClassification::FAR : ProximityClassification::CLOSE));
        assert(nextEventTime == completedAt + interval);
        hostNow = nextEventTime - 1; loop();
        assert(!waitingForAck && led.requests == 0);
        assert(selectedTransport == (far ? Transport::CC1101 : Transport::ESP_NOW));
        assert(displayFrames.back().distance == (far ? "FAR" : "CLOSE"));
        assert(displayFrames.back().radio == (far ? "CC1101" : "ESP-NOW"));
        hostNow = nextEventTime; loop();
        assert(waitingForAck && pendingMessage.type == Type::Event && led.requests == 1);
        assert(ackWaitStart == completedAt + interval && pendingTransport == selectedTransport);
        assert(countWire(Type::ProximityProbe) == 3); // Completion does not start background probing.
    }
    puts("PASS: UNKNOWN obtains three fresh correlated probe/reply RSSI samples within 12s without heartbeat EVENTs, LED, ACK transactions or peer mutation; first CLOSE/FAR EVENT waits 2500/4000ms after classification");
    puts("PASS: cold startup with peer UNKNOWN starts one check; both conceptually simultaneous UNKNOWN endpoints exchange three distinct probe replies without movement or heartbeat traffic");
}

void testUnknownHeartbeatSilence()
{
    freshApp(); pauseAutomaticHeartbeats = false; nextEventTime = FIRST_EVENT_DELAY_MS;
    for (uint32_t now : {0U, 999U, 1000U, 4000U, 12000U, 24000U})
    {
        hostNow = now; loop();
        assert(proximityClassification == ProximityClassification::UNKNOWN && automaticHeartbeatIntervalMs() == 0);
        assert(!waitingForAck && wire.empty() && ccWire.empty() && led.requests == 0 && !led.busy());
        assert(hostPixel().shown == 0 && nextMessageId == 1);
        assertProximityReset(); // Silence does not create a recurring check.
    }
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshApp(); selectedTransport = radio;
        const auto event = proximityObservation(90, -50, 0).message;
        receiveVia(event, radio);
        assert(haveLastPeerEvent && lastPeerEventId == 90 && peerState() == PeerState::ONLINE);
        receiveVia(event, radio);
        const auto& receipts = radio == Transport::ESP_NOW ? wire : ccWire;
        unsigned acks = 0;
        for (const auto& packet : receipts)
            if (packet.type == Type::Ack) { ++acks; assert(packet.ackForMessageId == 90); }
        assert(acks == 2 && led.requests == 0 && !led.busy() && hostPixel().shown == 0);
        assert(proximityClassification == ProximityClassification::UNKNOWN && !waitingForAck);
        assert(occurrences(Serial.log, "RX NEW EVENT") == 1 && occurrences(Serial.log, "RX DUPLICATE") == 1);
        startHeartbeatEvent(); // Even an explicitly created transaction cannot light an UNKNOWN device.
        assert(led.requests == 0 && !led.busy());
        for (unsigned attempt = 0; attempt < 3; ++attempt)
        {
            hostNow = ackWaitStart + 300; loop();
            assert(led.requests == 0 && !led.busy());
        }
        loop(); // Apply fallback after the expired transaction cancels its UNKNOWN check.
        assert(!waitingForAck && selectedTransport == Transport::CC1101);
        const auto sent = wire.size(), ccSent = ccWire.size();
        pauseAutomaticHeartbeats = false;
        hostNow += 20000; loop();
        assert(!waitingForAck && wire.size() == sent && ccWire.size() == ccSent && led.requests == 0);
    }
    puts("PASS: UNKNOWN has no automatic EVENT/local LED; new remote EVENT ACKs normally and duplicate re-ACKs without flashing on either radio");
}

void testKnownHeartbeatRechecks()
{
    for (auto prior : {ProximityClassification::CLOSE, ProximityClassification::FAR})
    for (bool cancel : {false, true})
    for (unsigned samples : {0U, 2U})
    {
        freshApp(); notePeerSeen(); proximityClassification = prior;
        selectedTransport = prior == ProximityClassification::FAR ? Transport::CC1101 : Transport::ESP_NOW;
        const uint32_t interval = prior == ProximityClassification::FAR ? 4000 : 2500;
        pauseAutomaticHeartbeats = false; nextEventTime = 100;
        startProximityCheck(0);
        hostNow = 100; loop();
        assert(proximityUpdateState == ProximityUpdateState::CHECKING && proximityClassification == prior);
        assert(waitingForAck && led.requests == 1);
        hostNow = 150; receiveVia(incoming(Type::Ack, pendingMessage.messageId), pendingTransport);
        assert(nextEventTime == 150 + interval && !waitingForAck && led.requests == 1);
        pauseAutomaticHeartbeats = true; // Isolate a failed measurement from unrelated ACK exhaustion.
        for (unsigned i = 0; i < samples; ++i) classificationSample(800 + i, -60);
        if (cancel) movementStep(hostNow + 10, MotionEvent::Activity);
        else { hostNow = CHECK_TIMEOUT_MS; loop(); }
        assertProximityReset();
        assert(proximityClassification == prior && automaticHeartbeatIntervalMs() == interval);
        assert(nextEventTime == 150 + interval && led.requests == 1);
    }
    puts("PASS: CLOSE/FAR keep their heartbeat and cadence while CHECKING; zero/partial timeout or movement cancellation preserves known classification and scheduling");
}

void testInitialProximityTimeout()
{
    for (unsigned samples : {0U, 2U})
    {
        freshApp(); pauseAutomaticHeartbeats = false; nextEventTime = FIRST_EVENT_DELAY_MS;
        const auto event = proximityObservation(90, -50, hostNow).message;
        receive(event);
        const auto began = checkStartedAt;
        for (unsigned i = 0; i < samples; ++i) classificationSample(100 + i, -50);
        hostNow = began + CHECK_TIMEOUT_MS - 1; loop();
        assert(checkStartedAt == began && proximitySampleCount == samples);
        hostNow = began + CHECK_TIMEOUT_MS; loop();
        assertProximityReset();
        assert(proximityClassification == ProximityClassification::UNKNOWN && peerState() == PeerState::ONLINE);
        assert(displayFrames.back().distance == "UNKNOWN" && selectedTransport == Transport::ESP_NOW);
        const auto probes = countWire(Type::ProximityProbe);
        receive(event); // Retry of the original first-contact EVENT after timeout.
        receive(proximityObservation(91, -50, hostNow).message);
        receive(incoming(Type::Ack, 999)); // Unrelated receipt cannot restart the expired UNKNOWN check.
        for (unsigned i = 0; i < 100; ++i) { hostNow += 1000; loop(); }
        assertProximityReset();
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | TIMEOUT") == 1);
        assert(countWire(Type::Event) == 0 && led.requests == 0 && !led.busy());
        assert(countWire(Type::ProximityProbe) == probes);
        settleForCheck(hostNow + 2000); // A legitimate future movement may try again.
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 2);
    }
    puts("PASS: initial check with zero/two samples times out at 12s, OLED returns UNKNOWN, ONLINE traffic/duplicates/loops never retry; future movement can start another check");
}

void testInitialProximityStartup()
{
    for (bool deep : {false, true})
    for (bool recovered : {false, true})
    for (bool runtimeOk : {false, true})
    {
        freshApp(); protocolReady = false; pauseAutomaticHeartbeats = false;
        delete receiveQueue; receiveQueue = nullptr;
        injectedBoot.deep = deep;
        RtcState::save({123, false, 0, {false, 0}});
        injectWakePacket = recovered;
        injectedPacket = proximityObservation(90, -50, hostNow).message;
        radioStarts = runtimeOk;
        atRadioStart = [] {
            assert(!protocolReady && motionInitializations == 1 && displayInitializations == 1);
            assert(hostPixel().begins == 1 && proximityClassification == ProximityClassification::UNKNOWN);
            assertProximityReset(); // Bootstrap waits for successful ESP-NOW/runtime initialization.
            assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 0);
            assert((peerState() == PeerState::ONLINE) == (injectedBoot.deep && injectWakePacket));
        };
        setup();
        assert(protocolReady == runtimeOk);
        assert((proximityUpdateState == ProximityUpdateState::CHECKING) == runtimeOk);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == (runtimeOk ? 1U : 0U));
        assert(wire.empty() && led.requests == 0);
        if (!runtimeOk)
        {
            for (unsigned i = 0; i < 100; ++i) loop();
            assertProximityReset();
            assert(wire.empty() && occurrences(Serial.log, "PROXIMITY CHECK | START") == 0);
            continue;
        }
        const auto peer = peerState();
        assert(peer == (deep && recovered ? PeerState::ONLINE : PeerState::UNKNOWN));
        if (deep && recovered) assert(rtcRestored && wakeReport.processed && wakeReport.ackSent);
        const auto began = checkStartedAt;
        // An ONLINE startup/peer-contact path cannot overlap the bootstrap check.
        startPeerAvailabilityProximityCheck(PeerState::UNKNOWN);
        assert(checkStartedAt == began && occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        for (uint32_t elapsed = 0; elapsed < CHECK_TIMEOUT_MS; elapsed += PROBE_REPLY_WAIT_MS)
        {
            hostNow = began + elapsed; loop();
            assert(checkStartedAt == began && proximityUpdateState == ProximityUpdateState::CHECKING);
            assert(proximityClassification == ProximityClassification::UNKNOWN && peerState() == peer);
            assert(!waitingForAck && led.requests == 0 && !led.busy());
        }
        hostNow = began + CHECK_TIMEOUT_MS; loop();
        assertProximityReset();
        assert(countWire(Type::ProximityProbe) == 24 && wire.size() == 24 && ccWire.empty());
        assert(proximityClassification == ProximityClassification::UNKNOWN && hostPixel().shown == 0);
        automaticSleepArmed = false; // Isolate non-recurring discovery; automatic sleep has its own tests.
        for (unsigned i = 0; i < 100; ++i) { hostNow += 1000; loop(); }
        assertProximityReset();
        assert(wire.size() == 24 && countWire(Type::Event) == 0 && led.requests == 0);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        assert(occurrences(Serial.log, "PROXIMITY CHECK | TIMEOUT") == 1);
        settleForCheck(hostNow + 2000); // A later real movement remains an eligible one-shot trigger.
        assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 2);
        hostNow = checkStartedAt + CHECK_TIMEOUT_MS; loop();
        assertProximityReset();
        assert(occurrences(Serial.log, "PROXIMITY CHECK | TIMEOUT") == 2 && led.requests == 0);
    }
    // Model another startup path having already started a valid check, or an
    // initialization callback leaving the system IDLE. Use the existing guards.
    for (bool checking : {false, true})
    {
        freshApp(); protocolReady = false;
        delete receiveQueue; receiveQueue = nullptr;
        if (checking) atRadioStart = [] {
            startProximityCheck(1);
            proximitySampleCount = 1; proximitySamples[0] = {900, -50};
        };
        else atRadioStart = [] { PowerManager::forceIdle(hostNow); };
        setup();
        assert(protocolReady && led.requests == 0 && wire.empty());
        if (checking)
        {
            assert(checkStartedAt == 1 && proximitySampleCount == 1 && proximitySamples[0].messageId == 900);
            assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 1);
        }
        else
        {
            assert(localState() == LocalState::IDLE);
            assertProximityReset();
            assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 0);
        }
    }
    puts("PASS: cold/deep runtime starts one UNKNOWN check without ONLINE; isolated startup times out at 12s after 24 bounded probes, stays silent without restart, permits later movement; retained wake, ACTIVE/overlap and failed-runtime guards preserved");
}

void testInitialProximityIsolation()
{
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        for (auto type : {Type::ProximityProbe, Type::ProximityProbeReply, Type::SleepRequest,
                          Type::SleepReady, Type::SleepCommit, Type::SleepAck, Type::SleepCancel, Type::Ack})
        {
            freshApp(); selectedTransport = radio;
            receiveVia(incoming(type, type == Type::ProximityProbe ? 0 : 23, 23), radio);
            assertProximityReset();
            assert(occurrences(Serial.log, "PROXIMITY CHECK | START") == 0);
        }
        for (unsigned invalid = 0; invalid < 6; ++invalid)
        {
            freshApp(); auto packet = proximityObservation(90, -50, hostNow).message;
            if (invalid == 0) ++packet.version;
            if (invalid == 1) packet.sender = LOCAL_DEVICE;
            if (invalid == 2) packet.event = Protocol::EventType::None;
            if (invalid == 3) packet.ackForMessageId = 1;
            if (invalid == 4) packet.type = static_cast<Type>(99);
            handleReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet) - (invalid == 5), radio);
            assert(peerState() == PeerState::UNKNOWN); assertProximityReset();
        }
        freshApp(); selectedTransport = radio; startHeartbeatEvent();
        const auto id = pendingMessage.messageId;
        receiveVia(incoming(Type::Ack, id), radio == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW);
        receiveVia(incoming(Type::Ack, id + 1), radio);
        auto malformed = incoming(Type::Ack, id); malformed.event = Protocol::EventType::Heartbeat;
        receiveVia(malformed, radio);
        assert(waitingForAck && peerState() == PeerState::UNKNOWN); assertProximityReset();
        for (auto prior : {ProximityClassification::CLOSE, ProximityClassification::FAR})
        {
            freshApp(); proximityClassification = prior;
            receiveVia(proximityObservation(90, -50, hostNow).message, radio);
            assert(peerState() == PeerState::ONLINE && proximityClassification == prior); assertProximityReset();
        }
        freshApp(); command('i');
        receiveVia(proximityObservation(90, -50, hostNow).message, radio);
        assert(peerState() == PeerState::ONLINE && localState() == LocalState::IDLE); assertProximityReset();
    }
    puts("PASS: initial checks reject probes, sleep traffic, unrelated/wrong-radio/malformed ACKs and malformed EVENTs; known classification and ACTIVE guards retained");
}

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
        displayedStatus = {"OFFLINE", "FAR", "CC1101", "IDLE", "MOVING"}; // Prior boot must not suppress the first draw.
        atRadioStart = [] { assert(motionInitializations == 1 && displayInitializations == 1); };
        setup();
        assert(protocolReady && motionInitializations == 1 && displayInitializations == 1);
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
    command('i'); assert(displayFrames.back().state == "IDLE");
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
    puts("PASS: OLED peer, UNKNOWN/CHECKING/CLOSE/FAR, selected radio and ACTIVE/IDLE labels; compact sleep labels, visible-text change detection");
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

void isolatedDisplayService()
{
    const auto before = displayObservedRuntime();
    serviceDisplayStatus();
    assert(displayObservedRuntime() == before);
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
    for (unsigned guard = 0; guard < 12; ++guard)
    {
        freshApp(); isolatedDisplayService();
        assert(displayFrames.size() == 1);
        const auto oldSnapshot = displayedStatus;
        const auto savedQueue = receiveQueue;
        const auto packet = incoming(Type::Ack, 999);
        switch (guard)
        {
            case 0: protocolReady = false; break;
            case 1: startHeartbeatEvent(); break;
            case 2: controlCount = 1; break;
            case 3: forceIdle(hostNow); requestSleep(nextMessageId++, hostNow); break;
            case 4: sleepDrainWaiting = true; break;
            case 5: awakeAckBusy = true; break;
            case 6: mockedTxInFlight = 1; break;
            case 7: mockedRxActive = true; break;
            case 8: receiveQueue = nullptr; break;
            case 9: queueReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)); break;
            case 10: probeOutstanding = true; break;
            case 11: displayReady = false; break;
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
    puts("PASS: every OLED busy guard defers silently, retains the last drawn snapshot and coalesces after drain; probe wait and runtime isolation");
}

void testDisplaySleep()
{
    freshApp(); command('i');
    assert(deepSleepFrames.empty());
    const auto idleFrames = displayFrames.size();
    command('s');
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
        assert(physicalSleeps == 0 && localState() == LocalState::IDLE);
        assert(deepSleepFrames.size() == (failure >= 4 ? 1U : 0U));
        mockedTxInFlight = 0; motionIntLevel = 0; loop();
        assert(!lastDisplayWasDeepSleep && displayFrames.back().state == "IDLE");
        assert(motionCancels == 1);
    }
    // Failed/unavailable OLED cannot veto physical entry.
    completedAwaitingCallbacks(false); mockedTxInFlight = 0; displayReady = false; loop();
    assert(physicalSleeps == 1 && deepSleepFrames.empty());
    puts("PASS: no final OLED during IDLE/negotiation/semantic SLEEPING/drain or failed Motion/radio/setup; physical entry draws once, post-frame abort restores awake display, no added delay, unavailable OLED does not gate sleep");
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

void testLedAnimation()
{
    struct Frame { uint32_t offset; uint8_t red; };
    const Frame frames[]{{0, 0}, {4, 5}, {65, 90}, {130, 180}, {134, 175}, {195, 90},
                         {260, 0}, {295, 0}, {330, 0}, {334, 5}, {422, 126},
                         {515, 255}, {519, 250}, {608, 127}, {699, 2}};
    for (uint32_t start : {0U, UINT32_MAX - 300U})
    {
        freshApp(); assert(!led.busy() && hostPixel().shown == 0);
        const auto shows = hostPixel().shows;
        led.requestHeartbeat();
        assert(led.busy() && hostPixel().shows == shows); // Request never touches hardware or waits.
        for (const auto& frame : frames) ledStep(start + frame.offset, frame.red);
        ledStep(start + 700, 0, false);
        ledStep(start + 2000, 0, false); // No blocking tail, cooldown or recurring animation.
    }
    for (uint32_t restartAt : {65U, 195U, 295U, 422U, 608U})
    {
        freshApp(); led.requestHeartbeat(); led.update(0); hostNow = restartAt; led.update(hostNow);
        const auto shows = hostPixel().shows;
        led.requestHeartbeat(); assert(hostPixel().shows == shows);
        ledStep(restartAt, 0); ledStep(restartAt + 65, 90); ledStep(restartAt + 130, 180);
        ledStep(restartAt + 515, 255); ledStep(restartAt + 700, 0, false);
    }
    freshApp(); led.requestHeartbeat(); ledStep(0, 0);
    ledStep(450, 165); ledStep(10000, 0, false); // Late service jumps directly to the current frame.
    freshApp(); hostPixel().ready = false; led.requestHeartbeat(); hostNow = 100;
    const auto shows = hostPixel().shows; led.update(hostNow);
    assert(led.busy() && hostPixel().shows == shows && hostNow == 100);
    hostPixel().ready = true; ledStep(100, 0); ledStep(230, 180);
    hostPixel().ready = false; hostNow = 300; led.update(hostNow);
    assert(hostPixel().shown == uint32_t(180) << 16 && hostNow == 300);
    hostPixel().ready = true; ledStep(360, 0); ledStep(800, 0, false);
    freshApp(); led.requestHeartbeat(); ledStep(0, 0); ledStep(130, 180);
    led.off(); assert(!led.busy() && hostPixel().shown == 0); ledStep(515, 0, false);
    puts("PASS: real LED 180/255 red double pulse, 130/70/185 ms fades/gap, 700 ms finish, rollover, restart, skipped frames, latch deferral and no delay/state side effects");
}

void testLedEvents(ProximityClassification classification)
{
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshKnownApp(classification);
        auto event = proximityObservation(70, -50, hostNow).message;
        if (radio == Transport::ESP_NOW)
        {
            queueReceivedData(reinterpret_cast<const uint8_t*>(&event), sizeof(event));
            assert(!led.busy() && hostPixel().shows == 0); // Callback only queues; no LED work.
            loop();
        }
        else receiveVia(event, radio);
        assert(led.busy() && hostPixel().shows == 0); // Exactly the new EVENT requested an animation.
        auto receipts = [&]() { return radio == Transport::ESP_NOW ? wire.size() : ccWire.size(); };
        const auto& receipt = radio == Transport::ESP_NOW ? wire.back() : ccWire.back();
        assert(receipt.type == Type::Ack && receipt.ackForMessageId == 70 && receipts() == 1);
        const auto start = hostNow; ledStep(start, 0);
        hostNow = start + 65; receiveVia(event, radio);
        assert(receipts() == 2 && hostPixel().shown == uint32_t(90) << 16);
        ledStep(start + 130, 180); // Duplicate did not restart the clock.
        receiveVia(event, radio == Transport::ESP_NOW ? Transport::CC1101 : Transport::ESP_NOW);
        ledStep(start + 195, 90); // Cross-radio duplicate uses the same dedup path.
        event.messageId = 71; receiveVia(event, radio);
        const auto restarted = hostNow;
        ledStep(restarted, 0); ledStep(restarted + 65, 90); ledStep(restarted + 515, 255);
        ledStep(restarted + 700, 0, false);
        const auto ackCount = receipts(); receiveVia(event, radio);
        assert(!led.busy() && receipts() == ackCount + 1); // A late retry is still re-ACKed, never replayed.
    }
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    for (auto type : {Type::Ack, Type::ProximityProbe, Type::ProximityProbeReply,
                      Type::SleepRequest, Type::SleepReady, Type::SleepCommit, Type::SleepAck, Type::SleepCancel})
    {
        freshKnownApp(classification); const auto packet = incoming(type, type == Type::ProximityProbe ? 0 : 23, 23);
        receiveVia(packet, radio); assert(!led.busy() && hostPixel().shown == 0);
    }
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshKnownApp(classification); selectedTransport = radio; startHeartbeatEvent();
        ledStep(0, 0); ledStep(65, 90);
        receiveVia(incoming(Type::Ack, pendingMessage.messageId), radio);
        assert(!waitingForAck && led.busy());
        ledStep(130, 180); // Local creation starts a pulse; its matching ACK does not restart it.
        for (unsigned invalid = 0; invalid < 6; ++invalid)
        {
            freshKnownApp(classification); auto packet = proximityObservation(70, -50, hostNow).message;
            if (invalid == 0) ++packet.version;
            if (invalid == 1) packet.sender = LOCAL_DEVICE;
            if (invalid == 2) packet.event = Protocol::EventType::None;
            if (invalid == 3) packet.ackForMessageId = 1;
            if (invalid == 4) packet.type = static_cast<Type>(99);
            handleReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet) - (invalid == 5), radio);
            assert(!led.busy() && hostPixel().shows == 0);
        }
    }
    puts("PASS: new remote EVENT on either radio requests identical LED pulses; queued callbacks, duplicates, ACK/probe/sleep traffic and malformed packets cannot replay/request them");
}

void testLedLocalHeartbeat(ProximityClassification classification)
{
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    for (bool accepted : {false, true})
    {
        freshKnownApp(classification); notePeerSeen(); selectedTransport = radio;
        radioAccepts = ccAccepts = accepted; nextMessageId = 0xFFFF;
        const Protocol::Message expected{Protocol::VERSION, Type::Event, 0xFFFF,
            LOCAL_DEVICE, Protocol::EventType::Heartbeat, 0};
        startHeartbeatEvent();
        assert(led.busy() && led.requests == 1 && hostPixel().shows == 0 && hostNow == 0);
        assert(waitingForAck && retryCount == 0 && ackWaitStart == 0 && nextMessageId == 0);
        assert(pendingTransport == radio && memcmp(&pendingMessage, &expected, sizeof(expected)) == 0);
        ledStep(0, 0); ledStep(65, 90); ledStep(130, 180);
        hostNow = 300; loop();
        assert(waitingForAck && retryCount == 1 && ackWaitStart == 300 && led.requests == 1);
        ledStep(300, 0); ledStep(334, 5); ledStep(515, 255);
        hostNow = 600; loop();
        assert(waitingForAck && retryCount == 2 && ackWaitStart == 600 && led.requests == 1);
        ledStep(600, 138); ledStep(608, 127); ledStep(700, 0, false);
        hostNow = 900; loop();
        assert(!waitingForAck && retryCount == 0 && peerState() == PeerState::OFFLINE);
        assert(!led.busy() && led.requests == 1 && hostPixel().shown == 0 && nextMessageId == 0);
        assert(pendingTransport == radio && memcmp(&pendingMessage, &expected, sizeof(expected)) == 0);
        const auto& packets = radio == Transport::ESP_NOW ? wire : ccSubmitAttempts;
        assert(packets.size() == 3);
        for (const auto& packet : packets) assert(memcmp(&packet, &expected, sizeof(expected)) == 0);
        assert(selectedTransport == Transport::CC1101); // Existing fallback, without replaying the exhausted EVENT.
        assert(radio == Transport::CC1101 ? wire.empty() : ccWire.empty());
        ledStep(1000, 0, false);
        startHeartbeatEvent(); // Only a fresh transaction on the selected route starts another pulse.
        assert(pendingMessage.messageId == 0 && pendingTransport == Transport::CC1101 && led.busy() && led.requests == 2);
        ledStep(1000, 0); ledStep(1065, 90);
    }
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshKnownApp(classification); notePeerSeen(); selectedTransport = radio; startHeartbeatEvent();
        auto sharedEvent = pendingMessage;
        ledStep(0, 0); ledStep(65, 90);
        receiveVia(incoming(Type::Ack, sharedEvent.messageId), radio);
        assert(led.requests == 1);
        ledStep(130, 180); ledStep(195, 90); // ACK cannot restart the sender's pulse.
        startHeartbeatEvent();
        assert(pendingMessage.messageId != sharedEvent.messageId && led.requests == 2);
        ledStep(195, 0); ledStep(260, 90); ledStep(325, 180); // Fresh local EVENT restarts an active pulse.

        // Model the opposite endpoint's identity; the same EVENT ID/payload is
        // delivered through its real receive path (both identities run this suite).
        freshKnownApp(classification); sharedEvent.sender = PEER_DEVICE;
        hostNow = 2; receiveVia(sharedEvent, radio);
        assert(led.busy() && led.requests == 1 && hostPixel().shows == 0 && lastPeerEventId == sharedEvent.messageId);
        ledStep(2, 0); ledStep(67, 90);
        receiveVia(sharedEvent, radio); // Re-ACK without restarting the peer's pulse.
        assert(led.requests == 1);
        ledStep(132, 180); ledStep(517, 255); ledStep(702, 0, false);
        const auto& receipts = radio == Transport::ESP_NOW ? wire : ccWire;
        assert(receipts.size() == 2);
        for (const auto& receipt : receipts)
            assert(receipt.type == Type::Ack && receipt.ackForMessageId == sharedEvent.messageId);
    }
    puts("PASS: new local EVENT starts one pulse even on rejected TX; identical 300/600ms retries and 900ms exhaustion/fallback never restart it; only fresh EVENT restarts; peer mirrors same ID/payload and re-ACKs duplicates");
}

void testLedProtocolIsolation(ProximityClassification classification)
{
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    for (bool acknowledged : {false, true})
    {
        std::string baseline;
        for (bool animate : {false, true})
        {
            freshKnownApp(classification); selectedTransport = radio; pendingMessage = {}; ackWaitStart = 0;
            receiveVia(proximityObservation(70, -50, hostNow).message, radio);
            if (!animate) led.off();
            led.update(hostNow);
            startHeartbeatEvent();
            if (!animate) led.off();
            const auto message = pendingMessage;
            const auto start = ackWaitStart;
            std::string trace;
            for (uint32_t elapsed : {100U, 299U, 300U, 599U, 600U, 700U, 900U})
            {
                hostNow = start + elapsed;
                if (elapsed == 100) receiveVia(proximityObservation(71, -50, hostNow).message, radio);
                else if (elapsed == 700 && acknowledged) receiveVia(incoming(Type::Ack, message.messageId), radio);
                else loop();
                if (!animate) led.off();
                if (elapsed == 299) assert(waitingForAck && retryCount == 0 && ackWaitStart == start);
                if (elapsed == 300) assert(waitingForAck && retryCount == 1 && ackWaitStart == start + 300);
                if (elapsed == 600) assert(waitingForAck && retryCount == 2 && ackWaitStart == start + 600);
                if (animate && elapsed <= 700) assert(led.busy());
                assert(memcmp(&pendingMessage, &message, sizeof(message)) == 0 && pendingTransport == radio);
                trace += displayObservedRuntime();
            }
            assert(!waitingForAck && retryCount == 0);
            assert(peerState() == (acknowledged ? PeerState::ONLINE : PeerState::OFFLINE));
            for (const auto& packet : wire) trace.append(reinterpret_cast<const char*>(&packet), sizeof(packet));
            for (const auto& packet : ccWire) trace.append(reinterpret_cast<const char*>(&packet), sizeof(packet));
            if (!animate) baseline = trace;
            else assert(trace == baseline);
        }
    }
    puts("PASS: identical EVENT/ACK traffic, pending bytes/radio, 300 ms/two-retry deadlines, exhaustion/fallback and power state with LED enabled or cancelled");
}

void testLedMovementAndSleep(ProximityClassification classification)
{
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshKnownApp(classification); selectedTransport = radio;
        receiveVia(proximityObservation(70, -50, hostNow).message, radio);
        const auto start = hostNow; ledStep(start, 0);
        movementStep(start + 65, MotionEvent::Activity);
        assert(movementState == MovementState::MOVING && led.busy() && hostPixel().shown == uint32_t(90) << 16);
        movementStep(start + 130, MotionEvent::Inactivity);
        assert(movementState == MovementState::WAITING && led.busy() && hostPixel().shown == uint32_t(180) << 16);
        const auto settled = settleStartedAt + SETTLE_MS;
        hostNow = settled - 100; receiveVia(proximityObservation(71, -50, hostNow).message, radio);
        const auto nextStart = hostNow; ledStep(nextStart, 0);
        movementStep(settled);
        assert(proximityUpdateState == ProximityUpdateState::CHECKING && led.busy() && hostPixel().shown != 0);
        if (radio == Transport::ESP_NOW) assert(displayFrames.back().distance == "CHECKING");
        else assert(probeOutstanding);
        movementStep(nextStart + 130, MotionEvent::Activity);
        assert(proximityUpdateState == ProximityUpdateState::READY && led.busy() && hostPixel().shown == uint32_t(180) << 16);
        movementStep(nextStart + 515);
        assert(led.busy() && hostPixel().shown == uint32_t(255) << 16);
        movementStep(nextStart + 700); assert(!led.busy() && hostPixel().shown == 0);
    }
    for (bool coordinated : {false, true})
    {
        freshKnownApp(classification); receive(proximityObservation(70, -50, hostNow).message);
        const auto start = hostNow; ledStep(start, 0); ledStep(start + 65, 90);
        if (coordinated)
        {
            command('i'); command('s'); const auto id = transaction().sleepId;
            receive(incoming(Type::Ack, pendingMessage.messageId)); receive(incoming(Type::SleepReady, id));
            receive(incoming(Type::Ack, pendingMessage.messageId));
            mockedTxInFlight = 1; receive(incoming(Type::SleepAck, id));
            assert(localState() == LocalState::SLEEPING && led.busy() && physicalSleeps == 0);
            mockedTxInFlight = 0; assert(sleepTransportBlockedReason() == nullptr);
            loop(); assert(physicalSleeps == 1);
        }
        else { command('x'); assert(benchSleepCalls == 1); }
        assert(!led.busy() && hostPixel().shown == 0);
        ledStep(start + 515, 0, false);
    }
    // Initialization resets a running animation on either cold boot or deep wake.
    for (bool deep : {false, true})
    {
        freshKnownApp(classification); receive(proximityObservation(70, -50, hostNow).message);
        const auto start = hostNow; ledStep(start, 0); ledStep(start + 65, 90);
        saveRtcHistory(); protocolReady = false; injectedBoot.deep = deep;
        delete receiveQueue; receiveQueue = nullptr;
        setup();
        assert(protocolReady && hostPixel().begins == ledBeginsAtBoot + 1 && !led.busy() && hostPixel().shown == 0);
    }
    puts("PASS: MOVING/WAITING/CHECKING and OLED/probe work do not pause/cancel LED; manual/coordinated sleep forces OFF immediately; cold/deep startup resets animation");
}

void freshAutomaticRuntime(uint32_t now = 0)
{
    freshApp(); hostNow = now;
    lastMeaningfulActivity = now;
    automaticSleepArmed = true;
}

void testAutomaticSleepClock()
{
    static_assert(AUTOMATIC_SLEEP_INACTIVITY_MS == 35000, "product inactivity duration changed");
    for (bool deep : {false, true})
    for (bool runtimeOk : {false, true})
    {
        freshApp(); protocolReady = false; delete receiveQueue; receiveQueue = nullptr;
        injectedBoot.deep = deep; radioStarts = runtimeOk; hostNow = 123;
        setup();
        assert(localState() == LocalState::ACTIVE && automaticSleepArmed == runtimeOk);
        if (!runtimeOk)
        {
            hostNow += 100000; loop();
            assert(!transaction().active && !automaticSleepArmed);
            continue;
        }
        const auto began = hostNow;
        assert(lastMeaningfulActivity == began && uint32_t(hostNow - lastMeaningfulActivity) == 0);
        hostNow = began + CHECK_TIMEOUT_MS; loop(); // Existing startup probe timeout completes normally.
        hostNow = began + 34999; loop();
        assert(localState() == LocalState::ACTIVE && automaticSleepArmed && countWire(Type::SleepRequest) == 0);
        hostNow = began + 35000; loop();
        assert(transaction().active && transaction().startedAt == began + 35000);
        assert(transaction().role == SleepRole::COORDINATOR && !automaticSleepArmed);
        assert(countWire(Type::SleepRequest) == 1 && lastMeaningfulActivity == began);
    }
    for (uint32_t start : {0U, UINT32_MAX - 31000})
    {
        freshAutomaticRuntime(start);
        movementStep(start + 30000, MotionEvent::Activity);
        assert(lastMeaningfulActivity == start + 30000 && automaticSleepArmed);
        movementStep(start + 33000, MotionEvent::Inactivity);
        assert(lastMeaningfulActivity == start + 30000 && movementState == MovementState::WAITING);
        movementStep(start + 34000);
        assert(proximityUpdateState == ProximityUpdateState::CHECKING);
        movementStep(start + 46000); // Measurement ends without changing the activity clock.
        movementStep(start + 64999);
        assert(!transaction().active && automaticSleepArmed && lastMeaningfulActivity == start + 30000);
        movementStep(start + 65000);
        assert(transaction().active && transaction().startedAt == start + 65000 && !automaticSleepArmed);
    }
    puts("PASS: cold/deep runtime inactivity starts at zero; 34999ms stays ACTIVE, 35000ms starts once; Activity at 30s moves eligibility to 65s, Inactivity/settlement/proximity do not restart it, including rollover");
}

void testAutomaticSleepBackgroundTraffic()
{
    for (auto radio : {Transport::ESP_NOW, Transport::CC1101})
    {
        freshAutomaticRuntime(); proximityClassification = ProximityClassification::CLOSE;
        selectedTransport = radio; pauseAutomaticHeartbeats = false; nextEventTime = 1000;
        uint16_t peerId = 10000, firstEvent = 0;
        bool sawRetry = false;
        for (uint32_t now = 0; now <= 35000; now += 50)
        {
            hostNow = now;
            if (now % 2500 == 0)
            {
                const Protocol::Message heartbeat{Protocol::VERSION, Type::Event, peerId++, PEER_DEVICE,
                                                   Protocol::EventType::Heartbeat, 0};
                queueReceivedData(reinterpret_cast<const uint8_t*>(&heartbeat), sizeof(heartbeat));
            }
            if (waitingForAck && pendingMessage.type == Type::Event)
            {
                if (!firstEvent) firstEvent = pendingMessage.messageId;
                if (pendingMessage.messageId != firstEvent || retryCount != 0)
                {
                    sawRetry = sawRetry || retryCount != 0;
                    const auto ack = incoming(Type::Ack, pendingMessage.messageId);
                    if (pendingTransport == Transport::CC1101) ccIncoming.push_back(ack);
                    else queueReceivedData(reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
                }
            }
            if (now == 10000)
            {
                const auto probe = incoming(Type::ProximityProbe, 0, peerId++);
                queueReceivedData(reinterpret_cast<const uint8_t*>(&probe), sizeof(probe));
                auto observation = proximityObservation(peerId++, -50, now);
                rssiObservations.push_back(observation);
            }
            loop();
            assert(lastMeaningfulActivity == 0);
            if (now < 35000) assert(automaticSleepArmed && !transaction().active);
        }
        assert(sawRetry && !automaticSleepArmed && transaction().startedAt == 35000);
        assert(countWire(Type::SleepRequest) == 1 && countWire(Type::ProximityProbeReply) == 1);
        assert(led.requests > 10 && !displayFrames.empty());
        const auto sleep = transaction();
        const auto next = nextMessageId;
        const Protocol::Message heartbeat{Protocol::VERSION, Type::Event, peerId++, PEER_DEVICE,
                                           Protocol::EventType::Heartbeat, 0};
        receiveVia(heartbeat, radio); receiveVia(heartbeat, radio);
        assert(transaction().active && transaction().sleepId == sleep.sleepId && peerState() == PeerState::SLEEP_PENDING);
        assert(transaction().phaseDeadline == sleep.phaseDeadline && transaction().hardDeadline == sleep.hardDeadline);
        assert(lastMeaningfulActivity == 0 && !automaticSleepArmed && countWire(Type::SleepCancel) == 0);
        assert(nextMessageId == uint16_t(next + 2)); // Normal receipts for new + duplicate EVENT.
    }
    freshAutomaticRuntime(); hostNow = 1000;
    startProximityCheck(hostNow); loop();
    classificationSample(800, -85); classificationSample(801, -85); classificationSample(802, -85);
    loop();
    assert(proximityClassification == ProximityClassification::FAR && selectedTransport == Transport::CC1101);
    assert(lastMeaningfulActivity == 0 && automaticSleepArmed && !displayFrames.empty());
    puts("PASS: periodic local/remote Heartbeats, LED, duplicates/ACKs/retries, both radios, probe/RSSI/classification/transport/OLED work do not reset inactivity; Heartbeats preserve accepted negotiation and peer sleep intent");
}

void testAutomaticSleepDeferral()
{
    for (unsigned busy = 0; busy < 14; ++busy)
    {
        freshAutomaticRuntime(); hostNow = 35000;
        QueueHandle_t savedQueue = receiveQueue;
        switch (busy)
        {
            case 0: protocolReady = false; break;
            case 1: motionReady = false; break;
            case 2: movementState = MovementState::MOVING; break;
            case 3: movementState = MovementState::WAITING; settleStartedAt = hostNow; break;
            case 4: startProximityCheck(hostNow); break;
            case 5: startHeartbeatEvent(); break;
            case 6: controlCount = 1; break;
            case 7: awakeAckBusy = true; break;
            case 8: mockedTxInFlight = 1; break;
            case 9: mockedRxActive = true; break;
            case 10:
            {
                const auto packet = incoming(Type::ProximityProbe, 0, 99);
                queueReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)); break;
            }
            case 11: forceIdle(hostNow); assert(requestSleep(nextMessageId++, hostNow)); break;
            case 12:
                forceIdle(hostNow); assert(requestSleep(nextMessageId++, hostNow));
                PowerManager::injectActivity(hostNow); break;
            case 13: receiveQueue = nullptr; break;
        }
        const auto id = nextMessageId;
        const auto requests = countWire(Type::SleepRequest);
        for (unsigned i = 0; i < 100; ++i) serviceAutomaticSleep();
        assert(automaticSleepArmed && lastMeaningfulActivity == 0 && nextMessageId == id);
        assert(countWire(Type::SleepRequest) == requests);
        switch (busy)
        {
            case 0: protocolReady = true; break;
            case 1: motionReady = true; break;
            case 2: case 3: resetMovement(); break;
            case 4: resetProximityCheck(); break;
            case 5: handleAck(incoming(Type::Ack, pendingMessage.messageId), pendingTransport); break;
            case 6: controlCount = 0; break;
            case 7: awakeAckBusy = false; break;
            case 8: mockedTxInFlight = 0; break;
            case 9: mockedRxActive = false; break;
            case 10:
            {
                Protocol::Message packet{};
                assert(xQueueReceive(receiveQueue, &packet, 0) == pdPASS);
                handleReceivedData(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)); break;
            }
            case 11: PowerManager::injectActivity(hostNow); // Exercise the guard, without creating a physical Activity.
                // fall through
            case 12: hostNow += 3000; PowerManager::update(hostNow); break;
            case 13: receiveQueue = savedQueue; break;
        }
        serviceAutomaticSleep(); sendNextControl();
        assert(transaction().active && !automaticSleepArmed && transaction().role == SleepRole::COORDINATOR);
        assert(countWire(Type::SleepRequest) == requests + 1);
        for (unsigned i = 0; i < 100; ++i) serviceAutomaticSleep();
        assert(countWire(Type::SleepRequest) == requests + 1);
    }
    puts("PASS: runtime/sensor, MOVING/WAITING/proximity, pending ACK/control, CC1101, ESP-NOW TX/callback/RX, active transaction and cooldown defer without consuming an ID or automatic opportunity; drain permits exactly one attempt");
}

void testAutomaticSleepFailures()
{
    for (unsigned outcome = 0; outcome < 5; ++outcome)
    for (auto peer : {PeerState::UNKNOWN, PeerState::ONLINE, PeerState::OFFLINE})
    {
        freshAutomaticRuntime();
        if (peer == PeerState::ONLINE) notePeerSeen();
        if (peer == PeerState::OFFLINE) notePeerUnreachable();
        hostNow = 35000; loop();
        const auto request = pendingMessage;
        assert(!automaticSleepArmed && transaction().active && request.type == Type::SleepRequest);
        if (outcome == 0)
        {
            for (uint32_t elapsed : {300U, 600U, 900U}) { hostNow = 35000 + elapsed; loop(); }
            assert(countWire(Type::SleepRequest) == 3 && peerState() == PeerState::OFFLINE);
            for (const auto& packet : wire) if (packet.type == Type::SleepRequest)
                assert(memcmp(&packet, &request, sizeof(packet)) == 0);
        }
        else if (outcome == 1) receive(incoming(Type::SleepCancel, request.messageId));
        else
        {
            receive(incoming(Type::Ack, request.messageId));
            if (outcome == 2) { hostNow = 38000; loop(); }
            else
            {
                hostNow = 37999; receive(incoming(Type::SleepReady, request.messageId));
                receive(incoming(Type::Ack, pendingMessage.messageId));
                if (outcome == 3) { hostNow = 40000; loop(); }
                else { entryFails = true; receive(incoming(Type::SleepAck, request.messageId)); }
            }
        }
        assert(!transaction().active && localState() == LocalState::IDLE && !automaticSleepArmed);
        assert(cooldownLeftMs(hostNow) > 0 && physicalSleeps == 0 && lastMeaningfulActivity == 0);
        const auto requests = countWire(Type::SleepRequest);
        for (unsigned i = 0; i < 100; ++i) { hostNow += 1000; loop(); }
        assert(!transaction().active && !automaticSleepArmed && countWire(Type::SleepRequest) == requests);
        assert(cooldownLeftMs(hostNow) == 0);
        const auto activity = hostNow;
        movementStep(activity, MotionEvent::Activity);
        assert(automaticSleepArmed && lastMeaningfulActivity == activity && localState() == LocalState::ACTIVE);
        movementStep(activity + 3000, MotionEvent::Inactivity);
        movementStep(activity + 4000); movementStep(activity + 16000);
        movementStep(activity + 34999); assert(!transaction().active);
        movementStep(activity + 35000);
        assert(transaction().active && !automaticSleepArmed && countWire(Type::SleepRequest) == requests + 1);
    }
    puts("PASS: UNKNOWN/OFFLINE/ONLINE attempts remain bounded by identical retries, refusal, phase/hard deadline or physical failure; cooldown never rearms, 100 later loops do not retry, new Activity permits one attempt 35s later");
}

void testAutomaticSleepAdmission()
{
    static_assert(AUTOMATIC_SLEEP_PEER_GRACE_MS == 3000, "participant inactivity grace changed");
    // Diagnostic IDLE alone cannot waive the recipient's own inactivity requirement.
    freshAutomaticRuntime(); command('i'); hostNow = 31999;
    receive(incoming(Type::SleepRequest, 90));
    assert(localState() == LocalState::IDLE && !transaction().active && automaticSleepArmed);
    assert(countWire(Type::SleepCancel) == 1 && countWire(Type::SleepReady) == 0);
    freshAutomaticRuntime(); hostNow = 31999;
    receive(incoming(Type::SleepRequest, 100));
    assert(!transaction().active && localState() == LocalState::ACTIVE && automaticSleepArmed);
    assert(countWire(Type::Ack) == 1 && countWire(Type::SleepCancel) == 1 && countWire(Type::SleepReady) == 0);
    hostNow = 32000; holdEspReceipts = true;
    receive(incoming(Type::SleepRequest, 101));
    assert(transaction().active && transaction().role == SleepRole::PARTICIPANT && !automaticSleepArmed);
    assert(transaction().startedAt == 32000 && mockedTxInFlight == 1 && countWire(Type::SleepReady) == 1);
    const auto hard = transaction().hardDeadline;
    receive(incoming(Type::SleepRequest, 101)); // Own receipt/control TX cannot block an accepted transaction.
    receive(incoming(Type::SleepCommit, 101, 102));
    assert(localState() == LocalState::SLEEPING && countWire(Type::SleepAck) == 1 && physicalSleeps == 0);
    assert(lastMeaningfulActivity == 0 && !automaticSleepArmed && hard == 37000);
    receive(incoming(Type::Ack, pendingMessage.messageId));
    holdEspReceipts = false; mockedTxInFlight = 0; loop();
    assert(physicalSleeps == 1);

    for (unsigned busy = 0; busy < 12; ++busy)
    {
        freshAutomaticRuntime(); hostNow = 32000;
        if (busy == 0) motionReady = false;
        if (busy == 1) movementState = MovementState::MOVING;
        if (busy == 2) movementState = MovementState::WAITING;
        if (busy == 3) startProximityCheck(hostNow);
        if (busy == 4) startHeartbeatEvent();
        if (busy == 5) awakeAckBusy = true;
        if (busy == 6) mockedTxInFlight = 1;
        if (busy == 7) mockedRxActive = true;
        if (busy == 8)
        {
            const auto queued = incoming(Type::ProximityProbe, 0, 99);
            queueReceivedData(reinterpret_cast<const uint8_t*>(&queued), sizeof(queued));
        }
        if (busy == 9) controlCount = 1;
        if (busy == 10)
        {
            forceIdle(hostNow); assert(requestSleep(nextMessageId++, hostNow));
            PowerManager::injectActivity(hostNow);
        }
        if (busy == 11) protocolReady = false;
        const auto request = incoming(Type::SleepRequest, 100);
        handleReceivedData(reinterpret_cast<const uint8_t*>(&request), sizeof(request));
        assert(!transaction().active && automaticSleepArmed && countWire(Type::SleepReady) == 0);
        // Runtime failure cannot enqueue/send a CANCEL; receipt behavior is unchanged.
        assert(countWire(Type::SleepCancel) == (busy == 10 ? 2U : busy == 11 ? 0U : 1U));
        assert(countWire(Type::Ack) == 1);
    }
    // Freshness must be decided before promoting ACTIVE to IDLE, too.
    freshAutomaticRuntime(); hostNow = 31999; receive(incoming(Type::SleepRequest, 100));
    automaticSleepArmed = false; hostNow = 32000; receive(incoming(Type::SleepRequest, 100));
    assert(localState() == LocalState::ACTIVE && !transaction().active);
    // Grace admission consumes an armed opportunity, but never rearms a spent one
    // or rewrites the activity clock, including across millis rollover.
    for (uint32_t start : {0U, UINT32_MAX - 31000})
    for (uint32_t age : {32000U, 33000U, 34999U})
    for (bool armed : {false, true})
    {
        freshAutomaticRuntime(start); automaticSleepArmed = armed; hostNow = start + age;
        receive(incoming(Type::SleepRequest, 100));
        assert(transaction().active && transaction().role == SleepRole::PARTICIPANT);
        assert(lastMeaningfulActivity == start && !automaticSleepArmed && countWire(Type::SleepRequest) == 0);
        receive(incoming(Type::SleepCancel, 100, 101));
        const auto cancelledAt = hostNow - 10;
        assert(cooldownLeftMs(cancelledAt) == 3000 && !automaticSleepArmed);
        hostNow = cancelledAt + 3000;
        for (unsigned i = 0; i < 100; ++i) loop();
        assert(!transaction().active && lastMeaningfulActivity == start && !automaticSleepArmed);
        assert(countWire(Type::SleepRequest) == 0);
    }
    // Recent real movement still refuses, even after settling and proximity finish.
    freshAutomaticRuntime(); movementStep(30000, MotionEvent::Activity);
    movementStep(33000, MotionEvent::Inactivity); movementStep(34000); movementStep(46000);
    assert(movementState == MovementState::READY && proximityUpdateState != ProximityUpdateState::CHECKING);
    assert(lastMeaningfulActivity == 30000 && sleepTransportBlockedReason() == nullptr);
    receive(incoming(Type::SleepRequest, 100));
    assert(!transaction().active && automaticSleepArmed && lastMeaningfulActivity == 30000);
    assert(countWire(Type::SleepCancel) == 1 && countWire(Type::SleepReady) == 0);
    puts("PASS: fresh peer REQUEST refuses at 31999ms, accepts at 32000ms only with all existing guards; grace never changes activity time/rearms/retries; recent Motion refuses, stale/duplicate/COMMIT handling is unchanged");
}

void testAutomaticSleepStaggeredClocks()
{
    // This harness has one runtime. Replay each endpoint's prefix to exchange
    // its actual emitted packets; both device builds exercise both time roles.
    const auto deliver = [](Protocol::Message message, uint32_t at) {
        message.sender = PEER_DEVICE;
        hostNow = at; receive(message);
    };
    for (uint32_t offset : {0U, 500U, 1000U, 2999U, 3000U, 4000U})
    {
        const bool reverse = offset > 3000;
        const uint32_t coordinatorAt = reverse ? offset + 35000 : 35000;
        const auto earlier = [] { freshAutomaticRuntime(); nextMessageId = 40; };
        const auto later = [offset] { freshAutomaticRuntime(offset); nextMessageId = 100; };
        Protocol::Message refusedRequest{}, refusal{};
        if (reverse)
        {
            earlier(); hostNow = 35000; loop(); refusedRequest = pendingMessage;
            assert(refusedRequest.type == Type::SleepRequest && !automaticSleepArmed);
            later(); deliver(refusedRequest, 35000); refusal = wire.back();
            assert(refusal.type == Type::SleepCancel && refusal.ackForMessageId == refusedRequest.messageId);
            assert(!transaction().active && automaticSleepArmed && lastMeaningfulActivity == offset);
            assert(countWire(Type::SleepReady) == 0 && cooldownLeftMs(hostNow) == 0);
        }
        const auto startCoordinator = [&] {
            if (reverse)
            {
                later(); deliver(refusedRequest, 35000);
                assert(!transaction().active && automaticSleepArmed);
            }
            else earlier();
            hostNow = coordinatorAt - 1; loop();
            assert(countWire(Type::SleepRequest) == 0 && !transaction().active && automaticSleepArmed);
            hostNow = coordinatorAt; loop();
            assert(transaction().role == SleepRole::COORDINATOR && !automaticSleepArmed);
            assert(transaction().startedAt == coordinatorAt && pendingMessage.type == Type::SleepRequest);
            assert(countWire(Type::SleepRequest) == 1 && lastMeaningfulActivity == (reverse ? offset : 0));
        };
        startCoordinator(); const auto request = pendingMessage;
        const auto startParticipant = [&] {
            if (reverse)
            {
                earlier(); hostNow = 35000; loop();
                deliver(refusal, 35010);
                assert(!transaction().active && !automaticSleepArmed && cooldownLeftMs(35010) == 3000);
                hostNow = 38009; loop(); assert(cooldownLeftMs(38009) == 1);
                hostNow = 38010; loop(); assert(cooldownLeftMs(38010) == 0);
                // Expiry and repeated loop service cannot retry the earlier attempt.
                while (hostNow < coordinatorAt) loop();
                assert(countWire(Type::SleepRequest) == 1 && !transaction().active && !automaticSleepArmed);
            }
            else later();
            deliver(request, coordinatorAt);
            assert(transaction().active && transaction().role == SleepRole::PARTICIPANT);
            assert(transaction().startedAt == coordinatorAt && !automaticSleepArmed);
            assert(lastMeaningfulActivity == (reverse ? 0 : offset));
            assert(countWire(Type::SleepRequest) == (reverse ? 1U : 0U));
            assert(countWire(Type::SleepReady) == 1 && pendingMessage.type == Type::SleepReady);
        };
        startParticipant(); const auto ready = pendingMessage;
        const auto advanceCoordinator = [&] {
            startCoordinator(); assert(memcmp(&pendingMessage, &request, sizeof(request)) == 0);
            deliver(ready, coordinatorAt + 10);
            assert(transaction().phase == SleepPhase::WAIT_ACK && pendingMessage.type == Type::SleepCommit);
        };
        advanceCoordinator(); const auto commit = pendingMessage;
        const auto advanceParticipant = [&] {
            startParticipant(); assert(memcmp(&pendingMessage, &ready, sizeof(ready)) == 0);
            deliver(commit, coordinatorAt + 20);
            assert(localState() == LocalState::SLEEPING && pendingMessage.type == Type::SleepAck);
            assert(physicalSleeps == 0);
        };
        advanceParticipant(); const auto sleepAck = pendingMessage;
        advanceCoordinator(); assert(memcmp(&pendingMessage, &commit, sizeof(commit)) == 0);
        deliver(sleepAck, coordinatorAt + 30); const auto receipt = wire.back();
        assert(receipt.type == Type::Ack && receipt.ackForMessageId == sleepAck.messageId);
        for (unsigned i = 0; i < 100; ++i) loop();
        assert(physicalSleeps == 1 && countWire(Type::SleepRequest) == 1 && !automaticSleepArmed);
        assert(countWire(Type::SleepCommit) == 1 && lastMeaningfulActivity == (reverse ? offset : 0));
        advanceParticipant(); assert(memcmp(&pendingMessage, &sleepAck, sizeof(sleepAck)) == 0);
        deliver(receipt, coordinatorAt + 40);
        for (unsigned i = 0; i < 100; ++i) loop();
        assert(physicalSleeps == 1 && !automaticSleepArmed && lastMeaningfulActivity == (reverse ? 0 : offset));
        assert(countWire(Type::SleepRequest) == (reverse ? 1U : 0U) && countWire(Type::SleepAck) == 1);
    }
    puts("PASS: offsets 0/500/1000/2999/3000ms complete both endpoints on the first request; 4000ms refuses then reverses after the unchanged cooldown, without rearming or a second automatic attempt by either endpoint");
}

void testAutomaticSleepMotionPriority()
{
    for (unsigned phase = 0; phase < 6; ++phase)
    {
        freshAutomaticRuntime(); hostNow = 35000;
        const bool participant = phase == 2 || phase == 3 || phase == 5;
        if (participant) receive(incoming(Type::SleepRequest, 100));
        else loop();
        const auto id = transaction().sleepId;
        if (phase == 1 || phase == 4) receive(incoming(Type::SleepReady, id));
        if (phase == 3 || phase == 5)
        {
            receive(incoming(Type::Ack, pendingMessage.messageId));
            delayControlsForTest = phase == 3;
            if (phase == 5) mockedTxInFlight = 1;
            receive(incoming(Type::SleepCommit, id, 101));
        }
        if (phase == 4)
        {
            mockedTxInFlight = 1;
            receive(incoming(Type::SleepAck, id));
        }
        const bool committed = phase >= 4;
        assert(localState() == (committed ? LocalState::SLEEPING : LocalState::SLEEP_NEGOTIATING));
        if (phase == 3) assert(transaction().phase == SleepPhase::WAIT_SLEEP_ACK_TX);
        const auto activity = hostNow;
        movementStep(activity, MotionEvent::Activity);
        assert(!transaction().active && automaticSleepArmed && lastMeaningfulActivity == activity);
        assert(localState() == (committed ? LocalState::WAKING : LocalState::ACTIVE));
        assert(countWire(Type::SleepCancel) == (committed ? 0U : 1U));
        assert(physicalSleeps == 0 && motionPreparations == 0);
        SleepDecision decision{}; assert(!takeSleepDecision(decision));
        if (committed) { hostNow = activity + 250; loop(); assert(localState() == LocalState::ACTIVE); }
    }
    for (bool late : {false, true})
    {
        freshAutomaticRuntime(); hostNow = 35000; loop(); const auto id = transaction().sleepId;
        receive(incoming(Type::SleepReady, id));
        if (late) afterAwakeService = [] { motionPendingEvent = MotionEvent::Activity; afterAwakeService = nullptr; };
        else motionPendingEvent = MotionEvent::Activity;
        receive(incoming(Type::SleepAck, id));
        assert(physicalSleeps == 0 && motionPreparations == 0 && automaticSleepArmed);
        assert(localState() == (late ? LocalState::WAKING : LocalState::ACTIVE));
        assert(countWire(Type::SleepCancel) == (late ? 0U : 1U));
    }
    puts("PASS: real Motion cancels WAIT_READY/WAIT_COMMIT/WAIT_ACK/WAIT_SLEEP_ACK_TX, revokes committed drain decisions through existing WAKING semantics, and wins both before queued SLEEP_ACK and after radio service just before physical execution");
}

void testAutomaticSleepCollisionAndManual()
{
    freshAutomaticRuntime(); hostNow = 35000;
    nextMessageId = LOCAL_DEVICE == Device::Bubu ? 40 : 33;
    loop(); const auto hard = transaction().hardDeadline;
    const auto peerId = LOCAL_DEVICE == Device::Bubu ? 33 : 40;
    receive(incoming(Type::SleepRequest, peerId));
    assert(transaction().hardDeadline == hard && !automaticSleepArmed && countWire(Type::SleepCancel) == 0);
    assert(transaction().sleepId == 40);
    if (LOCAL_DEVICE == Device::Bubu)
    {
        assert(transaction().role == SleepRole::COORDINATOR);
        receive(incoming(Type::SleepReady, 40)); receive(incoming(Type::SleepAck, 40));
    }
    else
    {
        assert(transaction().role == SleepRole::PARTICIPANT);
        receive(incoming(Type::SleepCommit, 40, 41)); receive(incoming(Type::Ack, pendingMessage.messageId));
    }
    assert(physicalSleeps == 1);
    freshAutomaticRuntime(); command('i'); command('s');
    assert(transaction().active && countWire(Type::SleepRequest) == 1 && !automaticSleepArmed);
    command('a'); assert(localState() == LocalState::ACTIVE && automaticSleepArmed);
    assert(lastMeaningfulActivity == hostNow - 10);
    freshAutomaticRuntime(); command('x'); assert(benchSleepCalls == 1 && countWire(Type::SleepRequest) == 0);
    puts("PASS: simultaneous automatic coordinators retain DeviceId arbitration and hard limit, both roles reach physical entry; manual i/s/a and diagnostic x remain available");
}

int main()
{
    static_assert(sizeof(Protocol::Message) == 8, "wire size changed");
    testAutomaticSleepClock(); testAutomaticSleepBackgroundTraffic(); testAutomaticSleepDeferral();
    testAutomaticSleepFailures(); testAutomaticSleepAdmission(); testAutomaticSleepMotionPriority();
    testAutomaticSleepCollisionAndManual(); testAutomaticSleepStaggeredClocks();
    testInitialProximityCadence(); // UNKNOWN must classify without application heartbeat traffic.
    testUnknownHeartbeatSilence(); testKnownHeartbeatRechecks();
    testFsm(); testTransport(); testDelayedCollision(); testFinalSendBounds();
    testSleepDecisionInterface(); testExecutionDrain(); testArmFailure();
    testRtcHistoryRestart(); testBootRouting(); testManualWakeTx(); testCoordinatedExecution();
    testMotionInitialization();
    testMotionSleepEntry();
    testMotionPeerWake();
    testAwakeMotionDiagnostics();
    testMovementSettle(); testMovementBoundaries(); testMovementIsolation();
    testRssiDiagnostics();
    testProximitySamples(); testProximityTimeout(); testProximityCancellation(); testProximityIsolation();
    testAwakeWakeService();
    testApplicationTransports(); testProximityProbes();
    testProximityClassification();
    testAutomaticHeartbeatCadence();
    testAutomaticSelection(); testAutomaticSelectionRetries(); testAutomaticSelectionGuards();
    testAutomaticSelectionManual(); testAutomaticSelectionSleep();
    testEspNowFallback(); testFallbackPrecedenceAndManual(); testFallbackRecovery();
    testPeerReturnEvents(); testPeerReturnIsolation(); testPeerReturnTimeout();
    testInitialProximityContact(); testInitialProximityTimeout(); testInitialProximityStartup(); testInitialProximityIsolation();
    testDisplayStartup(); testDisplayTransitions(); testDisplayMotion(); testDisplayGuards(); testDisplaySleep();
    testLedAnimation();
    for (auto classification : {ProximityClassification::CLOSE, ProximityClassification::FAR})
    {
        testLedEvents(classification); testLedLocalHeartbeat(classification);
        testLedProtocolIsolation(classification); testLedMovementAndSleep(classification);
    }
    puts("PASS: one arm attempt per decision, failure isolation, no rearming, ESP-NOW remains usable");
    delete receiveQueue;
    printf("PASS %s: coordinator/participant, collisions, stale/duplicates, hard/phase deadlines, activity, rollover\n", DEVICE_NAME);
    puts("PASS: actual RX queue/loop, packet ACK vs SLEEP_ACK, bounded same-ID retries, cancellation purge, heartbeat gating");
    puts("PASS: delayed collision, bounded WAIT_SLEEP_ACK_TX, duplicate/retry deadlines, unsent phase/hard expiry, cancellation");
    puts("PASS: one-shot sleep decisions, duplicate/stale suppression, transport drain/exhaustion, queued replay, activity revocation");
}
