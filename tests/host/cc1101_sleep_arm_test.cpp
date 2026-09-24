// Actual arm implementation against SPI/GPIO/time substitutes. This verifies
// register decisions and non-destructive behavior, not RF or physical wiring.
#include "cc1101/SPI.h"
#include "../../src/CC1101SleepArm.cpp"

uint32_t hostNow = 0, hostUs = 0;
HostSerial Serial;
HostSPI SPI;
bool misoHigh = false;
int gdoLevel = LOW, csLevel = HIGH;
using namespace CC1101SleepArm;

void freshRadio()
{
    SPI = HostSPI{};
    SPI.registers[0x31] = 0x14;
    SPI.registers[0x35] = 1;
    misoHigh = false; gdoLevel = LOW; hostUs = 0;
}

void check(Result expected)
{
    const auto before = SPI.registers;
    SPI.commands.clear();
    const uint32_t started = hostUs;
    const auto report = prepareForSleep();
    assert(report.result == expected);
    assert(uint32_t(hostUs - started) < 51000);
    assert(csLevel == HIGH && !SPI.active);
    assert(SPI.registers == before);
    for (auto command : SPI.commands) assert(command & 0x80); // Reads only.
    assert(std::string(toString(report.result)) != "INVALID");
}

int main()
{
    freshRadio(); assert(begin() == Result::Ready);
    assert(hostUs < 51000 && csLevel == HIGH && !SPI.active);
    assert(SPI.registers[0x02] == 7 && SPI.registers[0x3B] == 0);
    check(Result::Ready);
    // Every stable programmed field participates in readiness validation.
    for (const auto& setting : profile)
    {
        if (setting.address >= 0x23 && setting.address <= 0x26) continue;
        SPI.registers[setting.address] ^= 1;
        check(Result::WrongConfig);
        SPI.registers[setting.address] ^= 1;
    }
    gdoLevel = HIGH; check(Result::GdoHigh); gdoLevel = LOW;
    SPI.registers[0x3B] = 9; check(Result::RxPending);
    SPI.registers[0x3B] = 0x80; check(Result::RxOverflow);
    SPI.registers[0x3B] = 0;
    SPI.registers[0x35] = 1; check(Result::NotInRx);
    SPI.registers[0x35] = 0x0D;
    SPI.registers[0x30] = 0xFF; check(Result::RadioUnavailable);
    SPI.registers[0x30] = 0;
    SPI.registers[0x31] = 0; check(Result::RadioUnavailable);
    SPI.registers[0x31] = 0x14;
    misoHigh = true; check(Result::RadioUnavailable); misoHigh = false;
    check(Result::Ready); // Failure did not reset or disable a healthy radio.
    hostUs = UINT32_MAX - 1000; check(Result::Ready); // Wrap-safe polling budget.

    // Packet arriving during the second status sample must refuse readiness.
    SPI.statusReads = 0; SPI.injectPacketAt = 3;
    SPI.commands.clear();
    assert(prepareForSleep().result == Result::GdoHigh);
    assert(SPI.registers[0x3B] == 9 && gdoLevel == HIGH);
    for (auto command : SPI.commands) assert(command & 0x80);
    SPI.injectPacketAt = 0; check(Result::GdoHigh); // Packet remains latched.

    freshRadio(); misoHigh = true;
    assert(begin() == Result::RadioUnavailable && hostUs < 3000);
    check(Result::RadioUnavailable); // No automatic initialization retry.
    assert(SPI.commands.empty());
    freshRadio(); SPI.corruptWrite = 0x08;
    assert(begin() == Result::WrongConfig && hostUs < 51000);
    check(Result::RadioUnavailable);
    freshRadio(); SPI.reachRx = false;
    assert(begin() == Result::NotInRx && hostUs < 51000);
    assert(csLevel == HIGH && !SPI.active);
    check(Result::RadioUnavailable);
    puts("PASS: CC1101 profile/readback, FIFO/GDO preservation, RX/identity faults, bounded SPI/RX waits, rollover, no arm strobes");
}
