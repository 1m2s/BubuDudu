#pragma once

#include <Arduino.h>
#include "Protocol.h"


namespace ESPNowRadio
{
    // Local diagnostics only; never transmitted or used for protocol delivery.
    struct RssiObservation
    {
        Protocol::Message message;
        uint8_t sourceMac[6];
        int8_t rssi;
        uint32_t receivedAt; // millis() captured in the observer, not at dequeue.
    };
    bool takeRssiObservation(RssiObservation& observation);

    // Application observations only; counters are shared with Wi-Fi callbacks.
    unsigned txInFlight();
    bool receiveCallbackActive();
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
