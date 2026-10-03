#pragma once

#include <U8g2lib.h>


class Display
{
public:

    Display();

    bool begin();

#ifdef DEVICE_DUDU
    // U8g2 stores the shifted address; its Wire callback shifts it back by one.
    uint8_t diagnosticI2cAddress() { return u8x8_GetI2CAddress(oled.getU8x8()) >> 1; }
#endif

    void showStatus(
        const char* deviceName,
        const char* peer,
        const char* distance,
        const char* radio,
        const char* state,
        const char* motion
    );

    void showDeepSleep(const char* deviceName);


private:

    U8G2_SH1106_128X64_NONAME_F_HW_I2C oled;
};
