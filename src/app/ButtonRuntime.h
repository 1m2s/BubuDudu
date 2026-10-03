#pragma once

namespace ButtonRuntime
{
    void beginButton();
    void serviceButton();
    void serviceButtonHeartbeat();
    void failButtonWake(const char* reason);
    const char* buttonSleepBlockedReason();
    bool pending();
    bool wakeIntentHeld();
}
