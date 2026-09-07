# BubuDudu Requirements

## 1. Device concept

Bubu and Dudu are two symmetric wireless companion devices.

Each device must be capable of:
- detecting local input
- sending events
- receiving events
- displaying visual feedback
- detecting motion
- managing its own power state

Neither device is permanently the sender or receiver.

## 2. Primary interaction

When the user triggers an event on one device:

Bubu → wireless message → Dudu → LED reaction

or

Dudu → wireless message → Bubu → LED reaction

The main visual reaction will eventually be a heartbeat-style LED animation.

## 3. Primary wireless communication

ESP-NOW will be the primary communication method.

The application protocol must eventually support:
- message types
- message IDs
- acknowledgements
- timeout detection
- limited retries
- duplicate detection
- peer availability
- simultaneous bidirectional events

## 4. Motion sensing

The ADXL345 accelerometer will be used for:
- activity detection
- inactivity detection
- waking the ESP32 from sleep
- gesture detection

Possible gestures include:
- double tap
- shake
- movement
- orientation changes

## 5. Power management

The device must support:

Active → Idle → Sleep → Motion Wake → Active

The ADXL345 must be capable of waking the ESP32 when movement occurs.

## 6. Secondary wireless communication

The CC1101 433 MHz radio will be added as a secondary communication system.

ESP-NOW remains the primary radio.

The secondary radio may later be used for fallback communication.

## 7. Diagnostic display

The OLED should eventually display useful system information such as:
- device identity
- peer online/offline state
- RSSI
- active radio
- message ID
- ACK status
- power state
- battery information

## 8. LED interface

WS2812B LEDs will provide the main visual interface.

Animations must eventually be non-blocking.

Important states may include:
- heartbeat
- connected
- peer lost
- searching
- sleep
- wake
- fallback radio
- gesture events

## 9. Firmware architecture

Bubu and Dudu must use one shared firmware codebase.

Device identity and peer configuration must come from configuration rather than maintaining two separate programs.