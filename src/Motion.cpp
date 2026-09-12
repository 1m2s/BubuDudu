#include "Motion.h"

#include <Wire.h>


// ======================================================
// ADXL345 constants
// ======================================================

namespace
{
    constexpr uint8_t ADXL345_ADDRESS = 0x53;

    constexpr uint8_t DEVID           = 0x00;

    constexpr uint8_t THRESH_ACT      = 0x24;
    constexpr uint8_t THRESH_INACT    = 0x25;
    constexpr uint8_t TIME_INACT      = 0x26;
    constexpr uint8_t ACT_INACT_CTL   = 0x27;

    constexpr uint8_t POWER_CTL       = 0x2D;
    constexpr uint8_t INT_ENABLE      = 0x2E;
    constexpr uint8_t INT_MAP         = 0x2F;
    constexpr uint8_t INT_SOURCE      = 0x30;
    constexpr uint8_t DATA_FORMAT     = 0x31;

    constexpr uint8_t EXPECTED_DEVID  = 0xE5;
}


// ======================================================
// Static ISR flag
// ======================================================

volatile bool Motion::interruptOccurred = false;


// ======================================================
// ISR
// ======================================================

void IRAM_ATTR Motion::handleInterrupt()
{
    interruptOccurred = true;
}


// ======================================================
// Register write
// ======================================================

void Motion::writeRegister(
    uint8_t reg,
    uint8_t value
)
{
    Wire.beginTransmission(ADXL345_ADDRESS);

    Wire.write(reg);
    Wire.write(value);

    Wire.endTransmission();
}


// ======================================================
// Register read
// ======================================================

uint8_t Motion::readRegister(
    uint8_t reg
)
{
    Wire.beginTransmission(ADXL345_ADDRESS);

    Wire.write(reg);

    Wire.endTransmission(false);


    Wire.requestFrom(
        ADXL345_ADDRESS,
        (uint8_t)1
    );


    if (Wire.available())
    {
        return Wire.read();
    }


    return 0;
}


// ======================================================
// Convert INT_SOURCE into a useful application event
// ======================================================

MotionEvent Motion::decodeEvent(
    uint8_t interruptSource
)
{
    // Bit 4 = activity
    if (interruptSource & 0x10)
    {
        return MotionEvent::Activity;
    }


    // Bit 3 = inactivity
    if (interruptSource & 0x08)
    {
        return MotionEvent::Inactivity;
    }


    return MotionEvent::None;
}


// ======================================================
// Begin sensor
// ======================================================

bool Motion::begin(
    uint8_t sdaPin,
    uint8_t sclPin,
    uint8_t intPin
)
{
    interruptPin = intPin;


    // --------------------------------------------------
    // Start I2C
    // --------------------------------------------------

    Wire.begin(
        sdaPin,
        sclPin
    );


    pinMode(
        interruptPin,
        INPUT
    );


    // --------------------------------------------------
    // Verify that the device really is an ADXL345.
    //
    // ADXL345 DEVID should be 0xE5.
    // --------------------------------------------------

    uint8_t deviceId =
        readRegister(DEVID);


    if (deviceId != EXPECTED_DEVID)
    {
        return false;
    }


    // --------------------------------------------------
    // There may already be an event stored because
    // ADXL345 remained powered while ESP32 deep-slept.
    //
    // Capture it BEFORE reconfiguring the sensor.
    // --------------------------------------------------

    startupEvent =
        decodeEvent(
            readRegister(INT_SOURCE)
        );


    // --------------------------------------------------
    // Disable interrupts during configuration
    // --------------------------------------------------

    writeRegister(
        INT_ENABLE,
        0x00
    );


    // --------------------------------------------------
    // FULL_RES + +/-4 g
    //
    // 0x09 = 0000 1001
    // --------------------------------------------------

    writeRegister(
        DATA_FORMAT,
        0x09
    );


    // --------------------------------------------------
    // Activity threshold
    //
    // 4 * 0.0625 g = 0.25 g
    // --------------------------------------------------

    writeRegister(
        THRESH_ACT,
        4
    );


    // --------------------------------------------------
    // Inactivity threshold
    //
    // 2 * 0.0625 g = 0.125 g
    // --------------------------------------------------

    writeRegister(
        THRESH_INACT,
        2
    );


    // --------------------------------------------------
    // Inactivity must last approximately 3 seconds
    // --------------------------------------------------

    writeRegister(
        TIME_INACT,
        3
    );


    // --------------------------------------------------
    // AC-coupled activity/inactivity
    // X/Y/Z enabled
    // --------------------------------------------------

    writeRegister(
        ACT_INACT_CTL,
        0xFF
    );


    // --------------------------------------------------
    // Activity + inactivity -> INT1
    // --------------------------------------------------

    writeRegister(
        INT_MAP,
        0x00
    );


    // --------------------------------------------------
    // LINK + MEASURE
    //
    // Activity and inactivity now alternate cleanly.
    // --------------------------------------------------

    writeRegister(
        POWER_CTL,
        0x28
    );


    interruptOccurred = false;


    // --------------------------------------------------
    // Attach ESP32 awake interrupt
    // --------------------------------------------------

    resumeInterrupt();


    // --------------------------------------------------
    // Enable activity + inactivity
    //
    // Bit 4 = activity
    // Bit 3 = inactivity
    // --------------------------------------------------

    writeRegister(
        INT_ENABLE,
        0x18
    );


    return true;
}


// ======================================================
// Get event generated through ISR
// ======================================================

MotionEvent Motion::getEvent()
{
    if (!interruptOccurred)
    {
        return MotionEvent::None;
    }


    // Clear our software flag.
    interruptOccurred = false;


    // INT_SOURCE tells us why INT1 fired and
    // clears the ADXL345 interrupt.
    return readPendingEvent();
}


// ======================================================
// Read current ADXL345 interrupt event
// ======================================================

MotionEvent Motion::readPendingEvent()
{
    return decodeEvent(
        readRegister(INT_SOURCE)
    );
}


// ======================================================
// Startup event
// ======================================================

MotionEvent Motion::getStartupEvent() const
{
    return startupEvent;
}


// ======================================================
// Interrupt pin
// ======================================================

uint8_t Motion::getInterruptPin() const
{
    return interruptPin;
}


// ======================================================
// Pause normal awake ISR
// ======================================================

void Motion::pauseInterrupt()
{
    detachInterrupt(
        digitalPinToInterrupt(interruptPin)
    );

    interruptOccurred = false;
}


// ======================================================
// Restore normal awake ISR
// ======================================================

void Motion::resumeInterrupt()
{
    attachInterrupt(
        digitalPinToInterrupt(interruptPin),
        handleInterrupt,
        RISING
    );
}