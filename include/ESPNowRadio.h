#pragma once

#include <Arduino.h>


namespace ESPNowRadio
{
    // Function type used when ESP-NOW receives data.
    //
    // Another part of the program can give ESPNowRadio
    // a function matching this shape.
    using ReceiveHandler =
        void (*)(
            const uint8_t* data,
            size_t length
        );


    bool begin(
        ReceiveHandler receiveHandler
    );


    bool send(
        const uint8_t* data,
        size_t length
    );
}