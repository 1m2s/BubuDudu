# BubuDudu ESP32-C3 Pin Map

This document defines the proposed GPIO assignments for each BubuDudu device.

These assignments form the initial theoretical pin map and may be revised during hardware bring-up and testing.

## Pin Assignments

| Function | GPIO | Reason |
|---|---:|---|
| I2C SDA | GPIO0 | Shared communication line for ADXL345 and OLED |
| I2C SCL | GPIO1 | Shared clock line for ADXL345 and OLED |
| ADXL345 INT1 | GPIO3 | RTC-capable GPIO required for motion wake from Deep-sleep |
| CC1101 GDO | GPIO4 | Radio interrupt/event input |
| Button | GPIO5 | Input GPIO with future Deep-sleep button wake capability |
| CC1101 SCK | GPIO6 | SPI clock |
| CC1101 MOSI | GPIO7 | SPI data from ESP32 to CC1101 |
| CC1101 CS | GPIO10 | SPI chip-select for CC1101 |
| CC1101 MISO | GPIO20 | SPI data from CC1101 to ESP32 |
| WS2812B DATA | GPIO21 | LED data output |

## Reserved / Avoided GPIOs

| GPIO | Reason |
|---:|---|
| GPIO2 | ESP32-C3 strapping pin |
| GPIO8 | ESP32-C3 strapping pin |
| GPIO9 | ESP32-C3 strapping pin |

## Spare GPIO Capacity

The ESP32-C3 Super Mini exposes 13 GPIOs used in this design analysis.

Three GPIOs are intentionally avoided because they are strapping pins:

- GPIO2
- GPIO8
- GPIO9

This leaves 10 preferred GPIOs.

The current BubuDudu architecture requires all 10 of these GPIOs.

Therefore the initial theoretical design has no unused preferred GPIOs.

MOSFET control is intentionally not assigned a GPIO at this stage. A GPIO will only be allocated if subsystem power gating or another justified use is introduced later.