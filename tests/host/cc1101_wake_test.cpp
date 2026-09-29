#include "cc1101/SPI.h"
#include "../../src/CC1101SleepArm.cpp"
#include "../../src/RtcState.cpp"
#include "../../src/CC1101WakeRecovery.cpp"

uint32_t hostNow = 0, hostUs = 0;
HostSerial Serial;
HostSPI SPI;
bool misoHigh = false;
int gdoLevel = LOW, csLevel = HIGH;
namespace WakePlatform
{
    bool deepReset = true, sources = false, held = true, deepHeld = true;
    bool failSetup = false, failRelease = false;
    bool returnFromSleep = false;
    esp_sleep_wakeup_cause_t cause = ESP_SLEEP_WAKEUP_GPIO;
    uint64_t mask = 1ULL << 4;
    uint64_t enabledGpioMask = 0, timerUs = 0;
    unsigned timerCalls = 0, sleepCalls = 0;
}
using Type = Protocol::MessageType;
using Device = Protocol::DeviceId;
using namespace CC1101WakeRecovery;
#ifdef DEVICE_BUBU
constexpr Device local = Device::Bubu, peer = Device::Dudu;
#else
constexpr Device local = Device::Dudu, peer = Device::Bubu;
#endif
std::vector<Protocol::Message> appPackets;
bool receipt = false;
SubmitResult receiptResult = SubmitResult::Failed;
unsigned deliveries = 0, callbacks = 0, saves = 0;
bool duplicateEvent = false, highDuringSave = false, motionHighDuringSave = false;
unsigned guardCalls = 0, blockAtGuard = 0;
unsigned finalFrames = 0;
bool highDuringFrame = false, motionDuringFrame = false;
const char* guard()
{
    ++guardCalls;
    if (motionGpioLevel() != LOW) return "MOTION_INT1_HIGH";
    return blockAtGuard && guardCalls >= blockAtGuard ? "TX_IN_FLIGHT" : nullptr;
}
const Protocol::Message event{1, Type::Event, 70, peer, Protocol::EventType::Heartbeat, 0};

Protocol::Message handle(const Protocol::Message& packet, bool& processed)
{
    assert(packet.messageId == 70 && SPI.fifoReads == 9);
    // No reset/flush/configuration before application sees the recovered bytes.
    for (auto command : SPI.commands) assert(command & 0x80);
    ++callbacks; processed = !duplicateEvent;
    if (processed) ++deliveries;
    return {1, Type::Ack, 123, local, Protocol::EventType::None, packet.messageId};
}

void fresh()
{
    SPI = HostSPI{}; hostUs = 0; misoHigh = false; gdoLevel = LOW;
    SPI.registers[0x31] = 0x14; SPI.registers[0x35] = 1;
    assert(CC1101SleepArm::begin() == CC1101SleepArm::Result::Ready);
    SPI.commands.clear(); SPI.fifoReads = 0;
    WakePlatform::held = true; WakePlatform::deepHeld = true;
    WakePlatform::sources = false; WakePlatform::failSetup = false; WakePlatform::failRelease = false;
    WakePlatform::returnFromSleep = false;
    WakePlatform::enabledGpioMask = WakePlatform::timerUs = 0;
    WakePlatform::timerCalls = WakePlatform::sleepCalls = finalFrames = 0;
    highDuringFrame = motionDuringFrame = false;
    deliveries = 0; callbacks = 0; saves = 0; duplicateEvent = false; highDuringSave = false;
    motionGpioLevel() = LOW; motionHighDuringSave = false;
    WakePlatform::mask = 0x10; WakePlatform::cause = ESP_SLEEP_WAKEUP_GPIO;
    Serial.log.clear(); RtcState::invalidate(); guardCalls = blockAtGuard = 0;
    haveWakeAck = false; awakeState = AwakeState::Listening; wakeRetryTx = false;
    appPackets.clear(); receipt = false; receiptResult = SubmitResult::Failed;
}

void latch(Protocol::Message packet = event)
{
    SPI.rxFifo.push_back(sizeof(packet));
    const auto* bytes = reinterpret_cast<const uint8_t*>(&packet);
    for (size_t i = 0; i < sizeof(packet); ++i) SPI.rxFifo.push_back(bytes[i]);
    SPI.registers[0x3B] = 9; SPI.registers[0x35] = 1; gdoLevel = HIGH;
}

void save()
{
    assert(WakePlatform::held && WakePlatform::deepHeld && WakePlatform::sources);
    assert(guardCalls == 2); // After arm AND all setup/logging, at final boundary.
    ++saves;
    RtcState::save({123, true, 70, {true, 40}});
    if (highDuringSave) gdoLevel = HIGH;
    if (motionHighDuringSave) motionGpioLevel() = HIGH;
}

void finalFrame()
{
    assert(saves == 1 && guardCalls == 3 && finalFrames == 0 && WakePlatform::sleepCalls == 0);
    assert(WakePlatform::sources && WakePlatform::held && WakePlatform::deepHeld);
    assert(gdoLevel == LOW && motionGpioLevel() == LOW);
    ++finalFrames;
    if (highDuringFrame) gdoLevel = HIGH;
    if (motionDuringFrame) motionGpioLevel() = HIGH;
}

void service()
{
    const auto started = hostUs;
    serviceAwake(peer);
    // No 200-ms TX wait inside one loop call. Includes the existing 50-ms
    // restart budget and worst-case packet inspection/ACK setup failure.
    assert(uint32_t(hostUs - started) < 110000);
    assert(!SPI.active && csLevel == HIGH);
    for (auto command : SPI.commands) assert(command != 0x30);
}

void finishRetry()
{
    for (unsigned i = 0; i < 25 && awakeBusy(); ++i)
    {
        hostUs += 10000;
        service();
    }
    assert(!awakeBusy());
}

void testAwakeRetry()
{
    fresh(); latch();
    const auto boot = recover(true, peer, handle);
    assert(boot.processed && boot.ackSent && boot.rxReady && deliveries == 1);
    const auto firstAck = SPI.transmissions.front(); // Conceptually lost over RF.
    for (unsigned retry = 0; retry < 3; ++retry)
    {
        latch(); service(); assert(awakeBusy());
        assert(SPI.transmissions.back() == firstAck && SPI.transmissions.size() == retry + 2);
        finishRetry();
        assert(deliveries == 1 && callbacks == 1); // Application handler is never called again.
        assert(SPI.rxFifo.empty() && gdoLevel == LOW && SPI.registers[0x35] == 0x0D);
    }
    assert(Serial.log.find("processed=0 duplicate=1 | ACK_SENT=1 | RX_READY=1") != std::string::npos);
    const auto commands = SPI.commands; const auto time = hostUs; const auto log = Serial.log;
    for (unsigned i = 0; i < 50; ++i) service();
    assert(SPI.commands == commands && Serial.log == log && hostUs - time < 2000);

    // No new application behavior: wrong envelopes, lengths, or fresh EVENTs
    // are consumed/rejected once, with a bounded restart and no receipt.
    for (unsigned bad = 0; bad < 9; ++bad)
    {
        fresh(); latch(); recover(true, peer, handle);
        auto packet = event;
        if (bad == 0) packet.version = 2;
        if (bad == 1) packet.sender = local;
        if (bad == 2) packet.type = Type::Ack;
        if (bad == 3) packet.event = Protocol::EventType::None;
        if (bad == 4) packet.ackForMessageId = 99;
        if (bad == 5) ++packet.messageId;
        latch(packet);
        if (bad == 6) SPI.rxFifo[0] = 7;
        if (bad == 7) { SPI.rxFifo.resize(4); SPI.registers[0x3B] = 4; }
        if (bad == 8) { SPI.rxFifo.resize(11); SPI.registers[0x3B] = 11; } // No appended status allowed.
        service();
        assert(!awakeBusy() && SPI.transmissions.size() == 1 && deliveries == 1 && callbacks == 1);
        assert(SPI.rxFifo.empty() && SPI.registers[0x35] == 0x0D);
    }
    fresh(); latch(); service(); // Cold boot has no accepted wake EVENT to replay.
    assert(SPI.transmissions.empty() && callbacks == 0 && SPI.registers[0x35] == 0x0D);
    for (uint8_t count : {uint8_t(0), uint8_t(65), uint8_t(127), uint8_t(0x80)})
    {
        fresh(); gdoLevel = HIGH; SPI.registers[0x3B] = count; SPI.registers[0x35] = 1;
        service(); assert(SPI.fifoReads == 0 && callbacks == 0 && SPI.registers[0x35] == 0x0D);
    }
    fresh(); latch(); SPI.registers[0x35] = 0x0D;
    const auto fifo = SPI.rxFifo;
    service(); assert(SPI.rxFifo == fifo && SPI.fifoReads == 0); // Incomplete, never flush.
    fresh(); latch(); SPI.registers[0x02] = 6;
    service(); assert(SPI.fifoReads == 0 && SPI.rxFifo.size() == 9);
    const auto stoppedCommands = SPI.commands;
    service(); assert(SPI.commands == stoppedCommands); // Unknown configuration cutoff.

    fresh(); latch(); recover(true, peer, handle);
    latch(); SPI.finishTx = false; service(); assert(awakeBusy());
    finishRetry(); assert(deliveries == 1 && SPI.registers[0x35] == 0x0D);
    assert(Serial.log.find("ACK_TX_TIMEOUT") != std::string::npos);
    fresh(); latch(); recover(true, peer, handle);
    latch(); SPI.reachRx = false; service(); finishRetry();
    assert(Serial.log.find("STOPPED | RX restart failed") != std::string::npos);
    const auto failedCommands = SPI.commands;
    for (unsigned i = 0; i < 50; ++i) service();
    assert(SPI.commands == failedCommands && deliveries == 1);
    fresh(); latch(); recover(true, peer, handle);
    latch(); SPI.failAfterFifo = true; service();
    assert(!awakeBusy() && deliveries == 1 && SPI.transmissions.size() == 1);
    assert(Serial.log.find("STOPPED | RX restart failed") != std::string::npos);
    const auto spiFailureCommands = SPI.commands;
    service(); assert(SPI.commands == spiFailureCommands);
    fresh(); latch();
    SPI.statusHook = [] { if (SPI.command == 0xFB) misoHigh = true; };
    service(); // SPI fails after inspection but before FIFO selection.
    assert(SPI.fifoReads == 0 && SPI.rxFifo.size() == 9 && SPI.transmissions.empty());
    for (auto command : SPI.commands) assert(command & 0x80);
    const auto unreadCommands = SPI.commands;
    service(); assert(SPI.commands == unreadCommands);
    fresh(); latch(); recover(false, peer, handle);
    latch(); service(); // Invalid RTC never grants awake receipt authority.
    assert(deliveries == 0 && SPI.transmissions.empty());
    fresh(); latch(); recover(true, peer, handle);
    latch(); service(); SPI.registers[0x35] = 0x16; hostUs += 1000; service();
    assert(!awakeBusy() && SPI.registers[0x35] == 0x0D);
    assert(Serial.log.find("ACK_TX_UNDERFLOW") != std::string::npos);
    fresh(); latch(); recover(true, peer, handle);
    latch(); hostUs = 0xFFFFFE00; service(); finishRetry(); // micros wrap.
    assert(SPI.registers[0x35] == 0x0D && deliveries == 1);
    puts("PASS: lost boot ACK, repeated awake re-ACK without delivery, malformed/new rejection, idle no-op");
    puts("PASS: awake bounds, incomplete FIFO preserved, TX timeout, RX failure cutoff, micros rollover");
}

void testWakeSources()
{
    for (uint64_t mask : {0ULL, 8ULL, 16ULL, 24ULL})
    {
        fresh();
        WakePlatform::mask = mask;
        WakePlatform::cause = mask ? ESP_SLEEP_WAKEUP_GPIO : ESP_SLEEP_WAKEUP_TIMER;
        motionGpioLevel() = (mask & 8) ? HIGH : LOW;
        if (mask & 16) latch();
        const auto boot = captureBoot();
        assert(boot.wokeFromGpio(3) == bool(mask & 8));
        assert(boot.wokeFromGpio(4) == bool(mask & 16));
        assert(boot.motionAtBoot == motionGpioLevel() && SPI.commands.empty());
        const auto report = recover(true, peer, handle);
        assert(report.rxReady && report.packetRecovered == bool(mask & 16));
        assert(report.processed == bool(mask & 16) && report.ackSent == bool(mask & 16));
        if (!(mask & 16))
        {
            assert(callbacks == 0 && SPI.fifoReads == 0 && SPI.transmissions.empty());
            for (auto command : SPI.commands) assert(command & 0x80);
        }
        for (auto command : SPI.commands) assert(command != 0x30);
        printReport(boot, true, report);
        const char* source = mask == 24 ? "CC1101+MOTION" : mask == 16 ? "CC1101" : mask == 8 ? "MOTION" : "TIMER";
        assert(Serial.log.find(std::string("source=") + source) != std::string::npos);
    }
    // A radio packet can arrive after the wake mask was captured. Preserve it
    // even when GPIO3, rather than GPIO4, was the original electrical source.
    fresh(); WakePlatform::mask = 8; motionGpioLevel() = HIGH;
    const auto boot = captureBoot(); latch();
    const auto report = recover(true, peer, handle);
    assert(report.processed && report.ackSent && report.rxReady && !boot.wokeFromGpio(4));
    puts("PASS: GPIO4/Motion/timer/both masks, empty RX on motion, coincident retained packet preserved");
}


void appReceive(const Protocol::Message& packet)
{
    appPackets.push_back(packet);
    if (receipt && packet.type == Type::Event)
        receiptResult = submitAwake({1, Type::Ack, 501, local, Protocol::EventType::None, packet.messageId});
}
void serviceApplication()
{
    const auto began = hostUs;
    serviceAwake(peer, appReceive);
    assert(uint32_t(hostUs - began) < 110000);
    assert(csLevel == HIGH && !SPI.active);
}
void testApplicationRuntime()
{
    const Protocol::Message outbound{1, Type::Event, 88, local, Protocol::EventType::Heartbeat, 0};
    const Protocol::Message incomingAck{1, Type::Ack, 90, peer, Protocol::EventType::None, 88};
    fresh(); const auto began = hostUs;
    assert(submitAwake(outbound) == SubmitResult::Accepted);
    assert(uint32_t(hostUs - began) < 51000 && awakeBusy()); // No 300 ms ACK wait.
    assert(SPI.transmissions.size() == 1 && SPI.transmissions[0].size() == 9);
    assert(SPI.transmissions[0][0] == 8 && memcmp(&SPI.transmissions[0][1], &outbound, 8) == 0);
    assert(submitAwake(outbound) == SubmitResult::Busy && SPI.transmissions.size() == 1);
    hostUs += 10000; serviceApplication();
    assert(!awakeBusy() && SPI.registers[0x35] == 0x0D);
    const auto transmissions = SPI.transmissions;
    for (unsigned i = 0; i < 40; ++i) { hostUs += 10000; serviceApplication(); }
    assert(SPI.transmissions == transmissions); // No application retries in driver.
    latch(incomingAck); serviceApplication();
    assert(appPackets.size() == 1 && appPackets[0].ackForMessageId == 88 && !awakeBusy());

    fresh(); receipt = true; latch(); serviceApplication();
    assert(appPackets.size() == 1 && receiptResult == SubmitResult::Accepted && awakeBusy());
    assert(submitAwake(outbound) == SubmitResult::Busy); // Receipt has priority.
    assert(SPI.transmissions.size() == 1);
    Protocol::Message sent{}; memcpy(&sent, &SPI.transmissions[0][1], 8);
    assert(sent.type == Type::Ack && sent.ackForMessageId == event.messageId);
    hostUs += 10000; serviceApplication(); assert(!awakeBusy());
    assert(SPI.registers[0x35] == 0x0D);

    // The saved wake EVENT is never forwarded as general traffic, even after
    // other app packets. Preserve the exact saved receipt on every wake retry.
    fresh(); latch(); recover(true, peer, handle);
    const auto wakeReceipt = SPI.transmissions[0];
    auto newer = event; ++newer.messageId;
    latch(newer); serviceApplication(); assert(appPackets.size() == 1);
    latch(); serviceApplication(); assert(appPackets.size() == 1 && awakeBusy());
    assert(SPI.transmissions.back() == wakeReceipt && deliveries == 1);
    hostUs += 10000; serviceApplication(); assert(!awakeBusy());
    latch(incomingAck); serviceApplication(); assert(appPackets.size() == 2);

    for (unsigned bad = 0; bad < 10; ++bad)
    {
        fresh(); auto packet = event;
        if (bad == 0) packet.version = 2;
        if (bad == 1) packet.sender = local;
        if (bad == 2) packet.type = Type::SleepRequest;
        if (bad == 3) packet.event = Protocol::EventType::None;
        if (bad == 4) packet.ackForMessageId = 99;
        if (bad == 5) packet.type = Type::Ack; // ACK with heartbeat payload invalid.
        latch(packet);
        if (bad == 6) SPI.rxFifo[0] = 7;
        if (bad == 7) { SPI.rxFifo.resize(4); SPI.registers[0x3B] = 4; }
        if (bad == 8) { SPI.rxFifo.resize(11); SPI.registers[0x3B] = 11; }
        if (bad == 9) SPI.registers[0x3B] = 0x80;
        serviceApplication(); assert(appPackets.empty() && SPI.registers[0x35] == 0x0D);
    }
    // Refuse TX without consuming or overwriting a pending valid packet.
    fresh(); latch(); const auto retained = SPI.rxFifo;
    assert(submitAwake(outbound) == SubmitResult::Busy && SPI.rxFifo == retained && SPI.transmissions.empty());
    serviceApplication(); assert(appPackets.size() == 1);
    // Inject a CRC-valid frame precisely between preflight and SIDLE/readback.
    fresh(); bool injected = false;
    SPI.statusHook = [] {
        if (SPI.command == 0xFB && SPI.registers[0x35] == 1 && SPI.rxFifo.empty()) latch();
    };
    assert(submitAwake(outbound) == SubmitResult::Busy);
    injected = !SPI.rxFifo.empty(); SPI.statusHook = nullptr;
    assert(injected && SPI.transmissions.empty()); serviceApplication(); assert(appPackets.size() == 1);

    // Partially received packet at refusal: preserve while RX, bound a stuck RX.
    fresh(); SPI.rxFifo = {8, 1}; SPI.registers[0x3B] = 2; gdoLevel = LOW;
    assert(submitAwake(outbound) == SubmitResult::Busy && awakeBusy());
    serviceApplication(); assert(SPI.rxFifo.size() == 2 && appPackets.empty());
    hostUs += 50000; serviceApplication();
    assert(awakeState == AwakeState::Stopped && SPI.rxFifo.size() == 2);
    const auto stopped = SPI.commands;
    for (unsigned i = 0; i < 40; ++i) serviceApplication();
    assert(SPI.commands == stopped);

    fresh(); SPI.rxFifo = {8, 1}; SPI.registers[0x3B] = 2;
    assert(submitAwake(outbound) == SubmitResult::Busy);
    SPI.rxFifo.clear(); SPI.registers[0x3B] = 0; // Hardware rejected it and resumed RX.
    serviceApplication(); assert(!awakeBusy() && awakeState == AwakeState::Listening);
    fresh(); SPI.finishTx = false;
    assert(submitAwake(outbound) == SubmitResult::Accepted);
    hostUs += 200000; serviceApplication(); assert(!awakeBusy() && SPI.registers[0x35] == 0x0D);
    assert(SPI.transmissions.size() == 1);
    fresh(); assert(submitAwake(outbound) == SubmitResult::Accepted);
    SPI.registers[0x35] = 0x16; hostUs += 10000; serviceApplication();
    assert(SPI.registers[0x35] == 0x0D && !awakeBusy());
    fresh(); assert(submitAwake(outbound) == SubmitResult::Accepted);
    SPI.reachRx = false; hostUs += 10000; serviceApplication();
    assert(awakeState == AwakeState::Stopped && submitAwake(outbound) == SubmitResult::Failed);
    const auto failedCommands = SPI.commands;
    for (unsigned i = 0; i < 50; ++i) serviceApplication();
    assert(SPI.commands == failedCommands);
    fresh(); misoHigh = true; assert(submitAwake(outbound) == SubmitResult::Failed);
    assert(awakeState == AwakeState::Stopped && SPI.transmissions.empty());
    fresh(); SPI.registers[0x02] = 6; assert(submitAwake(outbound) == SubmitResult::Failed);
    assert(SPI.transmissions.empty());
    fresh(); latch(); SPI.reachRx = false; serviceApplication();
    assert(appPackets.size() == 1 && awakeState == AwakeState::Stopped); // Delivery survives RX failure.
    fresh(); latch(); SPI.failAfterFifo = true; serviceApplication();
    assert(appPackets.size() == 1 && awakeState == AwakeState::Stopped);
    fresh(); hostUs = UINT32_MAX - 5000;
    assert(submitAwake(outbound) == SubmitResult::Accepted);
    hostUs += 10000; serviceApplication(); assert(!awakeBusy() && SPI.registers[0x35] == 0x0D);
    // Rejected physical entry leaves the same runtime radio usable, no reset.
    fresh(); WakePlatform::failSetup = true; enterDeepSleep(save, guard, false);
    assert(submitAwake(outbound) == SubmitResult::Accepted);
    hostUs += 10000; serviceApplication(); assert(SPI.registers[0x35] == 0x0D);
    for (auto command : SPI.commands) assert(command != 0x30);
    puts("PASS: awake application TX/RX, receipt priority, wake retry precedence, FIFO races, failure cutoff and aborted sleep");
}

int main()
{
    testApplicationRuntime();
    testAwakeRetry();
    testWakeSources();
    fresh(); WakePlatform::deepReset = false;
    assert(!captureBoot().deep && SPI.commands.empty());
    WakePlatform::deepReset = true;
    for (bool duplicate : {false, true})
    {
        fresh(); latch(); duplicateEvent = duplicate;
        const auto boot = captureBoot();
        assert(boot.deep && boot.cause == Cause::Gpio && boot.gpioMask == 0x10 && boot.gdoAtBoot == HIGH);
        assert(SPI.commands.empty());
        const auto result = recover(true, peer, handle);
        assert(result.packetRecovered && result.ackSent && result.rxReady);
        assert(result.duplicate == duplicate && deliveries == unsigned(!duplicate) && callbacks == 1);
        assert(SPI.transmissions.size() == 1 && SPI.transmissions[0].size() == 9);
        Protocol::Message ack{}; memcpy(&ack, &SPI.transmissions[0][1], sizeof(ack));
        assert(ack.type == Type::Ack && ack.ackForMessageId == 70 && ack.messageId == 123);
        for (auto command : SPI.commands) assert(command != 0x30); // No SRES anywhere on wake.
        assert(!WakePlatform::held && !WakePlatform::deepHeld);
        assert(CC1101SleepArm::prepareForSleep().result == CC1101SleepArm::Result::Ready);
        printReport(boot, true, result);
        assert(Serial.log.find("CC1101_GPIO_WAKE=1") != std::string::npos);
    }
    for (uint8_t count : {uint8_t(0), uint8_t(65), uint8_t(127), uint8_t(0x80)})
    {
        fresh(); SPI.registers[0x3B] = count; SPI.registers[0x35] = 1; gdoLevel = HIGH;
        const auto began = hostUs;
        const auto result = recover(true, peer, handle);
        assert(!result.packetRecovered && !result.ackSent && callbacks == 0 && SPI.fifoReads == 0);
        assert(uint32_t(hostUs - began) < 110000 && result.rxReady);
    }
    fresh(); latch(); SPI.rxFifo[0] = 63;
    assert(!recover(true, peer, handle).packetRecovered && callbacks == 0);
    for (int bad = 0; bad < 5; ++bad)
    {
        fresh(); auto packet = event;
        if (bad == 0) packet.version = 2;
        if (bad == 1) packet.sender = local;
        if (bad == 2) packet.type = Type::SleepRequest;
        if (bad == 3) packet.event = Protocol::EventType::None;
        if (bad == 4) packet.ackForMessageId = 99;
        latch(packet); const auto result = recover(true, peer, handle);
        assert(result.packetRecovered && !result.ackSent && callbacks == 0 && result.rxReady);
    }
    fresh(); latch();
    assert(!recover(false, peer, handle).ackSent && callbacks == 0);
    fresh(); latch(); SPI.failAfterFifo = true;
    const auto failed = recover(true, peer, handle);
    assert(failed.packetRecovered && failed.processed && !failed.ackSent && !failed.rxReady);
    fresh(); latch(); SPI.finishTx = false;
    const auto began = hostUs;
    const auto stalled = recover(true, peer, handle);
    assert(stalled.processed && !stalled.ackSent && stalled.rxReady && uint32_t(hostUs - began) < 310000);
    fresh(); latch(); SPI.registers[0x08] ^= 1;
    assert(!recover(true, peer, handle).packetRecovered && SPI.fifoReads == 0 && gdoLevel == HIGH);
    for (auto command : SPI.commands) assert(command & 0x80);
    fresh(); latch(); SPI.registers[0x35] = 0x0D; // Packet still in progress.
    assert(!recover(true, peer, handle).packetRecovered && SPI.fifoReads == 0);
    for (auto command : SPI.commands) assert(command & 0x80);
    fresh(); latch(); WakePlatform::failRelease = true;
    assert(!recover(true, peer, handle).packetRecovered && SPI.commands.empty());
    fresh(); WakePlatform::cause = ESP_SLEEP_WAKEUP_TIMER;
    const auto timer = captureBoot(); const auto empty = recover(true, peer, handle);
    assert(timer.cause == Cause::Timer && timer.gpioMask == 0 && empty.rxReady && !empty.packetRecovered);
    for (auto command : SPI.commands) assert(command & 0x80);
    printReport(timer, true, empty);
    assert(Serial.log.find("CC1101_GPIO_WAKE=0") != std::string::npos);

    fresh(); WakePlatform::held = false; WakePlatform::deepHeld = false;
    highDuringSave = true;
    enterDeepSleep(save, guard, false);
    RtcState::History history{};
    assert(saves == 1 && !RtcState::load(history));
    assert(!WakePlatform::held && !WakePlatform::deepHeld && !WakePlatform::sources);
    assert(Serial.log.find("ABORTED") != std::string::npos);
    fresh(); WakePlatform::failSetup = true;
    enterDeepSleep(save, guard, false); assert(saves == 0 && !WakePlatform::held && !WakePlatform::sources);
    fresh();
    bool entered = false;
    try { enterDeepSleep(save, guard, false); } catch (const WakePlatform::Entered&) { entered = true; }
    assert(entered && saves == 1 && RtcState::load(history));
    assert(WakePlatform::held && WakePlatform::deepHeld && WakePlatform::sources && csLevel == HIGH);
    for (bool coordinated : {false, true})
    {
        fresh(); motionHighDuringSave = true;
        enterDeepSleep(save, guard, coordinated, finalFrame);
        assert(saves == 1 && !RtcState::load(history));
        assert(finalFrames == 0 && WakePlatform::sleepCalls == 0);
        assert(!WakePlatform::held && !WakePlatform::deepHeld && !WakePlatform::sources);
        assert(Serial.log.find("reason=MOTION_INT1_HIGH") != std::string::npos);
        for (unsigned blocked : {1U, 2U, 3U, 4U})
        {
            fresh(); blockAtGuard = blocked;
            enterDeepSleep(save, guard, coordinated, finalFrame);
            assert(saves == unsigned(blocked >= 3) && !RtcState::load(history));
            assert(finalFrames == unsigned(blocked == 4) && WakePlatform::sleepCalls == 0);
            assert(!WakePlatform::held && !WakePlatform::deepHeld && !WakePlatform::sources);
            assert(Serial.log.find("reason=TX_IN_FLIGHT") != std::string::npos);
            for (auto command : SPI.commands) assert(command & 0x80);
        }
        for (unsigned failure = 0; failure < 6; ++failure)
        {
            fresh();
            if (failure == 0) SPI.registers[0x02] = 6; // Arm failure.
            if (failure == 1) WakePlatform::failSetup = true;
            if (failure == 2) highDuringSave = true;
            if (failure == 3) WakePlatform::returnFromSleep = true;
            if (failure == 4) highDuringFrame = true;
            if (failure == 5) motionDuringFrame = true;
            enterDeepSleep(save, guard, coordinated, finalFrame);
            assert(saves == unsigned(failure >= 2) && !RtcState::load(history));
            assert(finalFrames == unsigned(failure >= 3));
            assert(WakePlatform::sleepCalls == unsigned(failure == 3));
            assert(!WakePlatform::held && !WakePlatform::deepHeld && !WakePlatform::sources);
            if (failure == 2) assert(Serial.log.find("reason=GDO_HIGH") != std::string::npos);
            if (failure == 3) assert(Serial.log.find("reason=DEEP_SLEEP_RETURNED") != std::string::npos);
            if (failure == 4) assert(Serial.log.find("reason=GDO_HIGH") != std::string::npos);
            if (failure == 5) assert(Serial.log.find("reason=MOTION_INT1_HIGH") != std::string::npos);
            for (auto command : SPI.commands) assert(command & 0x80);
        }
        fresh(); entered = false;
        WakePlatform::timerUs = 30000000; // A previously enabled timer must also be cleared.
        try { enterDeepSleep(save, guard, coordinated, finalFrame); } catch (const WakePlatform::Entered&) { entered = true; }
        assert(entered && saves == 1 && guardCalls == 4 && RtcState::load(history));
        assert(finalFrames == 1 && WakePlatform::sleepCalls == 1);
        assert(WakePlatform::held && WakePlatform::deepHeld && WakePlatform::sources);
        assert(WakePlatform::enabledGpioMask == ((1ULL << 3) | (1ULL << 4)));
        assert(WakePlatform::timerCalls == (coordinated ? 0U : 1U));
        assert(WakePlatform::timerUs == (coordinated ? 0U : 30000000U));
        assert((Serial.log.find("INTEGRATION SAFETY TIMER") != std::string::npos) == !coordinated);
        assert((Serial.log.find("timer=OFF") != std::string::npos) == coordinated);
        for (auto command : SPI.commands) assert(command & 0x80);
    }
    puts("PASS: retained FIFO before destructive strobes, bounds/format/RTC rejection, ACK/RX failures, timer, final-GDO abort and bench entry");
    puts("PASS: shared entry draws final frame once after arm/setup/RTC/guards, checks Motion/RX again after drawing, cleans up failures/returns; product GPIO3+GPIO4 only (no timer), bench retains 30s timer");
}
