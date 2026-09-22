#pragma once

namespace RadioTask
{
    // First operation in setup(): capture deep-boot evidence, no radio SPI.
    // Returns true only for an ESP32 deep-sleep reset.
    bool captureBootWake();
    bool begin();
}
