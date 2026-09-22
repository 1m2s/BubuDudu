#include "CC1101Radio.h"

#include <SPI.h>
#include <driver/gpio.h>
#include <cstring>

namespace
{
    constexpr uint8_t PIN_CS   = 10;
    constexpr uint8_t PIN_SCK  = 6;
    constexpr uint8_t PIN_MOSI = 7;
    constexpr uint8_t PIN_MISO = 20;

    constexpr uint32_t SPI_FREQUENCY = 100000;

    // Configuration registers
    constexpr uint8_t IOCFG0   = 0x02;
    constexpr uint8_t FIFOTHR  = 0x03;
    constexpr uint8_t SYNC1    = 0x04;
    constexpr uint8_t SYNC0    = 0x05;
    constexpr uint8_t PKTLEN   = 0x06;
    constexpr uint8_t PKTCTRL1 = 0x07;
    constexpr uint8_t PKTCTRL0 = 0x08;
    constexpr uint8_t CHANNR   = 0x0A;
    constexpr uint8_t FSCTRL1  = 0x0B;
    constexpr uint8_t FSCTRL0  = 0x0C;

    constexpr uint8_t FREQ2 = 0x0D;
    constexpr uint8_t FREQ1 = 0x0E;
    constexpr uint8_t FREQ0 = 0x0F;

    constexpr uint8_t MDMCFG4 = 0x10;
    constexpr uint8_t MDMCFG3 = 0x11;
    constexpr uint8_t MDMCFG2 = 0x12;
    constexpr uint8_t MDMCFG1 = 0x13;
    constexpr uint8_t MDMCFG0 = 0x14;
    constexpr uint8_t DEVIATN = 0x15;

    constexpr uint8_t MCSM1 = 0x17;
    constexpr uint8_t MCSM0 = 0x18;

    constexpr uint8_t FOCCFG   = 0x19;
    constexpr uint8_t BSCFG    = 0x1A;
    constexpr uint8_t AGCCTRL2 = 0x1B;
    constexpr uint8_t AGCCTRL1 = 0x1C;
    constexpr uint8_t AGCCTRL0 = 0x1D;

    constexpr uint8_t FREND1 = 0x21;
    constexpr uint8_t FREND0 = 0x22;

    constexpr uint8_t FSCAL3 = 0x23;
    constexpr uint8_t FSCAL2 = 0x24;
    constexpr uint8_t FSCAL1 = 0x25;
    constexpr uint8_t FSCAL0 = 0x26;

    constexpr uint8_t TEST2 = 0x2C;
    constexpr uint8_t TEST1 = 0x2D;
    constexpr uint8_t TEST0 = 0x2E;

    // Command strobes / status registers
    constexpr uint8_t SRES  = 0x30;
    constexpr uint8_t SRX   = 0x34;
    constexpr uint8_t STX   = 0x35;
    constexpr uint8_t SIDLE = 0x36;
    constexpr uint8_t SFRX  = 0x3A;
    constexpr uint8_t SFTX  = 0x3B;

    constexpr uint8_t PARTNUM   = 0x30;
    constexpr uint8_t VERSION   = 0x31;
    constexpr uint8_t MARCSTATE = 0x35;
    constexpr uint8_t TXBYTES   = 0x3A;
    constexpr uint8_t RXBYTES   = 0x3B;

    constexpr uint8_t PATABLE = 0x3E;
    constexpr uint8_t FIFO    = 0x3F;

    constexpr uint8_t READ_SINGLE = 0x80;
    constexpr uint8_t READ_BURST  = 0xC0;
    constexpr uint8_t WRITE_BURST = 0x40;

    constexpr uint8_t MARC_IDLE = 0x01;
    constexpr uint8_t MARC_RX   = 0x0D;

    constexpr uint8_t MARC_RXFIFO_OVERFLOW = 0x11;
    constexpr uint8_t MARC_TXFIFO_UNDERFLOW = 0x16;

    constexpr uint8_t MAX_PACKET_LENGTH = 61;

    // 433.92 MHz
    constexpr uint8_t FREQ2_433_92 = 0x10;
    constexpr uint8_t FREQ1_433_92 = 0xB0;
    constexpr uint8_t FREQ0_433_92 = 0x71;

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

            // A disconnected radio must not busy-spin the owning task.
            delay(1);
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


    bool strobe(uint8_t command)
    {
        SPI.beginTransaction(cc1101SpiSettings);

        digitalWrite(PIN_CS, LOW);

        if (!waitUntilReady())
        {
            digitalWrite(PIN_CS, HIGH);
            SPI.endTransaction();

            return false;
        }

        SPI.transfer(command);

        digitalWrite(PIN_CS, HIGH);

        SPI.endTransaction();

        return true;
    }


    bool writeBurst(
        uint8_t address,
        const uint8_t* data,
        uint8_t length
    )
    {
        SPI.beginTransaction(cc1101SpiSettings);

        digitalWrite(PIN_CS, LOW);

        if (!waitUntilReady())
        {
            digitalWrite(PIN_CS, HIGH);
            SPI.endTransaction();

            return false;
        }

        SPI.transfer(address | WRITE_BURST);

        for (uint8_t i = 0; i < length; i++)
        {
            SPI.transfer(data[i]);
        }

        digitalWrite(PIN_CS, HIGH);

        SPI.endTransaction();

        return true;
    }


    bool readBurst(
        uint8_t address,
        uint8_t* data,
        uint8_t length
    )
    {
        SPI.beginTransaction(cc1101SpiSettings);

        digitalWrite(PIN_CS, LOW);

        if (!waitUntilReady())
        {
            digitalWrite(PIN_CS, HIGH);
            SPI.endTransaction();

            return false;
        }

        SPI.transfer(address | READ_BURST);

        for (uint8_t i = 0; i < length; i++)
        {
            data[i] = SPI.transfer(0x00);
        }

        digitalWrite(PIN_CS, HIGH);

        SPI.endTransaction();

        return true;
    }


    bool waitForState(
        uint8_t wantedState,
        unsigned long timeoutMs
    )
    {
        const unsigned long start = millis();

        while (millis() - start < timeoutMs)
        {
            uint8_t state =
                readStatusRegister(MARCSTATE) & 0x1F;

            if (state == wantedState)
            {
                return true;
            }

            delay(1);
        }

        return false;
    }
}


namespace CC1101Radio
{
    bool begin()
    {
        // Set the output latch before enabling/releasing the CS pad.
        digitalWrite(PIN_CS, HIGH);
        pinMode(PIN_CS, OUTPUT);

        if (!holdChipSelectForDeepSleep(false))
            return false;

        SPI.begin(
            PIN_SCK,
            PIN_MISO,
            PIN_MOSI,
            PIN_CS
        );

        return true;
    }


    bool holdChipSelectForDeepSleep(bool hold)
    {
        const gpio_num_t cs = static_cast<gpio_num_t>(PIN_CS);
        if (hold)
        {
            digitalWrite(PIN_CS, HIGH);
            if (gpio_hold_en(cs) != ESP_OK)
                return false;
            gpio_deep_sleep_hold_en();
            return true;
        }
        // begin() configured CS HIGH before releasing a retained pad.
        // Cancellation uses the same HIGH configuration from before sleep.
        gpio_deep_sleep_hold_dis();
        return gpio_hold_dis(cs) == ESP_OK;
    }


    bool reset()
    {
        SPI.beginTransaction(cc1101SpiSettings);

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


    uint8_t readMarcState()
    {
        return readStatusRegister(MARCSTATE);
    }


    uint8_t readRxBytes()
    {
        return readStatusRegister(RXBYTES);
    }


    bool setFrequency433_92MHz()
    {
        writeRegister(FREQ2, FREQ2_433_92);
        writeRegister(FREQ1, FREQ1_433_92);
        writeRegister(FREQ0, FREQ0_433_92);

        return
            readRegister(FREQ2) == FREQ2_433_92 &&
            readRegister(FREQ1) == FREQ1_433_92 &&
            readRegister(FREQ0) == FREQ0_433_92;
    }


    bool configureForPacketTest()
    {
        /*
         * First simple BubuDudu CC1101 PHY:
         *
         * Frequency:      433.92 MHz
         * Modulation:     GFSK
         * Data rate:      ~38.4 kBaud
         * Deviation:      ~20 kHz
         * RX bandwidth:   ~100 kHz
         * Packet mode:    variable length
         * CRC:            enabled
         * CRC auto flush: enabled
         */

        writeRegister(FIFOTHR, 0x47);

        // GDO0: assert on CRC-valid packet; hold until first RX FIFO byte read.
        // This also restores the wake output after bounded radio recovery.
        writeRegister(IOCFG0, 0x07);

        // Explicit sync word used by both radios
        writeRegister(SYNC1, 0xD3);
        writeRegister(SYNC0, 0x91);

        // Maximum payload length
        writeRegister(PKTLEN, MAX_PACKET_LENGTH);

        // CRC auto flush, no address filtering
        writeRegister(PKTCTRL1, 0x08);

        // Variable packet length + CRC enabled
        writeRegister(PKTCTRL0, 0x05);

        writeRegister(CHANNR, 0x00);

        writeRegister(FSCTRL1, 0x06);
        writeRegister(FSCTRL0, 0x00);

        if (!setFrequency433_92MHz())
        {
            return false;
        }

        // ~38.4 kBaud GFSK settings
        writeRegister(MDMCFG4, 0xCA);
        writeRegister(MDMCFG3, 0x83);
        writeRegister(MDMCFG2, 0x13);
        writeRegister(MDMCFG1, 0x22);
        writeRegister(MDMCFG0, 0xF8);

        writeRegister(DEVIATN, 0x35);

        // Return to IDLE after TX/RX packet
        writeRegister(MCSM1, 0x30);

        // Automatic calibration when entering RX/TX
        writeRegister(MCSM0, 0x18);

        writeRegister(FOCCFG, 0x16);
        writeRegister(BSCFG, 0x6C);

        writeRegister(AGCCTRL2, 0x43);
        writeRegister(AGCCTRL1, 0x40);
        writeRegister(AGCCTRL0, 0x91);

        writeRegister(FREND1, 0x56);
        writeRegister(FREND0, 0x10);

        writeRegister(FSCAL3, 0xE9);
        writeRegister(FSCAL2, 0x2A);
        writeRegister(FSCAL1, 0x00);
        writeRegister(FSCAL0, 0x1F);

        writeRegister(TEST2, 0x81);
        writeRegister(TEST1, 0x35);
        writeRegister(TEST0, 0x09);

        // Approximately 0 dBm at 433 MHz
        writeRegister(PATABLE, 0x60);

        // Read back the most important settings
        return
            readRegister(IOCFG0) == 0x07 &&
            readRegister(FREQ2) == 0x10 &&
            readRegister(FREQ1) == 0xB0 &&
            readRegister(FREQ0) == 0x71 &&
            readRegister(MDMCFG4) == 0xCA &&
            readRegister(MDMCFG3) == 0x83 &&
            readRegister(MDMCFG2) == 0x13 &&
            readRegister(PKTLEN) == MAX_PACKET_LENGTH &&
            readRegister(PKTCTRL1) == 0x08 &&
            readRegister(MCSM1) == 0x30 &&
            readRegister(MCSM0) == 0x18 &&
            readRegister(PKTCTRL0) == 0x05;
    }


    bool sendPacket(
        const uint8_t* data,
        uint8_t length
    )
    {
        if (length == 0 || length > MAX_PACKET_LENGTH)
        {
            return false;
        }

        if (!strobe(SIDLE))
        {
            return false;
        }

        if (!waitForState(MARC_IDLE, 50))
        {
            return false;
        }

        if (!strobe(SFTX))
        {
            return false;
        }

        uint8_t frame[MAX_PACKET_LENGTH + 1];

        // Variable-length mode requires first FIFO byte = payload length
        frame[0] = length;

        memcpy(
            &frame[1],
            data,
            length
        );

        if (!writeBurst(
                FIFO,
                frame,
                length + 1
            ))
        {
            return false;
        }

        if (!strobe(STX))
        {
            return false;
        }

        /*
         * Give the radio time to leave IDLE and begin
         * calibration / transmission.
         */
        delay(1);

        const unsigned long start = millis();

        while (millis() - start < 200)
        {
            uint8_t state =
                readStatusRegister(MARCSTATE) & 0x1F;

            if (state == MARC_TXFIFO_UNDERFLOW)
            {
                strobe(SIDLE);
                strobe(SFTX);

                return false;
            }

            if (state == MARC_IDLE)
            {
                return true;
            }

            delay(1);
        }

        return false;
    }


    bool startReceive()
    {
        if (!strobe(SIDLE))
        {
            return false;
        }

        if (!waitForState(MARC_IDLE, 50))
        {
            return false;
        }

        if (!strobe(SFRX))
        {
            return false;
        }

        if (!strobe(SRX))
        {
            return false;
        }

        return waitForState(MARC_RX, 50);
    }


    ReceiveResult receivePacket(
        uint8_t* buffer,
        uint8_t maxLength,
        uint8_t& receivedLength
    )
    {
        receivedLength = 0;

        const uint8_t status = readStatusRegister(MARCSTATE);
        if (status == 0xFF)
        {
            return {false, false};
        }
        const uint8_t state = status & 0x1F;

        if (state == MARC_RXFIFO_OVERFLOW)
        {
            return {false, startReceive()};
        }

        /*
         * Our current MCSM1 configuration returns the
         * CC1101 to IDLE after a complete RX packet.
         *
         * So if it is still in RX, the packet is not
         * complete yet.
         */
        if (state != MARC_IDLE)
        {
            return {false, true};
        }

        const uint8_t rxStatus =
            readStatusRegister(RXBYTES);

        // RXBYTES bit 7 reports overflow; bits 6:0 hold the byte count.
        if (rxStatus & 0x80)
        {
            return {false, startReceive()};
        }

        const uint8_t rxBytes = rxStatus & 0x7F;

        /*
         * IDLE with zero bytes can happen after a packet
         * was rejected, for example because CRC failed.
         * Start listening again.
         */
        if (rxBytes == 0)
        {
            return {false, startReceive()};
        }

        uint8_t raw[64];

        // Reject abnormal counts before using them as a buffer write length.
        if (rxBytes > sizeof(raw))
        {
            return {false, startReceive()};
        }

        if (!readBurst(
                FIFO,
                raw,
                rxBytes
            ))
        {
            return {false, startReceive()};
        }

        uint8_t packetLength = raw[0];

        if (
            packetLength == 0 ||
            packetLength > maxLength ||
            packetLength + 1 > rxBytes
        )
        {
            return {false, startReceive()};
        }

        memcpy(
            buffer,
            &raw[1],
            packetLength
        );

        receivedLength = packetLength;

        // Delivery is independent of whether listening can be restored.
        return {true, startReceive()};
    }
}
