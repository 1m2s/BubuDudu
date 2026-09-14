#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>


// ======================================================
// LED
//
// Responsible for:
// - WS2812B initialization
// - LED output
// - heartbeat animation
//
// NOT responsible for:
// - deciding when the system sleeps
// - motion detection
// - communication
// - system state
// ======================================================

class LED
{
public:
    // Create the WS2812B object.
    LED();

    // Initialize the LED hardware.
    void begin();

    // Turn the LED completely off.
    void off();

    // Play one complete blocking heartbeat.
    void heartbeat();


private:
    Adafruit_NeoPixel pixel;


    // Play one fade-in / fade-out pulse.
    void heartbeatPulse(
        uint8_t peakBrightness
    );
};