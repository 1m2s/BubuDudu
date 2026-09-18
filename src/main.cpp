#include <Arduino.h>

#include "Config.h"
#include "ESPNowRadio.h"


namespace
{
    // ======================================================
    // Device-specific test message
    // ======================================================

#ifdef DEVICE_BUBU

    constexpr char TEST_MESSAGE[] =
        "HELLO FROM BUBU";

#elif defined(DEVICE_DUDU)

    constexpr char TEST_MESSAGE[] =
        "HELLO FROM DUDU";

#else

#error "Device identity not configured"

#endif


    // ======================================================
    // ESP-NOW test timing
    //
    // BOTH devices use the same timing intentionally.
    //
    // First transmission:
    //     1 second after startup
    //
    // Then:
    //     every 3 seconds
    //
    // This lets us test simultaneous bidirectional traffic.
    // ======================================================

    constexpr unsigned long FIRST_SEND_DELAY_MS =
        1000;

    constexpr unsigned long SEND_INTERVAL_MS =
        3000;


    unsigned long nextSendTime = 0;
}


// ==========================================================
// Arduino setup
// ==========================================================

void setup()
{
    Serial.begin(
        115200
    );


    delay(
        1500
    );


    Serial.println();
    Serial.println(
        "Starting BubuDudu ESP-NOW test..."
    );


    // ------------------------------------------------------
    // Initialize Wi-Fi + ESP-NOW + peer.
    // ------------------------------------------------------

    if (
        !ESPNowRadio::begin()
    )
    {
        Serial.println(
            "ESP-NOW STARTUP FAILED"
        );

        return;
    }


    Serial.println(
        "ESP-NOW startup successful."
    );


    // ------------------------------------------------------
    // Schedule first ESP-NOW transmission.
    //
    // Bubu and Dudu both use the SAME delay.
    // ------------------------------------------------------

    nextSendTime =
        millis() +
        FIRST_SEND_DELAY_MS;
}


// ==========================================================
// Arduino loop
// ==========================================================

void loop()
{
    // ------------------------------------------------------
    // Check whether it is time for our next transmission.
    // ------------------------------------------------------

    if (
        (long)(
            millis() -
            nextSendTime
        ) >= 0
    )
    {
        Serial.printf(
            "TX REQUEST | %s\n",
            TEST_MESSAGE
        );


        // --------------------------------------------------
        // Ask ESPNowRadio to send our temporary text packet.
        // --------------------------------------------------

        bool accepted =
            ESPNowRadio::sendText(
                TEST_MESSAGE
            );


        if (!accepted)
        {
            Serial.println(
                "TX REQUEST FAILED"
            );
        }


        // --------------------------------------------------
        // Schedule next transmission.
        // --------------------------------------------------

        nextSendTime =
            millis() +
            SEND_INTERVAL_MS;
    }


    delay(
        10
    );
}