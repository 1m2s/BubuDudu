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

### WS2812B RGB LED

| WS2812B Signal | Connection | Status | Purpose |
| -------------- | ---------- | ------ | ------- |
| DIN | ESP32-C3 GPIO21 through 330 Ω resistor | ✅ Validated | Digital LED data signal |
| 5V | 5 V supply | ✅ Validated | LED power |
| GND | GND | ✅ Validated | Common ground |

#### Validation notes

* GPIO21 successfully controls the WS2812B data input.

* A 330 Ω series resistor is placed between ESP32-C3 GPIO21 and the WS2812B `DIN` pin.

* The 330 Ω resistor is used to improve signal integrity and help protect the WS2812B data input from sharp signal transitions.

* The WS2812B is powered from 5 V.

* The WS2812B ground is connected to the ESP32-C3 ground so both devices share the same electrical reference for the data signal.

* WS2812B control was successfully tested on both Bubu and Dudu.

* RGB color output was successfully validated on both devices.

* The heartbeat-style LED animation was successfully tested using the WS2812B.

* GPIO21 should now be treated as reserved for the LED subsystem when assigning the remaining BubuDudu pins.

### OLED Display

| OLED Signal | ESP32-C3 Connection | Status       | Purpose          |
|-------------|----------------------|--------------|------------------|
| SDA         | GPIO0                | ✅ Validated | I²C data         |
| SCK / SCL   | GPIO1                | ✅ Validated | I²C clock        |
| VDD         | 3.3 V                | ✅ Validated | Power supply     |
| GND         | Common GND           | ✅ Validated | Ground reference |

**Module:** 1.3-inch 128×64 OLED  
**Controller:** SH1106  
**I²C address:** `0x3C`

The OLED shares the same I²C bus as the ADXL345:

- GPIO0 → SDA
- GPIO1 → SCL

Hardware validation confirmed both devices operate simultaneously:

- OLED detected at `0x3C`
- ADXL345 detected at `0x53`
- OLED text output works while the ADXL345 motion subsystem remains operational
- No additional ESP32-C3 GPIO pins are required for the OLED

### CC1101 433 MHz Radio

\| CC1101 Signal | ESP32-C3 Connection | Status | Purpose |

\| -------------- | -------------------- | ------ | ------- |

\| SCK | GPIO6 | ✅ Validated | SPI clock |

\| MOSI | GPIO7 | ✅ Validated | SPI data from ESP32 to CC1101 |

\| MISO | GPIO20 | ✅ Validated | SPI data from CC1101 to ESP32 |

\| CSN | GPIO10 | ✅ Validated | Active-low SPI chip select |

\| VCC | 3.3 V | ✅ Validated | Radio power supply |

\| GND | Common GND | ✅ Validated | Ground reference |

\| GDO0 | Not connected | ⏸️ Unused | Optional radio interrupt/event output |

\| GDO2 | Not connected | ⏸️ Unused | Optional radio interrupt/event output |

Operating frequency: `433.92 MHz`

SPI clock: `100 kHz`

SPI mode: `Mode 0`

Bit order: `MSB first`

The CC1101 uses the following SPI connections:

\- GPIO6 → SCK

\- GPIO7 → MOSI

\- GPIO20 → MISO

\- GPIO10 → CSN

#### Validation notes

\- GPIO6, GPIO7, GPIO20 and GPIO10 successfully communicate with the CC1101 over SPI.

\- The CC1101 reset sequence was successfully validated on both Bubu and Dudu.

\- CC1101 identification registers were successfully read after reset.

\- Both radios were successfully configured for operation at `433.92 MHz`.

\- Bidirectional packet transmission was successfully validated between Bubu and Dudu.

\- Structured `Protocol::Message` packets were successfully transmitted over the CC1101 link.

\- Application-level EVENT and ACK messages were successfully transmitted in both directions.

\- Message IDs and ACK matching were successfully validated.

\- A `300 ms` application ACK timeout was implemented and tested.

\- A maximum of two retries is used when an ACK is not received.

\- Retries reuse the same message ID so the retransmission represents the same logical event.

\- Duplicate EVENT packets are detected and are not processed twice.

\- Duplicate EVENT packets are still ACKed again so the sender can recover when a previous ACK was lost.

\- Peer online/offline behaviour and recovery after communication returns were successfully tested.

\- CC1101 communication was moved into a dedicated FreeRTOS `RadioTask`.

\- The current `RadioTask` is intended to be the sole owner of the CC1101 hardware.

\- GDO0 and GDO2 are currently not required because the existing driver polls CC1101 radio state and FIFO status through SPI.

\- GPIO4 was originally reserved for a CC1101 GDO connection but is currently unused.

\- GPIO6, GPIO7, GPIO10 and GPIO20 should now be treated as reserved for the CC1101 communication subsystem.