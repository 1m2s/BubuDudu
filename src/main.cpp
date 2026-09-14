#include <Arduino.h>
#include <Wire.h>

#include "esp_sleep.h"

#include "Motion.h"
#include "LED.h"
#include "Display.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


// ======================================================
// Hardware configuration
// ======================================================

constexpr uint8_t I2C_SDA_PIN = 0;
constexpr uint8_t I2C_SCL_PIN = 1;

constexpr gpio_num_t ADXL_INT_PIN = GPIO_NUM_3;


// ======================================================
// Subsystems
// ======================================================

Motion motion;
LED led;
Display display;


// ======================================================
// I2C scanner
//
// Temporary bring-up/debug tool.
// ======================================================

void scanI2C()
{
    Serial.println();
    Serial.println("Scanning I2C bus...");
    Serial.println("-------------------");

    uint8_t deviceCount = 0;


    for (
        uint8_t address = 1;
        address < 127;
        address++
    )
    {
        Wire.beginTransmission(address);

        uint8_t error =
            Wire.endTransmission();


        if (error == 0)
        {
            Serial.print(
                "Found I2C device at 0x"
            );


            if (address < 0x10)
            {
                Serial.print("0");
            }


            Serial.println(
                address,
                HEX
            );


            deviceCount++;
        }
    }


    Serial.println("-------------------");

    Serial.print(
        "Devices found: "
    );

    Serial.println(
        deviceCount
    );

    Serial.println();
}


// ======================================================
// LED FreeRTOS task
// ======================================================

void ledTask(void *parameter)
{
    while (true)
    {
        led.heartbeat();
    }
}


// ======================================================
// Enter deep sleep
// ======================================================

void enterDeepSleep()
{
    Serial.println();

    Serial.println(
        ">>> ESP32 entering DEEP SLEEP"
    );

    Serial.println(
        "Move device to wake it."
    );


    // --------------------------------------------------
    // Stop normal awake interrupt handling.
    // --------------------------------------------------

    motion.pauseInterrupt();


    // --------------------------------------------------
    // Movement may have happened between detecting
    // inactivity and entering deep sleep.
    // --------------------------------------------------

    if (
        digitalRead(
            motion.getInterruptPin()
        ) == HIGH
    )
    {
        MotionEvent event =
            motion.readPendingEvent();


        Serial.println(
            "Motion occurred before sleep."
        );

        Serial.println(
            "Deep sleep cancelled."
        );


        if (
            event == MotionEvent::Activity
        )
        {
            Serial.println(
                ">>> STATE: ACTIVE"
            );
        }


        motion.resumeInterrupt();

        return;
    }


    // --------------------------------------------------
    // GPIO3 deep-sleep wake mask
    // --------------------------------------------------

    uint64_t wakePinMask =
        1ULL << ADXL_INT_PIN;


    // --------------------------------------------------
    // Wake when ADXL345 INT1 drives GPIO3 HIGH.
    // --------------------------------------------------

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


        motion.resumeInterrupt();

        return;
    }


    // --------------------------------------------------
    // Turn WS2812B off before deep sleep.
    // --------------------------------------------------

    Serial.println(
        "Turning WS2812 off."
    );


    led.off();


    Serial.flush();


    // ==================================================
    // Deep sleep begins.
    //
    // This function does not return.
    // ==================================================

    esp_deep_sleep_start();
}


// ======================================================
// setup
// ======================================================

void setup()
{
    Serial.begin(115200);

    delay(1000);


    Serial.println();

    Serial.println(
        "BubuDudu subsystem integration test"
    );

    Serial.println(
        "-----------------------------------"
    );


    // ==================================================
    // LED
    // ==================================================

    led.begin();


    // ==================================================
    // Determine boot reason
    // ==================================================

    esp_sleep_wakeup_cause_t wakeCause =
        esp_sleep_get_wakeup_cause();


    if (
        wakeCause ==
        ESP_SLEEP_WAKEUP_UNDEFINED
    )
    {
        Serial.println(
            "Boot reason: NORMAL STARTUP"
        );
    }

    else if (
        wakeCause ==
        ESP_SLEEP_WAKEUP_GPIO
    )
    {
        Serial.println(
            "Boot reason: MOTION WAKE"
        );
    }

    else
    {
        Serial.print(
            "Other wake reason: "
        );

        Serial.println(
            (int)wakeCause
        );
    }


    // ==================================================
    // Motion
    //
    // Motion currently owns initialization of the
    // shared I2C bus:
    //
    // SDA -> GPIO0
    // SCL -> GPIO1
    // ==================================================

    bool motionReady =
        motion.begin(
            I2C_SDA_PIN,
            I2C_SCL_PIN,
            (uint8_t)ADXL_INT_PIN
        );


    if (!motionReady)
    {
        Serial.println();

        Serial.println(
            "ERROR: ADXL345 not detected."
        );


        while (true)
        {
            delay(1000);
        }
    }


    Serial.println(
        "Motion subsystem ready."
    );


    // ==================================================
    // Temporary shared I2C bus verification
    //
    // Expected:
    // OLED    -> 0x3C
    // ADXL345 -> 0x53
    // ==================================================

    scanI2C();


    // ==================================================
    // Display
    //
    // Display uses the already initialized I2C bus.
    // ==================================================

    bool displayReady =
        display.begin();


    if (!displayReady)
    {
        Serial.println(
            "ERROR: Display initialization failed."
        );


        while (true)
        {
            delay(1000);
        }
    }


    display.showTest();


    // ==================================================
    // Check event responsible for motion wake
    // ==================================================

    if (
        wakeCause ==
        ESP_SLEEP_WAKEUP_GPIO
    )
    {
        MotionEvent wakeEvent =
            motion.getStartupEvent();


        if (
            wakeEvent ==
            MotionEvent::Activity
        )
        {
            Serial.println(
                "ADXL345 wake event: ACTIVITY"
            );
        }
    }


    Serial.println();

    Serial.println(
        ">>> STATE: ACTIVE"
    );

    Serial.println();


    // ==================================================
    // Start LED FreeRTOS task
    // ==================================================

    xTaskCreate(
        ledTask,
        "LED Task",
        2048,
        nullptr,
        1,
        nullptr
    );
}


// ======================================================
// loop
// ======================================================

void loop()
{
    MotionEvent event =
        motion.getEvent();


    // ==================================================
    // Activity
    // ==================================================

    if (
        event ==
        MotionEvent::Activity
    )
    {
        Serial.println(
            ">>> STATE: ACTIVE"
        );
    }


    // ==================================================
    // Inactivity
    // ==================================================

    else if (
        event ==
        MotionEvent::Inactivity
    )
    {
        Serial.println(
            ">>> STATE: INACTIVE"
        );


        enterDeepSleep();
    }
}