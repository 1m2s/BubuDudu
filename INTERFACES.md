# BubuDudu Hardware Interfaces

v1 firmware assignments at the reported upload target `ecd9d4f`, shared by both
ESP32-C3 identities and unchanged by the later documentation checkpoints.
This is a source map, not a new hardware-validation result. Earlier bring-up
observations remain in [DEVLOG](DEVLOG.md); current physical checks remain in
[hardware acceptance](FINAL_FIRMWARE_TEST.md).

## GPIO and buses

| Component / signal | GPIO | Current use |
| --- | --- | --- |
| ADXL345 and SH1106 OLED SDA / SCL | 0 / 1 | Shared I²C; 7-bit addresses 0x53 / 0x3C respectively. |
| ADXL345 INT1 | 3 | Activity/inactivity input while awake; activity-only HIGH wake during sleep. |
| Momentary button to GND | 5 | Internal pull-up, debounced input, LOW wake. |
| WS2812B data | 21 | One pixel, loop-driven background and user heartbeat animation. |
| CC1101 SCK / MOSI / MISO / CSN | 6 / 7 / 20 / 10 | Separate SPI bus, 100 kHz, mode 0, MSB first; CSN active LOW. |
| CC1101 GDO0 | 4 | Active-high packet-ready indication and deep-sleep wake. |

These assignments use ten distinct GPIOs. There is no assigned MOSFET-control
pin or implemented GPIO power-gating path.

Sources: [identity and Motion/button pins](include/Config.h),
[SPI pins and settings](src/CC1101Bus.h), [Motion](src/Motion.cpp),
[display](src/Display.cpp), [LED](src/LED.cpp) and
[wake configuration](src/CC1101WakeRecovery.cpp).

## Shared I²C ownership

Motion initializes GPIO0/1 with `Wire.begin()`. Display initialization follows,
uses the existing bus at 100 kHz and suppresses U8g2's implicit bus initialization.
The OLED's 7-bit address is 0x3C; U8g2 receives the shifted value `0x3C << 1`.
Dudu's address/transfer diagnostics report observations, not panel visibility.

The archived [I²C debugging capture](docs/images/i2c-address-69-debug-capture.png)
is labelled “Address read: 69.” It does not validate the expected ADXL345/OLED
addresses, sensor behavior or visible OLED output.

## Sleep and retained hardware state

Product sleep uses GPIO3/4 HIGH and GPIO5 LOW, combined mask `0x38`, with the timer
OFF. Wake polarities stay fixed per pin. The wake design leaves ADXL345 and
CC1101 powered while the MCU sleeps; CC1101 listening current therefore remains.
Startup inspects retained radio packets before normal sensor/display work.

The older pin-map branch's unused GPIO4 and earlier 30-second safety-timer
experiments describe historical checkpoints. Current GPIO4 is connected to
CC1101 GDO0, and the product sleep policy has no timer fallback.

## Remaining hardware work and historical proposals

Development is paused. OLED visibility, intermittent radio/sleep recovery,
unreliable proximity/radio transitions, calibration and power measurements
remain open. Source assignments and bus ACKs are insufficient
to close physical acceptance.

MOSFET control and battery telemetry were design proposals. The
[battery-indicator model](simulations/battery_indicator_v1.txt) and its
[Falstad screenshot](docs/images/falstad-battery-indicator-simulation.png) are
simulation references, not implemented firmware interfaces or measured battery
performance. Existing battery-assembly observations remain historical evidence;
current consumption, runtime and charging under load still need measurement.
Possible future UWB ranging and purposeful MOSFET switching are
[v2 aspirations](docs/V1_REFLECTION.md#aspirations-for-a-possible-v2), not components
added to this pin map or purchases made during closure.
