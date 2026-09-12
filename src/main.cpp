#include <Arduino.h>
#include <Wire.h>

const uint8_t ADXL345_ADDRESS = 0x53;

const uint8_t POWER_CTL   = 0x2D;
const uint8_t DATA_FORMAT = 0x31;
const uint8_t DATAX0      = 0x32;

void setup()
{
    Serial.begin(115200);
    delay(1000);

    // Start I2C:
    // GPIO0 = SDA
    // GPIO1 = SCL
    Wire.begin(0, 1);

    // -------------------------------------------------
    // Configure DATA_FORMAT
    //
    // 0x09 = 0000 1001
    //
    // Bit 3 = 1  -> FULL_RES enabled
    // Bits 1:0 = 01 -> ±4 g range
    // -------------------------------------------------
    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(DATA_FORMAT);
    Wire.write(0x09);
    Wire.endTransmission();

    // -------------------------------------------------
    // Put ADXL345 into measurement mode
    //
    // POWER_CTL register = 0x2D
    // 0x08 sets the Measure bit
    // -------------------------------------------------
    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(POWER_CTL);
    Wire.write(0x08);
    Wire.endTransmission();

    Serial.println("ADXL345 configured:");
    Serial.println("FULL_RES enabled");
    Serial.println("Range: +/-4 g");
    Serial.println();
}

void loop()
{
    // Tell ADXL345 that we want to start reading
    // from DATAX0.
    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(DATAX0);
    Wire.endTransmission(false);

    // X, Y and Z each use 2 bytes.
    // Therefore we request 6 bytes total.
    Wire.requestFrom(ADXL345_ADDRESS, (uint8_t)6);

    if (Wire.available() == 6)
    {
        int16_t x = Wire.read() | (Wire.read() << 8);
        int16_t y = Wire.read() | (Wire.read() << 8);
        int16_t z = Wire.read() | (Wire.read() << 8);

        // FULL_RES scale factor is approximately
        // 3.9 mg per raw count = 0.0039 g per count.
        float xG = x * 0.0039f;
        float yG = y * 0.0039f;
        float zG = z * 0.0039f;

        Serial.print("RAW  X: ");
        Serial.print(x);
        Serial.print("  Y: ");
        Serial.print(y);
        Serial.print("  Z: ");
        Serial.println(z);

        Serial.print("g    X: ");
        Serial.print(xG, 3);
        Serial.print("  Y: ");
        Serial.print(yG, 3);
        Serial.print("  Z: ");
        Serial.println(zG, 3);

        Serial.println();
    }

    delay(500);
}