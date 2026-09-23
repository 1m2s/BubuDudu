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

## 2026-09-16

### CC1101 SPI bring-up

Started hardware bring-up of the CC1101 433 MHz secondary radio.

Created development branch:

- `feature/cc1101`

The branch was created from the validated `feature/oled` state. The OLED branch has not been merged into `main`.

### CC1101 hardware verification

Reviewed the actual 8-pin CC1101 module instead of relying blindly on the seller's wiring diagram.

The Amazon listing contained documentation for both an 8-pin and a different 10-pin CC1101 breakout, creating ambiguity around the power pins.

Verified the actual module using a multimeter:

- CC1101 pin 1 -> GND
- CC1101 pin 2 -> VCC
- pin 1 had continuity with the SMA connector outer shield
- pin 2 did not

Validated CC1101 supply voltage at the module:

- approximately 3.3 V

Validated continuity of all SPI connections.

Current CC1101 wiring on Bubu:

- GND -> GND
- VCC -> 3.3 V
- CSN -> GPIO10
- SCK -> GPIO6
- MOSI -> GPIO7
- MISO/GDO1 -> GPIO20

Currently unused:

- GDO0
- GDO2

### Raw SPI bring-up

Temporarily replaced the integrated application `main.cpp` with a minimal CC1101 SPI test program.

The first milestone is intentionally limited to reading known CC1101 registers before attempting any RF transmission.

Attempted to read:

- IOCFG2
- PARTNUM
- VERSION

Initial reads returned unstable values instead of stable identification values.

Examples included changing PARTNUM and VERSION values on every read.

Added the CC1101 SRES reset sequence.

Reset currently reports:

`RESET ERROR: CC1101 did not finish reset.`

Register values remain unstable.

No RF transmission has been attempted.

### Logic analyzer bring-up

Used the 8-channel 24 MHz USB logic analyzer for the first time.

Initial PulseView nightly build crashed on macOS.

Installed the stable PulseView 0.4.2 build instead.

The analyzer was detected by macOS as:

- Vendor ID: `0x0925`
- Product ID: `0x3881`

PulseView successfully detected it using the `fx2lafw` driver as:

- Saleae Logic with 8 channels

Connected logic analyzer channels:

- D0 / physical CH1 -> CSN
- D1 / physical CH2 -> SCK
- D2 / physical CH3 -> MOSI
- D3 / physical CH4 -> MISO

Captured real CC1101 SPI traffic.

The raw capture confirmed:

- CSN changes state during transactions
- SCK produces regular clock pulses
- MOSI carries structured data
- MISO is electrically active and carries structured transitions

This ruled out several simple failure cases such as completely missing clock activity or a permanently floating MISO line.

### SPI decoder investigation

Configured the PulseView SPI protocol decoder.

Current decoder configuration:

- CLK -> D1
- MOSI -> D2
- MISO -> D3
- CS -> D0
- CS active low
- MSB first
- 8-bit words

The decoded MOSI values currently appear as:

- `00 00`
- `E0 00`
- `E2 00`

The firmware intended commands equivalent to approximately:

- `80 00`
- `F0 00`
- `F1 00`

The decoded values therefore appear to be shifted by approximately one bit.

MISO also produced structured decoded values such as:

- `1E A4`
- `1E 84`
- `1E FF`

This suggests the next debugging step should be to verify SPI decoder timing / clock phase and compare the decoded bytes against the raw waveform before concluding that the CC1101 itself is faulty.

### Current status

CC1101 SPI communication is not yet validated.

Confirmed working:

- module receives 3.3 V
- wiring continuity
- ESP32 generates CSN activity
- ESP32 generates SPI clock
- MOSI activity reaches the bus
- MISO is electrically active
- logic analyzer works
- PulseView SPI captures work

Still unresolved:

- CC1101 reset completion
- unstable register values
- apparent one-bit shift in decoded MOSI data
- reliable PARTNUM / VERSION identification

### Next step

Resume on `feature/cc1101`.

Before changing hardware or attempting RF transmission:

1. restore the CC1101 bring-up code
2. verify PulseView SPI clock phase / sampling edge
3. determine why intended MOSI bytes such as `0x80`, `0xF0`, and `0xF1` decode as `0x00`, `0xE0`, and `0xE2`
4. compare analyzer-decoded MISO data with values received by the ESP32
5. obtain stable believable CC1101 register values

Do not continue to RF transmission until the SPI identification milestone is working reliably.

## 2026-09-17

### Completed

* Continued CC1101 integration and debugging on the ESP32-C3 Super Mini.
* Confirmed the CC1101 hardware and raw SPI communication were working correctly.
* Confirmed reliable bidirectional CC1101 communication between Bubu and Dudu.
* Verified the application-level reliability behavior:

  * structured EVENT messages
  * message IDs
  * ACK generation
  * ACK matching
  * 300 ms timeout
  * maximum 2 retries
  * retries reuse the same message ID
  * duplicate EVENT detection
  * duplicate EVENTs are not processed twice
  * duplicate EVENTs still receive another ACK
  * peer offline detection after failed communication
  * automatic recovery when communication returns
* Moved the working CC1101 communication stack into a dedicated FreeRTOS `RadioTask`.
* Verified that moving the radio communication into a FreeRTOS task did not break the previously working protocol behavior.
* Established the intended ownership rule that `RadioTask` is the only task that should directly access `CC1101Radio`.
* Experimented with FreeRTOS queues for communication between the application and `RadioTask`.
* Experimented with reintegrating Motion, OLED, LED, deep sleep and CC1101 into the full firmware.
* Tested a 30-second application-level inactivity grace period before deep sleep instead of immediately sleeping after the ADXL345 reports inactivity.
* Stashed this unfinished queue/power-policy integration rather than committing experimental code.
* Created the clean `feature/espnow` branch from the last committed CC1101 + FreeRTOS checkpoint in preparation for implementing the second communication transport.

### Important debugging lesson

A large part of the CC1101/SPI debugging effort was ultimately caused by a misplaced jumper wire on the breadboard.

The logic analyzer was therefore not required to solve the final hardware fault.

However, the debugging work was still valuable because I learned:

* what SPI is and how the bus works
* the roles of SCK, MOSI, MISO and CS/CSN
* how SPI transactions appear on a logic analyzer
* how to configure PulseView for SPI decoding
* what signals to inspect when debugging an SPI peripheral
* how to distinguish a software/protocol problem from a physical wiring problem
* why basic wiring verification should happen before deeper protocol debugging

This was a good reminder to check the simplest physical causes first before assuming a more complicated firmware or protocol failure.

### Personal note

Today's work became somewhat sloppy toward the end of the session.

I was mentally unfocused and, during parts of the queue/full-system integration, I was copying code without properly reading and understanding every change. That goes against the purpose of this project: I want to be able to explain every architectural and implementation decision myself rather than merely produce working firmware.

Because of that, I deliberately did **not** commit the unfinished queue/power integration. It was stashed instead.

Before building further on that work, I need to revisit the relevant code when focused and make sure I understand:

* how the FreeRTOS TX and RX queues work
* why the application should enqueue an event rather than construct radio packets itself
* how `RadioTask` owns protocol reliability and CC1101 access
* how the LED task receives application events
* how the power/sleep policy should interact with communication activity
* what should define peer presence, proximity and wake behavior

The goal is not just to have commits that work. The goal is to understand why they work.

### Current working checkpoint

Current branch:

`feature/espnow`

Current working tree:

clean

`feature/espnow` currently starts from:

`dd729bb refactor: move CC1101 communication into FreeRTOS task`

The unfinished queue + 30-second power-policy work is safely stored in a Git stash:

`wip: queue integration and 30s power policy`

Do not apply this stash to `feature/espnow`.

The committed CC1101 checkpoint remains working and contains:

* raw CC1101 driver
* 433.92 MHz configuration
* bidirectional communication
* `Protocol::Message`
* EVENT messages
* ACKs
* timeout/retry logic
* duplicate detection
* peer availability handling
* dedicated FreeRTOS `RadioTask`

### Exact next step

Begin clean ESP-NOW bring-up on `feature/espnow`.

Known Wi-Fi MAC addresses:

* Bubu: `E8:F6:0A:12:4C:A4`
* Dudu: `E8:F6:0A:12:5B:84`

First milestone:

1. Create a minimal `ESPNowRadio` module.
2. Initialize ESP-NOW on both devices.
3. Configure each device with the other device as its peer.
4. Verify the correct local and peer MAC addresses.
5. Prove simple bidirectional ESP-NOW transmission.
6. Only after basic communication works, integrate the existing BubuDudu protocol concepts.

Do not mix CC1101 fallback, proximity, deep-sleep discovery, queues or full-system integration into the initial ESP-NOW bring-up.

Also revisit today's FreeRTOS queue work before eventually using it in the final architecture.

## 2026-09-18

### Completed

* Continued development on the clean `feature/espnow` branch.
* Created a dedicated ESP-NOW transport module:

  * `include/ESPNowRadio.h`
  * `src/ESPNowRadio.cpp`
* Configured both ESP32-C3 devices for ESP-NOW communication using:

  * Wi-Fi station mode
  * Wi-Fi channel 1
  * Fixed Bubu/Dudu peer MAC addresses
* Verified the device MAC addresses:

  * Bubu: `E8:F6:0A:12:4C:A4`
  * Dudu: `E8:F6:0A:12:5B:84`
* Successfully initialized ESP-NOW and registered the opposite device as a peer on both Bubu and Dudu.
* Initially observed repeated ESP-NOW transmission failures despite correct initialization, peer registration, and channel configuration.
* Reapplied the previously discovered ESP32-C3 Super Mini radio workaround:

  * Disabled Wi-Fi sleep.
  * Reduced Wi-Fi TX power to `8.5 dBm`.
* Confirmed that ESP-NOW communication became reliable after applying this configuration.
* Verified bidirectional communication:

  * Bubu → Dudu
  * Dudu → Bubu
* Verified simultaneous and overlapping bidirectional ESP-NOW traffic.
* Replaced temporary text packets with the existing packed `Protocol::Message`.
* Confirmed that the existing 8-byte BubuDudu application protocol can be transported correctly over ESP-NOW.
* Implemented and validated application-level reliability:

  * EVENT messages
  * Message IDs
  * ACK generation and matching
  * 300 ms ACK timeout
  * Maximum 2 retries
  * Retries reuse the original message ID
  * Duplicate EVENT detection
  * Duplicate EVENTs are not processed twice
  * Duplicate EVENTs still receive another ACK
  * Communication failure detection when the peer is unavailable
  * Automatic communication recovery when the peer returns
* Deliberately dropped an ACK during testing to force a retransmission.
* Confirmed that the receiver recognized the retransmitted EVENT as a duplicate, ignored the duplicate application event, and transmitted another ACK.
* Confirmed experimentally that an ESP-NOW send callback reporting `SUCCESS` does not guarantee that the BubuDudu application ACK was received.
* Observed a real case where the ESP-NOW send callback succeeded but the application ACK timed out.
* Confirmed that an unavailable peer causes:

  * ACK timeout
  * Retry 1/2
  * Retry 2/2
  * Failure after retries are exhausted
* Confirmed that communication resumes after the peer becomes available again.
* Updated CC1101 pin-map documentation to match the physically validated hardware.
* Confirmed the CC1101 SPI connections:

  * GPIO6 → SCK
  * GPIO7 → MOSI
  * GPIO20 → MISO
  * GPIO10 → CSN
* Documented that CC1101 GDO0/GDO2 are currently unused.
* Released GPIO4 from its original theoretical CC1101 GDO assignment for possible future use.

### Important Debugging Lesson

The ESP-NOW bring-up reinforced that a communication system can fail even when its higher-level software configuration appears correct.

ESP-NOW initialization succeeded, the correct peer MAC addresses were registered, and both boards were using the same Wi-Fi channel. Nevertheless, the actual transmissions initially failed.

The problem was related to the previously observed behavior of the ESP32-C3 Super Mini radio.

The working configuration was:

* Wi-Fi sleep disabled.
* Wi-Fi TX power reduced to `8.5 dBm`.

Successful initialization does not necessarily mean the complete physical communication path is functioning.

Another important result was proving that:

`ESP-NOW send callback SUCCESS != application message delivered successfully`

The ESP-NOW callback reports the result of the lower-level transmission attempt.

The BubuDudu application ACK is still necessary to confirm that the remote application received and processed the message.

This justifies keeping message IDs, application ACKs, timeout handling, retries, and duplicate detection above the radio transport.

### Architecture Decision

Both BubuDudu communication technologies have now been independently proven.

The intended long-term communication architecture is:

```text
Application
    |
    v
Shared communication / reliability layer
    |
    v
Protocol::Message
    |
    +-------------------+
    |                   |
    v                   v
ESPNowRadio         CC1101Radio
Primary             Secondary / fallback
```

ESP-NOW will normally be the primary communication method.

CC1101 will eventually act as a secondary or fallback radio when ESP-NOW communication cannot be confirmed.

The intended failover behavior is:

```text
Send EVENT over ESP-NOW
          |
          v
Wait for application ACK
          |
          v
    ACK received?
       /     \
     YES      NO
      |        |
      v        v
   Continue   Retry ESP-NOW
   ESP-NOW         |
                   v
             Retries exhausted
                   |
                   v
             Test CC1101 link
                   |
                   v
             CC1101 available
                   |
                   v
          Resend same logical EVENT
                   |
                   v
           Use CC1101 as fallback
```

This architecture remains a future integration target. The two radio implementations should remain independent until the battery and power subsystem is ready.



## 2026-09-19

### Completed

* Paused further ESP-NOW and CC1101 integration to focus on the BubuDudu battery and power subsystem.
* Established the proposed single-cell 18650 battery architecture using the existing TP4056-style charging/protection modules.
* Confirmed that the charger/protection module does not provide a regulated 5 V output.
* Began designing an analog battery indicator using the LM358P and resistor networks.
* Built and tested a battery-voltage sensing circuit in Falstad.
* Implemented a 100kΩ / 47kΩ voltage divider to scale VBAT into a suitable sensing voltage.
* Verified the voltage-divider behavior at different simulated battery voltages.
* Implemented two comparator stages with simulated 1.1 V and 1.2 V reference voltages.
* Implemented three battery indication states:

  * LOW: below approximately 3.44 V.
  * MEDIUM: approximately 3.44–3.75 V.
  * GOOD: above approximately 3.75 V.
* Implemented an active-LOW warning LED using the lower comparator output.
* Added NOT and AND logic to create mutually exclusive battery indications.
* Verified that exactly one of the three indicator LEDs illuminates at representative battery voltages.
* Experimented with positive feedback resistors to investigate comparator hysteresis.
* Decided to defer hysteresis and retain the simpler, working three-state indicator as the current simulation baseline.

### Current Working State

ESP-NOW remains validated with bidirectional messaging, application ACKs, 300 ms timeouts, limited retries, duplicate detection, and recovery after peer restart.

CC1101 remains independently validated with equivalent reliability behavior.

The battery indicator is functionally validated in an idealized Falstad simulation.

No physical battery-power circuit has been assembled yet.

### Files

* `DEVLOG.md` — documented the battery indicator simulation and design decisions.
* `simulations/battery_indicator_v1.txt` — Falstad circuit export for the working three-state battery indicator.

### Hardware Changes

None. All battery-indicator development was performed in simulation.

### Problems and Design Decisions

* Identified that the TP4056-style module's OUT voltage follows the battery voltage rather than providing regulated 5 V.
* Used independent reference sources for the initial comparator simulation.
* Identified that actual LM358P output limitations must be considered before physical implementation.
* Investigated hysteresis but deferred its final implementation.
* Preserved the working indicator design rather than introducing additional complexity before hardware validation.

### Known Issues

* The simulated circuit uses ideal reference voltages and idealized 0–5 V comparator outputs.
* NOT and AND gates are currently ideal simulation components.
* Actual voltage-reference generation has not been designed.
* The ESP32-C3 battery power path has not been electrically validated.
* Battery charging, undervoltage protection, current consumption, and runtime have not yet been measured.
* Hardware UVLO and MOSFET power gating remain future work.

### Next Step

Design and verify the actual single-cell 18650 power path for the ESP32-C3 Super Mini.

Determine the safe power-input requirements, account for the TP4056 protection-board output, and identify which existing components can be used without adding an unnecessary boost converter.

Once the power architecture is established, adapt the battery sensing and indication design to the real LM358P and available components.

Do not connect the battery to the ESP32 until the power path and polarity have been verified.

## 2026-09-20 to 2026-09-21

### Completed

* Continued experimenting with full-system BubuDudu integration.

* Attempted to combine several previously working subsystems into one integrated firmware state, including:

  * ESP-NOW communication
  * CC1101 communication
  * application ACKs and retries
  * WS2812B heartbeat output
  * OLED diagnostics
  * ADXL345 motion detection
  * inactivity handling
  * ESP32 deep sleep
  * remote wake behavior
  * peer availability
  * RSSI-based proximity behavior

* Tested coordinated sleep behavior so that Bubu and Dudu would not independently enter incompatible power states.

* Investigated using the CC1101 as a method for waking the remote ESP32 when the primary ESP-NOW radio is unavailable during deep sleep.

* Connected CC1101 GDO0 to ESP32-C3 GPIO4 for wake experimentation.

* Continued experimenting with RSSI-based distance estimation.

* Reduced the original NEAR / MEDIUM / FAR interpretation to a simpler CLOSE / FAR result because the measurements were not stable enough to justify finer classification.

* Tested several variations of sleep timers, wake behavior and radio state handling.

### Integration Failure and Development Lesson

The largest problem during these two days was attempting too much integration at once.

Several independently working subsystems were combined before the interfaces and responsibilities between them were sufficiently defined.

This created failures involving communication, sleep state, wake behavior and application state that became extremely difficult to isolate.

At several points it was no longer obvious whether a failure originated from:

* ESP-NOW

* CC1101

* ACK/retry state

* FreeRTOS task interaction

* deep-sleep entry

* CC1101 receive state

* GDO wake signaling

* ADXL345 inactivity behavior

* peer-state logic

* or interactions between several of them

A large amount of time was spent trying to reverse-engineer what had broken after making broad integration changes.

Some experimental versions occasionally appeared to work, while other tests with apparently similar conditions failed.

Eventually the experimental integration had moved far enough away from the clean repository checkpoints that continuing to patch it was creating more uncertainty rather than progress.

The decision was made to stop trying to repair the large integration experiment and return to the individually validated subsystem branches.

### Important Development Lesson

One of the most important lessons from the project so far is that there is no real shortcut around incremental embedded-system development.

Trying to save time by integrating many features at once ultimately cost significantly more time because failures could no longer be isolated.

The faster-looking approach became the slower approach.

The improved workflow is therefore:

```text
one change
    |
    v
build
    |
    v
flash
    |
    v
physical test
    |
    v
understand the result
    |
    v
commit
    |
    v
next change
```

A subsystem should not be considered ready for integration simply because it worked once in a larger prototype.

Each subsystem should first have a known working checkpoint, a clear responsibility and a reproducible hardware test.

This experience changed the development strategy for the remainder of BubuDudu.

### Current Working State

The large experimental integration is not considered the production baseline.

The validated subsystem branches remain the trusted source of working implementations.

Development will continue from these verified checkpoints rather than trying to recover the experimental full-system firmware.

The immediate priority is to strengthen the communication subsystems individually before returning to system integration.

### Next Step

Return to the clean ESP-NOW and CC1101 branches.

Audit the implementations, fix concrete weaknesses individually, verify every change on both physical devices and only then begin the final system-integration phase.

---

## 2026-09-22

### Completed

* Returned to incremental development using the individually validated communication branches.

* Audited the existing ESP-NOW implementation before making further system-integration changes.

* Identified that the ESP-NOW receive callback and the Arduino application flow could both access application protocol state.

* Added a FreeRTOS receive queue so that:

  * the Wi-Fi callback only copies incoming packets
  * packet processing happens outside the Wi-Fi callback
  * the main application flow owns ACK/retry state
  * protocol-state modification is serialized through one execution context

* Preserved the existing ESP-NOW application protocol:

  * 8-byte `Protocol::Message`
  * EVENT messages
  * application ACKs
  * 300 ms ACK timeout
  * maximum 2 retries
  * retry using the same message ID
  * duplicate detection
  * bidirectional communication

* Built both Bubu and Dudu successfully after the ESP-NOW queue change.

* Hardware-tested the modified ESP-NOW implementation.

* Verified:

  * Bubu → Dudu communication
  * Dudu → Bubu communication
  * ACK generation and matching
  * retry behavior
  * retry exhaustion
  * peer loss
  * communication recovery when the peer returns

### CC1101 Driver Improvements

* Returned to `feature/cc1101` before integrating the radio into the complete system.

* Audited the CC1101 driver and FreeRTOS `RadioTask`.

* Confirmed that `RadioTask` already owns CC1101 protocol state and therefore does not have the same callback concurrency problem as ESP-NOW.

* Added bounds checking before CC1101 RX FIFO reads.

* Prevented abnormal RX byte counts from being used as an unchecked write length into the local receive buffer.

* Preserved the existing packet format and normal receive behavior.

* Hardware-tested bidirectional CC1101 communication after the change.

* Verified:

  * EVENT transmission
  * ACK generation
  * ACK matching
  * two retries
  * retry exhaustion
  * peer offline detection
  * communication recovery after peer restart

### CC1101 Radio Recovery

* Improved CC1101 receive recovery.

* Changed packet reception so successful packet delivery and successful restoration of RX mode are reported separately.

* Added bounded radio recovery owned by `RadioTask`.

* Recovery now:

  * detects failed RX restarts
  * resets and reconfigures only the CC1101
  * preserves protocol message IDs and duplicate history
  * attempts recovery a maximum of three times
  * enters a persistent fault state if recovery repeatedly fails
  * avoids uncontrolled reset loops

* Verified the recovery behavior with host-side fault-injection tests.

* Verified both Bubu and Dudu builds with no compiler or linker errors.

* Confirmed through physical regression testing that normal CC1101 communication still works after the recovery changes.

### CC1101 Remote Wake

One of the most difficult unresolved problems from the previous integration attempts was waking a sleeping ESP32 from the CC1101.

The earlier experiments were inconsistent and difficult to debug because radio state, sleep state and wake behavior were all changing at the same time.

The problem was therefore rebuilt incrementally.

* Confirmed physical wake wiring:

```text
CC1101 GDO0 -> ESP32-C3 GPIO4
```

* Configured CC1101 `IOCFG0 = 0x07`.

* With this configuration:

  * GDO0 asserts HIGH after a CRC-valid packet is received
  * GDO0 remains asserted until the first RX FIFO byte is read

* First implemented a controlled ESP32 light-sleep test.

* Verified the complete light-sleep wake chain:

```text
CC1101 packet
    |
    v
GDO0 HIGH
    |
    v
GPIO4
    |
    v
ESP32 light-sleep wake
    |
    v
packet processing
    |
    v
application ACK
```

* Verified that the sleeping device woke through GPIO4 and successfully acknowledged the packet that triggered the wake.

### CC1101 Deep-Sleep Remote Wake

After light-sleep wake was proven, the experiment was extended to ESP32 deep sleep.

Deep sleep required a different architecture because the ESP32 reboots after wake.

A major concern was that normal startup initialization could reset or flush the CC1101 before the wake packet was read.

The deep-sleep startup path was therefore changed so that the external radio is inspected before normal radio initialization.

* Added deep-sleep wake using GPIO4.

* Added a small RTC-memory checkpoint for protocol state that must survive the ESP32 reboot.

* Preserved:

  * next message ID
  * last received peer message ID
  * duplicate-history state

* Prevented the wake path from immediately resetting or flushing the CC1101.

* Preserved CC1101 chip-select state across ESP32 deep sleep.

* On deep-sleep wake, firmware now records:

  * ESP32 wake cause
  * GPIO wake mask
  * GDO0 state at boot
  * CC1101 `MARCSTATE`
  * CC1101 `RXBYTES`
  * CC1101 `IOCFG0`

* The retained CC1101 packet is inspected and processed before normal recovery or reinitialization.

### Deep-Sleep Hardware Result

Initial deep-sleep tests were inconsistent and several tests ended through the 30-second timer instead of GPIO wake.

The experiment was repeated after rebuilding and flashing both devices again.

Successful hardware tests then demonstrated:

```text
DEEP WAKE: cause=GPIO
GPIO_mask=0x10
GDO0_at_boot=1
GDO0_before_FIFO=1
```

The CC1101 state before reading the FIFO showed:

```text
MARCSTATE=0x01
RXBYTES=0x09
IOCFG0=0x07
```

`RXBYTES=0x09` confirmed that the wake packet remained inside the CC1101 after the ESP32 entered deep sleep and rebooted:

```text
1 byte CC1101 packet-length field
+
8 byte BubuDudu Protocol::Message
=
9 bytes
```

The firmware then successfully reported:

```text
ELECTRICAL DEEP-WAKE SUCCESS=YES

DEEP PACKET:
copied=1
peer_event_processed=1
ack_tx=1
```

The transmitting device also received the returned application ACK.

The complete hardware chain was therefore demonstrated:

```text
433 MHz EVENT
      |
      v
CC1101 receives packet
      |
      v
GDO0 HIGH
      |
      v
ESP32-C3 GPIO4
      |
      v
deep-sleep wake
      |
      v
ESP32 reboot
      |
      v
CC1101 configuration + FIFO preserved
      |
      v
wake packet recovered
      |
      v
Protocol::Message processed
      |
      v
application ACK transmitted
      |
      v
sender receives matching ACK
```

The experiment was reproduced after rebuilding and reflashing the devices.

The earlier inconsistent results are not yet attributed to one confirmed cause and should therefore not be described as a solved hardware fault.

### Problems Solved

* ESP-NOW protocol-state concurrency risk

* ESP-NOW receive handling moved out of the Wi-Fi callback

* CC1101 RX FIFO bounds protection

* CC1101 failed-RX restart detection

* bounded CC1101 recovery

* persistent radio-fault handling

* CC1101 GDO0 configuration

* CC1101-triggered ESP32 light-sleep wake

* CC1101-triggered ESP32 deep-sleep wake

* preserving the wake packet across ESP32 deep-sleep reboot

* restoring minimum protocol state from RTC memory

* processing and acknowledging the packet responsible for waking the MCU

### Git Commits

* `d24d72c` — `fix: serialize ESP-NOW receive handling through FreeRTOS queue`

* `b4665df` — `fix: bound CC1101 RX FIFO reads`

* `653a7b8` — `fix: add bounded CC1101 RX recovery`

* `ece6879` — `feat: add CC1101 deep-sleep remote wake`

All committed ESP-NOW and CC1101 work was pushed to the corresponding remote feature branches.

### Current Working State

ESP-NOW now has a cleaner receive architecture with one application context owning protocol state.

CC1101 now has:

* bounded FIFO access
* explicit RX restart status
* bounded radio recovery
* persistent-fault handling
* application ACK/retry reliability
* validated light-sleep remote wake
* validated deep-sleep remote wake
* preserved wake-packet processing after ESP32 reboot

The communication subsystems are now in a substantially stronger state than during the earlier full-system integration attempt.

### Next Step

Begin final system integration from the validated feature branches.

Do not restore the previous large experimental integration as the baseline.

Integrate features incrementally, beginning from the now-hardened ESP-NOW and CC1101 communication implementations.

For every integration step:

1. make one understandable change
2. build Bubu and Dudu
3. test both physical devices
4. investigate failures before adding another subsystem
5. commit only after the new state is verified

---

## 2026-09-23

### Development Strategy

Continued incremental development from the individually validated ESP-NOW checkpoint on `feature/espnow`:

`d24d72c` — `fix: serialize ESP-NOW receive handling through FreeRTOS queue`

The previous large full-system integration experiment remains abandoned as the architecture baseline.

The working rule is now explicit:

```text
one understandable change
      |
      v
build
      |
      v
flash
      |
      v
physical test
      |
      v
understand result
      |
      v
commit
      |
      v
next change
```

Today's firmware work added power-state policy and then a bounded ESP-NOW sleep agreement. It did not connect that agreement to actual ESP32 sleep execution.

### Power FSM Foundation

Created `feature/power-fsm` and added a dedicated `PowerManager` module in `include/PowerManager.h` and `src/PowerManager.cpp`.

PowerManager owns semantic system power state. The ESP-NOW transport continues to own packet transmission, application ACK matching, retries and receive-queue processing.

Local states:

* `ACTIVE`
* `IDLE`
* `SLEEP_NEGOTIATING`
* `SLEEPING`
* `WAKING`

Peer semantic states:

* `ONLINE`
* `SLEEP_PENDING`
* `SLEEPING`
* `UNKNOWN`
* `OFFLINE`

The important distinction is:

```text
SLEEPING != OFFLINE
```

A peer that intentionally entered sleep must not be treated as a failed or unreachable peer solely because its ESP-NOW traffic becomes silent.

Added a bounded `SleepTransaction` containing an active flag, `sleepId`, coordinator/participant role, phase, start timestamp, phase deadline and overall hard deadline. PowerManager also manages failure cooldown and deterministic cancellation.

The FSM runs through state checks and timestamps rather than blocking wait loops. Negotiation and waking have explicit escape paths; transitional states must not remain active indefinitely.

At this checkpoint, `SLEEPING` and the subsequent wake transition were simulated. No `esp_deep_sleep_start()` call was added.

Physical tests on both Bubu and Dudu verified:

* `ACTIVE -> IDLE`

* `IDLE -> SLEEP_NEGOTIATING`

* hard timeout closes the transaction and returns to `IDLE`

* failure cooldown applies

* expiration of cooldown does not automatically restart negotiation

* activity immediately cancels negotiation and returns to `ACTIVE`

* simulated `SLEEPING -> WAKING -> ACTIVE`

* existing ESP-NOW communication remained operational throughout the initial FSM tests

The verified foundation was committed before starting the handshake checkpoint.

### Coordinated Sleep Handshake

Created `feature/sleep-handshake` from the committed Power FSM foundation.

Extended the existing `Protocol::Message` with sleep-control message types while preserving its eight-byte size:

* `SLEEP_REQUEST`
* `SLEEP_READY`
* `SLEEP_COMMIT`
* `SLEEP_ACK`
* `SLEEP_CANCEL`

The `SLEEP_REQUEST` message ID becomes the transaction's `sleepId`. Replies reference that transaction using the existing 16-bit `ackForMessageId` field. Reliable control packets still have their own packet message IDs for application ACK matching and retries.

The ordinary application/packet ACK and semantic `SLEEP_ACK` have different meanings:

* packet ACK confirms receipt of one packet

* `SLEEP_ACK` confirms that the participant accepted COMMIT and completed its side of the semantic sleep agreement

Normal handshake:

```text
SLEEP_REQUEST
      |
      v
SLEEP_READY
      |
      v
SLEEP_COMMIT
      |
      v
SLEEP_ACK
```

The participant completes when its semantic `SLEEP_ACK` is actually accepted for transmission by ESP-NOW. The coordinator completes when it receives the matching semantic `SLEEP_ACK`. Queueing that packet alone is not participant completion, and an ordinary packet ACK cannot substitute for it.

After a successful exchange, both PowerManagers enter simulated `SLEEPING`. Both ESP32 CPUs remain physically awake.

### Symmetric Operation and Simultaneous Requests

Physical tests verified both directions:

* Bubu initiates and Dudu becomes participant

* Dudu initiates and Bubu becomes participant

There is no permanent master/slave relationship.

When both devices initiate at approximately the same time and are still coordinators waiting for READY, the lower numeric `DeviceId` wins the collision. This is a temporary transaction tie-break only.

In the observed collision, Bubu started with `sleepId=40` and Dudu started with `sleepId=33`. Dudu correctly reported:

```text
peer DeviceId=1 wins; abandon 33, accept 40
```

Bubu kept its transaction. Dudu abandoned its own transaction, adopted Bubu's `sleepId` and became participant. Adopting the winning transaction preserves the losing device's original hard deadline; a collision cannot extend the negotiation indefinitely.

The initial physical collision exposed the final-send phase bug described below. After fixing it, the physical collision test verified one surviving handshake, both devices reaching simulated `SLEEPING`, no deadlock and no second surviving transaction.

Bubu winning this particular collision does not make it a permanent master. Dudu can still initiate a normal handshake.

### Participant Final-Send Phase Bug

The bench command `d` introduces a non-blocking one-second delay before newly queued reliable controls can be sent. That delay exposed a reproducible state-machine error on the physical boards:

* participant received a valid matching `SLEEP_COMMIT`

* COMMIT was accepted and `SLEEP_ACK` was queued

* participant remained in the old `WAIT_COMMIT` phase

* the old phase deadline expired before the queued `SLEEP_ACK` could be transmitted

* `PHASE_TIMEOUT` incorrectly cancelled the transaction and sent `SLEEP_CANCEL`

Receiving a valid COMMIT is forward progress. Waiting to submit the final ACK needs its own bounded phase rather than retaining the deadline for waiting to receive COMMIT.

Added explicit phase `WAIT_SLEEP_ACK_TX`:

```text
WAIT_COMMIT
    |
    v
valid SLEEP_COMMIT
    |
    v
WAIT_SLEEP_ACK_TX
    |
    v
SLEEP_ACK submitted
    |
    v
SLEEPING
```

Only the first valid COMMIT starts the new phase deadline. Duplicate COMMIT remains idempotent and does not refresh either deadline. Retries also leave both deadlines unchanged.

The overall transaction hard deadline is preserved. If the final ACK cannot be submitted before the phase or hard deadline, the transaction still closes deterministically. The hard deadline takes precedence when both limits have expired.

The fix did not increase timeout values or disable the one-second bench delay.

Focused host tests reproduced the delayed collision sequence, including delayed READY, delayed COMMIT and delayed final ACK. The modeled old `WAIT_COMMIT` deadline was 4100 ms while final ACK submission was scheduled at 4110 ms. These are host-test timestamps, not measured hardware timing.

The corrected final-send behavior passed host tests and was then verified by rerunning the physical Bubu/Dudu simultaneous-request collision.

### Missing-Peer Failure Path

Physical testing with the peer unavailable verified:

* one initial `SLEEP_REQUEST`

* maximum two retries, reusing the same message ID

* existing 300 ms application ACK timeout retained

* retry exhaustion closes the negotiation

* peer becomes `OFFLINE` and local state returns to `IDLE`

* failure cooldown applies

* negotiation does not automatically restart after cooldown

* no infinite peer-search or retry loop

This directly addresses a major failure mode from the earlier full-system integration attempt: an unavailable peer must not keep the device searching or negotiating forever.

### Activity Cancellation

Physical testing verified meaningful activity while the coordinator was negotiating:

* local transaction closed immediately

* local state returned to `ACTIVE`

* peer semantic state was restored appropriately rather than left `SLEEP_PENDING`

* a best-effort `SLEEP_CANCEL` was transmitted

* participant received the matching CANCEL, closed its transaction and returned to `IDLE`

* delayed sleep traffic did not subsequently make either device enter `SLEEPING` in this test

Cancellation removes obsolete queued controls. If the best-effort CANCEL is lost, the peer's own bounded deadlines still provide an escape path.

### Reliability and Stale Messages

Implemented and tested protections include:

* control processing checks the peer, transaction ID and applicable role/phase before advancing an active transaction

* stale controls cannot create or advance unrelated transactions; a delayed old COMMIT cannot create a new sleep transaction

* duplicate REQUEST and COMMIT are safe and do not reset the hard deadline

* an exact duplicate of a previously completed COMMIT can replay `SLEEP_ACK` without creating another transaction or state transition

* new application activity can cancel an active negotiation; duplicate EVENT reception retains its existing re-ACK behavior without executing the event again

* `h` pauses automatic heartbeat generation for deterministic bench tests while pending transmissions, ACKs and retries continue

* automatic application events are suppressed during negotiation, simulated sleep and waking

* Wi-Fi receive callbacks continue to copy packets into the existing FreeRTOS RX queue; the main application context processes protocol and PowerManager state

Deterministic stale/duplicate edge cases were covered primarily through host tests. The main coordinator, participant, simultaneous-collision, missing-peer and activity-cancellation flows were verified on physical Bubu/Dudu hardware.

### Host Tests and Build Verification

Added focused tests in `tests/host/sleep_handshake_test.cpp`, run by `tests/host/run.sh` on `feature/sleep-handshake`.

The host harness exercises the production PowerManager and application code with substitutes for Arduino timing, ESP-NOW and the FreeRTOS queue. It builds for both Bubu and Dudu identities with compiler warnings treated as errors and address/undefined-behavior sanitizers.

Coverage includes:

* coordinator and participant completion

* simultaneous requests, including equal numeric transaction IDs

* stale controls and duplicate REQUEST, READY and COMMIT

* activity cancellation and removal of obsolete queued controls

* bounded phase and hard timeouts

* delayed collision through `WAIT_SLEEP_ACK_TX`

* duplicate COMMIT and retries without deadline extension

* phase timeout, hard timeout and cancellation while final `SLEEP_ACK` remains unsent

* `millis()` rollover and wrap-safe deadline comparisons

* existing EVENT/ACK matching, duplicate handling and bounded same-ID retry behavior

* receive-queue ownership and heartbeat gating

The focused host tests passed for both device identities. Both PlatformIO environments, `bubu` and `dudu`, built successfully, and `git diff --check` passed during firmware verification.

Host-test results establish deterministic software behavior under the modeled conditions; they are separate from the physical validation recorded above.

### Current Working State

The verified sleep-handshake architecture is:

```text
PowerManager
    |
    +-- local power state
    +-- peer semantic state
    +-- bounded sleep transaction
    +-- timeout / cancellation policy
    |
    v
ESP-NOW transport
    |
    v
coordinated sleep handshake
```

Both devices can negotiate the decision to sleep in the verified scenarios while remaining physically awake. Actual ESP32 deep-sleep execution is deliberately not connected yet.

CC1101, ADXL345, OLED, LED and proximity were not integrated into this checkpoint. The independently verified CC1101 implementation remains separate, and the old large integration remains abandoned as the baseline.

Successful tested exchanges do not guarantee agreement under permanent packet loss. In particular, accepting final `SLEEP_ACK` for transmission does not prove its delivery: a participant can reach simulated `SLEEPING` while the coordinator times out and records uncertainty. Real sleep execution must account for that boundary before it is connected.

The verified implementation is committed on `feature/sleep-handshake`; it has not been merged into `main` by this documentation update.

### Important Development Reflection

The earlier large integration attempt was a development mistake. Changing too many subsystems simultaneously made failures difficult to isolate and debugging unnecessarily costly.

It was nevertheless useful evidence. Failures in the combined system exposed requirements that were less obvious in independent subsystem tests:

* distinguishing a sleeping peer from an offline peer

* bounding peer searching and retries

* resolving simultaneous sleep requests

* handling wake/sleep state disagreement

* separating ownership of system state from radio state

* defining explicit transaction deadlines

* rejecting stale or delayed control messages

* considering failure paths before integrating more hardware

Development now asks what can happen one or two transitions after an apparently successful operation, and what happens when communication is lost, delayed, duplicated or simultaneous.

The experience was both costly and useful: the implementation approach was wrong, but the failures exposed concrete requirements that are making the incremental architecture stronger.

### Problems Solved

* bounded PowerManager FSM

* sleeping-peer versus offline-peer distinction

* coordinated ESP-NOW sleep negotiation

* symmetric coordinator/participant operation

* simultaneous-request collision resolution

* premature participant phase timeout before `SLEEP_ACK` transmission

* bounded missing-peer behavior

* prevention of automatic endless negotiation restart

* activity-driven sleep cancellation

* stale/duplicate sleep-control handling

* preserving existing ESP-NOW reliability behavior during the new power protocol

### Git Commits

Today's firmware checkpoints:

* `e3b849f` — `feat: add bounded power state machine foundation`

* `baea754` — `feat: add bounded coordinated sleep handshake`

The handshake commit includes the `WAIT_SLEEP_ACK_TX` fix and focused host tests; that work is committed, not awaiting a final firmware commit.

Also committed today:

* `49e1e85` — `docs: document integration lessons and radio hardening`

That documentation commit records the earlier integration lessons and radio-hardening work. The new entry for today is a separate documentation-only update.

### Next Step

Connect the verified coordinated sleep decision to actual sleep execution incrementally.

First preserve the handshake checkpoint, inspect the independently verified CC1101 deep-wake implementation, and define the smallest interface between PowerManager policy and wake/sleep execution.

Subsequent checkpoints should separately:

* arm CC1101 remote wake using confirmed `GDO0 -> ESP32-C3 GPIO4` wiring

* reconnect ADXL345 local motion wake through GPIO3

* verify wake-source setup and reboot handling, including preservation of a pending CC1101 wake packet

* only then connect actual ESP32 deep-sleep entry

These are separate changes and physical-test checkpoints, not one large integration task. Continue one understandable change, build, flash, physical test, understand, commit, then the next change.
