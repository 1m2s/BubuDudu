# BubuDudu Development Log

## 2026-09-07

### Completed

- Created the BubuDudu Git repository
- Defined system requirements
- Defined high-level system architecture
- Defined required hardware interfaces
- Identified ESP32-C3 GPIO constraints
- Created an initial theoretical GPIO assignment
- Created a dedicated `design/pin-map` branch for pin-map development

### Current Working State

The project is currently in the hardware architecture and pin-planning phase.

No final GPIO assignment has been hardware validated yet.

### Next Step

Validate the theoretical pin map against the actual BubuDudu hardware modules and revise assignments where necessary.

## 2026-09-08

### Completed

- Installed and configured PlatformIO in VS Code
- Restructured the project so the Git repository root is also the PlatformIO project root
- Configured the ESP32-C3 Super Mini using the `esp32-c3-devkitm-1` PlatformIO board profile
- Enabled native USB CDC communication at 115200 baud
- Successfully built, uploaded, and monitored firmware on the real ESP32-C3 Super Mini
- Created separate `bubu` and `dudu` PlatformIO build environments using one shared source tree
- Added compile-time device identity using `DEVICE_BUBU` and `DEVICE_DUDU`
- Added `include/Config.h` to expose the selected device identity to the firmware
- Verified that the same `src/main.cpp` builds and runs correctly as both Bubu and Dudu
- Created a dedicated `build/platformio` branch from `main`
- Moved the PlatformIO work away from `design/pin-map` without mixing branch histories
- Committed and pushed the PlatformIO firmware foundation to GitHub

### Current Working State

The project now has a working shared PlatformIO firmware foundation.

Bubu and Dudu use the same firmware source code but can be built with different compile-time identities.

The ESP32-C3 Super Mini has been hardware validated for building, uploading, automatic reset, native USB, and Serial Monitor communication using the current PlatformIO configuration.

The theoretical GPIO pin map remains separate on the `design/pin-map` branch and has not yet been fully hardware validated.

### Next Step

Continue hardware validation of the theoretical pin map, starting with the ESP32-C3 GPIO constraints and the GY-291 ADXL345 interface and wake-interrupt requirements.