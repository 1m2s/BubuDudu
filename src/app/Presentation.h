#pragma once

#include <stdint.h>
namespace Presentation
{
    void resetBootState();
    void begin();
    void updateLed(uint32_t now);
    void serviceFarHeartbeat();
    bool backgroundHeartbeatAllowed();
    void requestBackgroundHeartbeat();
    void requestUserHeartbeat();
    bool userHeartbeatActive();
    void deferUserHeartbeat(uint16_t messageId);
    void off();
    void showDeepSleepStatus();
    void serviceDisplayStatus();
} // namespace Presentation
