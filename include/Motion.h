#pragma once

#include <Arduino.h>

// ======================================================
// Events that the Motion module can report
// ======================================================

enum class MotionEvent
{
    None,
    Activity,
    Inactivity
};


// ======================================================
// Motion
//
// Responsible for:
// - ADXL345 configuration
// - activity/inactivity detection
// - ADXL345 interrupt handling
//
// NOT responsible for:
// - putting ESP32 to sleep
// - LEDs
// - ESP-NOW
// - system state
// ======================================================

class Motion
{
public:
    // Start I2C, configure ADXL345 and enable INT1.
    bool begin(
        uint8_t sdaPin,
        uint8_t sclPin,
        uint8_t interruptPin
    );

    // Check whether the ISR reported a new motion event.
    MotionEvent getEvent();

    // Event that was already stored in the ADXL345
    // when the ESP32 booted.
    //
    // Useful after deep-sleep wake.
    MotionEvent getStartupEvent() const;

    // Needed by the power code for deep-sleep wake.
    uint8_t getInterruptPin() const;

    // Temporarily stop normal awake ISR handling.
    void pauseInterrupt();

    // Restore normal awake ISR handling.
    void resumeInterrupt();

    // Read and clear the ADXL345's current event.
    MotionEvent readPendingEvent();


private:
    uint8_t interruptPin = 255;

    MotionEvent startupEvent = MotionEvent::None;


    // ISR flag shared between interrupt code and normal code.
    static volatile bool interruptOccurred;


    // GPIO interrupt function.
    static void IRAM_ATTR handleInterrupt();


    // ADXL345 register helpers.
    void writeRegister(
        uint8_t reg,
        uint8_t value
    );

    uint8_t readRegister(
        uint8_t reg
    );


    // Convert INT_SOURCE bits into a MotionEvent.
    MotionEvent decodeEvent(
        uint8_t interruptSource
    );
};