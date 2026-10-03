#include "Motion.h"

#include <Wire.h>

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

    // Trial 0.625 g awake setting (nearest to 0.6 g); physical validation pending.
    // Sleep keeps the independently verified deliberate-motion threshold (3 g).
    constexpr uint8_t AWAKE_ACTIVITY_THRESHOLD = 10;
    constexpr uint8_t SLEEP_ACTIVITY_THRESHOLD = 48;
    constexpr uint8_t STARTUP_ATTEMPTS = 3;
    constexpr uint32_t STARTUP_RETRY_DELAY_MS = 20;
}

volatile bool Motion::interruptOccurred = false;

void IRAM_ATTR Motion::handleInterrupt()
{
    interruptOccurred = true;
}

bool Motion::writeRegister(
    uint8_t reg,
    uint8_t value
)
{
    Wire.beginTransmission(ADXL345_ADDRESS);

    Wire.write(reg);
    Wire.write(value);

    return Wire.endTransmission() == 0;
}

uint8_t Motion::readRegister(
    uint8_t reg
)
{
    uint8_t value = 0;
    readRegister(reg, value);
    return value;
}

bool Motion::readRegister(uint8_t reg, uint8_t& value)
{
    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(ADXL345_ADDRESS, uint8_t(1)) != 1) return false;
    value = Wire.read();
    return true;
}

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

bool Motion::begin(
    uint8_t sdaPin,
    uint8_t sclPin,
    uint8_t intPin
)
{
    pauseInterrupt(); // Detach only an owned handler, before replacing its pin on reinitialization.
    initialized = false;
    interruptPin = intPin;
    startupEvent = MotionEvent::None;
    pinMode(interruptPin, INPUT);
    bool busReady = false, startupCaptured = false;
    for (uint8_t attempt = 0; attempt < STARTUP_ATTEMPTS; ++attempt)
    {
        if (attempt != 0)
        {
            Serial.printf("MOTION INIT | retry %u/%u\n", attempt, STARTUP_ATTEMPTS - 1);
            delay(STARTUP_RETRY_DELAY_MS);
        }
        // Motion remains the sole bus owner. Retry a failed Wire.begin(), but
        // keep a successfully initialized shared bus in place for sensor retries.
        if (!busReady)
        {
            busReady = Wire.begin(sdaPin, sclPin);
#ifdef DEVICE_DUDU
            diagnosticBusReady = busReady;
            Serial.printf("I2C STARTUP DUDU | Wire.begin SDA=%u SCL=%u | result=%u (%s)\n",
                          sdaPin, sclPin, unsigned(busReady), busReady ? "READY" : "FAILED");
#endif
        }
        if (busReady && beginAttempt(startupCaptured))
        {
            interruptOccurred = false;
            initialized = true;
            resumeInterrupt();
            return true;
        }
    }
    return false; // setup() reports FAILED and continues; no background retry.
}

#ifdef DEVICE_DUDU
void Motion::reportStartupI2cHealth(uint8_t oledAddress) const
{
    // Address-only START/address/STOP, once per device in setup(). No register
    // access, recovery or setting changes. These extra transfers affect timing.
    const struct { const char* name; uint8_t address; } devices[]{
        {"ADXL345", ADXL345_ADDRESS}, {"OLED", oledAddress}
    };
    for (const auto& device : devices)
    {
        if (!diagnosticBusReady)
        {
            Serial.printf("I2C ADDRESS DUDU | %s addr7=0x%02X | SKIPPED (Wire.begin failed)\n",
                          device.name, device.address);
            continue;
        }
        Wire.beginTransmission(device.address);
        const uint8_t result = Wire.endTransmission(true);
        // Installed ESP32 Wire (Arduino 2.0.17): ESP_OK->0, ESP_FAIL->2,
        // ESP_ERR_TIMEOUT->5, otherwise 4. Code 2 is not exclusive to NACK.
        const char* meaning = result == 0 ? "ACK" : result == 2 ? "NACK_OR_ESP_FAIL" :
            result == 4 ? "OTHER_ERROR" : result == 5 ? "TIMEOUT" : "UNEXPECTED_CODE";
        Serial.printf("I2C ADDRESS DUDU | %s addr7=0x%02X | result=%u (%s)\n",
                      device.name, device.address, result, meaning);
    }
}
#endif

bool Motion::beginAttempt(bool& startupCaptured)
{
    uint8_t deviceId, source;
    if (!readRegister(DEVID, deviceId) || deviceId != EXPECTED_DEVID) return false;
    // INT_SOURCE clears on read. Preserve the original deep-wake event if a
    // later configuration operation fails and requires another full attempt.
    if (!startupCaptured)
    {
        if (!readRegister(INT_SOURCE, source)) return false;
        startupEvent = decodeEvent(source);
        startupCaptured = true;
    }
    uint8_t activityThreshold;
    if (!writeRegister(INT_ENABLE, 0x00) ||
        !writeRegister(DATA_FORMAT, 0x09) || // FULL_RES, +/-4 g.
        !writeRegister(THRESH_ACT, AWAKE_ACTIVITY_THRESHOLD) ||
        !readRegister(THRESH_ACT, activityThreshold) ||
        activityThreshold != AWAKE_ACTIVITY_THRESHOLD ||
        !writeRegister(THRESH_INACT, 4) || // 0.25 g.
        !writeRegister(TIME_INACT, 3) ||
        !writeRegister(ACT_INACT_CTL, 0xFF) || // AC-coupled activity/inactivity on X/Y/Z.
        !writeRegister(INT_MAP, 0x00) || // Both interrupts on INT1.
        !writeRegister(POWER_CTL, 0x28) || // LINK + MEASURE.
        !writeRegister(INT_ENABLE, 0x18)) // Activity + inactivity.
        return false;
    return true;
}

MotionEvent Motion::getEvent()
{
    // INT1 can already be HIGH when the ISR is reattached after a sleep abort.
    // Service that latch too; an unavailable/uninitialized sensor produces no event.
    if (!initialized || (!interruptOccurred && digitalRead(interruptPin) == LOW))
    {
        return MotionEvent::None;
    }

    // Clear our software flag.
    interruptOccurred = false;

    // INT_SOURCE tells us why INT1 fired and
    // clears the ADXL345 interrupt.
    return readPendingEvent();
}

MotionEvent Motion::readPendingEvent()
{
    return decodeEvent(
        readRegister(INT_SOURCE)
    );
}

MotionEvent Motion::getStartupEvent() const
{
    return startupEvent;
}

uint8_t Motion::getInterruptPin() const
{
    return interruptPin;
}

void Motion::pauseInterrupt()
{
    if (interruptAttached)
    {
        detachInterrupt(
            digitalPinToInterrupt(interruptPin)
        );
        interruptAttached = false;
    }

    interruptOccurred = false;
}

void Motion::resumeInterrupt()
{
    if (!initialized || interruptAttached) return;
    attachInterrupt(
        digitalPinToInterrupt(interruptPin),
        handleInterrupt,
        RISING
    );
    interruptAttached = true;
}

bool Motion::prepareForSleep()
{
    if (!initialized) return false;
    pauseInterrupt();
    // Awake LINK mode requires servicing inactivity too. For this explicit
    // sleep interval use independent activity with the verified sleep threshold.
    // ADXL345 datasheet: clear LINK via standby before returning to MEASURE.
    uint8_t source, enabled, mapping, power, format, activityThreshold;
    if (!writeRegister(INT_ENABLE, 0x00) ||
        !writeRegister(POWER_CTL, 0x00) ||
        !writeRegister(THRESH_ACT, SLEEP_ACTIVITY_THRESHOLD) ||
        !readRegister(THRESH_ACT, activityThreshold) || activityThreshold != SLEEP_ACTIVITY_THRESHOLD ||
        !writeRegister(INT_MAP, 0x00) ||
        !readRegister(INT_SOURCE, source) ||
        !writeRegister(POWER_CTL, 0x08) ||
        !writeRegister(INT_ENABLE, 0x10) ||
        !readRegister(INT_ENABLE, enabled) || enabled != 0x10 ||
        !readRegister(INT_MAP, mapping) || mapping != 0x00 ||
        !readRegister(POWER_CTL, power) || power != 0x08 ||
        !readRegister(DATA_FORMAT, format) || format != 0x09)
        return false;
    // Old latched activity was cleared above. New/stuck HIGH refuses sleep.
    return digitalRead(interruptPin) == LOW;
}

bool Motion::cancelSleepPreparation()
{
    if (!initialized) return false;
    // Fixed number of I2C operations, no retries. Restore ISR even on I2C
    // failure and report the failure to main; never silently claim success.
    const bool disabled = writeRegister(INT_ENABLE, 0x00);
    const bool standby = writeRegister(POWER_CTL, 0x00);
    const bool thresholdWritten = writeRegister(THRESH_ACT, AWAKE_ACTIVITY_THRESHOLD);
    const bool mapped = writeRegister(INT_MAP, 0x00);
    const bool linked = writeRegister(POWER_CTL, 0x28);
    const bool enabled = writeRegister(INT_ENABLE, 0x18);
    uint8_t mapping = 0xFF, interrupts = 0, power = 0, activityThreshold = 0;
    const bool readThreshold = readRegister(THRESH_ACT, activityThreshold);
    const bool readMap = readRegister(INT_MAP, mapping);
    const bool readEnable = readRegister(INT_ENABLE, interrupts);
    const bool readPower = readRegister(POWER_CTL, power);
    resumeInterrupt();
    return disabled && standby && thresholdWritten && readThreshold &&
           activityThreshold == AWAKE_ACTIVITY_THRESHOLD && mapped && linked && enabled && readMap && readEnable && readPower &&
           mapping == 0x00 && interrupts == 0x18 && power == 0x28;
}
