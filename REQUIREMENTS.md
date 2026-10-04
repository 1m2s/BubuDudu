# Features and remaining work

This page describes the v1 code. [Final results](FINAL_FIRMWARE_TEST.md) explain
what worked on the devices and what is still unreliable.

## Implemented behavior

| Feature | Behavior |
| --- | --- |
| Shared firmware | Bubu and Dudu use the same source. Either can send and receive. |
| Button heartbeat | One stable press creates one request. The partner plays a 700 ms double pulse. Repeated copies do not repeat the action. |
| Delivery | Eight-byte messages with IDs and reply matching. Wait 300 ms, retry up to twice using the same ID, then give up. |
| Radio choice | CLOSE normally uses ESP-NOW; FAR uses CC1101. Failed ESP-NOW attempts can lead to CC1101. A fresh CLOSE result can switch back. A message being sent keeps its radio; sleep messages use ESP-NOW. |
| Motion/proximity | ADXL345 detects movement and inactivity. ESP-NOW signal strength gives a rough CLOSE/FAR result. A failed check keeps the previous result; UNKNOWN is also possible. |
| Sleep | After 35 seconds of local inactivity, both devices must agree to sleep. Movement, button input and unfinished work can delay entry. There is no timer wake. |
| Wake | Motion on GPIO3, CC1101 on GPIO4 or the button on GPIO5 wakes the ESP32. Startup checks saved message history and unread radio packets. |
| Output | LED animations run without long waits. Background pulses repeat every 2.5 s in CLOSE or 6 s in FAR when allowed. The OLED shows device, partner, proximity, radio, status and motion. |

[Button details](BUTTON_HEARTBEAT.md) · [Code layout](ARCHITECTURE.md) ·
[Hardware](INTERFACES.md)

## Problems and missing tests

- Proximity and radio switching are unreliable. The stationary device can stay
  FAR after its partner returns.
- CC1101 startup/stopped-radio errors, some sleep failures and OLED problems
  remain unresolved.
- The 0.625 g awake motion setting and proximity thresholds need proper testing.
- Full two-device tests, reliable fallback and overlapping wake/button traffic
  are not fully checked on hardware.
- Current draw, battery life and charging under load were not measured.

## Ideas not built

Gestures, battery readings, extra screen fields, more LED status patterns,
MOSFET power switching and a physical comparator battery indicator remain ideas.
Motion detection is implemented; gesture recognition is not.

Development is paused. The [restart plan](docs/V1_REFLECTION.md#saved-restart-point)
starts with the stationary-device problem.
