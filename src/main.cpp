#include <Arduino.h>
#include "Config.h"

void setup()
{
    Serial.begin(115200);
}

void loop()
{
    Serial.print("Device identity: ");
    Serial.println(DEVICE_NAME);

    delay(1000);
}