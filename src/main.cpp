#include <Arduino.h>
#include "PowerManager.h"
#include "app/ButtonRuntime.h"
#include "app/MotionRuntime.h"
#include "app/Presentation.h"
#include "app/RadioRuntime.h"
#include "app/SleepRuntime.h"

void setup()
{
    SleepRuntime::captureBoot(); // Earliest: before Serial, SPI or I2C.
    Presentation::resetBootState();
    RadioRuntime::resetBootState();
    MotionRuntime::resetBootState();
    SleepRuntime::resetBootState();
    Serial.begin(115200);
    SleepRuntime::beginWake(); // Recover/ACK retained FIFO before sensor bus work.
    MotionRuntime::begin();    // Sole shared-I2C initializer.
    Presentation::begin();     // OLED, then LED, then retained user animation.
    ButtonRuntime::beginButton();
    PowerManager::printStatus(millis());
    if (!RadioRuntime::begin())
    {
        ButtonRuntime::failButtonWake("RUNTIME_INIT_FAILED");
        return;
    }
    SleepRuntime::finishStartup(); // One-shot peer wake, bootstrap proximity, inactivity clock.
}

void loop()
{
    Presentation::updateLed(uint32_t(millis()));
    MotionRuntime::checkProximityEligibility();
    // Observe boundaries before activity/deadlines can return the FSM to ACTIVE.
    if (PowerManager::localState() != PowerManager::LocalState::ACTIVE) MotionRuntime::resetMovement();
    if (RadioRuntime::ready()) MotionRuntime::serviceMotion();
    ButtonRuntime::serviceButton();
    // Activity/deadlines precede controls; queued receipt ACKs precede retries.
    PowerManager::update(uint32_t(millis()));
    if (!RadioRuntime::ready())
    {
        ButtonRuntime::failButtonWake("RUNTIME_NOT_READY");
        MotionRuntime::resetMovement();
        MotionRuntime::cancelProximityCheck("RUNTIME_NOT_READY");
        delay(10);
        return;
    }

    RadioRuntime::drainReceiveQueue();
    RadioRuntime::serviceAwake();
    RadioRuntime::discardObsoleteControls();
    RadioRuntime::handleAckTimeout();
    SleepRuntime::serviceAutomaticSleep();
    RadioRuntime::sendNextControl();
    // Physical entry remains separate from semantic agreement.
    SleepRuntime::serviceSleepExecution();
    ButtonRuntime::serviceButtonHeartbeat();
    RadioRuntime::serviceAutomaticTransportSelection();
    RadioRuntime::servicePeriodicHeartbeat();
    Presentation::serviceFarHeartbeat();
    MotionRuntime::serviceProximityProbe(uint32_t(millis()));
    MotionRuntime::serviceRssiObservations();
    Presentation::serviceDisplayStatus();
    delay(10);
}
