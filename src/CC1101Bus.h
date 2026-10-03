#pragma once
// Internal, single-owner SPI primitives shared by cold arm and retained wake.
// No radio reset/configuration/flush occurs implicitly in these operations.
#include <Arduino.h>
#include <SPI.h>

namespace CC1101Bus
{
constexpr uint8_t CS = 10, SCK = 6, MOSI = 7, MISO = 20, GDO0 = 4;
constexpr uint32_t BUDGET_US = 50000, READY_WAIT_US = 2000;
inline const SPISettings& settings()
{
    static const SPISettings value(100000, MSBFIRST, SPI_MODE0);
    return value;
}

inline void setupPins()
{
    digitalWrite(CS, HIGH);
    pinMode(CS, OUTPUT);
    pinMode(GDO0, INPUT_PULLDOWN);
    SPI.begin(SCK, MISO, MOSI, CS);
}

inline bool withinBudget(uint32_t started)
{
    return uint32_t(micros() - started) < BUDGET_US;
}

inline bool waitReady(uint32_t started)
{
    const uint32_t waitStarted = micros();
    while (digitalRead(MISO) == HIGH)
    {
        if (!withinBudget(started) || uint32_t(micros() - waitStarted) >= READY_WAIT_US)
            return false;
        delayMicroseconds(10);
    }
    return withinBudget(started);
}

inline void releaseBus()
{
    digitalWrite(CS, HIGH);
    SPI.endTransaction();
}

inline bool select(uint32_t started)
{
    if (!withinBudget(started)) return false;
    SPI.beginTransaction(settings());
    digitalWrite(CS, LOW);
    if (waitReady(started)) return true;
    releaseBus();
    return false;
}

inline bool read(uint8_t address, uint8_t& value, uint32_t started)
{
    if (!select(started)) return false;
    // Status addresses need the burst bit even for a single byte.
    SPI.transfer(address | (address >= 0x30 ? 0xC0 : 0x80));
    value = SPI.transfer(0);
    releaseBus();
    return withinBudget(started);
}

}
