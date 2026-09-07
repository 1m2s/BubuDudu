# BubuDudu

BubuDudu is a pair of symmetric wireless companion devices built around the ESP32-C3.

Each device can detect user input and motion, communicate wirelessly with the other device, and provide visual feedback.

The project is being developed as a complete embedded-systems project with emphasis on communication protocols, sensor integration, power management, hardware interfaces, testing, and maintainable firmware architecture.

## Planned Features

- Bidirectional ESP-NOW communication
- Application-level acknowledgements and retries
- Peer availability detection
- ADXL345 motion sensing
- Gesture detection
- Deep-sleep power management
- Motion-based wake
- WS2812B heartbeat animations
- OLED diagnostic interface
- RSSI-based proximity experimentation
- CC1101 433 MHz secondary radio
- Battery-powered operation
- Shared firmware for Bubu and Dudu

## Development Approach

The project follows a requirements-first development process:

Requirements -> Architecture -> Interfaces -> Pin Map -> Protocol -> Firmware -> Testing

Hardware and software decisions are documented and validated incrementally.

## Current Status

System requirements, architecture, and hardware interfaces have been defined.

An initial theoretical ESP32-C3 pin map is currently being developed and validated on a dedicated design branch.