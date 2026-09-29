#pragma once

#include <cassert>
#include <cstdint>

constexpr uint16_t NEO_GRB = 0x52, NEO_KHZ800 = 0;

// One-pixel output recorder only; the harness runs the real LED state machine.
struct HostPixelState
{
    uint32_t buffered = 0, shown = 0;
    unsigned begins = 0, shows = 0;
    bool ready = true;
    void (*onBegin)() = nullptr;
};
inline HostPixelState& hostPixel() { static HostPixelState state; return state; }

class Adafruit_NeoPixel
{
public:
    Adafruit_NeoPixel(uint16_t count, int16_t pin, uint16_t type)
    { assert(count == 1 && pin == 21 && type == NEO_GRB + NEO_KHZ800); }
    void begin() { if (hostPixel().onBegin) hostPixel().onBegin(); ++hostPixel().begins; }
    void clear() { hostPixel().buffered = 0; }
    bool canShow() const { return hostPixel().ready; }
    void setPixelColor(uint16_t index, uint8_t red, uint8_t green, uint8_t blue)
    {
        assert(index == 0 && green == 0 && blue == 0);
        hostPixel().buffered = uint32_t(red) << 16;
    }
    void show() { hostPixel().shown = hostPixel().buffered; ++hostPixel().shows; }
};
