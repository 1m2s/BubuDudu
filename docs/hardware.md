# Hardware and wiring

This page describes the hardware used by the integrated firmware at `1b10419`. Wiring that was physically validated in earlier subsystem work is distinguished from modules that the current [`main.cpp`](../src/main.cpp) actually starts. The dated [development log](../DEVLOG.md) records the bench evidence; source constants define the current firmware assignments.

## Boards and build target

Both devices are **ESP32-C3 Super Mini** boards. [`platformio.ini`](../platformio.ini) uses the `esp32-c3-devkitm-1` build profile with Arduino; that profile name does not identify the physical board as a DevKitM-1. Native USB CDC is enabled, and serial output uses 115200 baud.

| Device | PlatformIO environment | Compile-time identity | Confirmed Wi-Fi MAC |
| --- | --- | --- | --- |
| Bubu | `bubu` | `DEVICE_BUBU` | `E8:F6:0A:12:4C:A4` |
| Dudu | `dudu` | `DEVICE_DUDU` | `E8:F6:0A:12:5B:84` |

Both builds use the same source tree and wiring. The board/USB foundation was physically validated on [September 8](../DEVLOG.md#2026-09-08); device identities were recorded on [September 9](../DEVLOG.md#2026-09-09).

## Confirmed wiring used by the current runtime

| Peripheral signal | ESP32-C3 GPIO | Current use / source |
| --- | ---: | --- |
| ADXL345 SDA | 0 | I²C data, [`Config.h`](../include/Config.h) |
| ADXL345 SCL | 1 | I²C clock, [`Config.h`](../include/Config.h) |
| ADXL345 INT1 | 3 | Active-HIGH motion interrupt and deep-sleep wake |
| CC1101 GDO0 | 4 | CRC-valid packet latch and active-HIGH deep-sleep wake |
| CC1101 SCK | 6 | SPI clock, [`CC1101Bus.h`](../src/CC1101Bus.h) |
| CC1101 MOSI | 7 | SPI data from ESP32 to radio |
| CC1101 CSN | 10 | Active-LOW chip select; held HIGH across deep sleep |
| CC1101 MISO / GDO1 | 20 | SPI data from radio to ESP32 |

The GY-291 ADXL345 uses I²C address `0x53`; [`Motion.cpp`](../src/Motion.cpp) checks `DEVID = 0xE5`. Historical wiring validation records sensor VCC and CS connected to 3.3 V, with common ground. The sensor remains powered while the ESP32 sleeps. GPIO0/1 communication was validated on both devices on [September 10](../DEVLOG.md#2026-09-10), GPIO3 wake on [September 12](../DEVLOG.md#2026-09-12), and the integrated motion-wake path on [September 25](../DEVLOG.md#2026-09-25).

The CC1101 supply is 3.3 V with common ground. On the actual eight-pin module, the [September 16 multimeter checks](../DEVLOG.md#2026-09-16) identified physical pin 1 as GND and pin 2 as VCC; seller diagrams included a different ten-pin breakout. Those physical pin numbers describe the tested module, not every CC1101 board. SPI wiring was checked by continuity and subsequently used for bidirectional packet tests.

GPIO4 is **in use** for CC1101 GDO0. The old `design/pin-map` branch describes an earlier polling-only radio checkpoint where GDO0 was unconnected and GPIO4 was available. The [September 22 wake validation](../DEVLOG.md#2026-09-22) and current source supersede that statement. GDO2 has no assignment in the active firmware.

## Current CC1101 profile

[`CC1101SleepArm.cpp`](../src/CC1101SleepArm.cpp) installs the tested **433.92 MHz** profile on cold boot. The active implementation uses [`CC1101Bus.h`](../src/CC1101Bus.h), [`CC1101WakeRecovery.cpp`](../src/CC1101WakeRecovery.cpp), and [`CC1101WakeTx.cpp`](../src/CC1101WakeTx.cpp). It does not start the historical `RadioTask`.

| Setting | Current value / behavior |
| --- | --- |
| SPI | 100 kHz, Mode 0, MSB first |
| Carrier | 433.92 MHz; `FREQ2/1/0 = 0x10 / 0xB0 / 0x71` |
| Modem | GFSK, approximately 38.4 kBaud; `MDMCFG4..0 = CA 83 13 22 F8`, `DEVIATN = 0x35` |
| Sync word | `0xD391` |
| Packet framing | Variable length, maximum payload 61 bytes; `PKTCTRL0 = 0x05` |
| CRC / filtering | CRC enabled, CRC auto-flush, no address filtering; `PKTCTRL1 = 0x08` |
| Appended status | Disabled; a normal wake packet occupies one length byte plus the eight-byte `Protocol::Message` |
| GDO0 | `IOCFG0 = 0x07`: HIGH after a CRC-valid packet, cleared by reading the first RX FIFO byte |
| Packet completion | `MCSM1 = 0x30`: returns to IDLE after RX/TX packet; `MCSM0 = 0x18` enables automatic calibration |
| Wake EVENT / ACK TX | `PATABLE = 0x60` is installed when transmitting; the receive-only arm profile does not program TX power |

The original profile is also documented in the dormant [`CC1101Radio.cpp`](../src/CC1101Radio.cpp). The active sleep-arm inspector verifies identity, configuration, RX state, empty FIFO, no overflow and LOW GDO0. Its check is read-only and bounded; calibration-updated FSCAL values are excluded from comparisons against their initial seeds.

This leaves the **CC1101 in RX while the ESP32 deep-sleeps**. It is not a claim of CC1101 wake-on-radio duty cycling or measured battery life. The radio must remain powered to retain configuration and the wake packet. See [deep sleep and wake](deep-sleep-wake.md) for the entry and recovery ordering.

## Physically validated peripherals that are inactive in this runtime

| Peripheral | Historical confirmed wiring | Present source | Current runtime status |
| --- | --- | --- | --- |
| WS2812B | DIN on GPIO21 through a **330 Ω series resistor**; 5 V supply; common ground | [`LED.cpp`](../src/LED.cpp), [`LED.h`](../include/LED.h) | One-pixel heartbeat implementation remains; `main.cpp` does not instantiate or call it |
| 1.3-inch 128×64 SH1106 OLED | SDA GPIO0, SCL GPIO1; 3.3 V supply; common ground; address `0x3C` | [`Display.cpp`](../src/Display.cpp), [`Display.h`](../include/Display.h) | Module remains; `main.cpp` does not initialize or update it |
| Earlier CC1101 radio task | Same confirmed SPI pins; earlier tests did not require GDO0 | [`RadioTask.cpp`](../src/RadioTask.cpp), [`CC1101Radio.cpp`](../src/CC1101Radio.cpp) | Dedicated FreeRTOS transport is dormant; current awake messaging uses ESP-NOW |

LED output was physically validated on both devices on [September 13](../DEVLOG.md#2026-09-13). The resistor value, heartbeat animation and shared-bus OLED operation are recorded on [September 14](../DEVLOG.md#2026-09-14). These are documented hardware results, not inferred wiring. The OLED shares the Motion I²C bus and adds no separate SDA/SCL GPIOs. Its dormant `Display::begin()` sets the bus to 100 kHz; the current runtime does not call that method.

## Unassigned or unfinished hardware

There is no confirmed, active application-button pin in the current integration. The historical theoretical map proposed GPIO5, but that proposal is not proof of installed or tested button wiring. Do not treat it as a current assignment.

Battery sensing, charging/power management and optional MOSFET power gating are unfinished. No production circuit, resistor network or additional GPIO assignment is established by this page. Earlier GPIO-planning constraints and subsystem prototypes are design history, not a complete current schematic.

## Evidence and limits

The [September 24](../DEVLOG.md#2026-09-24) and [September 25](../DEVLOG.md#2026-09-25) logs record actual coordinated deep sleep, CC1101 wake/retained-packet ACK, timer wake, local motion wake and motion-triggered peer wake. Both directions were tested. Lost-first-wake-ACK recovery was physically tested using temporary ACK suppression that was removed before the permanent checkpoint.

Host tests exercise injected faults and edge cases; they cannot establish physical pin continuity, sensor sensitivity, RF range or current consumption. In particular, combined GPIO3/GPIO4 wake classification is host-covered; the recorded individual-source tests do not establish a simultaneous two-pin hardware stimulus. An intermittent Dudu I²C connection was investigated on September 25, but the exact contact fault was not isolated. These checkpoints are bench evidence, not production qualification.
