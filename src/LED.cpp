#include "LED.h"

namespace
{
    constexpr uint8_t LED_PIN = 21, LED_COUNT = 1;
    constexpr uint32_t FIRST_FADE_MS = 130, GAP_MS = 70, SECOND_FADE_MS = 185;
    constexpr uint32_t FIRST_END_MS = 2 * FIRST_FADE_MS;
    constexpr uint32_t SECOND_START_MS = FIRST_END_MS + GAP_MS;
    constexpr uint32_t SECOND_PEAK_MS = SECOND_START_MS + SECOND_FADE_MS;
    constexpr uint32_t HEARTBEAT_END_MS = SECOND_PEAK_MS + SECOND_FADE_MS;
}

LED::LED() : pixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800) {}

void LED::begin()
{
    pixel.begin();
    off();
}

void LED::requestHeartbeat()
{
    if (userHeartbeat) return; // Background cannot replace or extend the user pulse.
    phase = Phase::Requested;
}

void LED::requestUserHeartbeat()
{
    userHeartbeat = true;
    phase = Phase::Requested;
}

bool LED::busy() const { return phase != Phase::Idle; }
bool LED::userHeartbeatActive() const { return userHeartbeat; }

void LED::off()
{
    phase = Phase::Idle;
    userHeartbeat = false;
    startedAt = 0;
    shownRed = 0;
    pixel.clear();
    pixel.show();
}

void LED::update(uint32_t now, bool backgroundAllowed)
{
    if (!userHeartbeat && !backgroundAllowed)
    {
        if (busy()) off();
        return;
    }
    // Avoid the driver's latch wait during animation; try again next loop.
    if (phase == Phase::Idle || !pixel.canShow()) return;
    if (phase == Phase::Requested)
    {
        startedAt = now;
        phase = Phase::Pulse1Up;
    }
    // Absolute elapsed time skips missed frames without catch-up loops and
    // remains correct across millis rollover. Each call sends at most one pixel.
    const uint32_t elapsed = uint32_t(now - startedAt);
    uint8_t red = 0;
    if (elapsed < FIRST_FADE_MS)
    {
        phase = Phase::Pulse1Up;
        red = elapsed * 180 / FIRST_FADE_MS;
    }
    else if (elapsed < FIRST_END_MS)
    {
        phase = Phase::Pulse1Down;
        red = 180 - (elapsed - FIRST_FADE_MS) * 180 / FIRST_FADE_MS;
    }
    else if (elapsed < SECOND_START_MS)
        phase = Phase::Gap;
    else if (elapsed < SECOND_PEAK_MS)
    {
        phase = Phase::Pulse2Up;
        red = (elapsed - SECOND_START_MS) * 255 / SECOND_FADE_MS;
    }
    else if (elapsed < HEARTBEAT_END_MS)
    {
        phase = Phase::Pulse2Down;
        red = 255 - (elapsed - SECOND_PEAK_MS) * 255 / SECOND_FADE_MS;
    }
    else
    {
        off();
        return;
    }
    if (red == shownRed) return;
    pixel.setPixelColor(0, red, 0, 0);
    pixel.show();
    shownRed = red;
}
