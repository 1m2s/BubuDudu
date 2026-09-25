#pragma once
#include "Arduino.h"
#include <array>

constexpr int HIGH = 1, LOW = 0, OUTPUT = 1, INPUT = 0, INPUT_PULLDOWN = 2;
constexpr int MSBFIRST = 1, SPI_MODE0 = 0;
extern uint32_t hostUs;
extern bool misoHigh;
extern int gdoLevel, csLevel;
inline uint32_t micros() { hostUs += 10; return hostUs; }
inline void delayMicroseconds(uint32_t us) { hostUs += us; }
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int value) { assert(pin == 10); csLevel = value; }
inline int& motionGpioLevel() { static int level = LOW; return level; }
inline int digitalRead(int pin) { return pin == 20 ? int(misoHigh) : pin == 3 ? motionGpioLevel() : gdoLevel; }
struct SPISettings { SPISettings(uint32_t, int, int) {} };

struct HostSPI
{
    std::array<uint8_t, 64> registers{};
    std::vector<uint8_t> commands;
    bool active = false, first = true, reachRx = true;
    int corruptWrite = -1;
    std::deque<uint8_t> rxFifo;
    std::vector<uint8_t> txFifo;
    std::vector<std::vector<uint8_t>> transmissions;
    unsigned fifoReads = 0;
    bool finishTx = true, failAfterFifo = false;
    uint8_t command = 0;
    unsigned statusReads = 0;
    unsigned injectPacketAt = 0;
    void (*statusHook)() = nullptr;
    uint32_t txAtUs = 0;
    void begin(int sck, int miso, int mosi, int cs)
    {
        assert(sck == 6 && miso == 20 && mosi == 7 && cs == 10);
    }
    void beginTransaction(const SPISettings&) { assert(!active); active = true; first = true; }
    void endTransaction() { assert(active); active = false; }
    uint8_t transfer(uint8_t value)
    {
        assert(active && csLevel == LOW);
        hostUs += 80; // Eight clocks at the real 100 kHz SPI rate.
        if (first)
        {
            first = false; command = value; commands.push_back(value);
            if (value == 0x30) // SRES
            {
                registers.fill(0);
                registers[0x31] = 0x14; registers[0x35] = 1;
            }
            if (value == 0x34 && reachRx)
            {
                registers[0x35] = 0x0D;
                registers[0x23] = 0xEA; // Calibration changes the seed.
            }
            if (value == 0x36) registers[0x35] = 1;
            if (value == 0x3A) { rxFifo.clear(); registers[0x3B] = 0; gdoLevel = LOW; }
            if (value == 0x3B) txFifo.clear();
            if (value == 0x35)
            {
                transmissions.push_back(txFifo);
                txAtUs = hostUs;
                registers[0x35] = finishTx ? 1 : 0x13;
            }
            return 0;
        }
        if (command == 0xFF)
        {
            assert(!rxFifo.empty());
            const auto byte = rxFifo.front(); rxFifo.pop_front(); ++fifoReads;
            registers[0x3B] = static_cast<uint8_t>(rxFifo.size()); gdoLevel = LOW;
            if (rxFifo.empty() && failAfterFifo) misoHigh = true;
            return byte;
        }
        if (command == 0x7F) { txFifo.push_back(value); return 0; }
        const uint8_t address = command & 0x3F;
        if (command & 0x80)
        {
            if (address >= 0x30) assert((command & 0xC0) == 0xC0);
            if (address == 0x35 || address == 0x3B)
            {
                if (statusHook) statusHook();
                ++statusReads;
                if (injectPacketAt && statusReads == injectPacketAt)
                {
                    registers[0x35] = 1; registers[0x3B] = 9; gdoLevel = HIGH;
                }
            }
            return registers[address];
        }
        registers[address] = address == corruptWrite ? uint8_t(value ^ 1) : value;
        return 0;
    }
};
extern HostSPI SPI;
