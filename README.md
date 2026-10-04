# BubuDudu

**Two companion devices, one shared embedded firmware.**

Bubu and Dudu are ESP32-C3 prototypes: press a button on one device and its
partner plays a heartbeat on a WS2812B LED. They combine motion sensing,
ESP-NOW and CC1101 radios, OLED feedback and coordinated sleep/wake.

**Status · 4 October 2026: v1 prototype, development paused.** Most features work
on their own; full two-device acceptance is incomplete. Coordinated deep sleep
and CC1101 peer wake worked in my informal demonstrations. The biggest open
problem is **proximity classification and automatic ESP-NOW/CC1101 switching**:
after one device moves away and returns, the device that stayed still often
keeps its old FAR / CC1101 state until it is moved.

[Final findings](FINAL_FIRMWARE_TEST.md) · [Learning & possible v2](docs/V1_REFLECTION.md) ·
[Architecture](ARCHITECTURE.md) · [Hardware & upload](INTERFACES.md) ·
[Builds & tests](tests/host/README.md)

## How it fits together

![Shared ESP32-C3 firmware with separate local proximity state, two radio paths and callback-to-loop packet delivery](docs/images/v1-system-flow.svg)

*Source-based system overview. For wiring, use [Interfaces](INTERFACES.md).
RSSI is received signal strength, not measured distance.*

| Capability | Implementation |
| --- | --- |
| Symmetric companions | `bubu` and `dudu` PlatformIO identities share one source tree. |
| Button heartbeat | Debounced request; receiver plays a priority double pulse; duplicates are suppressed. |
| Bounded delivery | Eight-byte messages, matching acknowledgements (ACKs), 300 ms timeout and up to two retries with the same message ID. |
| Motion and radio selection | ADXL345 movement/settling and provisional CLOSE/FAR classification select ESP-NOW or CC1101 event transport. |
| Sleep and wake | ESP-NOW sleep agreement, guarded deep sleep, and motion/button/CC1101 wake inputs. |
| Feedback | Non-blocking LED animation and OLED status; eligible CLOSE/FAR background pulses every 2.5/6 seconds. |

The application uses a cooperative `setup()`/`loop()` design: each responsibility
gets a turn in a fixed order. It creates no FreeRTOS application tasks.
`RadioTask` and `CC1101Radio` are the earlier FreeRTOS-based radio implementation,
kept for reference and not started by the application.

See [requirements](REQUIREMENTS.md) and [button behavior](BUTTON_HEARTBEAT.md)
for the implemented rules. Each device contains an ESP32-C3 Super Mini, GY-291
ADXL345, SH1106 1.3-inch OLED, CC1101 433 MHz radio, WS2812B and button.
The [hardware guide](INTERFACES.md#hardware-and-recorded-connections) records
supply connections and the battery assembly; [upload instructions](INTERFACES.md#upload-and-serial-monitor)
explain board selection.

## What I observed

After flashing both devices, I observed LED output and motion detection working,
coordinated sleep working, and CC1101 peer wake working nearby and once through
a closed door. I did not count trials, capture paired serial logs or measure
separation, so these are demonstrations, not acceptance passes. A door also does
not prove ESP-NOW was unavailable.

I saw the proximity failure directly: Dudu showed **FAR / CC1101** while Bubu
showed **CLOSE / ESP-NOW** and kept its fast heartbeat. [Final findings](FINAL_FIRMWARE_TEST.md)
separates these observations from the source-based explanation and defines the
evidence levels used in this repository.

## Which code was tested

I flashed both devices from [`ecd9d4f`](https://github.com/1m2s/BubuDudu/commit/ecd9d4fb9920d876e0acea3a3579429e222af718).
Its firmware sources (`src/`, `include/`, `platformio.ini`) are identical to
PR #1's merge [`3f6ae82`](https://github.com/1m2s/BubuDudu/commit/3f6ae82)
and the [`v1` tag](https://github.com/1m2s/BubuDudu/releases/tag/v1), which points to
[`d5acad1`](https://github.com/1m2s/BubuDudu/commit/d5acad1b4a348ce7434fcf82c9852260adb31117).
Changes between those checkpoints were documentation and images only. I did not
save upload logs, and the firmware has no version readback.

[CI run 37152115281](https://github.com/1m2s/BubuDudu/actions/runs/37152115281)
passed the host suite with sanitizers and both firmware builds for `ecd9d4f`.
CI checks software only. The subsequent [audit cleanup](docs/V1_AUDIT_RESOLUTION.md)
changes documentation and source comments; executable code, diagnostic strings,
tests and build settings retain the v1 behavior. The v1 tag remains fixed.

## A view of the work

### Demonstration videos

Original MOV recordings are downloadable assets on the [v1 prototype pre-release](https://github.com/1m2s/BubuDudu/releases/tag/v1).

| Recording | Demonstration |
| --- | --- |
| [ClosePeerWake.mov](https://github.com/1m2s/BubuDudu/releases/download/v1/ClosePeerWake.mov) | Local wake and peer wake with nearby devices. |
| [FarPeerWake.mov](https://github.com/1m2s/BubuDudu/releases/download/v1/FarPeerWake.mov) | Peer wake with the devices farther apart. |
| [MovingBehavior.mov](https://github.com/1m2s/BubuDudu/releases/download/v1/MovingBehavior.mov) | Moving, settling and checking status, plus button interaction. |

“Far” is the recording's name, not a measured range.
[Recording sizes and integrity hashes](docs/videos/README.md).

### Debugging and simulation

| Hardware debugging | Analog exploration |
| --- | --- |
| ![Historical SPI capture with decoded MOSI and MISO transfers](docs/images/cc1101-spi-debug-capture.png) | ![Falstad battery-indicator model with dividers, comparator stages and LEDs](docs/images/falstad-battery-indicator-simulation.png) |
| Historical CC1101 bus debugging. | Simulation only; the analog indicator was not assembled. |

The [evidence gallery](docs/images/README.md) also contains an I²C capture and
the early PlatformIO environment. Original images and the
[simulation export](simulations/battery_indicator_v1.txt) are preserved.

## Learning context and pause

I am an Electrical Engineering & IT student at RWU Ravensburg-Weingarten,
entering semester 4. I completed an embedded-systems course from the University
of Colorado Boulder and used this project to practise embedded design.
Development was AI-assisted. I want to improve my ability to trace, explain and
modify the C++ implementation independently.

The development records span 7 September–4 October 2026. Development is paused
until I have free time, with no promised v2 date.
The [reflection and restart point](docs/V1_REFLECTION.md) cover integration
lessons, state freshness, possible ranging improvements, a physical comparator
indicator and power measurements. The dated [development log](DEVLOG.md)
preserves working checkpoints and setbacks.
