#pragma once
#include "Arduino.h"
#include <array>
#include <utility>
#define IRAM_ATTR
constexpr int LOW = 0, HIGH = 1, INPUT = 0, RISING = 1;
namespace MotionPlatform
{
    extern bool isrAttached, stuckHigh;
    extern void (*isr)();
}
inline void pinMode(int pin, int mode) { assert(pin == 3 && mode == INPUT); }
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int pin, void (*handler)(), int mode)
{ assert(pin == 3 && mode == RISING); MotionPlatform::isr = handler; MotionPlatform::isrAttached = true; }
inline void detachInterrupt(int pin)
{ assert(pin == 3); MotionPlatform::isrAttached = false; MotionPlatform::isr = nullptr; }
struct HostWire
{
    std::array<uint8_t, 64> registers{};
    std::vector<uint8_t> tx;
    std::vector<std::pair<uint8_t, uint8_t>> writes;
    unsigned operations = 0, failAt = 0;
    int corruptRegister = -1;
    uint8_t reg = 0, received = 0;
    bool hasByte = false;
    bool step() { ++hostNow; return ++operations != failAt; }
    void begin(uint8_t sda, uint8_t scl) { assert(sda == 0 && scl == 1); }
    void beginTransmission(uint8_t address) { assert(address == 0x53); tx.clear(); }
    void write(uint8_t value) { tx.push_back(value); }
    int endTransmission(bool = true)
    {
        if (!step()) return 4;
        assert(tx.size() == 1 || tx.size() == 2);
        reg = tx[0];
        if (tx.size() == 2)
        {
            writes.emplace_back(reg, tx[1]);
            registers[reg] = reg == corruptRegister ? uint8_t(tx[1] ^ 1) : tx[1];
        }
        return 0;
    }
    uint8_t requestFrom(uint8_t address, uint8_t count)
    {
        assert(address == 0x53 && count == 1);
        hasByte = step();
        if (!hasByte) return 0;
        received = registers[reg];
        if (reg == 0x30) registers[reg] = 0; // Latched INT_SOURCE clears on read.
        return 1;
    }
    int available() { return hasByte ? 1 : 0; }
    uint8_t read() { assert(hasByte); hasByte = false; return received; }
};
extern HostWire Wire;
inline int digitalRead(int pin)
{
    assert(pin == 3);
    return MotionPlatform::stuckHigh || (Wire.registers[0x30] & Wire.registers[0x2E] & ~Wire.registers[0x2F]);
}
