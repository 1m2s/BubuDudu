#pragma once

#include <Arduino.h>

namespace CC1101Radio
{
    bool begin();
    bool reset();

    uint8_t readRegister(uint8_t address);
    void writeRegister(uint8_t address, uint8_t value);

    uint8_t readPartNumber();
    uint8_t readVersion();

    // Raw MARCSTATE for bench diagnostics; 0xFF indicates an SPI read failure.
    uint8_t readMarcState();
    // Raw RXBYTES: bit 7 is overflow, bits 6:0 are the FIFO byte count.
    uint8_t readRxBytes();

    // Keep CS inactive across the MCU's deep sleep/reboot; no radio strobes.
    bool holdChipSelectForDeepSleep(bool hold);

    bool setFrequency433_92MHz();

    bool configureForPacketTest();

    bool sendPacket(const uint8_t* data, uint8_t length);

    bool startReceive();

    struct ReceiveResult
    {
        bool packetReceived;
        // False requires recovery, even if packetReceived is true.
        // True also allows normal in-progress RX state transitions.
        bool rxReady;
    };

    ReceiveResult receivePacket(
        uint8_t* buffer,
        uint8_t maxLength,
        uint8_t& receivedLength
    );
}
