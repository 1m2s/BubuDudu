#pragma once

#include <stdint.h>

// Application context only. No protocol policy, packet consumption or sleep.
namespace CC1101SleepArm
{
    enum class Result { Ready, RadioUnavailable, WrongConfig, NotInRx, GdoHigh, RxPending, RxOverflow };
    struct Report
    {
        Result result = Result::RadioUnavailable;
        uint8_t part = 0xFF;
        uint8_t version = 0xFF;
        uint8_t iocfg0 = 0xFF;
        uint8_t marc = 0xFF;
        uint8_t rxBytes = 0xFF;
        int gdo = -1;
    };

    // One cold-boot initialization, before ESP-NOW starts. Resets/configures
    // CC1101 only; deep-wake boot uses attachRetained() to preserve the FIFO.
    Result begin();
    // MCU SPI/pins only, after releasing CS hold; no CC1101 writes/strobes.
    void attachRetained();
    // Read-only readiness snapshot, also used awake; no reset/flush/FIFO read/RX restart.
    // At most 50 ms of radio polling, with no automatic recovery/retry episode.
    Report prepareForSleep();
    const char* toString(Result result);
}
