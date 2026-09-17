#include <Arduino.h>

#include "CC1101Radio.h"

constexpr uint8_t IOCFG2 = 0x00;

void printIdentity()
{
    uint8_t iocfg2  = CC1101Radio::readRegister(IOCFG2);
    uint8_t partnum = CC1101Radio::readPartNumber();
    uint8_t version = CC1101Radio::readVersion();

    Serial.printf(
        "IOCFG2: 0x%02X | PARTNUM: 0x%02X | VERSION: 0x%02X\n",
        iocfg2,
        partnum,
        version
    );
}

void setup()
{
    Serial.begin(115200);

    delay(1500);

    Serial.println();
    Serial.println("========================================");
    Serial.println("CC1101 module refactor test");
    Serial.println("========================================");

    CC1101Radio::begin();

    Serial.println();
    Serial.println("TEST 1: Writing IOCFG2 = 0x2E");

    CC1101Radio::writeRegister(IOCFG2, 0x2E);

    uint8_t beforeReset = CC1101Radio::readRegister(IOCFG2);

    Serial.printf(
        "Before reset IOCFG2: 0x%02X\n",
        beforeReset
    );

    Serial.println();
    Serial.println("TEST 2: Sending SRES");

    if (CC1101Radio::reset())
    {
        Serial.println("RESET OK");
    }
    else
    {
        Serial.println("RESET FAILED");
    }

    Serial.println();
    Serial.println("TEST 3: Identity after reset");

    printIdentity();

    Serial.println();
    Serial.println("Repeating identity every 2 seconds...");
}

void loop()
{
    printIdentity();

    delay(2000);
}