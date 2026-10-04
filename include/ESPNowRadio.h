#pragma once

#include <Arduino.h>
#include "Protocol.h"


namespace ESPNowRadio
{
    // RSSI input for local diagnostics/proximity; classification can select transport.
    struct RssiObservation
    {
        Protocol::Message message;
        uint8_t sourceMac[6];
        int8_t rssi;
        uint32_t receivedAt; // millis() captured in the observer, not at dequeue.
    };
    bool takeRssiObservation(RssiObservation& observation);

    // Sleep-drain observations; counters are shared with Wi-Fi callbacks.
    unsigned txInFlight();
    bool receiveCallbackActive();
    // Registered receive handler runs in the Wi-Fi task and queues packet bytes.
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
