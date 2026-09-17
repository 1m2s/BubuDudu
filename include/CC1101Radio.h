#pragma once

#include <Arduino.h>

namespace CC1101Radio
{
    bool begin();
    bool reset();

    uint8_t readRegister(uint8_t address);
    void writeRegister(uint8_t address, uint8_t value);

    uint8_t readPartNumber();
    uint8_t readVersion();
}