#include "motion/Wire.h"
#include "../../src/Display.cpp"

uint32_t hostNow = 0;
HostSerial Serial;
HostWire Wire;
namespace MotionPlatform { bool isrAttached = false, stuckHigh = false; void (*isr)() = nullptr; }

int main()
{
    Display display;
    assert(display.begin());
    assert(Wire.begins == 0 && Wire.operations == 0); // Display never initializes or polls Motion's bus.
    assert(Wire.clock == 100000 && hostOled().clock == 100000 && hostOled().address == 0x78);
    assert(hostOled().hardwareInitializations == 0);
    for (const char* device : {"BUBU", "DUDU"})
    for (const char* motion : {"STILL", "MOVING", "SETTLING", "N/A"})
    for (const char* state : {"ACTIVE", "SLEEP NEG", "SLEEP", "WAKING"})
    {
        const auto sends = hostOled().sends;
        display.showStatus(device, "ONLINE", "CLOSE", "ESP-NOW", state, motion);
        const auto& text = hostOled().text;
        assert(hostOled().font == u8g2_font_6x10_tf && text.size() == 11);
        assert(text[0].value == device && text[0].x == 0 && text[0].y == 10);
        const char* labels[]{"PEER:", "DIST:", "RADIO:", "STATUS:", "MOTION:"};
        const char* values[]{"ONLINE", "CLOSE", "ESP-NOW", state, motion};
        for (unsigned row = 0; row < 5; ++row)
        {
            const auto& label = text[1 + 2 * row];
            const auto& value = text[2 + 2 * row];
            assert(label.value == labels[row] && value.value == values[row]);
            assert(label.x == 0 && value.x == 48 && label.y == 20 + 10 * row && value.y == label.y);
            assert(label.x + label.value.size() * 6 < value.x);
        }
        for (const auto& item : text)
        {
            assert(item.value != "STATE:" && item.value != "PWR:");
            assert(item.x + item.value.size() * 6 <= 128 && item.y >= 10 && item.y <= 60);
        }
        assert(hostOled().sends == sends + 1 && hostOled().clears == hostOled().sends);
        assert(hostOled().transfers == hostOled().sends && hostOled().hardwareInitializations == 0);
        assert(Wire.begins == 0 && Wire.operations == 0 && hostNow == 0);
    }
    puts("PASS: real OLED renderer has six readable 6x10 rows, STATUS/MOTION labels, both identities and all motion/status values within 128x64; shared-I2C init bypass retained, no polling or delays");
    for (const char* device : {"BUBU", "DUDU"})
    {
        display.showStatus(device, "ONLINE", "FAR", "CC1101", "ACTIVE", "MOVING");
        const auto sends = hostOled().sends, begins = hostOled().begins;
        display.showDeepSleep(device);
        const char* expected[]{device, "STATUS:", "DEEP SLEEP", "WAKE:", "MOTION / PEER"};
        const unsigned rows[]{10, 25, 35, 50, 60};
        const auto& text = hostOled().text;
        assert(text.size() == 5 && hostOled().font == u8g2_font_6x10_tf);
        for (unsigned i = 0; i < text.size(); ++i)
        {
            assert(text[i].value == expected[i] && text[i].x == 0 && text[i].y == rows[i]);
            assert(text[i].value.size() * 6 <= 128 && text[i].y <= 64);
        }
        assert(hostOled().sends == sends + 1 && hostOled().clears == hostOled().sends);
        assert(hostOled().begins == begins && hostOled().transfers == hostOled().sends);
        assert(hostOled().hardwareInitializations == 0 && Wire.begins == 0 && Wire.operations == 0 && hostNow == 0);
        display.showStatus(device, "ONLINE", "CLOSE", "ESP-NOW", "ACTIVE", "STILL");
        assert(hostOled().text.size() == 11 && hostOled().text[8].value == "ACTIVE");
    }
    puts("PASS: final OLED clears stale awake fields, fits both identities/DEEP SLEEP/MOTION / PEER in 128x64, transfers exactly once without reinitialization, polling or delay; awake layout restores unchanged");
}
