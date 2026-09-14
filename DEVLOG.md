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

## 2026-09-09

### Completed

* Improved the dual-device PlatformIO workflow for working with Bubu and Dudu at the same time

* Identified the permanent Wi-Fi MAC address of each ESP32-C3 Super Mini

* Confirmed the physical device identities:

  * Bubu: `E8:F6:0A:12:4C:A4`
  * Dudu: `E8:F6:0A:12:5B:84`

* Confirmed the current USB serial-port mapping for both connected boards

* Created a new `tools/` directory for project development utilities

* Added `tools/flash_device.py`

* Implemented automatic device detection by reading the connected ESP32-C3 MAC addresses

* Added logic that determines whether a connected board is Bubu or Dudu based on its MAC address

* Integrated the Python flashing script into the PlatformIO development workflow

* Verified that both ESP32-C3 boards can remain connected simultaneously while the flashing tool automatically identifies the correct physical device

* Verified automatic flashing of the correct Bubu or Dudu PlatformIO environment without manually selecting the board by USB port

### Current Working State

The project now has a working automated dual-board development workflow.

Bubu and Dudu can both be connected to the computer simultaneously, and the tooling can identify each physical ESP32-C3 by its permanent MAC address instead of relying on changing USB serial-port names.

The `tools/flash_device.py` utility is integrated into the PlatformIO workflow and can select the appropriate Bubu or Dudu firmware environment for the detected board.

This keeps the project on one shared firmware codebase while allowing reliable development and flashing of the two physical devices independently.

### Next Step

Begin the physical hardware assembly and bring-up phase by starting from the known-working ESP32-C3 PlatformIO foundation and adding hardware modules one at a time, beginning with the basic input stage before moving to the GY-291 ADXL345.

## 2026-09-10

### Completed

* Improved PlatformIO tooling so Bubu and Dudu are selected reliably for both upload and serial monitoring
* Replaced repeated `esptool read_mac` probing with USB serial-number based board identification
* Added `.pio/ports.ini` generation so PlatformIO's built-in Monitor and Upload-and-Monitor tasks open the correct board
* Added a safe `monitor_auto` custom task that resolves the requested board's port live
* Added busy-port detection using `lsof` and clear error messages when another process is holding the serial port
* Removed the previous JSON port-cache approach
* Validated ADXL345 communication over I²C on both Bubu and Dudu
* Confirmed both accelerometers respond at I²C address `0x53`
* Confirmed both boards independently produce live X/Y/Z acceleration data
* Hardware debugging on Bubu identified an incorrect SDA/SCL connection; after correcting the wiring, I²C communication worked normally

### Problems Solved

PlatformIO uploads were already routed to the correct ESP32, but the built-in serial monitor could still open the wrong board. The reason was that `pio device monitor` runs independently from PlatformIO's SCons build process, so `extra_scripts` did not execute and the dynamically selected monitor port was never applied.

The tooling now identifies Bubu and Dudu using the USB serial number exposed by macOS, writes the resolved monitor ports into `.pio/ports.ini`, and loads that file through `platformio.ini`. The script also refuses to silently fall back to another ESP32 and reports when a serial port is already occupied.

During ADXL345 bring-up, Dudu communicated successfully while Bubu initially showed no I²C devices. Swapping the accelerometer modules showed that the failure stayed with Bubu, proving the sensor module itself was not the problem. The issue was traced to incorrect wiring on Bubu: SDA had been connected to an unconnected ESP32 pin and SCL had been connected to GPIO0. After correcting the wiring to SDA → GPIO0 and SCL → GPIO1, both devices detected their ADXL345 at `0x53` and produced independent acceleration readings.

### Current Working State

Both Bubu and Dudu now have working ADXL345 communication over the shared I²C pin assignment:

* GPIO0 → SDA
* GPIO1 → SCL
* ADXL345 address → `0x53`

Both boards can be uploaded to and monitored independently without manually selecting serial ports.

The current ADXL345 test firmware reads the six acceleration data registers and prints raw X, Y and Z values to the serial monitor.

### Git Commits

* `c6efb58` — `tooling: reliably select per-board upload and monitor ports` on `main`
* `84316f5` — same tooling change on `feature/adxl345`
* `dfa68ac` — `feat: validate ADXL345 I2C communication` on `feature/adxl345`

### Next Step

Understand the ADXL345 acceleration data before adding more functionality: determine what the raw X/Y/Z values represent, how gravity appears in the readings, how the sensor's measurement range and resolution affect the values, and then decide the correct configuration for BubuDudu.

After the basic measurements are understood, continue toward ADXL345 activity/inactivity detection and the interrupt-based motion wake design.

## 2026-09-12

### Completed

* Continued ADXL345 development on `feature/adxl345`
* Verified and understood raw X/Y/Z acceleration readings
* Confirmed stationary acceleration measurements are dominated by gravity and depend on sensor orientation
* Converted raw ADXL345 readings into acceleration values in `g`
* Configured the ADXL345 explicitly for:

  * full-resolution mode
  * ±4 g measurement range
* Verified that approximately 1 raw count corresponds to 0.0039 g in the selected full-resolution configuration
* Observed normal sensor noise while stationary and confirmed that activity/inactivity thresholds must tolerate small measurement variations
* Configured ADXL345 hardware activity/inactivity detection
* Initial test thresholds:

  * activity threshold: 0.25 g
  * inactivity threshold: 0.125 g
  * inactivity time: 3 seconds
* Enabled activity/inactivity detection on X, Y and Z
* Verified activity and inactivity events by reading `INT_SOURCE` over I²C
* Connected ADXL345 `INT1` to ESP32-C3 GPIO3
* Debugged an initial interrupt failure caused by an incorrect physical INT1 connection
* Verified that the ADXL345 physically drives its interrupt output and that GPIO3 receives the signal
* Replaced GPIO polling with an ESP32 interrupt service routine (ISR)
* Kept the ISR intentionally minimal by only setting an event flag and handling I²C/Serial work in normal program execution
* Enabled ADXL345 LINK mode so activity and inactivity detection alternate cleanly instead of repeatedly generating identical activity events
* Verified stable transitions between:

  * ACTIVE
  * INACTIVE
* Implemented and tested ESP32 light-sleep wake using the ADXL345 interrupt
* Verified the sequence:

  * ACTIVE
  * INACTIVE
  * light sleep
  * motion
  * GPIO3 wake
  * ACTIVE
* Implemented and tested ESP32 deep-sleep wake using ADXL345 `INT1`
* Verified that GPIO3 successfully wakes the ESP32-C3 from deep sleep
* Verified the wake reason after reboot:

  * `ESP_SLEEP_WAKEUP_GPIO`
* Confirmed the GPIO wake mask identifies GPIO3
* Confirmed that the ADXL345 activity event remains available after the ESP32 wakes
* Verified the complete power-management sequence:

  * ACTIVE
  * inactivity detected
  * ESP32 enters deep sleep
  * ADXL345 remains active and monitors motion
  * movement generates an activity interrupt
  * GPIO3 wakes the ESP32
  * firmware boots again
  * motion wake is identified
  * system returns to ACTIVE
* Refactored the ADXL345 implementation out of `main.cpp`
* Added a dedicated `Motion` module:

  * `src/Motion.h`
  * `src/Motion.cpp`
* The `Motion` module now owns:

  * ADXL345 register configuration
  * activity/inactivity thresholds
  * ADXL345 interrupt handling
  * ISR event flag
  * activity/inactivity event reporting
* `main.cpp` now reacts to high-level `MotionEvent` values instead of directly managing ADXL345 registers
* Added a pre-sleep interrupt check to avoid entering deep sleep if motion occurs during the transition into sleep
* Verified that the refactored Motion module preserves the previously working deep-sleep motion-wake behavior

### Current Working State

The primary ADXL345 power-management functionality is now operational.

Bubu can:

1. detect when it has remained inactive
2. generate a hardware inactivity interrupt
3. place the ESP32-C3 into deep sleep
4. leave the ADXL345 powered and monitoring motion
5. detect movement while the ESP32 is asleep
6. drive `INT1`
7. wake the ESP32 through GPIO3
8. identify the wake as a motion-triggered GPIO wake
9. resume in the ACTIVE state

Current validated connections:

* ADXL345 SDA → GPIO0
* ADXL345 SCL → GPIO1
* ADXL345 INT1 → GPIO3
* ADXL345 I²C address → `0x53`

The ADXL345 is currently configured for full-resolution ±4 g operation with LINK-mode activity/inactivity detection.

### Known Issue / Development Annoyance

The current test firmware automatically places the ESP32 into deep sleep after approximately three seconds of inactivity.

Because of this, Bubu can enter deep sleep while PlatformIO is compiling or preparing an upload. When the ESP32 is already sleeping, `esptool` may fail to connect and report:

`Failed to connect to ESP32-C3: No serial data received.`

For now, the practical workaround is to keep moving/shaking the ADXL345 while starting an upload so the device remains in the ACTIVE state until `esptool` establishes the connection.

This is acceptable during development but should eventually be improved so firmware upload/debug workflows are not affected by automatic power management.

### Problems Solved

* ADXL345 raw data interpretation
* gravity/orientation behavior
* raw-to-`g` conversion
* stationary sensor noise handling
* activity/inactivity threshold configuration
* ADXL345 internal event detection
* physical `INT1` interrupt wiring
* ESP32 ISR handling
* repeated activity interrupt flooding using ADXL345 LINK mode
* ESP32 light-sleep motion wake
* ESP32 deep-sleep motion wake
* wake-cause identification
* separation of ADXL345 logic into a dedicated Motion module

### Next Step

Pause further ADXL345 feature expansion for now.

The primary ADXL345 purpose — motion-based power management — has been validated.

Do not add gestures yet.

Continue with the next BubuDudu hardware subsystem while preserving the current working Motion module and deep-sleep wake implementation.

## 2026-09-13

### Completed

- Added Adafruit NeoPixel library through PlatformIO
- Wired WS2812B LEDs to GPIO21
- Validated WS2812B control on Bubu
- Reworked soldered WS2812B assemblies
- Validated WS2812B control on Dudu
- Confirmed RGB color changes on both devices
- GPIO21 is now validated for the WS2812B subsystem on both Bubu and Dudu

### Problems Solved

- Initial WS2812B tests failed due to hardware/power and soldering issues
- One LED assembly showed unstable behavior after excessive heating
- Re-soldered and rebuilt the LED connections
- Verified ESP32 GPIO21 output and WS2812B power independently
- Final assemblies now work correctly on both devices

### Current Working State

Bubu and Dudu now both have working WS2812B LEDs on GPIO21.

The ADXL345 Motion subsystem remains complete and validated.

### Next Step

Integrate the WS2812B control with the existing Motion subsystem, then begin implementing the heartbeat-style LED pulse.

## 2026-09-14

### Completed

* Continued WS2812B integration with the existing Motion subsystem

* Tuned the ADXL345 motion sensitivity based on hardware testing

* Updated ADXL345 activity/inactivity configuration to:

  * activity threshold: 3.0 g

  * inactivity threshold: 0.25 g

  * inactivity time: 3 seconds

* Verified that the updated motion thresholds behave more reliably during normal device handling

* Added a dedicated LED module:

  * `include/LED.h`

  * `src/LED.cpp`

* Moved WS2812B control and heartbeat behavior out of `main.cpp` into the LED module

* Verified the heartbeat animation on the physical WS2812B hardware

* Updated the pin-map documentation with the validated WS2812B connection:

  * WS2812B DIN → GPIO21 through 330 ohm series resistor

* Confirmed that the existing Motion subsystem, inactivity detection, deep sleep, motion wake, and heartbeat continue to operate together

* Confirmed that FreeRTOS is already included through the Arduino-ESP32 / ESP-IDF environment and does not require a separate manual installation

* Created the `feature/freertos` branch for the first FreeRTOS integration

* Added a temporary FreeRTOS test task to verify task scheduling on the ESP32-C3

* Verified that the test task executes alongside the existing Arduino application flow

* Verified that FreeRTOS tasks stop when the ESP32 enters deep sleep and are recreated after wake and reboot

* Removed the temporary test task after FreeRTOS operation was confirmed

* Added the first real BubuDudu FreeRTOS task

* Moved the existing blocking heartbeat animation from the Arduino application flow into a dedicated LED task using `xTaskCreate()`

* Kept the existing heartbeat implementation unchanged during the RTOS migration

* Verified that heartbeat delays now block only the LED task rather than the Motion/system flow

* Verified that Motion events continue to be processed while the heartbeat animation is running

* Verified inactivity detection, deep sleep, motion wake, and LED behavior after FreeRTOS integration

* Did not introduce queues, mutexes, semaphores, event groups, or task notifications because no current requirement justifies them

* Created the `feature/oled` branch for OLED hardware and software bring-up

* Connected the 1.3-inch 128×64 SH1106 OLED to the existing I²C bus

* Validated OLED wiring:

  * SDA → GPIO0

  * SCK / SCL → GPIO1

  * VDD → 3.3 V

  * GND → common GND

* Ran an I²C scan with the ADXL345 and OLED connected simultaneously

* Confirmed both devices are visible on the same I²C bus:

  * OLED → `0x3C`

  * ADXL345 → `0x53`

* Added the U8g2 display library through PlatformIO

* Successfully initialized the SH1106 OLED

* Successfully displayed test text on the physical OLED

* Verified that the ADXL345 Motion subsystem continues operating while the OLED is connected

* Verified that inactivity detection, deep sleep, motion wake, WS2812B heartbeat, and OLED operation continue to work together

* Added a dedicated Display module:

  * `include/Display.h`

  * `src/Display.cpp`

* Moved OLED-specific implementation details out of `main.cpp`

* Kept the Display subsystem synchronous

* Did not create a dedicated Display FreeRTOS task because the current display behavior does not require independent execution

* Reorganized module headers into PlatformIO's `include/` directory

* Current module header layout now includes:

  * `include/Motion.h`

  * `include/LED.h`

  * `include/Display.h`

* Applied the header-directory cleanup to the relevant development branches

* Updated the pin-map documentation with the validated OLED configuration

* Confirmed that the OLED consumes no additional ESP32-C3 GPIO pins because it shares the existing GPIO0/GPIO1 I²C bus with the ADXL345


### Problems Solved

* Adjusted ADXL345 motion thresholds to produce more useful real-world activity/inactivity behavior

* Separated WS2812B control from `main.cpp` into a dedicated LED module

* Removed the blocking heartbeat animation from the main application flow by moving it into a FreeRTOS LED task

* Verified that FreeRTOS support is already provided by the ESP32 Arduino environment and that the separately downloaded FreeRTOS source code is unnecessary

* Established the first useful FreeRTOS architecture without introducing unnecessary RTOS mechanisms

* Verified that the ADXL345 and OLED can share the same SDA/SCL lines because they use different I²C addresses

* Confirmed simultaneous I²C communication with the OLED at `0x3C` and ADXL345 at `0x53`

* Debugged initial OLED initialization while preserving the already-working Motion subsystem

* Separated OLED implementation details from `main.cpp` into a dedicated Display module

* Corrected the project source layout by moving module header files from `src/` into PlatformIO's `include/` directory

* Updated the hardware pin-map documentation to reflect the validated WS2812B and OLED configurations


### Current Working State

The project currently has working ADXL345 motion-based power management, WS2812B heartbeat output, FreeRTOS LED execution, and SH1106 OLED output.

Current validated hardware connections:

* GPIO0 → shared I²C SDA

  * ADXL345 SDA

  * OLED SDA

* GPIO1 → shared I²C SCL

  * ADXL345 SCL

  * OLED SCK / SCL

* GPIO3 → ADXL345 INT1 / deep-sleep wake

* GPIO21 → WS2812B DIN through 330 ohm series resistor

Validated I²C addresses:

* ADXL345 → `0x53`

* OLED → `0x3C`

Current software modules:

* `Motion`

* `LED`

* `Display`

Current project structure uses:

* `include/Motion.h`

* `include/LED.h`

* `include/Display.h`

* `src/Motion.cpp`

* `src/LED.cpp`

* `src/Display.cpp`

* `src/main.cpp`

The current FreeRTOS structure consists of:

* Arduino `loopTask`

  * Motion event handling

  * inactivity handling

  * deep-sleep control

  * current synchronous Display operations

* LED task

  * blocking heartbeat animation

The heartbeat animation still intentionally contains blocking delays, but those delays now block only the LED task.

The main application flow can continue processing Motion events while the heartbeat is running.

The Display subsystem does not currently have its own FreeRTOS task.

This is intentional because the current display behavior does not require independent execution.

The OLED hardware, I²C communication, SH1106 initialization, and basic text output have been validated.

The final BubuDudu OLED user interface and diagnostic behavior have not yet been designed.

The ADXL345 deep-sleep and motion-wake functionality remains operational with the LED and OLED subsystems connected.


### Known Issue to Monitor

During one test, the WS2812B remained illuminated after the ESP32 entered deep sleep even though `led.off()` is called before entering deep sleep.

This has only been observed once and has not been confirmed as a repeatable problem.

No change will be made unless the behavior becomes reproducible.

If it occurs again, investigate the interaction between the LED FreeRTOS task, `led.off()`, and the transition into ESP32 deep sleep.


### Git Commits

* `c5e13df` — `tune: adjust ADXL345 motion sensitivity`

* `db1492a` — `refactor: add LED module`

* `88bff75` — `feat: run heartbeat in FreeRTOS LED task`

* `39fc271` — `feat: validate OLED on shared I2C bus`

* `947d6e7` — `refactor: move headers to include directory`

* Display module refactor committed on `feature/oled`

* OLED shared-I²C pin-map validation committed on `design/pin-map`


### Next Step

Continue development on `feature/oled`.

The OLED hardware bring-up is complete, so the next goal is to replace the temporary test text with a purposeful BubuDudu diagnostic interface.

Determine which system information should eventually be displayed, including candidates such as:

* device identity

* peer online/offline state

* RSSI / proximity state

* active communication radio

* message ID

* ACK state

* power state

Keep the Display subsystem synchronous for now.

Do not create a Display FreeRTOS task, mutex, queue, semaphore, event group, or task notification unless later system behavior creates a real requirement.

Preserve the current working Motion, deep-sleep wake, LED task, and OLED functionality while continuing development.