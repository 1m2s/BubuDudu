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
        int motionAtBoot = -1;
        bool wokeFromGpio(uint8_t pin) const
        { return deep && cause == Cause::Gpio && (gpioMask & (1ULL << pin)) != 0; }
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
    // Called once, before Serial/SPI. Only reads wake cause/mask and MCU GPIO4/GPIO3 inputs.
    BootInfo captureBoot();
    // Validated EVENT -> application dedup/history -> receipt packet for CC1101.
    using EventHandler = Protocol::Message (*)(const Protocol::Message&, bool& processed);
    // Inspect retained radio on ANY deep wake. Healthy empty RX is valid on
    // motion/timer wake; a coincident real RF packet is preserved/processed.
    Report recover(bool historyRestored, Protocol::DeviceId peer, EventHandler handler);
    using ReceiveHandler = void (*)(const Protocol::Message&);
    enum class SubmitResult { Accepted, Busy, Failed };
    // One runtime packet, no application ACK wait/retries. Acceptance starts TX;
    // serviceAwake polls completion and performs one bounded RX restart.
    SubmitResult submitAwake(const Protocol::Message& packet);
    // Loop-owned, single FIFO consumer. Saved wake receipts take precedence;
    // other validated EVENT/ACK packets go to the optional application handler.
    // Handler runs after FIFO copy/RX restoration and may submit a receipt ACK.
    // Failed RX restart/unknown radio disables service until reboot.
    void serviceAwake(Protocol::DeviceId peer, ReceiveHandler handler = nullptr);
    bool awakeBusy(); // TX/restoration or asserted packet latch; no competing owner.
    void printReport(const BootInfo& boot, bool historyRestored, const Report& report);
    // Shared manual/coordinated physical entry. Guard returns nullptr when
    // drained, otherwise a diagnostic reason. Success does not return; ANY
    // return is a refusal/abort. Saves only at the final entry boundary.
    // Caller prepares Motion first; guard must also reject asserted GPIO3.
    // Product sleep uses GPIO wake only; bench entry retains the 30s safety timer.
    // Optional presentation callback runs only after successful arm/setup/final
    // checks; guards are checked again after its framebuffer transfer.
    void enterDeepSleep(void (*saveHistory)(), const char* (*blockedReason)(), bool coordinated,
                        void (*beforeSleep)() = nullptr);
}
