# Pin Map Design Branch

## Purpose

This branch is used to design and refine the GPIO assignment for the BubuDudu hardware.

The initial pin map is theoretical and is based on:

- ESP32-C3 GPIO limitations
- boot and strapping pin restrictions
- Deep-sleep wake requirements
- I2C requirements
- SPI requirements
- interrupt requirements
- available GPIO capacity

## Development Approach

The first complete theoretical pin map is created before hardware integration.

The assignments are not considered final.

As individual subsystems are implemented and tested, the pin map may be revised based on:

- hardware compatibility
- sleep and wake testing
- ADXL345 interrupt behavior
- CC1101 SPI and interrupt behavior
- OLED and I2C integration
- WS2812B integration
- power-management requirements

Each meaningful design revision should be committed separately so that the evolution of the hardware design remains visible in the Git history.

## Merge Condition

This branch should only be merged into `main` once the pin map has been sufficiently validated through hardware testing.