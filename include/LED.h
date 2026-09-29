#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// Loop-owned presentation only; never gates communication, motion or sleep.
class LED
{
public:
    LED();
    void begin();
    void requestHeartbeat(); // Restart on the next update; no hardware work here.
    void update(uint32_t now);
    void off();              // Cancel immediately, including a requested heartbeat.
    bool busy() const;

private:
    enum class Phase : uint8_t { Idle, Requested, Pulse1Up, Pulse1Down, Gap, Pulse2Up, Pulse2Down };
    Adafruit_NeoPixel pixel;
    Phase phase = Phase::Idle;
    uint32_t startedAt = 0;
    uint8_t shownRed = 0;
};
