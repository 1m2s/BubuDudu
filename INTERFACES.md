# Hardware, pins and upload

Use this page for the current pin map. Pins match the firmware. Supply wiring
comes from the earlier build notes and was not rechecked after battery assembly.

## Hardware and recorded connections

| Part in each device | Supply / connection |
| --- | --- |
| ESP32-C3 Super Mini | USB during early tests; later powered by the battery/boost supply. The final board power-input connection was not rechecked. |
| GY-291 ADXL345 motion sensor | 3.3 V; CS tied to 3.3 V for I²C mode. |
| SH1106 1.3-inch 128×64 OLED | 3.3 V. |
| CC1101 433 MHz radio and antenna | 3.3 V; firmware uses 433.92 MHz. |
| One WS2812B LED | 5 V; data through a 330 Ω resistor. |
| Push button | GPIO5 to ground, with the ESP32's internal pull-up. |
| Battery supply | Protected Superfire 18650, USB-C charger/protection board and Adafruit TPS61023 MiniBoost. |

All modules share ground. The September [wiring record](https://github.com/1m2s/BubuDudu/blob/320af5d8532d9832dfe53d0d5612e6861a06d391/PIN_MAP.md)
records the supplies, CS tie, resistor and unused GDO2. Its old “GDO0 unused” note
is out of date: GDO0 now uses GPIO4.

Both devices ran from batteries on [3 October](DEVLOG.md#2026-10-03), with about
3.9 V per cell and 4.99–5.02 V at the booster. Current draw, runtime and charging
under load were not measured. There is no complete final power schematic here;
do not infer charger-terminal connections from this parts list.

## GPIO and buses

GPIO means a digital pin. I²C shares two wires between addressed devices.
SPI uses a clock, data lines and a chip-select line.

| Signal | GPIO | Setting |
| --- | --- | --- |
| ADXL345 / OLED SDA, SCL | 0, 1 | Shared I²C; addresses `0x53`, `0x3C`. |
| ADXL345 INT1 | 3 | Activity/inactivity while awake; activity-only HIGH wake in sleep. |
| Button | 5 | Internal pull-up; pressed LOW; 30 ms debounce; LOW wake. |
| WS2812B data | 21 | One pixel; GRB at 800 kHz. |
| CC1101 SCK, MOSI, MISO, CSN | 6, 7, 20, 10 | SPI: 100 kHz, mode 0, MSB first; CSN LOW selects the radio. |
| CC1101 GDO0 | 4 | HIGH after a valid packet; also wakes the ESP32. |
| CC1101 GDO2 | — | Unused; recorded as not connected. |

All ten selected GPIOs are used. GPIO2/8/9 are avoided because their levels at
reset affect boot. GPIO18/19 serve USB. GPIO3–5 support deep-sleep wake. See the
[ESP32-C3 datasheet](https://documentation.espressif.com/esp32-c3_datasheet_en.html).

Pin definitions: [motion/button](include/Config.h), [SPI](src/CC1101Bus.h),
[LED](src/LED.cpp) and [wake setup](src/CC1101WakeRecovery.cpp).

## Shared I²C ownership

[Motion](src/Motion.cpp) starts the bus on GPIO0/1.
[Display](src/Display.cpp) reuses it at 100 kHz without starting it again.
The OLED address is `0x3C`; U8g2 takes `0x3C << 1`.
Bus replies do not prove that the screen is visible. The old I²C screenshot
labelled “Address read: 69” is not a check of these addresses.

## Sleep and wake wiring

GPIO3/4 wake HIGH and GPIO5 wakes LOW: mask `0x38`, timer off. These levels stay
fixed. ADXL345 and CC1101 stay powered during sleep, so CC1101 still draws current.
CSN is held HIGH through sleep and released at startup. Startup checks unread
radio packets before setting up the sensor and display.

GPIO21 also carries UART0 boot output. It could cause a brief LED flash at reset,
but this has not been observed or tested here. See the [chip documentation](https://documentation.espressif.com/esp32-c3_datasheet_en.html).

## Upload and serial monitor

No reflash is needed for documentation edits. For a future upload, install
PlatformIO as described in the [build guide](tests/host/README.md), open this
repository, close the target board's serial monitor and run one upload at a time:

```sh
pio run -e bubu -t upload
pio run -e dudu -t upload
```

Leave `BUBUDUDU_BUILD_ONLY` unset. The [port helper](tools/pio_select_port.py)
finds each board by its MAC address (radio ID). It currently supports **macOS
`/dev/cu.*` ports**. It first checks the USB serial number; if that is missing,
it uses esptool to read the MAC and resets that candidate board. Missing, duplicate
or busy targets stop the upload.

| Device | Own MAC in PlatformIO | Partner MAC in ESPNowRadio |
| --- | --- | --- |
| Bubu | `E8:F6:0A:12:4C:A4` | `E8:F6:0A:12:5B:84` |
| Dudu | `E8:F6:0A:12:5B:84` | `E8:F6:0A:12:4C:A4` |

For different boards, update both [platformio.ini](platformio.ini) and the partner
arrays in [ESPNowRadio.cpp](src/ESPNowRadio.cpp). Each upload MAC identifies that
board; its partner MAC identifies the other board.

Open each 115200-baud monitor in its own terminal:

```sh
pio run -e bubu -t monitor_auto
pio run -e dudu -t monitor_auto
```

The helper saves monitor ports in ignored `.pio/ports.ini`.
[Build-only commands](tests/host/README.md#reproducible-build-inputs-and-ci) do not access USB.

## Unfinished hardware features

No pins are assigned for MOSFET switching or battery readings. The
[battery indicator](simulations/battery_indicator_v1.txt) is a simulation.
[Known problems](FINAL_FIRMWARE_TEST.md) and [future ideas](docs/V1_REFLECTION.md#aspirations-for-a-possible-v2)
cover the remaining work.
