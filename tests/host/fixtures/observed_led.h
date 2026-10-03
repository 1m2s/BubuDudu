#pragma once
#include "LED.h"
// Count application requests while executing the production LED state machine.
struct ObservedLED : LED
{
    unsigned requests = 0, userRequests = 0;
    void requestHeartbeat() { ++requests; LED::requestHeartbeat(); }
    void requestUserHeartbeat() { ++requests; ++userRequests; LED::requestUserHeartbeat(); }
};
