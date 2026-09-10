#include <Arduino.h>
#include <Wire.h>

constexpr uint8_t ADXL345_ADDRESS = 0x53;

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Wire.begin(0, 1); // SDA GPIO0, SCL GPIO1

    // Put ADXL345 into measurement mode
    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(0x2D);   // POWER_CTL register
    Wire.write(0x08);   // Measurement bit
    Wire.endTransmission();

    Serial.println("ADXL345 measuring...");
}

void loop()
{
    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(0x32); // First data register: DATAX0
    Wire.endTransmission(false);

    Wire.requestFrom(ADXL345_ADDRESS, (uint8_t)6);

    if (Wire.available() == 6)
    {
        int16_t x = Wire.read() | (Wire.read() << 8);
        int16_t y = Wire.read() | (Wire.read() << 8);
        int16_t z = Wire.read() | (Wire.read() << 8);

        Serial.print("X: ");
        Serial.print(x);

        Serial.print("  Y: ");
        Serial.print(y);

        Serial.print("  Z: ");
        Serial.println(z);
    }

    delay(300);
}