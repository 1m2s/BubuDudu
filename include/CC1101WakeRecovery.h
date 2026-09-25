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
    // Loop-owned, GDO-triggered service for retries of the boot-accepted EVENT.
    // No application delivery; ACK completion is polled across loop iterations.
    // Failed RX restart/unknown radio disables service until reboot.
    void serviceAwake(Protocol::DeviceId peer);
    bool awakeBusy(); // Prevent manual TX or physical sleep during this ACK.
    void printReport(const BootInfo& boot, bool historyRestored, const Report& report);
    // Shared manual/coordinated physical entry. Guard returns nullptr when
    // drained, otherwise a diagnostic reason. Success does not return; ANY
    // return is a refusal/abort. Saves only at the final entry boundary.
    void enterDeepSleep(void (*saveHistory)(), const char* (*blockedReason)(), bool coordinated);
}
