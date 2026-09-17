#include "CC1101Radio.h"

#include <SPI.h>

namespace
{
    constexpr uint8_t PIN_CS   = 10;
    constexpr uint8_t PIN_SCK  = 6;
    constexpr uint8_t PIN_MOSI = 7;
    constexpr uint8_t PIN_MISO = 20;

    constexpr uint32_t SPI_FREQUENCY = 100000;

    constexpr uint8_t PARTNUM = 0x30;
    constexpr uint8_t VERSION = 0x31;

    constexpr uint8_t SRES = 0x30;

    constexpr uint8_t READ_SINGLE = 0x80;
    constexpr uint8_t READ_BURST  = 0xC0;

    SPISettings cc1101SpiSettings(
        SPI_FREQUENCY,
        MSBFIRST,
        SPI_MODE0
    );

    bool waitUntilReady()
    {
        const unsigned long start = millis();

        while (digitalRead(PIN_MISO) == HIGH)
        {
            if (millis() - start > 100)
            {
                return false;
            }
        }

        return true;
    }

    uint8_t readStatusRegister(uint8_t address)
    {
        SPI.beginTransaction(cc1101SpiSettings);

        digitalWrite(PIN_CS, LOW);

        if (!waitUntilReady())
        {
            digitalWrite(PIN_CS, HIGH);
            SPI.endTransaction();

            return 0xFF;
        }

        SPI.transfer(address | READ_BURST);

        uint8_t value = SPI.transfer(0x00);

        digitalWrite(PIN_CS, HIGH);

        SPI.endTransaction();

        return value;
    }
}


namespace CC1101Radio
{
    bool begin()
    {
        pinMode(PIN_CS, OUTPUT);

        digitalWrite(PIN_CS, HIGH);

        SPI.begin(
            PIN_SCK,
            PIN_MISO,
            PIN_MOSI,
            PIN_CS
        );

        return true;
    }


    bool reset()
    {
        SPI.beginTransaction(cc1101SpiSettings);

        /*
         * CC1101 manual reset sequence:
         *
         * CS HIGH
         * CS LOW
         * CS HIGH
         * CS LOW
         * wait for CHIP_RDYn
         * send SRES
         * wait until reset completes
         */

        digitalWrite(PIN_CS, HIGH);
        delayMicroseconds(5);

        digitalWrite(PIN_CS, LOW);
        delayMicroseconds(10);

        digitalWrite(PIN_CS, HIGH);
        delayMicroseconds(40);

        digitalWrite(PIN_CS, LOW);

        if (!waitUntilReady())
        {
            digitalWrite(PIN_CS, HIGH);
            SPI.endTransaction();

            return false;
        }

        SPI.transfer(SRES);

        if (!waitUntilReady())
        {
            digitalWrite(PIN_CS, HIGH);
            SPI.endTransaction();

            return false;
        }

        digitalWrite(PIN_CS, HIGH);

        SPI.endTransaction();

        return true;
    }


    uint8_t readRegister(uint8_t address)
    {
        SPI.beginTransaction(cc1101SpiSettings);

        digitalWrite(PIN_CS, LOW);

        if (!waitUntilReady())
        {
            digitalWrite(PIN_CS, HIGH);
            SPI.endTransaction();

            return 0xFF;
        }

        SPI.transfer(address | READ_SINGLE);

        uint8_t value = SPI.transfer(0x00);

        digitalWrite(PIN_CS, HIGH);

        SPI.endTransaction();

        return value;
    }


    void writeRegister(uint8_t address, uint8_t value)
    {
        SPI.beginTransaction(cc1101SpiSettings);

        digitalWrite(PIN_CS, LOW);

        if (!waitUntilReady())
        {
            digitalWrite(PIN_CS, HIGH);
            SPI.endTransaction();

            return;
        }

        SPI.transfer(address);

        SPI.transfer(value);

        digitalWrite(PIN_CS, HIGH);

        SPI.endTransaction();
    }


    uint8_t readPartNumber()
    {
        return readStatusRegister(PARTNUM);
    }


    uint8_t readVersion()
    {
        return readStatusRegister(VERSION);
    }
}