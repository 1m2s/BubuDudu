#include "Display.h"

#include <Arduino.h>
#include <Wire.h>


namespace
{
    constexpr uint8_t OLED_ADDRESS = 0x3C;
}


// ======================================================
// Constructor
// ======================================================

Display::Display()
    : oled(
        U8G2_R0,
        U8X8_PIN_NONE
    )
{
}


// ======================================================
// Initialize display
// ======================================================

bool Display::begin()
{
    Serial.println(
        "Initializing Display subsystem..."
    );


    // --------------------------------------------------
    // Shared I2C bus has already been initialized
    // by Motion.
    // --------------------------------------------------

    Wire.setClock(
        100000
    );


    oled.setI2CAddress(
        OLED_ADDRESS << 1
    );


    oled.begin();


    Serial.println(
        "Display subsystem ready."
    );


    return true;
}


// ======================================================
// Temporary display test
// ======================================================

void Display::showTest()
{
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


    oled.sendBuffer();
}