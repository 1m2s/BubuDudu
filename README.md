# BubuDudu

| ▶ [Nearby wake](https://github.com/1m2s/BubuDudu/releases/download/v1/ClosePeerWake.mov) | ▶ [Wake farther apart](https://github.com/1m2s/BubuDudu/releases/download/v1/FarPeerWake.mov) | ▶ [Motion and button demo](https://github.com/1m2s/BubuDudu/releases/download/v1/MovingBehavior.mov) |
| :---: | :---: | :---: |
| Wake one device and its nearby partner. | Peer wake with the devices farther apart. | Movement, status changes and button interaction. |

**Open a video above to watch or download the original MOV.**
[All videos and file details](docs/videos/README.md).

## The idea

Press a button on Bubu and Dudu plays a heartbeat. It works the other way too.
Both devices use an ESP32-C3 and the same firmware, with motion sensing, two
radios, an OLED screen and a WS2812B LED.

**v1 prototype · development paused · 4 October 2026.** LED output, motion,
sleep and peer wake worked in my demos. The main problem is unreliable proximity
and radio switching: after one device moves away and returns, the stationary
one can stay stuck on FAR / CC1101 until it is moved. Full two-device testing
is incomplete. See [results and known problems](FINAL_FIRMWARE_TEST.md).

## What it does

- Sends button heartbeats over ESP-NOW or CC1101.
- Waits for a reply, retries twice if needed, and ignores duplicate actions.
- Uses motion and signal strength to choose CLOSE/FAR and a radio.
- Agrees with the other device before sleeping; wakes by motion, button or radio.
- Shows status on an OLED and runs LED animations while other work continues.

![Two devices share firmware, but keep their own state and radio choice](docs/images/v1-system-flow.svg)

[How the code works](ARCHITECTURE.md) · [Hardware and upload](INTERFACES.md) ·
[Button behavior](BUTTON_HEARTBEAT.md) · [Features](REQUIREMENTS.md) ·
[Builds and tests](tests/host/README.md)

## What I learned

I study Electrical Engineering & IT at RWU Ravensburg-Weingarten and am entering
semester 4. This project builds on an embedded-systems course I completed from
the University of Colorado Boulder, plus RWU's **Computer Technology** and
**Digital Electronics** lectures and labs.

I applied binary data, registers, bit masks, interrupts, logic, state machines
and timing to a working device. The Digital Electronics lab's LED patterns and
PWM exercises also connect to the heartbeat idea. [My reflection](docs/V1_REFLECTION.md#what-my-rwu-subjects-contributed)
shows the links to the coursework and what I would improve.

Development was AI-assisted. I am still improving my ability to explain and
change the C++ code myself. Work ran from September to October 2026.

Unavailable 18650 batteries and a 5 V boost module delayed realistic distance,
return and radio-fallback tests. That taught me to plan the power hardware needed
for testing early. I have ideas for fixes, but am pausing to focus on the university
semester. There is no planned v2 date. [Testing constraints and lessons](docs/V1_REFLECTION.md#why-physical-validation-came-late).

## Debugging and electronics

| Radio debugging | Battery-indicator idea |
| --- | --- |
| ![CC1101 SPI capture](docs/images/cc1101-spi-debug-capture.png) | ![Falstad battery-indicator simulation](docs/images/falstad-battery-indicator-simulation.png) |
| SPI signals from early radio work. | Simulation only; this indicator was not built. |

[Image notes](docs/images/README.md) · [Simulation file](simulations/battery_indicator_v1.txt) ·
[Development log](DEVLOG.md)

## Which code was tested

I flashed both devices from [`ecd9d4f`](https://github.com/1m2s/BubuDudu/commit/ecd9d4fb9920d876e0acea3a3579429e222af718).
Its firmware files match PR #1's merge [`3f6ae82`](https://github.com/1m2s/BubuDudu/commit/3f6ae82)
and the [`v1` release](https://github.com/1m2s/BubuDudu/releases/tag/v1)
at [`d5acad1`](https://github.com/1m2s/BubuDudu/commit/d5acad1b4a348ce7434fcf82c9852260adb31117).
Later edits changed documentation and comments only. The v1 tag stays fixed.

[CI run 37152115281](https://github.com/1m2s/BubuDudu/actions/runs/37152115281)
passed the computer tests and both builds for `ecd9d4f`. I did not save upload
logs, and the devices cannot display their firmware version.
[Cleanup checklist](docs/V1_AUDIT_RESOLUTION.md).
