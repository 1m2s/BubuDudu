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
}
using Type = Protocol::MessageType;
using Device = Protocol::DeviceId;
using namespace CC1101WakeRecovery;
unsigned deliveries = 0, callbacks = 0, saves = 0;
bool duplicateEvent = false, highDuringSave = false;
unsigned guardCalls = 0, blockAtGuard = 0;
const char* guard()
{
    ++guardCalls;
    return blockAtGuard && guardCalls >= blockAtGuard ? "TX_IN_FLIGHT" : nullptr;
}
const Protocol::Message event{1, Type::Event, 70, Device::Dudu, Protocol::EventType::Heartbeat, 0};

Protocol::Message handle(const Protocol::Message& packet, bool& processed)
{
    assert(packet.messageId == 70 && SPI.fifoReads == 9);
    // No reset/flush/configuration before application sees the recovered bytes.
    for (auto command : SPI.commands) assert(command & 0x80);
    ++callbacks; processed = !duplicateEvent;
    if (processed) ++deliveries;
    return {1, Type::Ack, 123, Device::Bubu, Protocol::EventType::None, packet.messageId};
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
    deliveries = 0; callbacks = 0; saves = 0; duplicateEvent = false; highDuringSave = false;
    Serial.log.clear(); RtcState::invalidate(); guardCalls = blockAtGuard = 0;
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
}

int main()
{
    fresh(); WakePlatform::deepReset = false;
    assert(!captureBoot().deep && SPI.commands.empty());
    WakePlatform::deepReset = true;
    for (bool duplicate : {false, true})
    {
        fresh(); latch(); duplicateEvent = duplicate;
        const auto boot = captureBoot();
        assert(boot.deep && boot.cause == Cause::Gpio && boot.gpioMask == 0x10 && boot.gdoAtBoot == HIGH);
        assert(SPI.commands.empty());
        const auto result = recover(true, Device::Dudu, handle);
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
        const auto result = recover(true, Device::Dudu, handle);
        assert(!result.packetRecovered && !result.ackSent && callbacks == 0 && SPI.fifoReads == 0);
        assert(uint32_t(hostUs - began) < 110000 && result.rxReady);
    }
    fresh(); latch(); SPI.rxFifo[0] = 63;
    assert(!recover(true, Device::Dudu, handle).packetRecovered && callbacks == 0);
    for (int bad = 0; bad < 5; ++bad)
    {
        fresh(); auto packet = event;
        if (bad == 0) packet.version = 2;
        if (bad == 1) packet.sender = Device::Bubu;
        if (bad == 2) packet.type = Type::SleepRequest;
        if (bad == 3) packet.event = Protocol::EventType::None;
        if (bad == 4) packet.ackForMessageId = 99;
        latch(packet); const auto result = recover(true, Device::Dudu, handle);
        assert(result.packetRecovered && !result.ackSent && callbacks == 0 && result.rxReady);
    }
    fresh(); latch();
    assert(!recover(false, Device::Dudu, handle).ackSent && callbacks == 0);
    fresh(); latch(); SPI.failAfterFifo = true;
    const auto failed = recover(true, Device::Dudu, handle);
    assert(failed.packetRecovered && failed.processed && !failed.ackSent && !failed.rxReady);
    fresh(); latch(); SPI.finishTx = false;
    const auto began = hostUs;
    const auto stalled = recover(true, Device::Dudu, handle);
    assert(stalled.processed && !stalled.ackSent && stalled.rxReady && uint32_t(hostUs - began) < 310000);
    fresh(); latch(); SPI.registers[0x08] ^= 1;
    assert(!recover(true, Device::Dudu, handle).packetRecovered && SPI.fifoReads == 0 && gdoLevel == HIGH);
    for (auto command : SPI.commands) assert(command & 0x80);
    fresh(); latch(); SPI.registers[0x35] = 0x0D; // Packet still in progress.
    assert(!recover(true, Device::Dudu, handle).packetRecovered && SPI.fifoReads == 0);
    for (auto command : SPI.commands) assert(command & 0x80);
    fresh(); latch(); WakePlatform::failRelease = true;
    assert(!recover(true, Device::Dudu, handle).packetRecovered && SPI.commands.empty());
    fresh(); WakePlatform::cause = ESP_SLEEP_WAKEUP_TIMER;
    const auto timer = captureBoot(); const auto empty = recover(true, Device::Dudu, handle);
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
        for (unsigned blocked : {1U, 2U, 3U})
        {
            fresh(); blockAtGuard = blocked;
            enterDeepSleep(save, guard, coordinated);
            assert(saves == unsigned(blocked == 3) && !RtcState::load(history));
            assert(!WakePlatform::held && !WakePlatform::deepHeld && !WakePlatform::sources);
            assert(Serial.log.find("reason=TX_IN_FLIGHT") != std::string::npos);
            for (auto command : SPI.commands) assert(command & 0x80);
        }
        for (unsigned failure = 0; failure < 4; ++failure)
        {
            fresh();
            if (failure == 0) SPI.registers[0x02] = 6; // Arm failure.
            if (failure == 1) WakePlatform::failSetup = true;
            if (failure == 2) highDuringSave = true;
            if (failure == 3) WakePlatform::returnFromSleep = true;
            enterDeepSleep(save, guard, coordinated);
            assert(saves == unsigned(failure >= 2) && !RtcState::load(history));
            assert(!WakePlatform::held && !WakePlatform::deepHeld && !WakePlatform::sources);
            if (failure == 2) assert(Serial.log.find("reason=GDO_HIGH") != std::string::npos);
            if (failure == 3) assert(Serial.log.find("reason=DEEP_SLEEP_RETURNED") != std::string::npos);
            for (auto command : SPI.commands) assert(command & 0x80);
        }
        fresh(); entered = false;
        try { enterDeepSleep(save, guard, coordinated); } catch (const WakePlatform::Entered&) { entered = true; }
        assert(entered && saves == 1 && guardCalls == 3 && RtcState::load(history));
        assert(WakePlatform::held && WakePlatform::deepHeld && WakePlatform::sources);
        assert(Serial.log.find("INTEGRATION SAFETY TIMER") != std::string::npos);
        for (auto command : SPI.commands) assert(command & 0x80);
    }
    puts("PASS: retained FIFO before destructive strobes, bounds/format/RTC rejection, ACK/RX failures, timer, final-GDO abort and bench entry");
    puts("PASS: shared manual/coordinated entry, final RTC boundary, transport rechecks, setup/GDO/API-return cleanup");
}
