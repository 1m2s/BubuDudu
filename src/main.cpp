#include <Arduino.h>
#include <SPI.h>

// ============================================================
// CC1101 <-> ESP32-C3 wiring
// ============================================================

constexpr uint8_t CC1101_CSN  = 10;
constexpr uint8_t CC1101_SCK  = 6;
constexpr uint8_t CC1101_MOSI = 7;
constexpr uint8_t CC1101_MISO = 20;


// ============================================================
// CC1101 commands / addresses used in this test
// ============================================================

// IOCFG2 configuration register address
constexpr uint8_t CC1101_IOCFG2_WRITE = 0x00;

// Read bit added to address 0x00
constexpr uint8_t CC1101_IOCFG2_READ = 0x80;

// Status register reads
constexpr uint8_t CC1101_PARTNUM_READ = 0xF0;
constexpr uint8_t CC1101_VERSION_READ = 0xF1;

// Reset command strobe
constexpr uint8_t CC1101_SRES = 0x30;


// ============================================================
// SPI settings
// ============================================================

SPISettings cc1101SPISettings(
    100000,      // 100 kHz
    MSBFIRST,
    SPI_MODE0
);


// ============================================================
// Wait for CHIP_RDYn
//
// With CSN LOW, CC1101 uses MISO/SO as CHIP_RDYn.
// HIGH = not ready
// LOW  = ready
// ============================================================

bool waitForCC1101Ready(uint32_t timeoutUs)
{
    uint32_t startTime = micros();

    while (digitalRead(CC1101_MISO) == HIGH)
    {
        if (micros() - startTime >= timeoutUs)
        {
            return false;
        }
    }

    return true;
}


// ============================================================
// Start one normal CC1101 SPI transaction
// ============================================================

bool beginCC1101Transaction()
{
    SPI.beginTransaction(cc1101SPISettings);

    digitalWrite(CC1101_CSN, LOW);

    if (!waitForCC1101Ready(5000))
    {
        digitalWrite(CC1101_CSN, HIGH);
        SPI.endTransaction();

        return false;
    }

    return true;
}


// ============================================================
// End one normal CC1101 SPI transaction
// ============================================================

void endCC1101Transaction()
{
    digitalWrite(CC1101_CSN, HIGH);

    SPI.endTransaction();
}


// ============================================================
// Write one CC1101 configuration register
// ============================================================

bool writeCC1101Register(uint8_t address, uint8_t value)
{
    if (!beginCC1101Transaction())
    {
        return false;
    }

    SPI.transfer(address);
    SPI.transfer(value);

    endCC1101Transaction();

    return true;
}


// ============================================================
// Read one CC1101 register
//
// command is already the complete read command.
// Examples:
//
// IOCFG2  -> 0x80
// PARTNUM -> 0xF0
// VERSION -> 0xF1
// ============================================================

bool readCC1101Register(uint8_t command, uint8_t &value)
{
    if (!beginCC1101Transaction())
    {
        return false;
    }

    SPI.transfer(command);

    value = SPI.transfer(0x00);

    endCC1101Transaction();

    return true;
}


// ============================================================
// Manual CC1101 reset
//
// Datasheet sequence:
//
// 1. SCLK HIGH, SI LOW
// 2. Pulse CSN LOW -> HIGH
// 3. Wait at least 40 us
// 4. CSN LOW
// 5. Wait for SO / CHIP_RDYn LOW
// 6. Send SRES = 0x30
// 7. Wait for SO LOW again
// 8. CSN HIGH
// ============================================================

bool resetCC1101()
{
    // Stop hardware SPI temporarily so we can manually
    // control SCK and MOSI during the beginning of reset.
    SPI.end();

    pinMode(CC1101_CSN, OUTPUT);
    pinMode(CC1101_SCK, OUTPUT);
    pinMode(CC1101_MOSI, OUTPUT);
    pinMode(CC1101_MISO, INPUT);

    // Required starting levels
    digitalWrite(CC1101_CSN, HIGH);
    digitalWrite(CC1101_SCK, HIGH);
    digitalWrite(CC1101_MOSI, LOW);

    delayMicroseconds(5);

    // CSN strobe
    digitalWrite(CC1101_CSN, LOW);

    delayMicroseconds(10);

    digitalWrite(CC1101_CSN, HIGH);

    // Datasheet requires >= 40 us
    delayMicroseconds(50);

    // Return control to hardware SPI
    SPI.begin(
        CC1101_SCK,
        CC1101_MISO,
        CC1101_MOSI,
        CC1101_CSN
    );

    SPI.beginTransaction(cc1101SPISettings);

    // Select CC1101
    digitalWrite(CC1101_CSN, LOW);

    // Wait until chip is ready for SRES
    if (!waitForCC1101Ready(5000))
    {
        digitalWrite(CC1101_CSN, HIGH);
        SPI.endTransaction();

        return false;
    }

    // Send reset command strobe
    SPI.transfer(CC1101_SRES);

    // Keep CSN LOW and wait until reset is finished
    if (!waitForCC1101Ready(5000))
    {
        digitalWrite(CC1101_CSN, HIGH);
        SPI.endTransaction();

        return false;
    }

    digitalWrite(CC1101_CSN, HIGH);

    SPI.endTransaction();

    return true;
}


// ============================================================
// Read and print CC1101 identity
// ============================================================

void printCC1101Identity()
{
    uint8_t iocfg2;
    uint8_t partnum;
    uint8_t version;

    bool iocfg2OK =
        readCC1101Register(CC1101_IOCFG2_READ, iocfg2);

    bool partnumOK =
        readCC1101Register(CC1101_PARTNUM_READ, partnum);

    bool versionOK =
        readCC1101Register(CC1101_VERSION_READ, version);

    if (!iocfg2OK || !partnumOK || !versionOK)
    {
        Serial.println("ERROR: CC1101 register read failed");
        return;
    }

    Serial.printf(
        "IOCFG2: 0x%02X | PARTNUM: 0x%02X | VERSION: 0x%02X\n",
        iocfg2,
        partnum,
        version
    );
}


// ============================================================
// Setup
// ============================================================

void setup()
{
    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("CC1101 SPI bring-up test");
    Serial.println("========================================");

    pinMode(CC1101_CSN, OUTPUT);

    digitalWrite(CC1101_CSN, HIGH);

    SPI.begin(
        CC1101_SCK,
        CC1101_MISO,
        CC1101_MOSI,
        CC1101_CSN
    );


    // --------------------------------------------------------
    // TEST 1:
    // Deliberately change IOCFG2 from 0x29 to 0x2E
    // --------------------------------------------------------

    Serial.println();
    Serial.println("TEST 1: Writing IOCFG2 = 0x2E");

    if (!writeCC1101Register(
            CC1101_IOCFG2_WRITE,
            0x2E))
    {
        Serial.println("ERROR: IOCFG2 write failed");
        return;
    }


    // Read it back
    uint8_t beforeReset;

    if (!readCC1101Register(
            CC1101_IOCFG2_READ,
            beforeReset))
    {
        Serial.println("ERROR: IOCFG2 read failed");
        return;
    }

    Serial.printf(
        "Before reset IOCFG2: 0x%02X\n",
        beforeReset
    );


    // --------------------------------------------------------
    // TEST 2:
    // Perform SRES
    // --------------------------------------------------------

    Serial.println();
    Serial.println("TEST 2: Sending SRES");

    if (resetCC1101())
    {
        Serial.println("RESET OK");
    }
    else
    {
        Serial.println("RESET ERROR");
        return;
    }


    // --------------------------------------------------------
    // TEST 3:
    // Read identity after reset
    // --------------------------------------------------------

    Serial.println();
    Serial.println("TEST 3: Identity after reset");

    printCC1101Identity();

    Serial.println();
    Serial.println("Repeating identity every 2 seconds...");
}


// ============================================================
// Loop
// ============================================================

void loop()
{
    printCC1101Identity();

    delay(2000);
}