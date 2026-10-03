#pragma once

#include "Arduino.h"

constexpr uint8_t U8G2_R0 = 0, U8X8_PIN_NONE = 255;
constexpr uint8_t U8X8_MSG_BYTE_INIT = 0, U8X8_MSG_BYTE_SEND = 1;
const uint8_t u8g2_font_6x10_tf[]{0};
struct u8x8_t
{
    uint8_t i2c_address = 0;
    uint8_t (*byte_cb)(u8x8_t*, uint8_t, uint8_t, void*) = nullptr;
};
#define u8x8_GetI2CAddress(bus) ((bus)->i2c_address)
struct HostOledState
{
    struct Text { uint8_t x, y; std::string value; };
    std::vector<Text> text;
    const uint8_t* font = nullptr;
    uint32_t clock = 0;
    uint8_t address = 0;
    unsigned begins = 0, clears = 0, sends = 0, hardwareInitializations = 0, transfers = 0;
};
inline HostOledState& hostOled() { static HostOledState state; return state; }
inline uint8_t u8x8_byte_arduino_hw_i2c(u8x8_t*, uint8_t message, uint8_t, void*)
{
    if (message == U8X8_MSG_BYTE_INIT) ++hostOled().hardwareInitializations;
    else ++hostOled().transfers;
    return 1;
}

// The firmware-loop harness substitutes Display; the renderer test uses this
// recorder with the real Display.cpp, including its shared-I2C callback.
class U8G2_SH1106_128X64_NONAME_F_HW_I2C
{
    u8x8_t bus;
public:
    U8G2_SH1106_128X64_NONAME_F_HW_I2C(uint8_t rotation = U8G2_R0, uint8_t reset = U8X8_PIN_NONE)
    { assert(rotation == U8G2_R0 && reset == U8X8_PIN_NONE); }
    void setBusClock(uint32_t value) { hostOled().clock = value; }
    void setI2CAddress(uint8_t value) { bus.i2c_address = hostOled().address = value; }
    u8x8_t* getU8x8() { return &bus; }
    bool begin() { ++hostOled().begins; assert(bus.byte_cb); return bus.byte_cb(&bus, U8X8_MSG_BYTE_INIT, 0, nullptr); }
    void clearBuffer() { ++hostOled().clears; hostOled().text.clear(); }
    void setFont(const uint8_t* font) { hostOled().font = font; }
    void drawStr(uint8_t x, uint8_t y, const char* text) { hostOled().text.push_back({x, y, text}); }
    void sendBuffer() { ++hostOled().sends; bus.byte_cb(&bus, U8X8_MSG_BYTE_SEND, 0, nullptr); }
};
