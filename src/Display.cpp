#include "Display.h"

#include <Wire.h>

namespace
{
    constexpr uint8_t OLED_ADDRESS = 0x3C;
    constexpr uint32_t OLED_I2C_CLOCK = 100000;

    uint8_t sharedI2cByte(u8x8_t* bus, uint8_t message, uint8_t value, void* data)
    {
        // Motion owns Wire.begin(). Skip U8g2's implicit bus initialization;
        // all actual transfers still use its standard hardware-I2C callback.
        if (message == U8X8_MSG_BYTE_INIT) return 1;
        return u8x8_byte_arduino_hw_i2c(bus, message, value, data);
    }
}

Display::Display() : oled(U8G2_R0, U8X8_PIN_NONE) {}

bool Display::begin()
{
    // Called only after Motion has initialized the shared GPIO0/GPIO1 bus.
    Wire.setClock(OLED_I2C_CLOCK);
    oled.setBusClock(OLED_I2C_CLOCK);
    oled.setI2CAddress(OLED_ADDRESS << 1);
    oled.getU8x8()->byte_cb = sharedI2cByte;
    // U8g2's return value does not detect a physically missing panel.
    return oled.begin();
}

void Display::showStatus(const char* deviceName, const char* peer,
                         const char* distance, const char* radio, const char* state,
                         const char* motion)
{
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tf);
    oled.drawStr(0, 10, deviceName);
    oled.drawStr(0, 20, "PEER:");
    oled.drawStr(48, 20, peer);
    oled.drawStr(0, 30, "DIST:");
    oled.drawStr(48, 30, distance);
    oled.drawStr(0, 40, "RADIO:");
    oled.drawStr(48, 40, radio);
    oled.drawStr(0, 50, "STATUS:");
    oled.drawStr(48, 50, state);
    oled.drawStr(0, 60, "MOTION:");
    oled.drawStr(48, 60, motion);
    oled.sendBuffer();
}

void Display::showDeepSleep(const char* deviceName)
{
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tf);
    oled.drawStr(0, 10, deviceName);
    oled.drawStr(0, 25, "STATUS:");
    oled.drawStr(0, 35, "DEEP SLEEP");
    oled.drawStr(0, 50, "WAKE:");
    // Frozen v1 caption omits button wake; GPIO5 LOW is also enabled.
    oled.drawStr(0, 60, "MOTION / PEER");
    oled.sendBuffer();
}
