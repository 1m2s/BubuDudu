#include <Arduino.h>

#include "Config.h"
#include "CC1101Radio.h"

bool radioReady = false;


void setup()
{
    Serial.begin(115200);

    delay(1500);

    Serial.println();
    Serial.println("========================================");
    Serial.println("CC1101 first one-way packet test");
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
    Serial.println("Configuring 433.92 MHz packet radio...");

    if (!CC1101Radio::configureForPacketTest())
    {
        Serial.println("RADIO CONFIG FAILED");
        return;
    }

    Serial.println("RADIO CONFIG OK");

    radioReady = true;


#ifdef DEVICE_BUBU

    Serial.println();
    Serial.println("Role: TRANSMITTER");
    Serial.println("Bubu will send a packet every 2 seconds.");

#elif defined(DEVICE_DUDU)

    Serial.println();
    Serial.println("Role: RECEIVER");

    if (!CC1101Radio::startReceive())
    {
        Serial.println("FAILED TO ENTER RX");
        radioReady = false;
        return;
    }

    Serial.println("Dudu listening...");

#endif
}


void loop()
{
    if (!radioReady)
    {
        return;
    }


#ifdef DEVICE_BUBU

    static unsigned long lastSend = 0;

    if (millis() - lastSend >= 2000)
    {
        lastSend = millis();

        const uint8_t message[] =
        {
            'H', 'E', 'L', 'L', 'O',
            ' ',
            'D', 'U', 'D', 'U'
        };

        bool success =
            CC1101Radio::sendPacket(
                message,
                sizeof(message)
            );

        if (success)
        {
            Serial.println("TX OK: HELLO DUDU");
        }
        else
        {
            Serial.println("TX FAILED");
        }
    }


#elif defined(DEVICE_DUDU)

    uint8_t buffer[32];

    uint8_t length = 0;

    if (
        CC1101Radio::receivePacket(
            buffer,
            sizeof(buffer) - 1,
            length
        )
    )
    {
        buffer[length] = '\0';

        Serial.print("RX OK: ");

        for (uint8_t i = 0; i < length; i++)
        {
            Serial.write(buffer[i]);
        }

        Serial.println();
    }

    delay(5);

#endif
}