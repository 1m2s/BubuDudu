# BubuDudu ESP32-C3 Pin Map

This document defines the physical GPIO assignments used by each BubuDudu device.

## Target Board

ESP32-C3 Super Mini

## Exposed GPIOs

- GPIO0
- GPIO1
- GPIO2
- GPIO3
- GPIO4
- GPIO5
- GPIO6
- GPIO7
- GPIO8
- GPIO9
- GPIO10
- GPIO20
- GPIO21

Total exposed GPIOs: 13

## Design Goal

Assign GPIOs for:

- Button
- WS2812B data
- I2C SDA
- I2C SCL
- ADXL345 interrupt / wake
- CC1101 SPI SCK
- CC1101 SPI MOSI
- CC1101 SPI MISO
- CC1101 CS
- CC1101 GDO
- Optional MOSFET control

## GPIO Classification

Before assigning hardware functions, the exposed GPIOs are classified based on ESP32-C3 restrictions and special functions.

### Good General GPIOs

- GPIO0
- GPIO1
- GPIO3
- GPIO10

GPIO0 to GPIO5 also belong to the RTC power domain and can be used for Deep-sleep wake functionality.

### Special / Use Carefully

- GPIO4 - JTAG function available; also Deep-sleep wake capable
- GPIO5 - JTAG function available; also Deep-sleep wake capable
- GPIO6 - JTAG function available
- GPIO7 - JTAG function available
- GPIO20 - UART0 RX function available
- GPIO21 - UART0 TX function available

These GPIOs can still be used by BubuDudu, but their alternate functions should be considered before assignment.

### Avoid If Possible

- GPIO2 - strapping pin
- GPIO8 - strapping pin
- GPIO9 - strapping pin

Strapping pins are sampled during reset and influence ESP32-C3 boot configuration. They should be avoided when sufficient alternative GPIOs are available.
