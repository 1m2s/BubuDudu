# BubuDudu Hardware Interfaces

This is the **single current pin reference** for both identities. The GPIO table
records what v1 firmware expects. Physical connections below are labelled by
when they were recorded; they are not a new inspection of the assembled devices.
See [final findings](FINAL_FIRMWARE_TEST.md) for physical evidence.

## Hardware and recorded connections

| Component per device | Role | Recorded supply / connection |
| --- | --- | --- |
| ESP32-C3 Super Mini | Main controller | USB during bench bring-up; battery/boost operation recorded on 3 October. The assembled board's exact power-input connection was not rechecked in this documentation review. |
| GY-291 ADXL345 | Motion sensing and wake | 3.3 V; CS tied to 3.3 V for I²C mode; common ground. |
| SH1106 1.3-inch 128×64 OLED | Status display | 3.3 V and common ground. |
| CC1101 433 MHz module and antenna | Secondary radio and peer wake | 3.3 V and common ground; firmware profile is 433.92 MHz. |
| One WS2812B RGB pixel | Heartbeat output | 5 V; GPIO21 to DIN through a 330 Ω series resistor; common ground. |
| Momentary button | User heartbeat and wake | GPIO5 to ground; internal pull-up. |
| Protected Superfire 18650, USB-C charger/protection board and Adafruit TPS61023 MiniBoost | Battery supply | Assembly and basic battery operation recorded on 3 October. |

**Historical wiring evidence:** module supplies, ADXL345 CS, the LED resistor
and unused GDO2 were recorded during September bring-up in
[PIN_MAP.md at `320af5d`](https://github.com/1m2s/BubuDudu/blob/320af5d8532d9832dfe53d0d5612e6861a06d391/PIN_MAP.md).
They were not rechecked after battery assembly. That record's unused **GDO0**
is obsolete; current firmware uses GPIO4. It is a source for physical history,
not the current pin map.

The [3 October battery record](DEVLOG.md#2026-10-03) gives approximately 3.9 V
per cell and 4.99–5.02 V at the boost output. Current consumption, runtime and
charging under load were not measured. No complete final power schematic is
available; this table should not be used to infer charger-terminal connections.

## GPIO and buses

A **GPIO** is a configurable digital pin. **I²C** shares two communication lines
between addressed devices. **SPI** uses a clock, separate data lines and a
chip-select signal to choose the peripheral.

| Component / signal | GPIO | Current use |
| --- | --- | --- |
| ADXL345 and OLED SDA / SCL | 0 / 1 | Shared I²C; 7-bit addresses `0x53` / `0x3C`. |
| ADXL345 INT1 | 3 | Activity/inactivity input while awake; activity-only HIGH wake during sleep. |
| Button to GND | 5 | Internal pull-up, 30 ms debounce, LOW wake. |
| WS2812B data | 21 | One pixel, GRB data at 800 kHz. |
| CC1101 SCK / MOSI / MISO / CSN | 6 / 7 / 20 / 10 | SPI at 100 kHz, mode 0, MSB first; CSN active LOW. |
| CC1101 GDO0 | 4 | Active-high CRC-valid packet latch and deep-sleep wake. |
| CC1101 GDO2 | — | Unused in firmware; recorded as not connected during bring-up. |

The design uses ten distinct GPIOs and assigns no MOSFET-control or battery-ADC
pin. GPIO2/8/9 are avoided as **strapping pins**, whose levels during reset select
boot settings. GPIO18/19 serve native USB. GPIO3–5 are in the C3's GPIO0–5
RTC-powered group used for deep-sleep wake. These chip restrictions are described
in the [ESP32-C3 datasheet](https://documentation.espressif.com/esp32-c3_datasheet_en.html).

Sources: [Motion/button pins](include/Config.h), [SPI pins/settings](src/CC1101Bus.h),
[button input](src/app/ButtonRuntime.cpp), [Motion](src/Motion.cpp),
[display](src/Display.cpp), [LED](src/LED.cpp) and
[wake configuration](src/CC1101WakeRecovery.cpp). Definitions are currently
spread across these files; consolidating them is future firmware work.

## Shared I²C ownership

Motion initializes GPIO0/1 with `Wire.begin()`. Display initialization follows,
uses the bus at 100 kHz and suppresses U8g2's implicit bus initialization.
The OLED address is `0x3C`; U8g2 receives `0x3C << 1` as required by that API.
Dudu's address/transfer diagnostics observe bus responses, not panel visibility.
The historical [I²C capture](docs/images/i2c-address-69-debug-capture.png), labelled
“Address read: 69,” is not validation of these sensor/display addresses.

## Sleep and retained hardware state

Product sleep uses GPIO3/4 HIGH and GPIO5 LOW, combined mask `0x38`, with timer
wake disabled. Wake polarities stay fixed per pin. ADXL345 and CC1101 remain
powered while the MCU sleeps, so radio listening current remains part of the
power budget. CC1101 CSN is held HIGH during deep sleep; boot releases the hold
before bus access. Startup inspects retained packets before sensor/display work.

**Unverified hardware question:** GPIO21 also serves UART0 TX, and ROM boot
messages can appear on that output depending on boot configuration. Boot-time
activity could therefore reach WS2812B DIN. A visible LED flash is a hypothesis,
not an observed defect or a reason to change v1 wiring. It would need physical
observation if development resumes. See the [ESP32-C3 pin and boot-print documentation](https://documentation.espressif.com/esp32-c3_datasheet_en.html).

## Upload and serial monitor

These are reference commands for reproducing the firmware; no reflash is needed
for the documentation/comment cleanup. Install the pinned PlatformIO version
listed in the [build guide](tests/host/README.md), open the repository and use the
`bubu` and `dudu` environments sequentially. Each environment builds the same
source with its own identity.

```sh
pio run -e bubu -t upload
pio run -e dudu -t upload
```

Run these without `BUBUDUDU_BUILD_ONLY=1`. Close any monitor using the target
port first. [pio_select_port.py](tools/pio_select_port.py) selects by the board's
**MAC address**, its radio identifier, rather than relying on changing USB port
names. The helper currently discovers Espressif `/dev/cu.*` ports on **macOS**;
it is not a portable Windows/Linux upload recipe. It first matches the USB
serial number; if a candidate lacks one, its fallback reads the MAC with esptool
and resets that board. It refuses missing, ambiguous or busy targets.

| Identity | `custom_device_mac` in PlatformIO | Peer MAC compiled into ESPNowRadio |
| --- | --- | --- |
| Bubu | `E8:F6:0A:12:4C:A4` | Dudu: `E8:F6:0A:12:5B:84` |
| Dudu | `E8:F6:0A:12:5B:84` | Bubu: `E8:F6:0A:12:4C:A4` |

Other boards require corresponding changes in **both** [platformio.ini](platformio.ini)
and the conditional peer arrays in [ESPNowRadio.cpp](src/ESPNowRadio.cpp).
An environment's upload MAC is its own board; its firmware peer MAC is the
other board. Centralizing those definitions is deferred beyond v1.

For the 115200-baud monitor, run the appropriate command in each terminal:

```sh
pio run -e bubu -t monitor_auto
pio run -e dudu -t monitor_auto
```

The helper stores discovered monitor ports in ignored `.pio/ports.ini`.
For software-only builds without enumerating or resetting USB devices, use the
[build-only commands](tests/host/README.md#reproducible-build-inputs-and-ci).

## Unfinished hardware features

MOSFET power gating and battery telemetry are unimplemented. The
[battery-indicator model](simulations/battery_indicator_v1.txt) and
[Falstad screenshot](docs/images/falstad-battery-indicator-simulation.png) are
simulation references. Calibration, OLED visibility, intermittent radio/sleep
recovery and power characterization remain in [final findings](FINAL_FIRMWARE_TEST.md).
Possible UWB ranging and additional electronics are [v2 aspirations](docs/V1_REFLECTION.md#aspirations-for-a-possible-v2).
