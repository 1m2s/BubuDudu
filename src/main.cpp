#include <Arduino.h>
#include <Wire.h>

// ======================================================
// ADXL345 I2C address
// ======================================================

const uint8_t ADXL345_ADDRESS = 0x53;


// ======================================================
// ADXL345 registers
// ======================================================

const uint8_t THRESH_ACT    = 0x24;
const uint8_t THRESH_INACT  = 0x25;
const uint8_t TIME_INACT    = 0x26;
const uint8_t ACT_INACT_CTL = 0x27;

const uint8_t POWER_CTL     = 0x2D;
const uint8_t INT_ENABLE    = 0x2E;
const uint8_t INT_SOURCE    = 0x30;

const uint8_t DATA_FORMAT   = 0x31;
const uint8_t DATAX0        = 0x32;


// ======================================================
// setup()
// ======================================================

void setup()
{
    Serial.begin(115200);
    delay(1000);

    // Start I2C
    // GPIO0 = SDA
    // GPIO1 = SCL
    Wire.begin(0, 1);


    // --------------------------------------------------
    // 1. Measurement format
    //
    // FULL_RES enabled
    // +/-4 g range
    // --------------------------------------------------

    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(DATA_FORMAT);
    Wire.write(0x09);
    Wire.endTransmission();


    // --------------------------------------------------
    // 2. Activity threshold
    //
    // 1 register count = 0.0625 g
    //
    // 4 * 0.0625 g = 0.25 g
    //
    // Motion must change by roughly 0.25 g
    // to count as activity.
    // --------------------------------------------------

    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(THRESH_ACT);
    Wire.write(4);
    Wire.endTransmission();


    // --------------------------------------------------
    // 3. Inactivity threshold
    //
    // 2 * 0.0625 g = 0.125 g
    //
    // Small sensor noise such as 0.01 g is therefore
    // still safely considered stationary.
    // --------------------------------------------------

    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(THRESH_INACT);
    Wire.write(2);
    Wire.endTransmission();


    // --------------------------------------------------
    // 4. Inactivity time
    //
    // Must remain below inactivity threshold
    // for approximately 3 seconds.
    // --------------------------------------------------

    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(TIME_INACT);
    Wire.write(3);
    Wire.endTransmission();


    // --------------------------------------------------
    // 5. Activity / inactivity axis configuration
    //
    // 0xFF = 1111 1111
    //
    // Activity:
    // AC coupled
    // X Y Z enabled
    //
    // Inactivity:
    // AC coupled
    // X Y Z enabled
    // --------------------------------------------------

    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(ACT_INACT_CTL);
    Wire.write(0xFF);
    Wire.endTransmission();


    // --------------------------------------------------
    // 6. Measurement mode
    // --------------------------------------------------

    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(POWER_CTL);
    Wire.write(0x08);
    Wire.endTransmission();


    // --------------------------------------------------
    // 7. Enable activity + inactivity events
    //
    // 0x18 = 0001 1000
    //
    // Bit 4 = activity
    // Bit 3 = inactivity
    // --------------------------------------------------

    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(INT_ENABLE);
    Wire.write(0x18);
    Wire.endTransmission();


    // --------------------------------------------------
    // Read INT_SOURCE once to clear any old event
    // that may already be stored.
    // --------------------------------------------------

    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(INT_SOURCE);
    Wire.endTransmission(false);

    Wire.requestFrom(ADXL345_ADDRESS, (uint8_t)1);

    if (Wire.available())
    {
        Wire.read();
    }


    Serial.println();
    Serial.println("ADXL345 activity test");
    Serial.println("---------------------");

    Serial.println("Range: +/-4 g");
    Serial.println("FULL_RES: enabled");

    Serial.println();
    Serial.println("Activity threshold: 0.25 g");
    Serial.println("Inactivity threshold: 0.125 g");
    Serial.println("Inactivity time: 3 seconds");

    Serial.println();
    Serial.println("Leave Bubu still for >3 seconds.");
    Serial.println("Then move it.");
    Serial.println();
}


// ======================================================
// loop()
// ======================================================

void loop()
{
    // ==================================================
    // READ INTERRUPT SOURCE REGISTER
    //
    // We are NOT using the physical interrupt pin yet.
    //
    // The ESP32 simply asks the ADXL345 through I2C:
    //
    // "Did anything happen?"
    // ==================================================

    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(INT_SOURCE);
    Wire.endTransmission(false);

    Wire.requestFrom(ADXL345_ADDRESS, (uint8_t)1);


    if (Wire.available())
    {
        uint8_t interruptSource = Wire.read();


        // ----------------------------------------------
        // Bit 4 = ACTIVITY
        // ----------------------------------------------

        if (interruptSource & 0x10)
        {
            Serial.println(">>> ACTIVITY detected");
        }


        // ----------------------------------------------
        // Bit 3 = INACTIVITY
        // ----------------------------------------------

        if (interruptSource & 0x08)
        {
            Serial.println(">>> INACTIVITY detected");
        }
    }


    // ==================================================
    // READ NORMAL X/Y/Z DATA
    // ==================================================

    Wire.beginTransmission(ADXL345_ADDRESS);
    Wire.write(DATAX0);
    Wire.endTransmission(false);

    Wire.requestFrom(ADXL345_ADDRESS, (uint8_t)6);


    if (Wire.available() == 6)
    {
        int16_t x = Wire.read() | (Wire.read() << 8);
        int16_t y = Wire.read() | (Wire.read() << 8);
        int16_t z = Wire.read() | (Wire.read() << 8);

        float xG = x * 0.0039f;
        float yG = y * 0.0039f;
        float zG = z * 0.0039f;


        Serial.print("X: ");
        Serial.print(xG, 3);

        Serial.print(" g   Y: ");
        Serial.print(yG, 3);

        Serial.print(" g   Z: ");
        Serial.print(zG, 3);

        Serial.println(" g");
    }


    delay(100);
}