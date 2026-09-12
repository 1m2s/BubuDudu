#include <Arduino.h>
#include <Wire.h>

#include "esp_sleep.h"
#include "driver/gpio.h"

// ======================================================
// Hardware
// ======================================================

const uint8_t ADXL345_ADDRESS = 0x53;

// ADXL345 INT1 -> ESP32-C3 GPIO3
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
// Interrupt flag
// ======================================================

volatile bool adxlInterruptOccurred = false;


// ======================================================
// ISR
//
// Runs when ADXL345 INT1 causes GPIO3 to rise.
// ======================================================

void IRAM_ATTR handleAdxlInterrupt()
{
    adxlInterruptOccurred = true;
}


// ======================================================
// Write one ADXL345 register
// ======================================================

void writeRegister(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}


// ======================================================
// Read one ADXL345 register
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
// Enter ESP32 light sleep
// ======================================================

void enterLightSleep()
{
    Serial.println();
    Serial.println(">>> ESP32 entering LIGHT SLEEP");
    Serial.println("Move Bubu to wake it.");

    // --------------------------------------------------
    // We don't need the normal awake ISR while sleeping.
    //
    // Sleep hardware will watch GPIO3 instead.
    // --------------------------------------------------

    detachInterrupt(digitalPinToInterrupt((int)ADXL_INT_PIN));

    adxlInterruptOccurred = false;


    // --------------------------------------------------
    // Configure GPIO3 as a wake-up source.
    //
    // ADXL345 INT1 is active HIGH.
    //
    // Therefore:
    //
    // GPIO3 HIGH -> wake ESP32
    // --------------------------------------------------

    gpio_wakeup_enable(
        ADXL_INT_PIN,
        GPIO_INTR_HIGH_LEVEL
    );


    // --------------------------------------------------
    // Enable GPIO wake-up for light sleep.
    // --------------------------------------------------

    esp_err_t wakeResult = esp_sleep_enable_gpio_wakeup();

    if (wakeResult != ESP_OK)
    {
        Serial.println("ERROR: Could not enable GPIO wake.");

        attachInterrupt(
            digitalPinToInterrupt((int)ADXL_INT_PIN),
            handleAdxlInterrupt,
            RISING
        );

        return;
    }


    // Make sure Serial output is sent before CPU sleeps.
    Serial.flush();


    // ==================================================
    // ESP32 GOES TO SLEEP HERE
    //
    // Program execution pauses at this line.
    // ==================================================

    esp_light_sleep_start();


    // ==================================================
    // Execution continues HERE after waking.
    // ==================================================


    // --------------------------------------------------
    // Find out WHY the ESP32 woke.
    // --------------------------------------------------

    esp_sleep_wakeup_cause_t wakeCause =
        esp_sleep_get_wakeup_cause();


    Serial.println();
    Serial.println(">>> ESP32 WOKE UP");


    if (wakeCause == ESP_SLEEP_WAKEUP_GPIO)
    {
        Serial.println("Wake source: ADXL345 GPIO3");
    }
    else
    {
        Serial.print("Unexpected wake source: ");
        Serial.println((int)wakeCause);
    }


    // --------------------------------------------------
    // Disable sleep wake configuration now that
    // the ESP32 is awake again.
    // --------------------------------------------------

    esp_sleep_disable_wakeup_source(
        ESP_SLEEP_WAKEUP_GPIO
    );

    gpio_wakeup_disable(ADXL_INT_PIN);


    // --------------------------------------------------
    // INT1 should still be HIGH because the ADXL345
    // activity event is latched.
    //
    // Read INT_SOURCE:
    //
    // 1. Discover what event caused wake.
    // 2. Clear the ADXL345 interrupt.
    // --------------------------------------------------

    uint8_t interruptSource =
        readRegister(INT_SOURCE);


    if (interruptSource & 0x10)
    {
        Serial.println(">>> STATE: ACTIVE");
    }


    if (interruptSource & 0x08)
    {
        Serial.println(">>> STATE: INACTIVE");
    }


    // --------------------------------------------------
    // Restore normal awake interrupt handling.
    // --------------------------------------------------

    attachInterrupt(
        digitalPinToInterrupt((int)ADXL_INT_PIN),
        handleAdxlInterrupt,
        RISING
    );


    Serial.println("Normal execution resumed.");
    Serial.println();
}


// ======================================================
// setup
// ======================================================

void setup()
{
    Serial.begin(115200);
    delay(1000);


    // --------------------------------------------------
    // I2C
    //
    // SDA = GPIO0
    // SCL = GPIO1
    // --------------------------------------------------

    Wire.begin(0, 1);


    // --------------------------------------------------
    // Interrupt input
    // --------------------------------------------------

    pinMode((int)ADXL_INT_PIN, INPUT);


    // --------------------------------------------------
    // Disable ADXL interrupts while configuring
    // --------------------------------------------------

    writeRegister(INT_ENABLE, 0x00);


    // --------------------------------------------------
    // Measurement format
    //
    // FULL_RES
    // +/-4 g
    // --------------------------------------------------

    writeRegister(DATA_FORMAT, 0x09);


    // --------------------------------------------------
    // Activity threshold
    //
    // 4 * 0.0625 g = 0.25 g
    // --------------------------------------------------

    writeRegister(THRESH_ACT, 4);


    // --------------------------------------------------
    // Inactivity threshold
    //
    // 2 * 0.0625 g = 0.125 g
    // --------------------------------------------------

    writeRegister(THRESH_INACT, 2);


    // --------------------------------------------------
    // Require approximately 3 seconds of inactivity
    // --------------------------------------------------

    writeRegister(TIME_INACT, 3);


    // --------------------------------------------------
    // Activity / inactivity:
    //
    // AC coupled
    // X Y Z enabled
    // --------------------------------------------------

    writeRegister(ACT_INACT_CTL, 0xFF);


    // --------------------------------------------------
    // Activity and inactivity -> INT1
    // --------------------------------------------------

    writeRegister(INT_MAP, 0x00);


    // --------------------------------------------------
    // LINK + MEASURE
    //
    // 0x28 = 0010 1000
    //
    // This gives us:
    //
    // ACTIVE -> wait for INACTIVE
    // INACTIVE -> wait for ACTIVE
    // --------------------------------------------------

    writeRegister(POWER_CTL, 0x28);


    // Clear old interrupt
    readRegister(INT_SOURCE);


    // --------------------------------------------------
    // Normal awake ISR
    // --------------------------------------------------

    attachInterrupt(
        digitalPinToInterrupt((int)ADXL_INT_PIN),
        handleAdxlInterrupt,
        RISING
    );


    // --------------------------------------------------
    // Enable activity + inactivity
    //
    // Activity   = bit 4
    // Inactivity = bit 3
    //
    // 0x18
    // --------------------------------------------------

    writeRegister(INT_ENABLE, 0x18);


    Serial.println();
    Serial.println("BubuDudu light-sleep test");
    Serial.println("-------------------------");
    Serial.println();
    Serial.println("Leave Bubu still.");
    Serial.println("After inactivity, ESP32 should sleep.");
    Serial.println("Move Bubu to wake it.");
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


        // ------------------------------------------------
        // Ask ADXL345 what caused INT1.
        // ------------------------------------------------

        uint8_t interruptSource =
            readRegister(INT_SOURCE);


        // ------------------------------------------------
        // ACTIVITY
        // ------------------------------------------------

        if (interruptSource & 0x10)
        {
            Serial.println(">>> STATE: ACTIVE");
        }


        // ------------------------------------------------
        // INACTIVITY
        // ------------------------------------------------

        if (interruptSource & 0x08)
        {
            Serial.println(">>> STATE: INACTIVE");

            // This is our first actual power transition.
            enterLightSleep();
        }
    }


    // No GPIO polling.
    //
    // Eventually normal BubuDudu work will happen here.
}