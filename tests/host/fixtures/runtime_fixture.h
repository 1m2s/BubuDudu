#pragma once
// Exercise the actual production FSM and loop with deterministic time/radio
// substitutes. No PlatformIO, Wi-Fi, hardware, or additional test framework.
#include "Arduino.h"
#include "../../../src/PowerManager.cpp"
#include "../../../src/RtcState.cpp"
// MCU attributes/GPIO and the existing Motion driver are substituted on host.
#define IRAM_ATTR
constexpr int LOW = 0, HIGH = 1, INPUT_PULLUP = 2;
int digitalRead(int pin);
void pinMode(int pin, int mode);
#include "LED.h"
#include "observed_led.h"
#define LED ObservedLED
#define loop firmwareLoop
#define setup firmwareSetup
#include "production_app.h"
#undef loop
#undef setup
#undef LED
#include "../../../src/LED.cpp"
// A real reset discards RAM queues. Simulated startup must release the previous
// host allocation before the unchanged production setup creates a new queue.
void setup()
{
    delete receiveQueue;
    receiveQueue = nullptr;
    firmwareSetup();
}
struct PhysicalSleepEntered {};
// Protocol fixtures can postpone the periodic due time without a firmware pause
// switch. Cadence/policy tests run the unmodified schedule instead.
bool suppressPeriodicForTest = true;
void loop()
{
    const auto scheduled = nextEventTime;
    const unsigned long postponed = static_cast<unsigned long>(millis()) + 1000000UL;
    if (suppressPeriodicForTest) nextEventTime = postponed;
    try { firmwareLoop(); } catch (const PhysicalSleepEntered&) {}
    if (suppressPeriodicForTest && nextEventTime == postponed) nextEventTime = scheduled;
}

#include "runtime_doubles.h"

using Type = Protocol::MessageType;
using Device = Protocol::DeviceId;
using namespace PowerManager;
// Pure FSM tests supply admission explicitly, as the production loop now does.
// Product-policy tests below exercise real inactivity/motion admission separately.
void handleControl(const Protocol::Message& message, uint32_t now)
{
    PowerManager::handleControl(message, now, true);
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
bool queueTestSleepControl(Type type, uint16_t sleepId);
extern bool deferControlsForTest;
void freshApp()
{
    pendingMessage = {}; ackWaitStart = 0;
    for (auto& entry : controlQueue) entry = {};
    for (auto& id : recentUserEventIds) id = 0;
    for (auto& time : recentUserEventTimes) time = 0;
    wakeReport = {}; rtcRestored = false; retainedUserAnimationId = 0;
    motionReady = displayReady = true; displayedStatus = {}; // Model an initialized awake runtime.
#ifdef DEVICE_DUDU
    displayDiagnostic = {};
#endif
    Serial.writeCapacity = 256; Serial.capacityChecks = Serial.writes = 0;
    displayInitializations = 0; displayInitOk = true; displayFrames.clear();
    i2cHealthReports = 0;
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
    recentUserEventCount = nextUserEventSlot = 0;
    nextEventTime = 100000; controlCount = 0; suppressPeriodicForTest = true; deferControlsForTest = false;
    hostNow = 0; wire.clear(); radioAccepts = true; Serial.log.clear(); Serial.input.clear();
    armAttempts = 0; armResult = CC1101SleepArm::Result::Ready;
    armInitializations = 0; wakeRecoveries = 0;
    injectedBoot = {}; injectWakePacket = false;
    awakeAckBusy = awakeRuntimeStopped = false; awakeServices = 0;
    wakeEvents.clear(); wakeTxResult = CC1101WakeTx::Result::AckTimeout;
    wakeTxRxReady = true; wakeTxElapsedMs = 0; duringWakeTx = nullptr;
    deferredWakePending = false;
    retainedUserAnimationPending = false;
    radioStarts = true; atRadioStart = nullptr;
    coordinatedAttempts = physicalSleeps = mockedTxInFlight = 0;
    mockedRxActive = entryFails = false; afterArm = nullptr;
    holdEspReceipts = false; afterAwakeService = nullptr;
    sleepDrainWaiting = false; sleepDrainStarted = 0;
    motionInitializations = 0; motionInitOk = motionPrepareOk = true;
    motionPreparations = motionCancels = 0;
    motionIntLevel = 0; motionStartup = MotionEvent::None;
    bootInfo = {}; buttonLevel = HIGH; beginButton(); buttonConfigurations = 0;
    motionPendingEvent = MotionEvent::None; motionEventPolls = 0;
    PowerManager::begin(LOCAL_DEVICE, queueTestSleepControl);
    // Existing protocol tests model an old, stationary runtime with automatic
    // initiation already consumed. Dedicated policy tests arm a fresh episode.
    lastMeaningfulActivity = uint32_t(0 - AUTOMATIC_SLEEP_INACTIVITY_MS);
    automaticSleepArmed = false;
    hostPixel() = {}; led.begin(); hostPixel() = {}; // Fresh awake LED, with startup instrumentation reset.
    led.requests = led.userRequests = 0;
    haveFarBackgroundHeartbeat = false; lastFarBackgroundHeartbeatAt = 0;
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
// Explicit harness setup, never a serial command dispatcher. Delay injection is
// confined to the FSM's test callback; production queues are immediately due.
bool deferControlsForTest = false;
bool queueTestSleepControl(Type type, uint16_t sleepId)
{
    const size_t before = controlCount;
    const bool accepted = queueSleepControl(type, sleepId);
    if (accepted && deferControlsForTest && controlCount > before)
        controlQueue[controlCount - 1].notBefore += 1000U;
    return accepted;
}
void requestSleepForTest()
{
    assert(protocolReady && !waitingForAck && controlCount == 0);
    if (PowerManager::requestSleep(nextMessageId++, hostNow)) automaticSleepArmed = false;
    loop();
}
void activityForTest() { noteLocalActivity(hostNow); loop(); }
void deferControls() { deferControlsForTest = true; loop(); }
void wakePeerForTest() { requestPeerWake(); loop(); }
void selectTransportForTest(Transport transport)
{
    selectedTransport = transport;
    automaticSelectionPending = espNowFallbackPending = false;
    loop();
}
void typeSerialForTest(const char* text)
{
    while (*text) Serial.input.push_back(*text++);
    loop();
}
void receive(const Protocol::Message& message)
{
    queueReceivedData(reinterpret_cast<const uint8_t*>(&message), sizeof(message));
    loop();
}
Protocol::Message incoming(Type type, uint16_t id, uint16_t packet = 90)
{
    return control(type, id, packet, PEER_DEVICE);
}
void failedSleepEntryForTest()
{
    PowerManager::begin(LOCAL_DEVICE, [](Type, uint16_t) { return true; });

    const auto id = nextMessageId++;
    assert(PowerManager::requestSleep(id, hostNow));
    PowerManager::handleControl(incoming(Type::SleepReady, id), hostNow, true);
    PowerManager::handleControl(incoming(Type::SleepAck, id), hostNow, true);
    const auto originalFailure = entryFails;
    entryFails = true; // Exercise abort/cleanup, without throwing a simulated reboot.
    serviceSleepExecution();
    entryFails = originalFailure;
}
size_t countWire(Type type)
{
    size_t count = 0;
    for (const auto& message : wire) if (message.type == type) ++count;
    return count;
}

// Each suite is a separate process; each case also starts from the same fixture.
template<typename Case>
void runCase(Case test)
{
    RtcState::invalidate();
    freshApp();
    test();
}
void finishSuite(const char* name)
{
    delete receiveQueue;
    receiveQueue = nullptr;
    printf("PASS %s: %s suite\n", DEVICE_NAME, name);
}
