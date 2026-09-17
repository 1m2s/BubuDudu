#include <Arduino.h>

#include "RadioTask.h"


void setup()
{
    Serial.begin(115200);

    delay(1500);


    Serial.println();
    Serial.println(
        "Starting BubuDudu RTOS test..."
    );


    if (!RadioTask::begin())
    {
        Serial.println(
            "FAILED TO CREATE RadioTask"
        );

        return;
    }


    Serial.println(
        "RadioTask created successfully."
    );
}


void loop()
{
    /*
     * The CC1101 no longer belongs to loop().
     *
     * RadioTask owns it.
     *
     * Later this main firmware will initialize the
     * other BubuDudu subsystems too.
     */

    delay(1000);
}