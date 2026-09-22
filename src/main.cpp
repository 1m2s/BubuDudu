#include <Arduino.h>

#include "RadioTask.h"


void setup()
{
    const bool deepWake = RadioTask::captureBootWake();
    Serial.begin(115200);

    // The external radio may be holding a wake packet. Do not spend the
    // peer's ACK/retry window waiting for USB on a deep-sleep reboot.
    if (!deepWake)
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
