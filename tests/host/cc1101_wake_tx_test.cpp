#include "cc1101/SPI.h"
#include "../../src/CC1101SleepArm.cpp"
#include "../../src/CC1101WakeTx.cpp"
#include <algorithm>

uint32_t hostNow = 0, hostUs = 0;
HostSerial Serial;
HostSPI SPI;
bool misoHigh = false;
int gdoLevel = LOW, csLevel = HIGH;
using Type = Protocol::MessageType;
using Device = Protocol::DeviceId;
using namespace CC1101WakeTx;
#ifdef DEVICE_BUBU
constexpr Device local = Device::Bubu, peer = Device::Dudu;
#else
constexpr Device local = Device::Dudu, peer = Device::Bubu;
#endif
const Protocol::Message event{1, Type::Event, 123, local, Protocol::EventType::Heartbeat, 0};
const Protocol::Message ack{1, Type::Ack, 50, peer, Protocol::EventType::None, 123};
std::vector<uint8_t> reply;
unsigned replyAttempt = 1, deliveredAttempt = 0;
bool injectOverflow = false, failRestart = false, transientRestart = false;

std::vector<uint8_t> frame(const Protocol::Message& packet)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(&packet);
    std::vector<uint8_t> raw{sizeof(packet)};
    raw.insert(raw.end(), bytes, bytes + sizeof(packet));
    return raw;
}
void inject()
{
    if (transientRestart && !SPI.reachRx &&
        std::count(SPI.commands.begin(), SPI.commands.end(), uint8_t(0x3A)))
    {
        SPI.reachRx = true;
        SPI.registers[0x35] = 0x0D;
    }
    const auto attempt = SPI.transmissions.size();
    if (SPI.registers[0x35] != 0x0D || attempt < replyAttempt || attempt == deliveredAttempt ||
        uint32_t(hostUs - SPI.txAtUs) < 6000 || (reply.empty() && !injectOverflow)) return;
    deliveredAttempt = attempt;
    SPI.rxFifo.assign(reply.begin(), reply.end());
    SPI.registers[0x35] = injectOverflow ? 0x11 : 1;
    SPI.registers[0x3B] = injectOverflow ? 0x80 : static_cast<uint8_t>(reply.size());
    gdoLevel = HIGH;
    if (failRestart) SPI.reachRx = false;
}
void fresh()
{
    SPI = HostSPI{}; hostUs = 0; misoHigh = false; gdoLevel = LOW;
    SPI.registers[0x31] = 0x14; SPI.registers[0x35] = 1;
    assert(CC1101SleepArm::begin() == CC1101SleepArm::Result::Ready);
    SPI.commands.clear(); Serial.log.clear();
    SPI.statusHook = inject; reply = frame(ack); replyAttempt = 1; deliveredAttempt = 0;
    injectOverflow = failRestart = transientRestart = false;
}
void checkSafety(uint32_t began)
{
    assert(uint32_t(hostUs - began) < 2300000); // Includes failure/recovery SPI budgets.
    assert(!SPI.active && csLevel == HIGH);
    assert(std::count(SPI.commands.begin(), SPI.commands.end(), uint8_t(0x30)) == 0); // No SRES.
    assert(std::count(SPI.commands.begin(), SPI.commands.end(), uint8_t(0x3A)) <= 1);
    assert(SPI.transmissions.size() <= 3);
    for (const auto& sent : SPI.transmissions) assert(sent == frame(event)); // Same ID AND payload.
}
Report run()
{
    const auto began = hostUs;
    const auto report = send(event, peer);
    checkSafety(began);
    return report;
}
int main()
{
    for (unsigned attempt = 1; attempt <= 3; ++attempt)
    {
        fresh(); replyAttempt = attempt;
        const auto result = run();
        assert(result.result == Result::Acked && result.attempts == attempt && result.rxReady);
        assert(SPI.transmissions.size() == attempt && SPI.registers[0x35] == 0x0D);
        assert(SPI.registers[0x3E] == 0x60 && SPI.fifoReads == 9);
        assert(std::count(SPI.commands.begin(), SPI.commands.end(), uint8_t(0x3A)) == 0);
    }
    fresh(); reply.clear(); auto began = hostUs; auto result = run();
    assert(result.result == Result::AckTimeout && result.attempts == 3 && result.rxReady);
    assert(uint32_t(hostUs - began) >= 900000 && uint32_t(hostUs - began) < 950000);
    // micros rollover must not change the deadline arithmetic.
    fresh(); reply.clear(); hostUs = 0xFFFF0000; result = run();
    assert(result.result == Result::AckTimeout && result.attempts == 3 && result.rxReady);
    for (unsigned bad = 0; bad < 8; ++bad)
    {
        fresh(); auto wrong = ack;
        switch (bad)
        {
            case 0: ++wrong.ackForMessageId; break;
            case 1: wrong.sender = local; break;
            case 2: ++wrong.version; break;
            case 3: wrong.type = Type::SleepAck; break;
            case 4: wrong.event = Protocol::EventType::Heartbeat; break;
        }
        reply = frame(wrong);
        if (bad == 5) reply[0] = 7;
        if (bad == 6) reply.resize(5);
        if (bad == 7) reply.resize(64);
        result = run();
        assert(result.result == Result::InvalidAck && result.attempts == 3 && result.rxReady);
    }
    // Retained complete/in-progress data or overflow at entry: read-only refusal.
    for (uint8_t count : {uint8_t(9), uint8_t(65), uint8_t(0x80), uint8_t(0xFF)})
    {
        fresh(); SPI.rxFifo.assign(reply.begin(), reply.end()); SPI.registers[0x3B] = count;
        const auto retained = SPI.rxFifo; result = run();
        assert(result.result != Result::Acked && result.attempts == 0 && SPI.rxFifo == retained);
        assert(SPI.fifoReads == 0 && SPI.transmissions.empty());
        for (auto command : SPI.commands) assert(command & 0x80);
    }
    fresh(); SPI.injectPacketAt = SPI.statusReads + 5; // After preflight, before TX.
    result = run(); assert(result.result == Result::Busy && !result.rxReady && SPI.transmissions.empty() && SPI.fifoReads == 0);
    fresh(); misoHigh = true; result = run();
    assert(result.result == Result::RadioUnavailable && result.attempts == 0);
    fresh(); SPI.registers[0x02] = 6; result = run();
    assert(result.result == Result::RadioUnavailable && result.attempts == 0);
    fresh(); SPI.finishTx = false; reply.clear(); result = run();
    assert(result.result == Result::TxFailed && result.attempts == 2 && !result.rxReady);
    assert(SPI.transmissions.size() == 2); // Second TX failure exhausts the ONE recovery budget.
    fresh(); SPI.reachRx = false; reply.clear(); result = run();
    assert(result.result == Result::RadioUnavailable && result.attempts == 1 && !result.rxReady);
    fresh(); SPI.reachRx = false; transientRestart = true; result = run();
    assert(result.result == Result::Acked && result.rxReady && result.attempts == 1);
    fresh(); injectOverflow = true; reply.clear(); result = run();
    assert(result.result == Result::InvalidAck && !result.rxReady && result.attempts == 2);
    fresh(); failRestart = true; result = run();
    assert(result.result == Result::Acked && !result.rxReady && result.attempts == 1);
    fresh(); SPI.failAfterFifo = true; result = run();
    assert(result.result == Result::Acked && !result.rxReady && result.attempts == 1);
    puts("PASS: CC1101 wake TX matching/rejected ACKs, same-ID max-two retries, bounded timeout/rollover, RX return");
    puts("PASS: retained FIFO refusal, no reset, TX/RX/SPI faults, single recovery budget, ACK preserved on RX failure");
}
