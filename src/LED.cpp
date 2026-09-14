#include "LED.h"


// ======================================================
// Hardware configuration
// ======================================================

namespace
{
    constexpr uint8_t LED_PIN = 21;
    constexpr uint8_t LED_COUNT = 1;
}


// ======================================================
// Constructor
// ======================================================

LED::LED()
    : pixel(
        LED_COUNT,
        LED_PIN,
        NEO_GRB + NEO_KHZ800
    )
{
}


// ======================================================
// Initialize LED
// ======================================================

void LED::begin()
{
    pixel.begin();

    off();
}


// ======================================================
// Turn LED off
// ======================================================

void LED::off()
{
    pixel.clear();
    pixel.show();
}


// ======================================================
// One heartbeat pulse
// ======================================================

void LED::heartbeatPulse(
    uint8_t peakBrightness
)
{
    // Fade IN
    for (
        int brightness = 0;
        brightness <= peakBrightness;
        brightness += 5
    )
    {
        pixel.setPixelColor(
            0,
            pixel.Color(
                brightness,
                0,
                0
            )
        );

        pixel.show();

        delay(4);
    }


    // Fade OUT
    for (
        int brightness = peakBrightness;
        brightness >= 0;
        brightness -= 5
    )
    {
        pixel.setPixelColor(
            0,
            pixel.Color(
                brightness,
                0,
                0
            )
        );

        pixel.show();

        delay(4);
    }


    off();
}


// ======================================================
// Full heartbeat
// ======================================================

void LED::heartbeat()
{
    // First beat
    heartbeatPulse(180);


    // Short gap between beats
    delay(80);


    // Second beat
    heartbeatPulse(255);


    // Longer pause before next heartbeat
    delay(650);
}