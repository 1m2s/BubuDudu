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
    // CC1101 only; NOT suitable for a future deep-wake boot with retained FIFO.
    Result begin();
    // MCU SPI/pins only, after releasing CS hold; no CC1101 writes/strobes.
    void attachRetained();
    // Read-only readiness snapshot; never reset, flush, read FIFO or restart RX.
    // At most 50 ms of radio polling, with no automatic recovery/retry episode.
    Report prepareForSleep();
    const char* toString(Result result);
}
