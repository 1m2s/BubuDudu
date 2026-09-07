# BubuDudu System Architecture

## Device Architecture

Bubu and Dudu use the same hardware and firmware architecture.

Each device contains an ESP32-C3 as the main controller.

The ESP32-C3 is responsible for:

- processing local input
- processing motion data
- wireless communication
- LED control
- diagnostic display control
- system state management
- power management

## Inputs

### Button
Provides direct user input.

### ADXL345 Accelerometer
Provides:
- motion detection
- inactivity detection
- gesture detection
- wake interrupt for the ESP32

## Outputs

### WS2812B LEDs
Main visual/emotional output.

### OLED Display
Diagnostic and system-status interface.

## Communication

### ESP-NOW
Primary low-latency wireless communication between Bubu and Dudu.

### CC1101
Secondary 433 MHz wireless communication system.

## Power Management

The system will eventually support:

Active -> Idle -> Sleep -> Motion Wake -> Active

The ADXL345 interrupt output will be used to wake the ESP32 from sleep.

## Shared Firmware

Both devices use one firmware codebase.

Configuration determines whether a build operates as Bubu or Dudu.