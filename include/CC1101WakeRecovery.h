#pragma once
#include "Protocol.h"
#include "CC1101SleepArm.h"

namespace CC1101WakeRecovery
{
    enum class Cause { Cold, Gpio, Timer, Other };
    struct BootInfo
    {
        bool deep = false;
        Cause cause = Cause::Cold;
        uint64_t gpioMask = 0;
        int gdoAtBoot = -1;
    };
    struct Report
    {
        CC1101SleepArm::Report radio;
        const char* reason = "NOT_ATTEMPTED";
        bool packetRecovered = false;
        bool processed = false;
        bool duplicate = false;
        bool ackSent = false;
        bool rxReady = false;
    };
    // Called once, before Serial/SPI. Only reads wake cause and MCU GDO input.
    BootInfo captureBoot();
    // Validated EVENT -> application dedup/history -> receipt packet for CC1101.
    using EventHandler = Protocol::Message (*)(const Protocol::Message&, bool& processed);
    Report recover(bool historyRestored, Protocol::DeviceId peer, EventHandler handler);
    void printReport(const BootInfo& boot, bool historyRestored, const Report& report);
    // Explicit x command only. Caller checks runtime/transport are quiescent.
    // Saves at final entry boundary; abort invalidates RTC and releases holds.
    void benchDeepSleep(void (*saveHistory)());
}
