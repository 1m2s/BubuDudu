# BubuDudu

**Two companion devices, one shared embedded firmware.**

Bubu and Dudu are ESP32-C3 prototypes for exchanging a small physical signal:
press a button on one device and its partner plays a heartbeat on a WS2812B LED.
The project brings together motion sensing, two radio paths, OLED feedback and
coordinated sleep/wake in a compact embedded-systems learning project.

**v1 checkpoint · development paused · 4 October 2026**

v1 is a **partially working integrated prototype**, with incomplete hardware
acceptance. It closes this development period; it does not mean every feature
passed. Sleep and CC1101 peer wake are among the stronger user-reported behaviors.
The major limitation is **extremely unreliable proximity classification and
automatic ESP-NOW/CC1101 transitions**: a stationary partner can retain stale
state until it is moved.

[Final findings & limitations](FINAL_FIRMWARE_TEST.md) ·
[Learning & possible v2](docs/V1_REFLECTION.md) ·
[Architecture](ARCHITECTURE.md) · [Builds & tests](tests/host/README.md)

## How it fits together

![Bubu and Dudu run shared ESP32-C3 firmware with independent local state. ESP-NOW carries application events and sleep controls; CC1101 carries application events and peer wake. ESP-NOW callbacks queue packets for the cooperative loop.](docs/images/v1-system-flow.svg)

*Source-based system overview, not a wiring schematic or proof of acceptance.
Each device owns its own proximity estimate; RSSI is not measured distance.*

| Capability in the firmware | Implementation |
| --- | --- |
| Symmetric companions | `bubu` and `dudu` PlatformIO identities share one source tree. |
| Button heartbeat | Debounced request, receiver-only priority double pulse, duplicate suppression. |
| Bounded message delivery | Eight-byte messages, matching ACKs, 300 ms timeout and up to two same-ID retries. |
| Motion and radio policy | ADXL345 movement/settling; provisional CLOSE/FAR RSSI classification; ESP-NOW and CC1101 event transport. |
| Sleep and wake | ESP-NOW sleep agreement followed by guarded deep sleep; motion, button and CC1101 wake inputs. |
| Local feedback | Non-blocking LED output and OLED status; CLOSE/FAR background cadence of 2.5/6 seconds when eligible. |

These are implemented paths, not a table of hardware passes. See the
[requirements](REQUIREMENTS.md), [GPIO and buses](INTERFACES.md) and
[button behavior](BUTTON_HEARTBEAT.md) for details.

## What the evidence supports

After flashing both devices, I observed partial operation: LED output
and motion appeared to work, sleep functioned, and nearby CC1101 peer wake worked
reliably in my informal observations, including with a door between the devices.
No counted success rate, paired serial trace, precise separation or controlled
radio-isolation test was supplied. A door does not establish that ESP-NOW was
unavailable. These observations do not complete any whole acceptance case.

In one mismatch, Dudu showed **FAR / CC1101**, while Bubu showed **CLOSE / ESP-NOW**
and continued its fast background heartbeat. The [final record](FINAL_FIRMWARE_TEST.md)
separates these observations from the source-based explanation and retains the
other radio, sleep-recovery, OLED, calibration and power-measurement limitations.

The reported firmware upload target was [`ecd9d4f`](https://github.com/1m2s/BubuDudu/commit/ecd9d4fb9920d876e0acea3a3579429e222af718).
Later commits document the findings and v1 closure without changing that firmware.
The [verified CI run for that checkpoint](https://github.com/1m2s/BubuDudu/actions/runs/37152115281)
passed the complete host suite with sanitizers and both firmware builds.
[Live CI](https://github.com/1m2s/BubuDudu/actions/workflows/firmware-ci.yml)
is software evidence; it cannot establish reliable physical radio transitions,
visible OLED output or battery runtime.

## A view of the work

### v1 demonstration videos

Original MOV recordings are available as assets on the
[v1 prototype pre-release](https://github.com/1m2s/BubuDudu/releases/tag/v1).

| Recording | Demonstration described by the author |
| --- | --- |
| [ClosePeerWake.mov](https://github.com/1m2s/BubuDudu/releases/download/v1/ClosePeerWake.mov) | Local wake and peer wake with the devices nearby. |
| [FarPeerWake.mov](https://github.com/1m2s/BubuDudu/releases/download/v1/FarPeerWake.mov) | Peer wake with the devices farther apart. |
| [MovingBehavior.mov](https://github.com/1m2s/BubuDudu/releases/download/v1/MovingBehavior.mov) | Moving, settling and checking status, plus button interaction. |

These show selected v1 demonstrations; the proximity/transition limitations above
remain open. “Far” is the recording's name, not a measured range.
[Recording provenance and download sizes](docs/videos/README.md).

### Debugging and simulation

| Hardware debugging | Analog exploration |
| --- | --- |
| ![Historical SPI capture with decoded MOSI and MISO transfers](docs/images/cc1101-spi-debug-capture.png) | ![Falstad battery-indicator candidate with dividers, comparator stages and LEDs](docs/images/falstad-battery-indicator-simulation.png) |
| CC1101 SPI debugging history. This capture does not prove RF delivery or final-firmware acceptance. | Idealized Falstad simulation. The analog indicator was not assembled and measured. |

The [evidence gallery and provenance](docs/images/README.md) also include the I²C
debugging capture and early PlatformIO environment. Original images and the
[simulation export](simulations/battery_indicator_v1.txt) are preserved.

## Learning context and pause

I am an Electrical Engineering & IT student at RWU Ravensburg-Weingarten,
entering semester 4. I tried to apply what I learned from a University of Colorado
Boulder embedded-systems course that I completed, to the best of my understanding.
This is a personal project, not a claim of university assessment or endorsement.
Development was AI-assisted; the repository does not prove that I independently
understand every C++ implementation detail. Deepening that understanding is part
of the next learning step.

I estimate about **14 active working days** during limited summer availability.
That is my estimate, not a verified count of days or hours. The broader DEVLOG
date range is not a timesheet, and limited time does not dismiss the defects.
Development is paused until I have free time, possibly during a holiday, with
no promised v2 date.

The [reflection and saved restart point](docs/V1_REFLECTION.md) cover integration
lessons, a possible ultra-wideband (UWB) ranging evaluation, purposeful power
switching, a physical comparator indicator and power measurements. These are
future aspirations. The dated [development log](DEVLOG.md) preserves both the
working checkpoints and the setbacks.
