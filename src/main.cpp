#include <Arduino.h>

#include "Config.h"
#include "CC1101Radio.h"


bool radioReady = false;
unsigned long nextSendTime = 0;

constexpr unsigned long SEND_PERIOD_MS = 4000;


#ifdef DEVICE_BUBU

const uint8_t TX_MESSAGE[] =
{
    'B', 'U', 'B', 'U',
    ' ', '-', '>', ' ',
    'D', 'U', 'D', 'U'
};

constexpr unsigned long FIRST_SEND_DELAY_MS = 1500;

#elif defined(DEVICE_DUDU)

const uint8_t TX_MESSAGE[] =
{
    'D', 'U', 'D', 'U',
    ' ', '-', '>', ' ',
    'B', 'U', 'B', 'U'
};

constexpr unsigned long FIRST_SEND_DELAY_MS = 3000;

#endif


void sendTestPacket()
{
    bool success =
        CC1101Radio::sendPacket(
            TX_MESSAGE,
            sizeof(TX_MESSAGE)
        );

    if (success)
    {
        Serial.print("TX OK: ");

        for (uint8_t i = 0; i < sizeof(TX_MESSAGE); i++)
        {
            Serial.write(TX_MESSAGE[i]);
        }

        Serial.println();
    }
    else
    {
        Serial.println("TX FAILED");
    }


    /*
     * sendPacket() finishes in IDLE.
     *
     * Both devices should normally be listening,
     * so immediately return the radio to RX.
     */
    if (!CC1101Radio::startReceive())
    {
        Serial.println("FAILED TO RETURN TO RX");
        radioReady = false;
    }
}


void checkForReceivedPacket()
{
    uint8_t buffer[32];
    uint8_t length = 0;

    if (
        CC1101Radio::receivePacket(
            buffer,
            sizeof(buffer),
            length
        )
    )
    {
        Serial.print("RX OK: ");

        for (uint8_t i = 0; i < length; i++)
        {
            Serial.write(buffer[i]);
        }

        Serial.println();
    }
}


void setup()
{
    Serial.begin(115200);

    delay(1500);

    Serial.println();
    Serial.println("========================================");
    Serial.println("CC1101 bidirectional packet test");
    Serial.println("========================================");

    Serial.printf("Device: %s\n", DEVICE_NAME);


    CC1101Radio::begin();


    Serial.println();
    Serial.println("Resetting CC1101...");

    if (!CC1101Radio::reset())
    {
        Serial.println("RESET FAILED");
        return;
    }

    Serial.println("RESET OK");


    Serial.println();
    Serial.println("Configuring packet radio...");

    if (!CC1101Radio::configureForPacketTest())
    {
        Serial.println("RADIO CONFIG FAILED");
        return;
    }

    Serial.println("RADIO CONFIG OK");


    Serial.println();
    Serial.println("Entering RX...");

    if (!CC1101Radio::startReceive())
    {
        Serial.println("FAILED TO ENTER RX");
        return;
    }

    Serial.println("RX OK");


    radioReady = true;

    nextSendTime =
        millis() + FIRST_SEND_DELAY_MS;

    Serial.println();
    Serial.println("Bidirectional test running...");
}


void loop()
{
    if (!radioReady)
    {
        return;
    }


    /*
     * Most of the time both radios stay in RX
     * and check whether a packet has arrived.
     */
    checkForReceivedPacket();


    /*
     * When this device's transmit slot arrives,
     * temporarily leave RX, transmit, then go
     * straight back into RX.
     */
    if ((long)(millis() - nextSendTime) >= 0)
    {
        sendTestPacket();

        nextSendTime += SEND_PERIOD_MS;
    }


    delay(5);
}