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
                         const char* distance, const char* radio, const char* state)
{
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x12_tf);
    oled.drawStr(0, 12, deviceName);
    oled.drawStr(0, 24, "PEER:");
    oled.drawStr(42, 24, peer);
    oled.drawStr(0, 36, "DIST:");
    oled.drawStr(42, 36, distance);
    oled.drawStr(0, 48, "RADIO:");
    oled.drawStr(42, 48, radio);
    oled.drawStr(0, 60, "STATE:");
    oled.drawStr(42, 60, state);
    oled.sendBuffer();
}
