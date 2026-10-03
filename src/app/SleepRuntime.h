#pragma once

#include "CC1101WakeRecovery.h"
namespace SleepRuntime
{
    constexpr uint32_t AUTOMATIC_SLEEP_INACTIVITY_MS = 35000;
    constexpr uint32_t AUTOMATIC_SLEEP_PEER_GRACE_MS = 3000;
    void captureBoot();
    const CC1101WakeRecovery::BootInfo &boot();
    void resetBootState();
    void beginWake();
    void finishStartup();
    void noteLocalActivity(uint32_t now);
    bool productSleepEligible(uint32_t now, uint32_t requiredInactivityMs = AUTOMATIC_SLEEP_INACTIVITY_MS);
    void consumeAutomaticOpportunity();
    bool draining();
    void serviceAutomaticSleep();
    void serviceSleepExecution();
} // namespace SleepRuntime
