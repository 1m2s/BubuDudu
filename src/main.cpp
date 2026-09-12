#include <Arduino.h>
#include <Wire.h>

#include "esp_sleep.h"

// ======================================================
// Hardware
// ======================================================

const uint8_t ADXL345_ADDRESS = 0x53;

const gpio_num_t ADXL_INT_PIN = GPIO_NUM_3;


// ======================================================
// ADXL345 registers
// ======================================================

const uint8_t THRESH_ACT    = 0x24;
const uint8_t THRESH_INACT  = 0x25;
const uint8_t TIME_INACT    = 0x26;
const uint8_t ACT_INACT_CTL = 0x27;

const uint8_t POWER_CTL     = 0x2D;
const uint8_t INT_ENABLE    = 0x2E;
const uint8_t INT_MAP       = 0x2F;
const uint8_t INT_SOURCE    = 0x30;
const uint8_t DATA_FORMAT   = 0x31;


// ======================================================
// ISR flag
// ======================================================

volatile bool adxlInterruptOccurred = false;


// ======================================================
// ISR
// ======================================================

void IRAM_ATTR handleAdxlInterrupt()
{
    adxlInterruptOccurred = true;
}


// ======================================================
// ADXL345 register write
// ======================================================

void writeRegister(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}


// ======================================================
// ADXL345 register read
// ======================================================

uint8_t readRegister(uint8_t reg)
{
    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(reg);
    Wire.endTransmission(false);

    Wire.requestFrom(ADXL345_ADDRESS, (uint8_t)1);

    if (Wire.available())
    {
        return Wire.read();
    }

    return 0;
}


// ======================================================
// Configure ADXL345
// ======================================================

void configureADXL345()
{
    // Disable interrupts while configuring
    writeRegister(INT_ENABLE, 0x00);

    // FULL_RES + +/-4 g
    writeRegister(DATA_FORMAT, 0x09);

    // Activity:
    // 4 * 0.0625 g = 0.25 g
    writeRegister(THRESH_ACT, 4);

    // Inactivity:
    // 2 * 0.0625 g = 0.125 g
    writeRegister(THRESH_INACT, 2);

    // 3 seconds inactivity
    writeRegister(TIME_INACT, 3);

    // AC-coupled activity/inactivity
    // X Y Z enabled
    writeRegister(ACT_INACT_CTL, 0xFF);

    // Send events to INT1
    writeRegister(INT_MAP, 0x00);

    // LINK + MEASURE
    //
    // 0x28 = 0010 1000
    writeRegister(POWER_CTL, 0x28);

    // Clear old event
    readRegister(INT_SOURCE);

    // Enable activity + inactivity
    writeRegister(INT_ENABLE, 0x18);
}


// ======================================================
// Enter deep sleep
// ======================================================

void enterDeepSleep()
{
    Serial.println();
    Serial.println(">>> ESP32 entering DEEP SLEEP");
    Serial.println("Move Bubu to wake it.");

    // We no longer need the normal awake ISR.
    detachInterrupt(
        digitalPinToInterrupt((int)ADXL_INT_PIN)
    );

    adxlInterruptOccurred = false;


    // --------------------------------------------------
    // Safety check
    //
    // Wake is HIGH-level triggered.
    //
    // If INT1 is already HIGH when we try sleeping,
    // the ESP32 could immediately wake again.
    // --------------------------------------------------

    if (digitalRead((int)ADXL_INT_PIN) == HIGH)
    {
        Serial.println(
            "INT1 is still HIGH - clearing interrupt first."
        );

        readRegister(INT_SOURCE);

        delay(10);
    }


    // --------------------------------------------------
    // Configure GPIO3 as DEEP-SLEEP wake source.
    //
    // 1ULL << 3 creates a bit mask selecting GPIO3.
    //
    // Wake when GPIO3 becomes HIGH.
    // --------------------------------------------------

    uint64_t wakePinMask =
        1ULL << ADXL_INT_PIN;

    esp_err_t result =
        esp_deep_sleep_enable_gpio_wakeup(
            wakePinMask,
            ESP_GPIO_WAKEUP_GPIO_HIGH
        );


    if (result != ESP_OK)
    {
        Serial.println(
            "ERROR: Could not configure deep-sleep wake."
        );

        attachInterrupt(
            digitalPinToInterrupt((int)ADXL_INT_PIN),
            handleAdxlInterrupt,
            RISING
        );

        return;
    }


    Serial.flush();


    // ==================================================
    // DEEP SLEEP STARTS HERE
    //
    // THIS FUNCTION NEVER RETURNS.
    // ==================================================

    esp_deep_sleep_start();


    // Nothing below this line will ever execute.
}


// ======================================================
// setup
// ======================================================

void setup()
{
    Serial.begin(115200);
    delay(1000);


    // --------------------------------------------------
    // Ask ESP32:
    //
    // "Why did I boot?"
    //
    // This must happen after every deep-sleep wake
    // because setup() starts again from scratch.
    // --------------------------------------------------

    esp_sleep_wakeup_cause_t wakeCause =
        esp_sleep_get_wakeup_cause();


    Serial.println();
    Serial.println("BubuDudu deep-sleep test");
    Serial.println("------------------------");


    // --------------------------------------------------
    // Start I2C again.
    //
    // Deep sleep destroyed the normal digital peripheral
    // state, so initialization must happen again.
    // --------------------------------------------------

    Wire.begin(0, 1);

    pinMode((int)ADXL_INT_PIN, INPUT);


    // --------------------------------------------------
    // Was this a normal startup?
    // --------------------------------------------------

    if (wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED)
    {
        Serial.println("Boot reason: NORMAL STARTUP");
    }


    // --------------------------------------------------
    // Was this a GPIO deep-sleep wake?
    // --------------------------------------------------

    else if (wakeCause == ESP_SLEEP_WAKEUP_GPIO)
    {
        Serial.println(
            "Boot reason: DEEP-SLEEP GPIO WAKE"
        );

        uint64_t wakePins =
            esp_sleep_get_gpio_wakeup_status();

        Serial.print("Wake GPIO mask: 0x");
        Serial.println(
            (unsigned long)wakePins,
            HEX
        );


        // ----------------------------------------------
        // ADXL345 stayed powered while ESP32 slept.
        //
        // The activity event which woke us may still
        // be stored inside INT_SOURCE.
        // ----------------------------------------------

        uint8_t source =
            readRegister(INT_SOURCE);


        if (source & 0x10)
        {
            Serial.println(
                "ADXL345 wake event: ACTIVITY"
            );
        }


        if (source & 0x08)
        {
            Serial.println(
                "ADXL345 wake event: INACTIVITY"
            );
        }
    }


    // --------------------------------------------------
    // Some other wake source
    // --------------------------------------------------

    else
    {
        Serial.print("Other wake cause: ");
        Serial.println((int)wakeCause);
    }


    // --------------------------------------------------
    // Deep sleep rebooted the ESP32.
    //
    // Therefore we initialize our sensor configuration
    // again from a known state.
    // --------------------------------------------------

    configureADXL345();


    // --------------------------------------------------
    // Restore normal awake ISR behavior.
    // --------------------------------------------------

    attachInterrupt(
        digitalPinToInterrupt((int)ADXL_INT_PIN),
        handleAdxlInterrupt,
        RISING
    );


    Serial.println();
    Serial.println(">>> STATE: ACTIVE");
    Serial.println();
    Serial.println("Leave Bubu still.");
    Serial.println(
        "After ~3 seconds it should deep sleep."
    );
    Serial.println(
        "Then move it to cause a full wake/reboot."
    );
    Serial.println();
}


// ======================================================
// loop
// ======================================================

void loop()
{
    if (adxlInterruptOccurred)
    {
        adxlInterruptOccurred = false;


        // Ask ADXL345 why INT1 fired.
        uint8_t source =
            readRegister(INT_SOURCE);


        // ------------------------------------------------
        // ACTIVE
        // ------------------------------------------------

        if (source & 0x10)
        {
            Serial.println(">>> STATE: ACTIVE");
        }


        // ------------------------------------------------
        // INACTIVE
        // ------------------------------------------------

        if (source & 0x08)
        {
            Serial.println(">>> STATE: INACTIVE");

            enterDeepSleep();
        }
    }
}