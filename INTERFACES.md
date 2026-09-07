# BubuDudu Hardware Interfaces

| Component | Interface | Required ESP32 Resources | Purpose |
|---|---|---|---|
| Button | Digital input | 1 GPIO | User input |
| WS2812B LEDs | Digital output | 1 GPIO | LED data signal |
| ADXL345 | I2C | SDA + SCL | Motion and acceleration data |
| ADXL345 interrupt | Digital interrupt | 1 GPIO | Motion/inactivity detection and ESP32 wake |
| OLED | I2C | Shared SDA + SCL | Diagnostic display |
| CC1101 | SPI | SCK + MOSI + MISO + CS | 433 MHz radio communication |
| CC1101 GDO | Digital interrupt/input | At least 1 GPIO | Radio event notification |
| MOSFET control | Digital output | 1 GPIO if used | Power gating or load control |

## Shared buses

The ADXL345 and OLED can share the same I2C bus.

Therefore both devices can use the same:

- SDA pin
- SCL pin

The CC1101 uses a separate SPI bus.

## Estimated GPIO Usage

Unique GPIO signals currently required:

- Button: 1
- WS2812B: 1
- I2C SDA: 1
- I2C SCL: 1
- ADXL345 interrupt: 1
- SPI SCK: 1
- SPI MOSI: 1
- SPI MISO: 1
- CC1101 CS: 1
- CC1101 GDO: at least 1
- MOSFET control: 1 if required

Estimated total:

10 GPIOs without MOSFET control

11 GPIOs if MOSFET control is used