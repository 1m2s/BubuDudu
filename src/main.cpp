#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

#include "esp_sleep.h"

#include "Motion.h"
#include "LED.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


// ======================================================
// Hardware configuration
// ======================================================

constexpr uint8_t I2C_SDA_PIN = 0;
constexpr uint8_t I2C_SCL_PIN = 1;

constexpr gpio_num_t ADXL_INT_PIN = GPIO_NUM_3;

constexpr uint8_t OLED_ADDRESS = 0x3C;


// ======================================================
// Subsystems
// ======================================================

Motion motion;
LED led;


// ======================================================
// OLED
//
// SH1106
// 128 x 64
//
// IMPORTANT:
// Motion initializes the shared I2C bus first.
// U8g2 will use that existing hardware I2C bus.
// ======================================================

U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
    U8G2_R0,
    U8X8_PIN_NONE
);


// ======================================================
// I2C scanner
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
// OLED basic test
// ======================================================

void testOLED()
{
    Serial.println();
    Serial.println(
        "Starting OLED test..."
    );


    // --------------------------------------------------
    // Slow I2C down to 100 kHz for first bring-up.
    // --------------------------------------------------

    Wire.setClock(
        100000
    );


    // --------------------------------------------------
    // U8g2 uses an 8-bit form of the I2C address.
    // Physical device address is 0x3C.
    // --------------------------------------------------

    oled.setI2CAddress(
        OLED_ADDRESS << 1
    );


    Serial.println(
        "About to call oled.begin()..."
    );


    // --------------------------------------------------
    // Initialize display controller.
    // --------------------------------------------------

    oled.begin();


    Serial.println(
        "oled.begin() returned successfully."
    );


    // --------------------------------------------------
    // Build image in ESP32 framebuffer.
    // --------------------------------------------------

    Serial.println(
        "Preparing framebuffer..."
    );


    oled.clearBuffer();


    oled.setFont(
        u8g2_font_6x12_tf
    );


    oled.drawStr(
        0,
        15,
        "BubuDudu"
    );


    oled.drawStr(
        0,
        32,
        "OLED works!"
    );


    // --------------------------------------------------
    // Send framebuffer over I2C to OLED.
    // --------------------------------------------------

    Serial.println(
        "Sending framebuffer..."
    );


    oled.sendBuffer();


    Serial.println(
        "OLED test complete."
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
    // Stop normal awake ISR handling.
    // --------------------------------------------------

    motion.pauseInterrupt();


    // --------------------------------------------------
    // Movement may have happened between detecting
    // inactivity and actually entering deep sleep.
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
        "BubuDudu Motion + Heartbeat + OLED test"
    );

    Serial.println(
        "---------------------------------------"
    );


    // --------------------------------------------------
    // Initialize LED subsystem
    // --------------------------------------------------

    led.begin();


    // --------------------------------------------------
    // Why did ESP32 boot?
    // --------------------------------------------------

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


    // --------------------------------------------------
    // Initialize Motion subsystem.
    //
    // This initializes Wire using:
    //
    // SDA -> GPIO0
    // SCL -> GPIO1
    // --------------------------------------------------

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


    // --------------------------------------------------
    // Verify both devices exist on shared I2C bus.
    // --------------------------------------------------

    scanI2C();


    // --------------------------------------------------
    // OLED test
    // --------------------------------------------------

    testOLED();


    // --------------------------------------------------
    // Check event responsible for motion wake.
    // --------------------------------------------------

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


    // --------------------------------------------------
    // Start LED FreeRTOS task
    // --------------------------------------------------

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


    // --------------------------------------------------
    // Activity
    // --------------------------------------------------

    if (
        event ==
        MotionEvent::Activity
    )
    {
        Serial.println(
            ">>> STATE: ACTIVE"
        );
    }


    // --------------------------------------------------
    // Inactivity
    // --------------------------------------------------

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