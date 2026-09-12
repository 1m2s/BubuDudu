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

### ADXL345 Accelerometer

| ADXL345 Signal | Connection     | Status      | Purpose                                            |
| -------------- | -------------- | ----------- | -------------------------------------------------- |
| SDA            | ESP32-C3 GPIO0 | ✅ Validated | I²C data                                           |
| SCL            | ESP32-C3 GPIO1 | ✅ Validated | I²C clock                                          |
| INT1           | ESP32-C3 GPIO3 | ✅ Validated | Activity/inactivity interrupt and ESP32 sleep wake |
| CS             | 3.3 V          | ✅ Validated | Held HIGH to operate the ADXL345 in I²C mode       |
| VCC            | 3.3 V          | ✅ Validated | Sensor power                                       |
| GND            | GND            | ✅ Validated | Common ground                                      |

**I²C address:** `0x53`

#### Validation notes

* GPIO0/GPIO1 successfully communicate with the ADXL345 over I²C.
* `CS` is tied to 3.3 V so the ADXL345 operates using I²C rather than SPI.
* Both Bubu and Dudu ADXL345 modules were successfully detected at address `0x53`.
* GPIO3 was tested with the ADXL345 `INT1` output.
* Activity and inactivity interrupts were successfully received on GPIO3.
* GPIO3 successfully woke the ESP32-C3 from both light sleep and deep sleep.
* The ADXL345 remains powered while the ESP32 is in deep sleep and can monitor motion and wake the controller.
* GPIO0, GPIO1 and GPIO3 should now be treated as reserved for the motion subsystem when assigning the remaining BubuDudu pins.
