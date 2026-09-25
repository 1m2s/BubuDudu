# BubuDudu

Two symmetric ESP32-C3 companion devices, built to explore reliable event delivery and coordinated low-power operation. The intended interaction is local input → wireless EVENT → a heartbeat-style reaction on the other device.

The current checkpoint concentrates on the difficult boundary between communication and sleep: negotiate sleep over ESP-NOW, preserve protocol history across reboot, recover a wake packet still held by an external CC1101, and wake both devices when one detects motion. LED/OLED drivers exist, but the current application reports events through Serial rather than driving the final visual interface.

**Firmware baseline:** [`1b10419`](https://github.com/1m2s/BubuDudu/commit/1b1041976b4200668c3090d27c8138dc80aa53f2) on `feature/sleep-execution`. This `chore/repository-polish` branch documents that firmware without changing its behavior. `main` currently carries documentation history and an older firmware tree; it is not the latest firmware checkout.

## Current capabilities

| Capability | Status at this checkpoint |
| --- | --- |
| Bidirectional ESP-NOW, eight-byte EVENT/ACK protocol | Implemented; host-tested and physically tested on both boards |
| Same-ID retries, duplicate handling, receive queue | Implemented; bounded failure paths tested |
| Coordinated sleep, collision arbitration, RTC history | Implemented; host tests plus two-board sleep/reboot tests |
| CC1101 retained-packet deep wake and awake duplicate re-ACK | Implemented; physically tested in both directions, including lost first ACK |
| ADXL345 GPIO3 deep wake → one CC1101 peer-wake transaction | Implemented; physically tested in both directions and with unavailable peer |
| Combined GPIO3/GPIO4 wake-source reporting | Host-tested; simultaneous physical stimulus not recorded |
| OLED, WS2812B, button-driven interaction | Separate subsystem/design history; not active in this application |
| Sensitive awake motion, gestures, proximity refresh, general radio fallback | Not integrated |
| Battery/power hardware and product power policy | Incomplete; 30-second safety timer remains |

The [validation record](docs/validation.md) separates deterministic host checks, developer-reported hardware results and evidence still to be archived. This is an engineering prototype, not a production-ready product.

## Hardware and system architecture

Each board uses an ESP32-C3 Super Mini, ADXL345 over I2C and CC1101 at 433.92 MHz over SPI. ADXL345 INT1 connects to GPIO3; CC1101 GDO0 connects to GPIO4. See the [confirmed pin map and inactive peripherals](docs/hardware.md).

Arduino `setup()`/`loop()` own application state. ESP-NOW callbacks copy received packets into a FreeRTOS queue; the loop handles packet validation, ACKs, retries and the power FSM. The current application does **not** start the older `RadioTask` module.

[Architecture and ownership diagram →](docs/architecture.md)

## Communication and power

ESP-NOW is the primary awake application path. Application ACKs use a 300 ms timeout and at most two retries of the same message ID. Duplicate EVENTs are acknowledged again without a second application action. These rules bound failure; they do not guarantee delivery under arbitrary RF loss.

PowerManager negotiates `SLEEP_REQUEST → SLEEP_READY → SLEEP_COMMIT → SLEEP_ACK`. A packet receipt ACK and semantic `SLEEP_ACK` have different jobs. Physical sleep waits for transport drain, radio readiness and Motion preparation; a semantic `SLEEPING` state alone is insufficient.

CC1101 remains listening while the ESP32 sleeps. GPIO4 wake reboots the ESP32, so startup inspects and processes the retained FIFO **before** normal peripheral initialization can erase it. GPIO3 motion wake uses a checked activity-only ADXL345 profile with the existing approximately 3 g threshold. Pure motion wake triggers one bounded peer wake; radio, combined-pin, timer and cold boots do not send an automatic return wake.

[Protocol](docs/protocol.md) · [Power FSM and handshake](docs/power-management.md) · [Deep sleep, Motion and peer wake](docs/deep-sleep-wake.md)

## Build and test

Use PlatformIO Core or the VS Code PlatformIO terminal:

```bash
pio run -e bubu
pio run -e dudu
bash tests/host/run.sh
```

Builds do not upload firmware. Host tests need Bash and a C++11 compiler with AddressSanitizer/UndefinedBehaviorSanitizer support; they run without connected boards. PlatformIO's `bubu`/`dudu` environments select identities from one source tree.

[Setup, upload/monitor commands, CI and current physical test procedure →](docs/testing.md)

## Repository guide

| Location | Purpose |
| --- | --- |
| [`src/`](src/) / [`include/`](include/) | Firmware and interfaces; active/dormant roles are [audited](docs/repository-audit.md) |
| [`tests/host/`](tests/host/) | Production-code tests with deterministic time/radio/GPIO substitutes |
| [`docs/architecture.md`](docs/architecture.md) | Five-minute system and ownership overview |
| [`docs/protocol.md`](docs/protocol.md) / [`docs/power-management.md`](docs/power-management.md) | Packet fields, reliability, state transitions and sleep agreement |
| [`docs/hardware.md`](docs/hardware.md) / [`docs/deep-sleep-wake.md`](docs/deep-sleep-wake.md) | Wiring, boot order and external-device wake behavior |
| [`docs/testing.md`](docs/testing.md) / [`docs/validation.md`](docs/validation.md) | Reproduction steps and evidence boundaries |
| [`docs/assets/`](docs/assets/) | Instructions for adding real photos, traces and logs; no fabricated captures |
| [`REQUIREMENTS.md`](REQUIREMENTS.md) | Product intent with current status mapping |
| [`DEVLOG.md`](DEVLOG.md) | Dated debugging, checkpoint and physical-test history |

## Limits and development history

The radio links are not authenticated application protocols, the EVENT duplicate cache is limited, and 16-bit freshness assumes bounded ID gaps. Cold resets and permanent packet loss remain important limits. No measured battery-life, RF-range or exact-distance claim is made. Ordinary awake movement must eventually retain the last confirmed CLOSE/FAR presentation while a fresh proximity check is pending; that UI policy is not implemented.

Development follows one understandable change → build → flash → physical test → understand → commit. The [development lessons](docs/development-lessons.md) explain why callback isolation, bounded deadlines and non-destructive boot inspection replaced the earlier large-integration approach. Older root architecture/interface sketches and the simulated-sleep procedure remain explicitly marked as historical.
