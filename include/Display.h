#pragma once

#include <U8g2lib.h>


class Display
{
public:

    Display();

    bool begin();

    void showStatus(
        const char* deviceName,
        const char* peer,
        const char* distance,
        const char* radio,
        const char* state
    );


private:

    U8G2_SH1106_128X64_NONAME_F_HW_I2C oled;
};
