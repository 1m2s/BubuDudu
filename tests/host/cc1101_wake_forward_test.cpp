// Real wake ACK waiter, deferred forwarding, awake receipt driver, application
// consumer and LED. Only MCU/SPI and unrelated Motion/OLED/ESP-NOW are substitutes.
#include "cc1101/SPI.h"
#define IRAM_ATTR
constexpr int INPUT_PULLUP = 3;
#include "LED.h"
struct ObservedLED : LED
{
    unsigned userRequests = 0;
    void requestUserHeartbeat() { ++userRequests; LED::requestUserHeartbeat(); }
};
#define LED ObservedLED
#include "../../src/main.cpp"
#undef LED
#include "../../src/LED.cpp"
#include "../../src/PowerManager.cpp"
#include "../../src/RtcState.cpp"
#include "../../src/CC1101SleepArm.cpp"
#include "../../src/CC1101WakeRecovery.cpp"
#include "../../src/CC1101WakeTx.cpp"
#include <algorithm>

uint32_t hostNow = 0, hostUs = 0;
HostSerial Serial;
HostSPI SPI;
bool misoHigh = false;
int gdoLevel = LOW, csLevel = HIGH;
namespace WakePlatform
{
    bool deepReset = false, sources = false, held = false, deepHeld = false;
    bool failSetup = false, failRelease = false, returnFromSleep = false;
    esp_sleep_wakeup_cause_t cause = ESP_SLEEP_WAKEUP_UNDEFINED;
    uint64_t mask = 0, enabledGpioMask = 0, timerUs = 0;
    uint64_t highWakeMask = 0, hardwareGpioMask = 0, hardwareHighMask = 0;
    bool timerEnabled = false;
    unsigned gpioWakeCalls = 0, failGpioWakeCall = 0, timerCalls = 0, sleepCalls = 0;
}
bool Motion::begin(uint8_t, uint8_t, uint8_t) { return true; }
bool Motion::prepareForSleep() { return true; }
bool Motion::cancelSleepPreparation() { return true; }
MotionEvent Motion::getEvent() { return MotionEvent::None; }
MotionEvent Motion::getStartupEvent() const { return MotionEvent::None; }
uint8_t Motion::getInterruptPin() const { return 3; }
Display::Display() = default;
bool Display::begin() { return false; }
void Display::showStatus(const char*, const char*, const char*, const char*, const char*, const char*) {}
void Display::showDeepSleep(const char*) {}
std::vector<Protocol::Message> espPackets;
namespace ESPNowRadio
{
    bool takeRssiObservation(RssiObservation&) { return false; }
    unsigned txInFlight() { return 0; }
    bool receiveCallbackActive() { return false; }
    bool begin(ReceiveHandler) { return true; }
    bool send(const uint8_t* data, size_t length)
    {
        assert(length == sizeof(Protocol::Message));
        Protocol::Message packet{}; memcpy(&packet, data, length);
        espPackets.push_back(packet); return true;
    }
}
using Type = Protocol::MessageType;
using Event = Protocol::EventType;
const Protocol::Message user{1, Type::Event, 132, PEER_DEVICE, Event::UserHeartbeat, 0};
const Protocol::Message wakeReceipt{1, Type::Ack, 70, PEER_DEVICE, Event::None, 100};
std::vector<uint8_t> frame(const Protocol::Message& packet)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(&packet);
    std::vector<uint8_t> raw{sizeof(packet)};
    raw.insert(raw.end(), bytes, bytes + sizeof(packet)); return raw;
}
Protocol::Message transmitted(size_t index)
{
    const auto& raw = SPI.transmissions.at(index);
    assert(raw.size() == 9 && raw[0] == 8);
    Protocol::Message packet{}; memcpy(&packet, raw.data() + 1, sizeof(packet)); return packet;
}
struct Arrival { uint32_t at; std::vector<uint8_t> raw; bool failRx; };
std::vector<Arrival> arrivals;
size_t nextArrival = 0;
uint32_t firstTxAt = 0;
uint32_t visualStartedAt = 0;
void latch(const std::vector<uint8_t>& raw)
{
    SPI.rxFifo.assign(raw.begin(), raw.end());
    SPI.registers[0x35] = 1; SPI.registers[0x3B] = static_cast<uint8_t>(raw.size()); gdoLevel = HIGH;
}
void inject()
{
    hostNow = hostUs / 1000;
    if (!CC1101WakeTx::sending) return;
    // Attempting the PRODUCTION drain while wake owns SPI must do no application
    // work and must never recurse into a receipt transmitter.
    const auto txCount = SPI.transmissions.size();
    Protocol::Message packet{};
    assert(!CC1101WakeTx::takeDeferredEvent(packet));
    serviceDeferredWakeEvents();
    assert(SPI.transmissions.size() == txCount && led.userRequests == 0);
    assert(std::count(SPI.commands.begin(), SPI.commands.end(), uint8_t(0x3A)) <= 1);
    for (size_t i = 0; i < txCount; ++i)
    {
        assert(transmitted(i).type == Type::Event && transmitted(i).event == Event::Heartbeat);
        assert(SPI.transmissions[i] == SPI.transmissions[0]); // Immutable wake retries.
    }
    if (!txCount) return;
    if (!firstTxAt) firstTxAt = SPI.txAtUs;
    if (nextArrival == arrivals.size() || uint32_t(hostUs - firstTxAt) < arrivals[nextArrival].at ||
        SPI.registers[0x35] != 0x0D || !SPI.rxFifo.empty()) return;
    const auto& arrival = arrivals[nextArrival++];
    latch(arrival.raw);
    if (arrival.failRx) SPI.reachRx = false;
}
void fresh(bool buttonWake = true)
{
    Protocol::Message packet{};
    while (CC1101WakeTx::takeDeferredEvent(packet)) {}
    CC1101WakeRecovery::awakeState = CC1101WakeRecovery::AwakeState::Listening;
    CC1101WakeRecovery::haveWakeAck = false;
    SPI = HostSPI{}; hostUs = hostNow = 0; misoHigh = false; gdoLevel = LOW;
    buttonGpioLevel() = HIGH; motionGpioLevel() = LOW;
    SPI.registers[0x31] = 0x14; SPI.registers[0x35] = 1;
    assert(CC1101SleepArm::begin() == CC1101SleepArm::Result::Ready);
    hostNow = hostUs / 1000; SPI.commands.clear(); Serial.log.clear();
    SPI.statusHook = inject; arrivals.clear(); nextArrival = 0; firstTxAt = visualStartedAt = 0;
    if (receiveQueue) delete receiveQueue;
    receiveQueue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(Protocol::Message));
    protocolReady = true; waitingForAck = false; controlCount = retryCount = 0;
    nextMessageId = 100; haveLastPeerEvent = false;
    recentUserEventCount = nextUserEventSlot = 0; testAckAlreadyDropped = false;
    selectedTransport = Transport::CC1101; proximityClassification = ProximityClassification::UNKNOWN;
    sleepDrainWaiting = false; resetMovement(); resetProximityCheck();
    espNowFallbackPending = automaticSelectionPending = false;
    nextEventTime = 100000;
    motionReady = true; displayReady = false;
    hostPixel() = {}; led.begin(); led.userRequests = 0; espPackets.clear();
    PowerManager::begin(LOCAL_DEVICE, queueSleepControl);
    bootInfo = {};
    if (buttonWake) { bootInfo.deep = true; bootInfo.cause = CC1101WakeRecovery::Cause::Gpio; bootInfo.gpioMask = 0x20; }
    beginButton(); // Already released: exercise the production wake-origin handoff.
}
void arrive(uint32_t at, const Protocol::Message& packet, bool failRx = false)
{ arrivals.push_back({at, frame(packet), failRx}); }
void step(uint32_t ms = 10)
{
    hostUs += ms * 1000; hostNow = hostUs / 1000;
    if (led.userRequests && !visualStartedAt) visualStartedAt = hostNow;
    loop();
}
void drain()
{
    for (unsigned i = 0; i < 30 && (CC1101WakeTx::deferredPending() || CC1101WakeRecovery::awakeBusy()); ++i) step();
    assert(!CC1101WakeTx::deferredPending() && !CC1101WakeRecovery::awakeBusy());
}
unsigned receipts(uint16_t id)
{
    unsigned count = 0;
    for (size_t i = 0; i < SPI.transmissions.size(); ++i)
    {
        const auto packet = transmitted(i);
        if (packet.type == Type::Ack && packet.ackForMessageId == id)
        { assert(packet.sender == LOCAL_DEVICE && packet.event == Event::None); ++count; }
    }
    for (const auto& packet : espPackets) assert(packet.type != Type::Ack);
    return count;
}
size_t occurrences(const std::string& needle)
{
    size_t count = 0, at = 0;
    while ((at = Serial.log.find(needle, at)) != std::string::npos) { ++count; at += needle.size(); }
    return count;
}
void assertVisualOnce()
{
    assert(led.userRequests == 1 && occurrences("RX NEW EVENT") == 1);
    assert(occurrences("PARTNER LED | USER HEARTBEAT | id=132") == 1);
    const auto start = visualStartedAt; assert(start != 0);
    led.update(start + 130, false);
    assert(hostPixel().shown == uint32_t(180) << 16);
    led.update(start + 515, false); assert(hostPixel().shown == uint32_t(255) << 16);
    led.update(start + 700, false); assert(!led.busy());
}
void testSuccess()
{
    fresh(); arrive(6000, user); arrive(12000, user); arrive(18000, user); arrive(24000, wakeReceipt);
    serviceButtonHeartbeat();
    assert(nextArrival == 4 && SPI.fifoReads == 36);
    assert(!buttonWakeIntentHeld && buttonHeartbeatPending && !waitingForAck);
    assert(led.userRequests == 1 && receipts(132) == 1 && CC1101WakeTx::deferredPending());
    assert(std::string(sleepTransportBlockedReason()) == "CC1101_WAKE_EVENTS_PENDING");
    assert(Serial.log.find("peer_ack=1 | RX_READY=1 | attempts=1") != std::string::npos);
    drain();
    assert(receipts(132) == 3 && occurrences("RX DUPLICATE") == 2);
    assert(!buttonHeartbeatPending && waitingForAck && pendingMessage.event == Event::UserHeartbeat);
    const auto localUser = pendingMessage;
    assert(localUser.messageId != 100 && pendingTransport == Transport::CC1101);
    assert(SPI.transmissions.size() == 5 && transmitted(4).messageId == localUser.messageId);
    latch(frame({1, Type::Ack, 71, PEER_DEVICE, Event::None, localUser.messageId})); step();
    assert(!waitingForAck && led.userRequests == 1);
    assertVisualOnce();
    hostNow = lastMeaningfulActivity + 35000; cancelProximityCheck("TEST_DONE");
    assert(productSleepEligible(hostNow)); // Forwarded work/receipts cannot strand the sleep guard.
}
void testTimeoutAndFailures()
{
    fresh(); arrive(6000, user); arrive(306000, user); arrive(606000, user);
    const auto began = hostUs; serviceButtonHeartbeat(); const auto elapsed = hostUs - began;
    assert(nextArrival == 3 && elapsed >= 900000 && elapsed < 950000);
    assert(!buttonWakeIntentHeld && !buttonHeartbeatPending && buttonWakeRetryOnPress);
    assert(Serial.log.find("peer_ack=0 | RX_READY=1 | attempts=3") != std::string::npos);
    drain(); assert(receipts(132) == 3 && occurrences("RX DUPLICATE") == 2); assertVisualOnce();
    for (size_t i = 0; i < SPI.transmissions.size(); ++i)
        if (transmitted(i).type == Type::Event) assert(transmitted(i).event == Event::Heartbeat);
    const auto sent = SPI.transmissions.size(); for (unsigned i = 0; i < 20; ++i) step();
    assert(SPI.transmissions.size() == sent); // No loop-based new episode.

    for (bool acked : {false, true})
    {
        fresh(); arrive(6000, user, !acked);
        if (acked) arrive(12000, wakeReceipt, true);
        serviceButtonHeartbeat(); drain();
        assert(!buttonWakeIntentHeld && !buttonHeartbeatPending && led.userRequests == 1);
        assert(CC1101WakeRecovery::awakeStopped() && receipts(132) == 0);
        assert(Serial.log.find("TX ACK REQUEST FAILED | ackFor=132") != std::string::npos);
        assert(Serial.log.find(acked ? "reason=LOCAL_RX_NOT_READY" : "reason=RADIO_UNAVAILABLE") != std::string::npos);
        assert(!CC1101WakeTx::deferredPending()); // Delivery survives RX/receipt failure.
    }
}
void testValidation()
{
    for (unsigned bad = 0; bad < 10; ++bad)
    {
        fresh(false); auto invalid = user;
        if (bad == 0) ++invalid.version;
        if (bad == 1) invalid.sender = LOCAL_DEVICE;
        if (bad == 2) invalid.type = Type::SleepAck;
        if (bad == 3) invalid.event = Event::None;
        if (bad == 4) invalid.event = static_cast<Event>(3);
        if (bad == 5) invalid.ackForMessageId = 1;
        if (bad == 9) { invalid = wakeReceipt; --invalid.ackForMessageId; }
        auto raw = frame(invalid);
        if (bad == 6) raw[0] = 7;
        if (bad == 7) raw.resize(5);
        if (bad == 8) raw.resize(64);
        arrivals.push_back({6000, raw, false}); arrive(12000, wakeReceipt);
        const auto result = requestPeerWake();
        assert(result.result == CC1101WakeTx::Result::Acked && result.attempts == 1 && result.rxReady);
        assert(!CC1101WakeTx::deferredPending() && led.userRequests == 0 && receipts(132) == 0);
        assert(SPI.transmissions.size() == 1 && occurrences("DEFERRED EVENT") == 0);
    }
    // A valid background EVENT also uses the application consumer, without a user pulse.
    fresh(false); auto background = user; background.event = Event::Heartbeat;
    arrive(6000, background); arrive(12000, wakeReceipt);
    const auto result = requestPeerWake(); drain();
    assert(result.result == CC1101WakeTx::Result::Acked && receipts(132) == 1 && led.userRequests == 0);
}
void testFull()
{
    for (bool distinct : {false, true})
    {
        fresh();
        for (unsigned i = 0; i < 9; ++i)
        {
            auto packet = user; if (distinct) packet.messageId += i;
            arrive(6000 + i * 3000, packet);
        }
        serviceButtonHeartbeat();
        assert(nextArrival == 9 && SPI.fifoReads == 72 && SPI.rxFifo.size() == 9);
        assert(CC1101WakeTx::deferredCount == 8 && led.userRequests == 0 && receipts(132) == 0);
        assert(!buttonWakeIntentHeld && !buttonHeartbeatPending);
        assert(Serial.log.find("DEFERRED_FULL | episode stopped | FIFO preserved | no receipt") != std::string::npos);
        const auto commands = SPI.commands.size();
        const Protocol::Message event{1, Type::Event, 200, LOCAL_DEVICE, Event::Heartbeat, 0};
        const auto result = CC1101WakeTx::send(event, PEER_DEVICE);
        assert(result.result == CC1101WakeTx::Result::Busy && result.attempts == 0 && SPI.commands.size() == commands);
        drain(); // Real serviceAwake consumes the preserved ninth packet, then deferred retries re-ACK.
        assert(SPI.rxFifo.empty() && SPI.fifoReads == 81);
        if (distinct)
        {
            assert(led.userRequests == 9 && occurrences("RX NEW EVENT") == 9);
            for (uint16_t id = 132; id < 141; ++id) assert(receipts(id) == 1);
        }
        else
        {
            assert(receipts(132) == 9 && occurrences("RX DUPLICATE") == 8); assertVisualOnce();
        }
        assert(std::count(SPI.commands.begin(), SPI.commands.end(), uint8_t(0x30)) == 0);
    }
}
int main()
{
    testSuccess(); testTimeoutAndFailures(); testValidation(); testFull();
    delete receiveQueue; receiveQueue = nullptr;
    printf("PASS %s: real wake waiter/application forwarding/receipt TX, duplicate retries/one user pulse, interleaved matching ACK, validation, unchanged deadlines/budgets, RX failures and full queue preserves FIFO\n", DEVICE_NAME);
    return 0;
}
