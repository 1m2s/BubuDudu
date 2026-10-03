#pragma once

#include "Motion.h"
#include "ESPNowRadio.h"
namespace MotionRuntime
{
    enum class MovementState : uint8_t { READY, MOVING, WAITING };
    enum class ProximityUpdateState : uint8_t { READY, CHECKING };
    enum class ProximityClassification : uint8_t { UNKNOWN, CLOSE, FAR };
    void resetBootState();
    void begin();
    bool ready();
    MovementState movement();
    ProximityClassification classification();
    ProximityUpdateState checkState();
    bool probePending();
    void resetMovement();
    void serviceMotion();
    void checkProximityEligibility();
    void startProximityCheck(uint32_t now);
    void cancelProximityCheck(const char* reason);
    void serviceProximityProbe(uint32_t now);
    void serviceRssiObservations();
    bool prepareForSleep();
    bool cancelSleepPreparation();
#ifdef DEVICE_DUDU
    void reportStartupI2cHealth(uint8_t oledAddress);
#endif
}
