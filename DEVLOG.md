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

---

## 2026-09-24

### Development Strategy

Continued on `feature/sleep-execution` from the verified coordinated sleep-handshake checkpoint:

`baea754` — `feat: add bounded coordinated sleep handshake`

Today's work connected the semantic sleep agreement to real ESP32-C3 deep sleep through separately built and physically tested checkpoints. The previous large full-system integration was not restored, and the independently verified CC1101 implementation was used as reference material rather than merged as a complete radio architecture.

The sequence remained one understandable change, build, flash, physical test, understand the result, commit, then the next change.

### One-Shot Sleep Execution Handoff

Added `PowerManager::SleepDecision`, containing the completed `sleepId` and coordinator/participant role, and `takeSleepDecision()` as a one-time handoff to the application.

PowerManager decides that semantic sleep agreement has completed. It does not perform hardware sleep or depend on ESP32, CC1101 or RTC APIs.

The initial checkpoint only printed `SLEEP EXECUTION READY` after the application ACK/retry transaction and sleep-control queue drained. CPU and radios remained awake while that interface was verified.

Duplicate or stale controls do not create another execution decision. Activity revokes an unconsumed decision, and cancelled or failed negotiations do not produce one.

The participant's verified completion sequence remained:

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
semantic SLEEPING
```

That semantic transition can precede the ordinary packet ACK for `SLEEP_ACK`. Physical sleep therefore needs a separate transport-drain gate. The initial application-level gate was strengthened later in the day to include callback completion.

### CC1101 Sleep-Arm Readiness

Added the small `CC1101SleepArm` layer to the current firmware. It initializes the proven 433.92 MHz packet configuration on cold boot and provides a read-only pre-sleep readiness snapshot.

The readiness inspection checks:

* CC1101 identity and response

* expected packet configuration, including `IOCFG0 = 0x07`

* radio in RX

* empty RX FIFO, without an overflow indication

* GDO0/GPIO4 LOW

* bounded SPI readiness and radio polling

The inspection does not reset the radio, flush the FIFO, consume a packet or restart RX. Calibration-updated registers are not incorrectly compared against their initial seed values.

Physical tests passed with Bubu initiating, Dudu initiating and simultaneous sleep requests. Each successful, drained sleep decision produced exactly one arm inspection. Activity cancellation and missing-peer failure did not run the arm step.

This checkpoint still left the ESP32 awake; it established that wake hardware was ready before connecting physical sleep.

### RTC History Checkpoint

Added `RtcState` with a validated 16-byte RTC-memory checkpoint.

The retained fields are:

* `nextMessageId`

* last peer EVENT message ID and its validity flag

* newest peer sleep REQUEST watermark and its validity flag

* magic, version and checksum metadata

The design rule is:

**RTC retains history, not work in progress.**

The checkpoint does not retain an active sleep transaction, coordinator/participant phase, phase or hard deadline, cooldown timestamp, ACK/retry state, pending packet, control queue, RX/TX queue state, `SleepDecision`, or previous local/peer runtime power states.

History is needed to avoid reusing an old allocator snapshot, executing a duplicate EVENT again, or accepting an already handled sleep REQUEST after reboot. Live execution must start fresh.

The first RTC checkpoint established storage and restart tests without immediately connecting physical sleep. The later deep-wake checkpoint connected save/load to real sleep boundaries: cold boot invalidates old history; deep-wake startup restores validated history and consumes the snapshot.

Host tests covered invalid checkpoints, replacement, checksum/version validation, validity flags, message-ID rollover, EVENT duplicate history, stale REQUEST rejection and absence of live FSM/transport work after simulated restart.

### Retained CC1101 Deep-Wake Recovery

Added `CC1101WakeRecovery` and manual bench command `x`, initially separate from coordinated sleep execution.

Cold boot still permits normal CC1101 reset and configuration. Deep wake follows a different startup order because resetting or flushing the external radio would destroy the packet that woke the ESP32.

The deep-wake path now:

1. captures ESP32 reset/wake evidence and GDO0 level before normal initialization
2. initializes fresh PowerManager state and restores RTC history
3. releases the retained CS hold
4. attaches MCU SPI and pins without resetting CC1101
5. inspects retained `MARCSTATE`, `RXBYTES`, `IOCFG0` and GDO0
6. copies a valid retained packet before any RX recovery
7. processes the heartbeat EVENT through the existing handler and restored duplicate history
8. transmits its CC1101 ACK before restarting RX
9. makes a bounded RX restart attempt and continues normal ESP-NOW runtime

Packet processing and ACK evidence remain separate from whether RX can be restored. The retained packet is not discarded merely because a later restart fails.

The first physical proof used current Bubu firmware and the older proven `ece6879` wake sender on Dudu. Bubu entered deep sleep through `x`, received the RF wake packet and reported:

```text
GPIO wake mask=0x10
RTC RESTORE=OK
GDO0 at boot=HIGH
MARCSTATE=0x01
RXBYTES=0x09
IOCFG0=0x07
```

The nine FIFO bytes were one CC1101 length byte plus the existing eight-byte `Protocol::Message`. The packet survived the ESP32 reboot, the EVENT was processed once, the wake ACK was transmitted and RX returned ready. Dudu independently received the matching ACK.

The complete verified chain was:

```text
CC1101 EVENT -> GDO0 HIGH -> GPIO4 deep wake -> ESP32 reboot
    -> RTC history restored -> retained FIFO recovered
    -> EVENT processed -> CC1101 ACK -> sender matches ACK
```

The 30-second bench timer fallback was also physically verified:

* TIMER wake and RTC restore OK

* `EMPTY_FIFO`

* no false EVENT delivery

* no wake ACK transmitted

* radio ready in RX

Timer wake is not counted as CC1101 GPIO wake.

### Current-Firmware CC1101 Peer Wake Transmitter

Added `CC1101WakeTx` and manual serial command `w` so the current firmware could wake its peer without needing the older CC1101 test firmware.

This is a narrow awake-board bench sender, not a general CC1101 transport, fallback selector or new `RadioTask`.

The sender preserves:

* the existing eight-byte heartbeat EVENT

* one ID allocation through the application's existing `nextMessageId`

* the same message ID and payload on retries

* 300 ms ACK timeout and maximum two retries

* ACK version, sender, type and `ackForMessageId` validation

* bounded TX, RX and SPI waits

* one RX recovery budget across the command

* no CC1101 reset or `SRES`

* refusal when existing retained FIFO data makes transmission unsafe

The TX sequence reuses the proven IDLE/TX FIFO preparation, `PATABLE = 0x60`, length-prefixed packet write, STX, bounded completion wait and return to RX. ACK success remains distinguishable from final RX readiness.

Physical tests passed with current firmware on both boards:

* Bubu `x` -> Dudu `w` -> Bubu GPIO4 wake -> ACK returned to Dudu

* Dudu `x` -> Bubu `w` -> Dudu GPIO4 wake -> ACK returned to Bubu

In both directions, RTC restoration succeeded, the retained nine-byte FIFO packet was recovered, the EVENT was processed exactly once and the CC1101 ACK matched at the sender. Normal ESP-NOW runtime resumed afterward.

The sender remains manual. General CC1101 reception while the application is already awake was not added; sender retries alone therefore do not guarantee a second ACK if the initial boot-time wake ACK is lost.

### Callback-Level Transport Drain

Physical log ordering exposed a remaining gap before connecting real sleep: `SLEEP EXECUTION READY` could appear before the final ESP-NOW `TX CALLBACK ... SUCCESS`.

Checking only `waitingForAck == false` and `controlCount == 0` did not prove that a fire-and-forget packet ACK had finished transmission. In particular, the coordinator could otherwise sleep before its ordinary receipt ACK for the participant's `SLEEP_ACK` had completed.

Added atomic ESP-NOW TX-in-flight and active-RX-callback tracking without replacing the transport architecture.

A TX reservation is incremented before `esp_now_send()` because the Wi-Fi task can invoke its callback before that call returns. Rejected sends roll back the reservation. Accepted sends release it exactly once at the end of their callback, after logging; both SUCCESS and FAILED callbacks count as completed. Callback completion is not itself proof of application-level delivery.

The shared physical-sleep drain check now requires:

* runtime initialized

* no pending application ACK/retry transaction

* no queued sleep controls

* no active sleep transaction

* zero outstanding ESP-NOW TX callbacks

* no active ESP-NOW RX callback

* an existing, empty receive queue

`SleepDecision` is not consumed until that gate passes. A stalled final drain has a separate bounded three-second timeout; it cannot leave the awake device waiting indefinitely in semantic `SLEEPING`.

The participant also remains awake if retries for the ordinary receipt of its final `SLEEP_ACK` exhaust. A rejected receipt-ACK send after semantic completion aborts local execution rather than allowing an empty callback count to imply successful transport.

The handshake sequence, DeviceId arbitration, packet format, 300 ms ACK timeout and maximum two retries remain unchanged.

### Shared Physical Sleep Entry

Connected coordinated sleep execution to the same proven physical-entry primitive used by manual `x`.

Both coordinator and participant use one application execution path:

```text
semantic handshake complete
    -> transport fully drained
    -> SleepDecision consumed once
    -> CC1101 arm READY
    -> shared physical sleep entry
```

The entry primitive performs a read-only CC1101 readiness check, configures GPIO4 HIGH wake and the 30-second integration safety timer, drives CS HIGH, and enables GPIO/deep-sleep hold.

Transport is rechecked after arm inspection and again after wake-source setup and Serial logging. RTC history is saved at the final possible boundary, followed by another transport check and the final GDO0 LOW check immediately before `esp_deep_sleep_start()`.

No CC1101 reset or destructive FIFO operation is used to prepare sleep.

An arm refusal, newly busy transport, wake-source setup failure, final GDO HIGH or unexpected deep-sleep return aborts entry. Cleanup invalidates the RTC snapshot, releases CS hold, disables configured wake sources and leaves the CPU awake, with the failure reason logged.

Added the hardware-independent `PowerManager::notifySleepExecutionFailed()` notification. After semantic completion, failure returns local state to `IDLE` with a three-second cooldown, keeps the transaction inactive, discards execution/replay authority and does not recreate a `SleepDecision`.

Peer state is preserved because the peer may already be asleep. Local entry failure does not mark it `OFFLINE`, start another handshake or enter an automatic retry loop.

### Coordinated Real-Sleep Hardware Validation

Physical tests were performed after the final execution checkpoint was built and flashed to both boards.

Bubu-initiated sleep passed:

* `SLEEP_REQUEST -> SLEEP_READY -> SLEEP_COMMIT -> SLEEP_ACK`

* outstanding transport callbacks drained

* both devices reported sleep execution ready and CC1101 arm ready

* both entered real deep sleep exactly once

* both USB serial monitors disconnected

* both woke through the timer after approximately 30 seconds

* RTC restoration succeeded and retained RX FIFOs were empty

* both returned to fresh `ACTIVE` runtime

* normal ESP-NOW communication recovered automatically

Dudu-initiated sleep passed the same complete sequence with coordinator and participant reversed.

Simultaneous requests passed with the non-blocking one-second control delay enabled:

* both devices began their own coordinator transaction

* deterministic DeviceId arbitration selected Bubu's transaction

* Dudu abandoned its local transaction and became participant

* both converged on the same sleep ID without extending the hard deadline

* transport drained on both sides

* both entered physical sleep exactly once

* both timer-woke and recovered normal communication

Activity cancellation passed:

* activity cancelled the active sleep transaction

* a best-effort `SLEEP_CANCEL` was transmitted

* both devices remained awake

* no sleep execution began

Missing-peer testing passed:

* one initial REQUEST plus maximum two retries used the same message ID

* retry exhaustion marked the peer `OFFLINE` and returned local state to `IDLE`

* no sleep decision was executed and no physical sleep occurred

* no endless search, retry or automatic re-negotiation loop followed

### Host Tests and Build Verification

The focused host suites passed for both Bubu and Dudu identities, including the existing EVENT/ACK, sleep-handshake and collision regressions.

Coverage added through today's checkpoints includes:

* one-shot decisions, duplicate/stale suppression and transport drain

* CC1101 identity/configuration checks, FIFO/GDO preservation and bounded arm waits

* RTC validity, checksum/version checks, replacement, ID rollover and history-only restart

* retained FIFO recovery, malformed packets, wake ACK/RX failures and timer `EMPTY_FIFO`

* current-firmware wake transmission, ACK validation, same-ID retries, bounded cutoff and one recovery budget

* actual ESP-NOW wrapper accounting for early, delayed, failed and rejected sends

* coordinator and participant callback gates and exactly one physical-entry attempt

* cancellation, missing peer and delayed simultaneous-request convergence

* traffic arriving during arm/setup, final GDO HIGH, setup failure and unexpected deep-sleep return

* final-boundary RTC save and abort cleanup

* fresh transaction, queue, retry, deadline and execution state after timer restart

* unchanged manual `x` and `w` operation

The host harness uses compiler warnings as errors and address/undefined-behavior sanitizers. Both PlatformIO environments built successfully without warnings or errors, and `git diff --check` passed during firmware verification.

These deterministic software checks are separate from the physical board results recorded above. Hardware validation established the actual GPIO wake, packet retention, returned ACK, coordinated deep sleep and timer recovery behavior in the tested scenarios.

### Current Working State

Both devices now use the same current firmware architecture on `feature/sleep-execution`.

The verified system includes:

* reliable bidirectional ESP-NOW EVENT/ACK communication

* bounded sleep negotiation with deterministic simultaneous-request resolution

* bounded activity cancellation and missing-peer behavior

* RTC history retained across deep sleep

* CC1101 armed for GPIO4 deep wake

* retained CC1101 wake-packet recovery and EVENT/ACK operation in both directions

* coordinated handshake connected to real deep sleep on both devices

* fresh ACTIVE runtime and automatic ESP-NOW recovery after timer wake

The 30-second timer remains an integration safety net, not final product policy. Manual `x` and `w` remain bench/debug commands. ADXL345 motion wake is not yet integrated into this coordinated sleep path, and the production power policy is not finished.

The successful physical exchanges do not establish agreement under every RF-loss condition. A completed callback does not guarantee a peer processed its packet, and one device may already be asleep when the other aborts. Bounded failure handling and the integration timer keep those cases observable without automatic retry loops.

Today's firmware checkpoints are committed and pushed on `feature/sleep-execution`. This DEVLOG update is maintained separately on `main`; no feature source or tests are merged or cherry-picked into `main`.

### Important Development Reflection

Semantic `SLEEPING` is a policy decision, not permission to immediately suspend the CPU. Required packet work, including ESP-NOW callbacks, is part of the physical sleep-entry contract.

Deep-wake startup order is equally important: normal CC1101 initialization would erase the retained packet before software could identify and acknowledge the wake event. RTC restoration must recover history and freshness without reviving stale runtime execution.

Hardware sleep failure needs an explicit bounded path back to an awake semantic state. Leaving an awake CPU permanently labeled `SLEEPING` would hide a failed transition rather than recover from it.

Today's work continues the lesson from the abandoned large integration attempt. Separately proving the decision handoff, arm check, RTC history, retained wake, peer transmitter and final execution gate made failures diagnosable and preserved each last working checkpoint before the next subsystem was connected.

### Problems Solved

* one completed handshake producing at most one physical execution attempt

* non-destructive CC1101 pre-sleep readiness inspection

* preserving protocol history across deep-sleep reboot without restoring unfinished work

* recovering and acknowledging a retained CC1101 wake packet before normal runtime

* waking either board using the current firmware on its peer

* preventing deep sleep while required ESP-NOW callbacks remain outstanding

* sharing manual and coordinated physical sleep entry

* explicit IDLE/cooldown recovery after local execution failure

* coordinated real sleep, timer wake and subsequent ESP-NOW recovery on both boards

### Git Commits

Today's firmware checkpoints, in implementation order:

* `b0554a8` — `feat: add one-shot sleep execution handoff`

* `2f5d71d` — `feat: validate CC1101 sleep-arm readiness`

* `4ddb5d8` — `feat: add RTC sleep history checkpoint`

* `9b2cc12` — `feat: add retained CC1101 deep-wake recovery`

* `78a00c7` — `feat: add bounded CC1101 peer wake transmission`

* `d62f0fc` — `feat: connect coordinated sleep to deep sleep`

All six checkpoints are committed on `feature/sleep-execution` and were confirmed present on its matching remote branch before this documentation update.

### Next Step

Integrate the ADXL345 motion interrupt into the proven coordinated deep-sleep/wake architecture, incrementally, without destabilizing CC1101 peer wake or the sleep handshake.

Keep the integration timer while validating that next checkpoint. Preserve the current working state and continue one understandable change, build, flash, physical test, understand, commit, then the next change.

---

## 2026-09-25

### Development Strategy

Continued from the verified `feature/sleep-execution` architecture, starting with `d62f0fc` — `feat: connect coordinated sleep to deep sleep`.

The goal was to reconnect ADXL345 motion without destabilizing the retained CC1101 wake-packet path. Work remained incremental: one understandable change, build, flash, physical test, understand the result, commit, then the next change.

Historical subsystem branches supplied hardware knowledge and earlier experiments. They were not merged wholesale, and the abandoned large integration was not restored. Motion initialization, awake CC1101 retry handling, local motion deep wake and motion-triggered peer wake were separately verified checkpoints.

This entry covers the continuous September 25 session, including the final Checkpoint 3 validation and commit completed after midnight on September 26.

### Lost CC1101 Wake ACK and Awake Retry Handling

The retained wake path exposed a reliability gap after an otherwise successful wake:

```text
sleeping receiver accepts wake EVENT N
    -> ESP32 wakes and processes EVENT
    -> receiver sends ACK, but ACK is lost
    -> sender retries the same EVENT N
    -> receiver is now awake
    -> earlier firmware does not service that retry
    -> sender can exhaust retries despite successful peer wake
```

The sender's retry logic was already bounded and reused the same ID. The missing behavior was on the awakened receiver.

Added narrow awake CC1101 service for retries of the EVENT accepted during boot. The receiver retains that EVENT's receipt, validates the incoming packet and peer, and matches the accepted wake ID. A matching retry retransmits the retained ACK without calling the application EVENT handler again, then restores RX.

This receipt remains valid even if subsequent ESP-NOW traffic advances the application's last-EVENT history. A new or malformed awake CC1101 EVENT does not gain permission to execute application behavior through this path. It is not a general awake CC1101 transport or fallback mechanism.

ACK completion is polled from the existing loop with bounded timing. Existing RX recovery remains bounded, and failed RX restart or unavailable radio state stops service instead of creating an automatic recovery loop. Physical sleep and competing manual wake transmission are blocked while the re-ACK is in progress.

Temporary deterministic first-ACK suppression proved this recovery path on both boards. Representative sender evidence included:

```text
CC1101 WAKE TX | id=69 | attempt=0
CC1101 WAKE TX | id=69 | attempt=1
CC1101 WAKE ACK | id=69 | OK | RX_READY=1
```

The awakened receiver also reported:

```text
CC1101 AWAKE RETRY | id=69 | re-ACK started
```

Both Bubu-to-Dudu and Dudu-to-Bubu tests passed. The temporary ACK-drop flag, RAM-only suppression state, injection logging and injection-only tests were removed before committing. Permanent lost-ACK, duplicate, no-second-delivery, RX recovery and sender retry tests remain.

The permanent fix is `ed1d227` — `fix: re-ack CC1101 wake retries while awake`.

### ADXL345 Initialization Checkpoint

Integrated the existing Motion module into the current firmware without changing sleep policy first.

The confirmed wiring is SDA on GPIO0, SCL on GPIO1 and ADXL345 INT1 on GPIO3. The driver uses I2C address `0x53` and checks DEVID against `0xE5`.

Motion already provides activity/inactivity configuration, an awake ISR, startup-event inspection, interrupt pause/resume, and reading/clearing `INT_SOURCE`. The new integration calls `Motion::begin()` once and reports `MOTION INIT`, the INT1 level and the startup event. A failed DEVID check is reported while the rest of startup continues.

The ordering rule remained explicit:

```text
CC1101 cold initialization OR retained deep-wake recovery
    -> Motion/I2C initialization
    -> normal ESP-NOW runtime
```

On deep wake, the external radio may still hold the packet that caused the reboot. Sensor initialization must not move ahead of that inspection, EVENT processing and ACK. No normal radio reset or FIFO flush was introduced to make Motion startup convenient.

An intermittent Dudu I2C failure was investigated before adding more behavior. The same sensor worked on Bubu, and Dudu's GPIO0/GPIO1 showed I2C-like traffic. A temporary bounded address scan later found the OLED at `0x3C` and ADXL345 at `0x53`; `MOTION INIT | OK (DEVID=0xE5)` also returned.

That evidence pointed toward breadboard, jumper, contact or supply intermittency rather than a reproducible firmware architecture defect. The exact intermittent connection was not isolated by software. No arbitrary firmware delays or retry machinery were added to hide it. The scanner and scanner-only host support were removed after the diagnostic completed.

Initialization and continued runtime were physically verified on both boards. The permanent checkpoint is `89014ad` — `feat: integrate ADXL345 motion initialization`. It did not yet enable GPIO3 deep wake, inactivity-driven sleep or peer wake from motion.

### ADXL345 Deep-Sleep Wake — Checkpoint 2

Created `feature/adxl345-motion-wake` from the verified integration branch. The older `feature/adxl345` commits were read for the previously proven GPIO3 wake API, active-HIGH interrupt behavior and interrupt clearing. Their old startup flow and automatic inactivity-sleep policy were not restored.

The ESP32-C3 now arms both GPIO sources in one `esp_deep_sleep_enable_gpio_wakeup()` call using HIGH-level wake:

* GPIO4 / CC1101 GDO0: mask `0x10`

* GPIO3 / ADXL345 INT1: mask `0x08`

* combined GPIO mask: `0x18`

The 30-second timer remains enabled as an integration safety fallback.

`BootInfo` captures the wake cause, actual GPIO wake mask and both input levels early. Diagnostics distinguish `CC1101`, `MOTION`, `CC1101+MOTION` and `TIMER`. GPIO wake alone is no longer interpreted as proof that CC1101 caused the wake.

Retained CC1101 inspection still happens on every deep wake. An empty healthy RX FIFO is normal for motion or timer wake; it does not require a fabricated radio EVENT or ACK. Conversely, an RF packet arriving near a motion wake is not reset away merely because GPIO4 was absent from the captured mask. If both bits are set, both sources are reported and the retained radio path is still serviced before Motion initialization.

### Sleep-Specific Motion Preparation

The normal awake sensor setup enables linked activity/inactivity detection. Simply adding GPIO3 to the wake mask would also allow inactivity on INT1 to wake the ESP32. Linked detection also expects interrupt servicing that the sleeping CPU cannot perform.

Added a small sleep-preparation helper inside Motion. It pauses the awake ISR, disables sensor interrupts, moves through standby to clear LINK, clears the latched `INT_SOURCE`, then enables an activity-only measurement configuration:

```text
POWER_CTL = 0x08
INT_ENABLE = 0x10
INT_MAP = 0x00
```

Important state is checked through I2C readback, including interrupt enable, mapping, power mode and the active-HIGH data format. GPIO3 must be LOW after clearing the old source, and it is checked again at the physical sleep-entry boundaries. A newly asserted or stuck-HIGH input aborts entry rather than blindly entering an immediate wake cycle.

The sensor remains powered and measuring while the ESP32 sleeps. Its activity interrupt remains latched until the wake startup reads the source. The existing threshold is unchanged: `THRESH_ACT = 48`, approximately 3 g, with full-resolution +/-4 g operation. This relatively insensitive sleep profile is intentional: deliberate pickup, tap or movement should wake the device rather than minor desk vibration. It is a tested starting point, not a guarantee of vibration rejection in every enclosure or mounting arrangement.

On any returned/aborted entry, Motion attempts to restore the normal awake `POWER_CTL = 0x28`, `INT_ENABLE = 0x18` and `INT_MAP = 0x00`, then resumes its ISR. I2C work is bounded with no retry loop; restoration failure is logged. The existing coordinated execution-failure path remains responsible for semantic state and cooldown. Motion does not take ownership of PowerManager.

A more sensitive awake threshold was not implemented in this checkpoint; the current awake configuration still uses the existing threshold.

### Checkpoint 2 Physical Validation and Integration

CC1101 deep wake remained working in both directions. `source=CC1101` was followed by RTC restoration, retained FIFO recovery, EVENT processing, ACK transmission and RX ready.

Local motion wake passed on Bubu and Dudu. Representative evidence was `source=MOTION` and `GPIO3_BOOT=1`, with RTC restore OK, no retained CC1101 packet, no false EVENT processing, no false wake ACK and the radio ready in RX. At this checkpoint, local motion woke only the local board.

Timer wake also passed: TIMER was reported without inventing a radio EVENT or wake ACK. All three individual wake routes were physically demonstrated. Combined GPIO3/GPIO4 reporting and routing were covered by host tests; the individual-source bench results are not a claim that a simultaneous two-pin hardware stimulus was reproduced.

After physical validation, Checkpoint 2 was committed and pushed:

`4fe8171dd29f409aeaa19a00fe7eff2039f078a6` — `feat: wake from ADXL345 motion during deep sleep`

It was explicitly merged back into `feature/sleep-execution`:

`8ce8f23a61072959d802d1fbfbfc961502284bf7` — `merge: integrate ADXL345 motion wake`

The merge tree matched the verified feature tree. Full host tests, both PlatformIO builds and `git diff --check` passed during checkpoint verification and after integration. The historical `feature/adxl345` branch was not merged wholesale.

### Motion Wake Wakes the Peer — Checkpoint 3

Connected pure local motion wake to the existing bounded CC1101 peer-wake transmitter:

```text
both devices sleeping
    -> Bubu moves; GPIO3 wakes Bubu
    -> retained radio inspection and safe startup complete
    -> Bubu starts one bounded CC1101 wake transaction
    -> GPIO4 wakes Dudu
    -> Dudu recovers EVENT, processes it and returns ACK
    -> both devices ACTIVE
```

The reverse Dudu-to-Bubu sequence uses the same code.

Extracted `requestPeerWake()` from the proven manual `w` command. Both callers use the same runtime/transport guards, one EVENT allocation and `CC1101WakeTx::send()`. The existing eight-byte heartbeat EVENT, ACK matching, 300 ms ACK timeout, maximum two retries, same-ID retransmission and RX restoration are unchanged. This is reuse of the existing wake mechanism, not a new radio protocol.

The automatic policy uses captured wake evidence:

* GPIO3 present and GPIO4 absent: request peer wake once

* GPIO4 only: no return wake

* GPIO3 and GPIO4 together: no additional wake

* timer or cold boot: no peer wake

This prevents the radio-woken peer from automatically waking the sender back. The trigger runs once at the end of successful `setup()`, after retained CC1101 recovery, Motion initialization and normal ESP-NOW runtime setup. It is not polled from `loop()` and needs no persistent pending flag.

A runtime guard refusal or failed bounded transaction does not rearm the automatic request. Incomplete startup does not send. The wake EVENT retains its established application meaning at the receiver; local motion does not separately execute a local heartbeat action, and retries do not bypass the accepted-wake duplicate/re-ACK path.

### Checkpoint 3 Physical Validation

Two-board tests passed in both directions:

* Bubu motion wake -> one CC1101 transaction -> Dudu GPIO4 wake and ACK

* Dudu motion wake -> one CC1101 transaction -> Bubu GPIO4 wake and ACK

The motion side reported `source=MOTION`, followed by `MOTION PEER WAKE | one-shot request`, `CC1101 WAKE TX` and a successful ACK. The peer reported `source=CC1101`, recovered and processed the retained packet, transmitted its ACK and returned RX ready. Both devices became ACTIVE.

The radio-woken receiver did not initiate a return motion-triggered transaction. A separate CC1101-only test and timer-wake test also produced no automatic peer wake. No ping-pong or repeated automatic wake transaction occurred in the tested scenarios.

### Unavailable-Peer Failure Test

With Dudu powered off, Bubu entered deep sleep and was then moved. GPIO3 woke Bubu, startup completed and one automatic CC1101 transaction attempted to wake Dudu. No ACK arrived; the existing retries exhausted cleanly.

Representative bench output:

```text
CC1101 WAKE TX | id=536 | GIVE_UP | retries=2 | reason=ACK_TIMEOUT | RX_READY=1
POWER: LOCAL=ACTIVE PEER=OFFLINE
TRANSPORT: pending=0
```

Message ID 536 is a session example, not a protocol constant. Bubu remained ACTIVE and responsive, CC1101 returned to RX, and the automatic CC1101 transaction did not restart. No infinite search or retry loop followed.

Normal automatic ESP-NOW heartbeat traffic is separate. The bench `h` pause flag is RAM-only and resets on reboot, so ordinary ESP-NOW EVENTs can resume after a motion wake. Each uses its own bounded retry episode. That traffic is not evidence that the one-shot CC1101 wake is looping. The existing ESP-NOW loss path can subsequently mark an unreachable peer OFFLINE; the CC1101 helper itself does not directly change that peer state.

### Host Tests and Build Verification

Focused application tests cover pure-motion startup, suppression for CC1101/both-pin/timer/cold wakes, safe startup ordering, successful/failed/unavailable peer results, one ID allocation including rollover, and repeated loop execution without another automatic transaction. They also verify that startup failure or a guard refusal cannot turn into an unbounded deferred request.

Existing radio tests exercise the real transmitter's same-ID/payload retries, ACK matching, retry cutoff and bounded RX recovery. Retained-packet and awake re-ACK tests preserve the lost-first-ACK scenario and absence of duplicate application execution. Manual `w`, coordinated sleep, collision handling, callback drain, RTC history and Motion failure regressions continue to pass.

Checkpoint 2 additionally covered GPIO3/GPIO4/both/timer classification, healthy empty RX on motion wake, preservation of coincident packets, stuck-HIGH input, checked-I2C failures, configuration readback and abort restoration. A GPIO3 assertion injected after the RTC save caused entry to abort and the saved checkpoint/wake configuration to be cleared.

The full host suite passed with compiler warnings as errors and address/undefined-behavior sanitizers. Bubu and Dudu firmware builds passed without warnings or errors, and `git diff --check` passed. Software fault injection in host tests is distinct from the real two-board evidence above.

After physical verification, Checkpoint 3 was committed and pushed as:

`1b1041976b4200668c3090d27c8138dc80aa53f2` — `feat: wake sleeping peer after local motion wake`

Only `src/main.cpp` and `tests/host/sleep_handshake_test.cpp` were included in that commit.

### Future Awake Motion and Emotional UI

The next product requirement is recorded here, not implemented by today's firmware: Motion should eventually have separate sensitivity profiles for SLEEP and AWAKE.

SLEEP should remain relatively insensitive; the current approximately 3 g threshold is acceptable for deliberate wake movement. AWAKE should detect ordinary pickup/movement more sensitively, with its threshold determined through physical tuning.

Movement while ACTIVE must not take ownership of the emotional UI. Keep confirmed proximity separate from the status of an update:

* confirmed proximity: `CLOSE` or `FAR`

* update status: `READY`, `MOVING`, or `WAITING/CHECKING`

For example:

```text
last confirmed proximity = FAR
    -> movement begins: status MOVING; FAR display/heartbeat continues
    -> movement settles: status CHECKING; retain FAR while measuring
    -> fresh proximity result confirmed CLOSE
    -> update confirmed proximity and LED behavior; status READY
```

Movement alone must not blank LEDs, stop the heartbeat animation, erase the last confirmed CLOSE/FAR value, immediately change proximity or create an emotional heartbeat EVENT. This future awake-motion policy is separate from the already-defined CC1101 wake EVENT used by Checkpoint 3.

No awake-sensitive movement detection, proximity refresh or emotional UI integration was implemented during this session.

### Current Working State

The integration branch now combines reliable ESP-NOW transport and application ACK/retries, duplicate handling, bounded coordinated sleep negotiation, real deep-sleep execution and RTC history persistence with retained CC1101 packet recovery, awake duplicate re-ACK service, ADXL345 deliberate-motion wake and one-shot motion-triggered peer wake.

Both directions and the unavailable-peer path were physically verified. The tested failures remain bounded, with local runtime usable after peer-wake failure and no infinite wake/search loop in those scenarios. This is not a production-ready claim or proof against every RF-loss condition.

The 30-second timer remains an integration safety net. Awake-sensitive motion/proximity refresh, battery and power hardware, and final repository polish/evidence remain unfinished.

Firmware remains committed and pushed on `feature/sleep-execution`. This DEVLOG is maintained on `main`; this documentation update does not merge or cherry-pick feature source or tests into `main`.

### Problems Solved

* lost first CC1101 wake ACK causing sender failure despite successful peer wake

* servicing awake retries with a retained ACK and no second application execution

* integrating Motion after, rather than ahead of, retained CC1101 recovery

* ADXL345 deep wake on GPIO3 alongside the existing GPIO4 radio source

* explicit combined GPIO mask and separate wake-source reporting

* sleep-specific Motion preparation, asserted-input refusal and bounded abort restoration

* propagating local motion wake to the sleeping peer in both directions

* suppressing automatic return wakes and tested ping-pong behavior

* bounded unavailable-peer failure with CC1101 restored to RX

* keeping local state ACTIVE and runtime responsive after peer-wake failure

### Git Commits

The session's firmware checkpoints and integration commit:

* `89014ad` — `feat: integrate ADXL345 motion initialization`

* `ed1d227` — `fix: re-ack CC1101 wake retries while awake`

* `4fe8171` — `feat: wake from ADXL345 motion during deep sleep`

* `8ce8f23` — `merge: integrate ADXL345 motion wake`

* `1b10419` — `feat: wake sleeping peer after local motion wake`

All five were verified in `feature/sleep-execution` history, with its local HEAD matching origin before this DEVLOG update. They remain on the feature/integration history and are not being merged into `main` by the documentation commit.

### Next Step

Begin the more sensitive AWAKE Motion profile as a separate, physically testable checkpoint. First establish reliable ordinary movement detection without changing the existing wake path or taking control of the emotional UI.

The later sequence should be MOVING while retaining confirmed CLOSE/FAR and uninterrupted heartbeat LEDs, then CHECKING after movement settles, followed by a fresh rough RSSI/proximity result. Only a confirmed result should change CLOSE/FAR. RSSI remains a rough proximity mechanism, not exact distance.

Do not combine sensor tuning, UI ownership and proximity refresh into one large change. Preserve the verified power/wake checkpoints and continue build, flash, physical test, understand, commit, then the next step.

## 2026-09-27

### Completed

Continued practical firmware development on `feature/sleep-execution`, from the verified motion-to-peer wake checkpoint through awake motion tuning, movement settlement, passive ESP-NOW RSSI measurement and manually selectable dual-radio application transport.

This session began during the previous evening and continued after midnight. It is recorded under September 27, the date the session was closed. Each practical checkpoint followed the existing sequence: one understandable change, software validation, physical testing, understanding the result, then commit.

### Awake Motion Profile — Checkpoints 4A and 4B

Checkpoint 4A was inspection and design only. The ADXL345 awake and sleep paths originally shared the same `THRESH_ACT` register value. Changing awake sensitivity alone would also have changed deep-sleep wake sensitivity, because sleep preparation inherited the current threshold.

Inspection also found that the awake event path already existed:

```text
ADXL345 activity
    -> INT1 / GPIO3
    -> ISR flag
    -> Motion::getEvent()
```

The application did not yet consume those Motion events during normal awake runtime. There was no need to introduce another ISR, task or sensor event mechanism.

Checkpoint 4B separated the awake and sleep activity profiles explicitly. The provisional awake threshold of 8, approximately 0.5 g, was physically tested and found too sensitive. It was adjusted to 12, approximately 0.75 g, and accepted on both boards: ordinary pickup was detected reliably while small/random desk vibration was rejected sufficiently for the current bench setup.

The sleep threshold remained 48, approximately 3 g. Sleep preparation now explicitly writes and verifies 48; returned or aborted sleep preparation restores and verifies the awake value 12. This preserves deliberate-motion wake sensitivity independently of awake tuning.

Awake event consumption initially added diagnostics only:

```text
MOTION AWAKE | MOVING
MOTION AWAKE | INACTIVITY
```

No radio transaction, power transition, proximity measurement, heartbeat policy, LED or OLED behavior was attached to awake Motion in this checkpoint. GPIO3 deep wake and the existing one-shot peer wake remained separate from ordinary awake movement.

Committed and pushed as `71264056ef4a0c5bab4c9882e35b8ffedfd1d603` — `feat: add tuned awake motion detection`.

### Movement Settle Detection — Checkpoint 5

Added a small application-owned movement tracker in `main.cpp`, with states `READY`, `MOVING` and `WAITING`:

```text
READY + Activity       -> MOVING
MOVING + Inactivity    -> WAITING
WAITING + Activity     -> MOVING; cancel pending settlement
WAITING + settle expiry -> one-shot SETTLED -> READY
```

The timestamp-based timer is non-blocking and rollover-safe. Repeated Inactivity does not restart the timer. Activity processed on the exact expiry iteration takes priority, so new movement cannot produce a stale SETTLED result.

The first additional settle period was 2000 ms. Physical testing confirmed correct transitions and cancellation, but the extra two seconds felt unnecessarily slow. It was tuned to `SETTLE_MS = 1000`, which felt more natural without changing the state machine or timer rules.

ADXL345 `TIME_INACT` remains 3 seconds. The practical sequence is therefore movement stopping, the sensor's inactivity period, entry into WAITING, an additional one-second settle period, then SETTLED. The application settle timer is not a replacement for the sensor's inactivity timer.

Movement state remained independent of proximity and heartbeat behavior at this checkpoint. It did not begin sleep negotiation, transmit an emotional EVENT or change the confirmed proximity concept.

Committed and pushed as `aab2ef03f8b5ef2fb6eca5bea7637d67f2ce2fc1` — `feat: add tuned movement settle detection`.

### Passive ESP-NOW RSSI Observation — Checkpoints 6A and 6B

Checkpoint 6A inspection established that the installed stack, PlatformIO Espressif32 7.1.2 with Arduino-ESP32 2.0.17 and ESP-IDF 4.4.7, does not expose per-packet RSSI through its `esp_now_recv_cb_t` callback. `WiFi.RSSI()` would describe an associated access point rather than the ESP-NOW peer and was unsuitable for this measurement.

Checkpoint 6B added a separate passive Wi-Fi promiscuous observer using `wifi_promiscuous_pkt_t::rx_ctrl.rssi`. The two paths deliberately remain independent:

```text
normal ESP-NOW callback
    -> existing Protocol::Message RX queue
    -> application processing and receipt ACK

promiscuous callback
    -> strict peer ESP-NOW frame filtering
    -> separate bounded RSSI observation queue
    -> loop diagnostics
```

Each `RssiObservation` copies the `Protocol::Message`, source MAC, signed RSSI and capture timestamp. SDK buffer pointers are not retained, and neither path assumes that the other callback ran first.

The application message remains exactly eight bytes, and its existing receive queue is unchanged. The diagnostic observation queue holds four entries; the loop drains at most two observations per iteration. Overflow drops observations only, not application packets. RSSI backlog is not a sleep blocker.

Actual ESP-NOW v1 peer frames were observed successfully on both Bubu and Dudu while normal EVENT/ACK operation continued. Close-range movement and orientation stress produced raw RSSI from roughly the mid -40s dBm to occasional -67/-68 dBm readings. These were bench observations, not calibrated distance measurements or classification thresholds.

Committed and pushed as `a7898999227ed25bc03ede4ecfe39a750f83a35f` — `feat: add passive ESP-NOW RSSI diagnostics`.

### Settled RSSI Measurement — Checkpoint 6C

Connected the one-shot SETTLED result to a separate `ProximityUpdateState`, with `READY` and `CHECKING`. `MovementState` remained independent. Settlement begins a measurement; it does not itself confirm proximity.

CHECKING collects three fresh, distinct peer ESP-NOW observations. Each must have been captured strictly after the check started. Repeated message IDs do not count again, so RF retries cannot artificially supply multiple samples. Exactly three signed RSSI values are retained, then deterministic compare/swap logic calculates their median.

For example:

```text
-52, -67, -54 dBm
    -> median -54 dBm
```

The median reduces the effect of an isolated orientation or multipath spike compared with an average. It does not turn RSSI into exact distance.

`CHECK_TIMEOUT_MS = 12000` is an absolute failure ceiling, not a mandatory measurement delay. A check completes as soon as its three valid samples arrive. Partial progress cannot extend the deadline. New Motion Activity, peer OFFLINE, a non-ACTIVE power state, unavailable runtime or sleep handoff cancels the measurement.

Physical testing repeatedly completed measurements on both devices. Representative medians were approximately -52, -49, -64 and -54 dBm on Bubu, and -51, -49, -63 and -52 dBm on Dudu. Passive acquisition of three distinct samples takes noticeable time, but the filtering and reliability benefit was judged acceptable.

No CLOSE/FAR classifier was implemented. Movement, check progress and a future confirmed proximity result remain separate concepts.

Committed and pushed as `fc769f2a56360c7d9dff22f2de990772c5fef1ed` — `feat: add settled RSSI proximity measurement`.

### Proximity Product Decision

The intended user-facing model remains only `CLOSE` and `FAR`; it will not claim exact distance in meters. CLOSE intentionally includes the normal medium-range interaction area. FAR should represent clear separation and should be harder to trigger than one weak, orientation-dependent observation.

During deliberately close testing, aggressive board movement and orientation changes occasionally produced approximately -67/-68 dBm while the boards were still considered CLOSE. Those readings must not be treated as a chosen FAR threshold.

Future classification should be biased against false FAR and is expected to use filtered median RSSI, hysteresis and empirically tuned thresholds. No final thresholds or hysteresis values have been selected or implemented.

The earlier product rule still applies: movement or CHECKING should retain the last confirmed proximity rather than replace it. A future confirmed FAR result remains FAR through MOVING, WAITING and CHECKING; only a completed fresh measurement classified as CLOSE should change it. This is a product direction, not a claim that confirmed CLOSE/FAR state or its emotional UI is already implemented.

### Dual-Radio Product Direction

Established the intended future division of responsibility:

* ESP-NOW remains initialized whenever the device is awake, supplies proximity evidence, and carries normal application traffic when CLOSE.

* CC1101 retains its deep-sleep wake role and becomes the awake application transport when FAR.

* ESP-NOW remains alive while CC1101 carries FAR-mode application traffic, so fresh proximity evidence can eventually return application routing to ESP-NOW without reboot or Wi-Fi reinitialization.

FAR must not mean disabling ESP-NOW. Future automatic switching will need hysteresis to avoid repeated ESP-NOW/CC1101 changes near a boundary. This session implemented manual selection only; it did not implement the classifier or automatic policy.

### CC1101 Runtime Inspection — Checkpoint 7A

Inspection showed that the active firmware did not yet support general awake CC1101 application EVENT/ACK traffic. Active ownership was concentrated in `CC1101Bus`, `CC1101SleepArm`, `CC1101WakeTx` and `CC1101WakeRecovery`, including the retained wake EVENT and saved duplicate-ACK service.

The older `RadioTask` and `CC1101Radio` runtime still exist in the repository but are inactive and retain stale ownership assumptions. The decision was to keep them inactive. The synchronous `CC1101WakeTx` remains a bounded wake transaction, not the normal awake application transmitter.

The radio already uses the same packed eight-byte `Protocol::Message`:

```text
CC1101 FIFO: length byte 8 + eight Protocol::Message bytes
```

There was no need to redesign the wire protocol. The application ACK/retry and duplicate machinery in `main.cpp` was identified as the shared owner for both transports.

### Selectable Dual-Radio Application Transport — Checkpoint 7B

Added a local transport selection, `ESP_NOW` or `CC1101`. Boot defaults to ESP-NOW; selection exists in RAM only and is not persisted in RTC memory.

The bench controls include `e` to select ESP-NOW, `c` to select CC1101, `p` to report selected/pending transport and CC1101 busy state, and `h` to pause/resume automatic heartbeats. Selection is guarded while transport or sleep work is pending.

There is one shared application reliability state machine. A new reliable EVENT captures `selectedTransport` into `pendingTransport`. Every retry preserves the same message bytes, message ID and pending transport. The existing 300 ms ACK timeout and maximum two retries remain unchanged.

ACK matching requires the expected peer, the acknowledged message ID matching the pending transaction, and an incoming transport matching `pendingTransport`. A receipt on the other radio cannot finish the transaction.

Receipt ACK routing follows the incoming EVENT, independently of the receiver's selected outbound transport:

```text
EVENT received through ESP-NOW -> ACK through ESP-NOW
EVENT received through CC1101  -> ACK through CC1101
```

Application duplicate history is shared across radios. No second radio-specific application ACK/retry or duplicate state machine was created.

Extended the existing `CC1101WakeRecovery` awake owner with general EVENT/ACK reception, bounded submission, TX completion polling and RX restoration. Submission reports accepted, busy or failed; it does not wait for the application's receipt ACK or own its retries. `main.cpp` retains that responsibility.

Saved wake-EVENT retry handling and its retained ACK take precedence over general awake delivery. The runtime protects pending FIFO contents, bounds TX/RX handling, participates in physical-sleep guards and reports persistent radio failures. Retained boot recovery and the existing wake transaction remain distinct from normal application transport. Legacy `RadioTask` and `CC1101Radio` were not activated.

Selecting CC1101 does not deinitialize ESP-NOW, disable Wi-Fi, change its channel, enable Wi-Fi sleep, stop callbacks or remove the RSSI observer. Coordinated `SLEEP_REQUEST`, `SLEEP_READY`, `SLEEP_COMMIT`, `SLEEP_ACK` and `SLEEP_CANCEL` controls remain on ESP-NOW regardless of application selection.

### Checkpoint 7B Physical Validation

Two-board bench testing passed for:

* default ESP-NOW application EVENT/ACK operation

* CC1101 application EVENT/ACK in both Bubu-to-Dudu and Dudu-to-Bubu directions, including repeated transactions

* manual ESP-NOW -> CC1101 -> ESP-NOW selection without reboot or Wi-Fi reinitialization

* mixed transport operation, with one device sending application EVENTs over CC1101 and the other over ESP-NOW

* receipt ACKs returning through the incoming EVENT's radio rather than the receiver's selected outbound transport

* bounded retries and pending transactions retaining their selected transport

* Motion responsiveness during CC1101 traffic and continued ESP-NOW availability

Some serial retry/GIVE_UP and disconnect sequences occurred while boards were intentionally unplugged and replugged. Those observations were not treated as evidence of a new CC1101 runtime design defect. Repeated application transactions succeeded once both boards were powered and running, and bounded retry behavior handled transient startup/transition conditions.

### Post-7B Sleep and Wake Regression

The bench command `x` was confirmed still present and still calls `enterPhysicalSleep(false)`. Initial bench confusion involved asymmetric sleep conditions, not removal of the command.

Moving an already ACTIVE board produces ordinary awake Motion events. It does not automatically wake a sleeping peer. The automatic peer-wake policy is specifically a one-shot startup action after a local Motion deep wake:

```text
both devices sleeping
    -> move one device
    -> ADXL345 / GPIO3 wakes the local ESP32
    -> startup identifies local Motion wake and completes safely
    -> one bounded CC1101 peer-wake transaction
    -> peer wakes through GPIO4
    -> retained wake EVENT recovery and ACK
    -> both devices ACTIVE
```

This full Motion deep-wake -> CC1101 peer-wake path was physically retested after 7B and passed. The tested regression confirms that the new awake CC1101 application runtime preserved the previously verified motion-to-peer wake behavior. It is distinct from a policy that would wake peers on every awake movement.

Checkpoint 7B was committed and pushed as `aad3045c46bd0bf0949329676472488fb919b400` — `feat: add selectable dual-radio application transport`.

### RSSI Availability While CC1101 Carries Application Traffic

Physical testing exposed an expected limitation for the next checkpoint: ESP-NOW being initialized does not guarantee that any ESP-NOW frames are being transmitted.

When both devices route normal application heartbeats through CC1101, SETTLED can start CHECKING without any fresh ESP-NOW RSSI samples arriving. The check can therefore reach its bounded timeout even though ESP-NOW itself remains available.

Future FAR-mode operation needs a bounded ESP-NOW monitoring/probe mechanism. The planned direction is a small probe/reply exchange during CHECKING, allowing the existing observer to collect fresh samples while normal application EVENTs continue through CC1101. This is not implemented in 7B, and no background probe policy was added during this session.

### Host Tests and Build Verification

Final Checkpoint 7B validation passed before its commit and push:

* focused application/transport tests for both Bubu and Dudu, including route-pinned retries, wrong-transport ACK rejection, mixed routing and shared duplicate handling

* CC1101 awake runtime, retained wake, duplicate re-ACK, wake-TX, FIFO protection and bounded-failure tests

* automated coordinated sleep/wake, transport drain, startup ordering and one-shot Motion-to-peer wake regressions

* ESP-NOW observer/drain and Motion/proximity measurement tests

* full host suite with AddressSanitizer and UndefinedBehaviorSanitizer, with compiler warnings treated as errors

* Bubu and Dudu PlatformIO firmware builds

* `git diff --check`

No compiler warnings or errors were reported. These automated results complement the physical evidence above; host fault injection is not a substitute for the reported two-board tests.

### Current Working State

The practical firmware remains on `feature/sleep-execution` at `aad3045c46bd0bf0949329676472488fb919b400`, with the local and origin feature refs matching at session close.

Verified settings remain: eight-byte `Protocol::Message`, 300 ms ACK timeout, maximum two retries, awake activity threshold 12, sleep activity threshold 48, three-second sensor inactivity, one-second application settlement, three fresh distinct RSSI samples with median filtering, a 12-second absolute check ceiling, and the 30-second safety wake timer.

Manual dual-radio application selection is implemented. CLOSE/FAR classification, RSSI thresholds, hysteresis, automatic switching and a bounded ESP-NOW probe policy remain future work. No new LED/OLED integration or emotional UI behavior was introduced by these checkpoints.

`main` intentionally does not contain the current practical firmware. This DEVLOG update is documentation only on `main`; it does not merge or cherry-pick firmware or tests. `chore/repository-polish` remains frozen and untouched. Existing stashes are preserved. The unrelated `.vscode/extensions.json` modification remains unchanged and unstaged, and practical work resumes on the verified feature branch after the documentation update.

### Git Commits

The session's practical checkpoints, all verified in `feature/sleep-execution` history:

* `71264056ef4a0c5bab4c9882e35b8ffedfd1d603` — `feat: add tuned awake motion detection`

* `aab2ef03f8b5ef2fb6eca5bea7637d67f2ce2fc1` — `feat: add tuned movement settle detection`

* `a7898999227ed25bc03ede4ecfe39a750f83a35f` — `feat: add passive ESP-NOW RSSI diagnostics`

* `fc769f2a56360c7d9dff22f2de990772c5fef1ed` — `feat: add settled RSSI proximity measurement`

* `aad3045c46bd0bf0949329676472488fb919b400` — `feat: add selectable dual-radio application transport`

### Next Step

Begin with inspection/design for bounded ESP-NOW proximity probes while CC1101 is selected for application traffic. Determine how a lightweight probe/reply can generate fresh RSSI evidence during CHECKING without triggering emotional EVENT behavior, taking over the normal application reliability transaction, changing CC1101 routing, interfering with sleep, changing peer semantics or changing the eight-byte `Protocol::Message` layout.

After that inspection, implement and physically verify one narrow sequence:

```text
manual CC1101 application mode
    -> movement -> settlement -> CHECKING
    -> bounded ESP-NOW probe/reply
    -> three fresh distinct RSSI samples
    -> median COMPLETE
```

Normal application EVENTs must remain on CC1101 during that experiment. Probes serve proximity measurement/recovery only. CLOSE/FAR classification remains a later checkpoint; do not combine probe transport, threshold tuning and automatic radio switching into one change.

### September 27 Session Continuation

The sections above preserve the earlier stopping point at Checkpoint 7B. Work continued later on September 27 through bounded probing, proximity classification, automatic application transport selection, fallback/recovery, live OLED diagnostics and implementation of the non-blocking heartbeat LED layer. The end-of-session state and next step below supersede the earlier checkpoint summary without changing its history.

This continuation was recorded on September 28, a travel day, for work completed on September 27. Checkpoints through 8B were committed after physical validation. Checkpoint 9B was implemented and software-validated before stopping, but remains uncommitted and has not yet been physically validated.

### Bounded Probes and Provisional Classification — Checkpoints 7C and 7D

Checkpoint 7C supplied the missing ESP-NOW evidence while CC1101 carries application traffic. After movement and settlement, an existing CHECKING measurement can send bounded ESP-NOW `ProximityProbe` / `ProximityProbeReply` exchanges, correlate three fresh RSSI samples and complete the median without changing application transport. Probe message types are 8 and 9; `Protocol::Message` remains exactly eight bytes.

Physical testing passed in both directions while normal CC1101 EVENT/ACK traffic continued. An unavailable peer produced bounded failed transmissions at approximately 500 ms pacing and the existing 12-second hard timeout with zero samples. Probe failure did not mark the peer OFFLINE. Movement cancelled the old check, and later settlement could start a fresh measurement. Coordinated deep sleep, GPIO3 motion wake, CC1101/GPIO4 peer wake and retained wake-packet recovery were also physically revalidated.

Checkpoint 7D added provisional RAM-only `UNKNOWN`, `CLOSE` and `FAR` classification from a completed median of three samples:

* `ENTER_FAR_DBM = -80`: UNKNOWN or CLOSE becomes FAR at a median of -80 dBm or lower; otherwise UNKNOWN becomes CLOSE and CLOSE remains CLOSE.

* `ENTER_CLOSE_DBM = -75`: FAR becomes CLOSE at a median of -75 dBm or higher; otherwise it remains FAR.

Partial, failed, timed-out or cancelled measurements retain the previous classification. Reboot/deep sleep resets it to UNKNOWN; no RTC persistence or distance-in-meters claim was added. A nearby physical measurement completed and reported CLOSE with normal application operation. The FAR threshold remains deliberately provisional, and a dedicated physical FAR-distance campaign is deferred.

These checkpoints were committed and pushed separately as `135e382519acb6e3cf17f4421c0363d446f48df0` — `feat: add bounded ESP-NOW proximity probing`, and `95f3f6d1e672551f3bd7c3d714ac44818e9b9f88` — `feat: add conservative proximity classification`.

### Automatic Proximity-Based Application Transport — Checkpoint 7E

Connected completed proximity evidence to application-radio selection. CLOSE requests ESP-NOW for future application EVENTs; FAR requests CC1101. UNKNOWN leaves the current selection unchanged. Classification remains RAM-only.

Selection waits for the existing awake/drained safe boundary and cannot apply while proximity is CHECKING. An in-flight EVENT never changes radio: `pendingTransport` remains the transport captured when the transaction began, and retries retain the same message ID, bytes and radio. The policy changes `selectedTransport` only after that boundary is safe.

Manual `e` / `c` controls remain available. A successful manual selection clears pending automatic policy and lasts until fresh completed proximity evidence requests another selection. A refused command preserves pending policy. Sleep controls remain on ESP-NOW, and pending automatic selection does not block physical sleep.

Physical validation started with Dudu manually selected to CC1101. Movement settled, bounded ESP-NOW probing collected three samples around -52 dBm, and the median completed at -52 dBm with classification CLOSE:

```text
APP TRANSPORT AUTO | selected=ESP-NOW | proximity=CLOSE
TX EVENT | sender=DUDU | id=30 | via=ESP-NOW | waiting for ACK
ACK MATCHED | message=30 | via=ESP-NOW
```

This verified CC1101 application mode -> fresh ESP-NOW proximity sampling -> CLOSE -> automatic ESP-NOW selection -> a real ESP-NOW EVENT/ACK, without reboot or Wi-Fi reinitialization. Asymmetric transport operation remained functional. FAR-to-CC1101 policy, safe switch boundaries, live retries, wrong-radio ACK rejection, manual controls, sleep interaction, reboot reset and latest-classification behavior were covered by the host suite; the dedicated real-world -80 dBm FAR test remains deferred.

Committed and pushed as `c57daf7f5ea9fe5116583775e68f9c028bde7bdf` — `feat: add automatic application transport selection`.

### ESP-NOW Failure Fallback to CC1101 — Checkpoint 7F

Added one RAM-only fallback-policy flag, `espNowFallbackPending`. Exhausting the existing reliability budget for a normal ESP-NOW application EVENT requests CC1101 for future application traffic. Sleep-control failures do not request fallback, and CC1101 EVENT failures do not cause reverse failover or transport ping-pong.

Application reliability remains a 300 ms ACK timeout with two retries, using the same message ID, bytes and transport. The exhausted EVENT is never replayed on CC1101. Its pending message and pending transport are preserved through exhaustion; only a future EVENT uses the fallback radio.

Transport failure never manufactures FAR or changes `proximityClassification`. Newer ESP-NOW failure clears older automatic-selection intent; a fresh completed proximity measurement can supersede older fallback intent. Successful manual selection clears both policy flags, while refused selection preserves them. Fallback waits through unsafe or sleep states and never blocks physical sleep. Reboot clears the flag and restores UNKNOWN proximity and ESP-NOW selection. Local ESP-NOW initialization-failure handling was not changed.

Existing PowerManager logic still owns peer OFFLINE behavior. Selecting CC1101 does not itself mark the peer ONLINE; real subsequent application evidence restores reachability.

Physical testing made Bubu unavailable while Dudu was using ESP-NOW. Dudu EVENT id=24 followed the existing bounded transaction:

```text
initial send via ESP-NOW
    -> retry 1/2 via ESP-NOW
    -> retry 2/2 via ESP-NOW
    -> GIVE UP
    -> POWER PEER: ONLINE -> OFFLINE
    -> APP TRANSPORT FALLBACK | selected=CC1101 | reason=ESP_NOW_EVENT_GIVE_UP
```

The failed id=24 was not replayed. The next EVENT used a fresh message ID and CC1101. CC1101 failures neither reversed the fallback nor created a proximity classification.

After Bubu returned, real communication restored peer ONLINE and Dudu completed successful CC1101 EVENT/ACK traffic. Selection stayed on CC1101 until new proximity evidence existed. Dudu was then moved and settled; three ESP-NOW probe replies were approximately -50, -51 and -50 dBm, producing median -50 dBm and CLOSE. Automatic selection returned to ESP-NOW, and the next real ESP-NOW EVENT received its matching ACK.

The full physically verified recovery loop was:

```text
ESP-NOW loss
    -> bounded same-radio retry exhaustion
    -> CC1101 fallback
    -> peer recovery and working CC1101 EVENT/ACK
    -> fresh ESP-NOW proximity measurement
    -> CLOSE
    -> automatic return to ESP-NOW
    -> working ESP-NOW EVENT/ACK
```

Committed and pushed as `5ca9beff4d7bbd1af74828d7ecc88824e6b4c5ce` — `feat: add ESP-NOW failure fallback to CC1101`.

### Live OLED Status — Checkpoint 8B

Re-integrated the SH1106 OLED into the practical runtime as a presentation-only diagnostic display. It reports the device identity and compact PEER, DIST, RADIO and STATE values, for example:

```text
BUBU
PEER:  ONLINE
DIST:  CLOSE
RADIO: ESP-NOW
STATE: ACTIVE
```

Distance supports UNKNOWN, CHECKING, CLOSE and FAR. RADIO shows the selected application transport, ESP-NOW or CC1101. Compact power-state labels include ACTIVE, IDLE, SLEEP NEG, SLEEP and WAKING. The display does not own PowerManager, proximity classification or transport policy.

The OLED remains at I2C address `0x3C` and shares GPIO0/GPIO1 with the ADXL345 at `0x53`. Motion remains the owner of `Wire.begin(GPIO0, GPIO1)`. Display initialization occurs only after critical retained CC1101 wake recovery and Motion initialization.

The framebuffer is not redrawn every loop. `main.cpp` owns a last-drawn snapshot; visible changes are coalesced while busy and drawn at a safe transport boundary. Display work never becomes a physical-sleep blocker. Both physical OLEDs displayed the live runtime status successfully.

### Peer-Return Recovery Exposed by the OLED

The live display exposed a pre-existing recovery gap during flashing. One board temporarily disappeared; the other exhausted ESP-NOW EVENT retries and correctly fell back to CC1101. After the peer returned, ESP-NOW reception was healthy and application evidence restored ONLINE, but selected application transport could remain CC1101 indefinitely.

The previous return policy required a fresh completed proximity measurement. Without movement, no new measurement was started. The fix captures the previous peer state before application EVENT/ACK processing. When real application evidence produces OFFLINE -> ONLINE while `selectedTransport == CC1101`, it starts one existing bounded proximity check.

No new persistent state, periodic polling or direct CLOSE inference was added. The returning packet establishes reachability, not distance. The existing check still needs three correlated fresh ESP-NOW samples, their median, the existing CLOSE/FAR classifier and the existing automatic selector.

Physical testing reproduced the flashing scenario without moving the recovering device:

```text
ESP-NOW failure -> CC1101 fallback
    -> peer returns -> OFFLINE -> ONLINE
    -> one automatic PROXIMITY CHECK
    -> three samples around -52 dBm -> median -52 dBm
    -> CLOSE -> APP TRANSPORT AUTO selected ESP-NOW
    -> next real ESP-NOW EVENT/ACK succeeds
```

This removed the sticky-CC1101 behavior after temporary outages without bypassing proximity evidence or changing application reliability.

### OLED / Shared-I2C Sleep and Wake Regression

One intermittent Dudu Motion initialization failure appeared during a flashing sequence, with a Wire `requestFrom` error and `MOTION INIT FAILED`. No speculative code change was made. It was not reproducible after reflashing; subsequent startup reported `MOTION INIT | OK (DEVID=0xE5)`, and normal communication remained healthy.

Because OLED and ADXL345 share I2C, coordinated sleep and the complete motion-to-peer wake path were physically re-tested with OLED enabled. Both devices completed:

```text
SLEEP_REQUEST -> SLEEP_READY -> SLEEP_COMMIT -> SLEEP_ACK
    -> HANDSHAKE_COMPLETE -> SLEEP TRANSPORT DRAINED
    -> MOTION SLEEP ARM READY -> CC1101 SLEEP ARM READY
    -> deep sleep
```

Bubu woke from Motion with GPIO mask `0x8` and `RTC RESTORE OK`. Dudu woke through CC1101 with GPIO mask `0x10` and `RTC RESTORE OK`; retained recovery reported `CC1101 WAKE PACKET recovered=1`, `WAKE EVENT processed=1`, `WAKE ACK sent=1` and `RX_READY=1`. ESP-NOW application communication resumed after wake.

These physical results confirmed that the OLED/shared-I2C integration preserved coordinated sleep, GPIO3 motion wake, CC1101/GPIO4 peer wake and retained wake-packet handling.

OLED status and peer-return recovery were committed and pushed together as `4a2d912970ebc21a978dee3a0fe810dfe9aedc3a` — `feat: add live OLED status and peer recovery`.

### Non-Blocking WS2812B Heartbeat — Checkpoint 9B

Checkpoint 9B is implemented and software-validated only. It has not been physically validated or committed. The September 27 session ended before hardware testing, and September 28 is a travel day. The uncommitted work remains on `feature/sleep-execution` above committed HEAD `4a2d912970ebc21a978dee3a0fe810dfe9aedc3a`.

Changed files are:

* `include/LED.h`
* `src/LED.cpp`
* `src/main.cpp`
* `tests/host/sleep_handshake_test.cpp`
* `tests/host/Adafruit_NeoPixel.h` — new minimal host-only pixel recorder

Replaced the old prototype's blocking `heartbeat()` / fade loops and `delay()` calls with a small loop-owned state machine for the existing single WS2812B on GPIO21. No FreeRTOS task, animation queue, pending-heartbeat counter or battery behavior was added. The public API is:

```cpp
LED();
void begin();
void requestHeartbeat();
void update(uint32_t now);
void off();
bool busy() const;
```

The phases are:

```text
Idle -> Requested -> Pulse1Up -> Pulse1Down
    -> Gap -> Pulse2Up -> Pulse2Down -> Idle
```

The first red pulse rises from 0 to 180 over 144 ms and falls to 0 over another 144 ms. An 80 ms OFF gap precedes the stronger second pulse, which rises from 0 to 255 over 204 ms and falls to 0 over another 204 ms. Total duration is 776 ms from the first serviced update, ending OFF with no blocking tail or cooldown.

Each main loop calls one bounded `update(now)`. Unsigned elapsed-time subtraction handles millis rollover, and late servicing skips directly to the current frame without catch-up loops. Requests perform no hardware work and restart the animation at the next update instead of queueing another animation. Repeated unchanged brightness does not resend the pixel.

Only a genuinely new remote application Heartbeat EVENT requests the animation, in the validated new-EVENT path after duplicate rejection. Duplicate retries are ACKed again without replay. ACKs, proximity probes/replies, sleep controls and malformed packets do not trigger it. Received ESP-NOW and CC1101 EVENTs use the same LED reaction; callback/queue processing does not drive the pixel.

Movement, settlement, proximity CHECKING, OLED updates and protocol processing do not pause or cancel the heartbeat. Sleep wins immediately: physical-sleep entry cancels animation and forces OFF without waiting for completion. LED busy state is not a transport-drain or sleep-blocking condition.

LED initialization follows retained CC1101 wake recovery and Motion/Display initialization, leaving the pixel OFF. Cold boot and deep wake do not restore animation state from RTC. A genuinely newly delivered retained wake EVENT can schedule a fresh heartbeat after LED initialization; this is delivery of a new event, not continuation of an old animation.

### Checkpoint 9B Host and Build Validation

The host harness runs the real LED implementation with a minimal fake `Adafruit_NeoPixel` output recorder. It does not emulate graphics or replace the animation logic. Coverage includes:

* full double-pulse progression, first peak 180, second peak 255, 776 ms completion and final OFF

* millis rollover, restart during active phases, skipped frames, delayed servicing and bounded pixel writes

* new EVENT triggers on both radios, duplicate re-ACK without replay, and exclusion of ACK/probe/sleep/malformed traffic

* normal incoming EVENT/ACK traffic and unchanged pending message bytes, transport, 300 ms ACK deadlines and two-retry behavior during animation

* continued animation through Activity/Inactivity, MOVING, WAITING, proximity checks and OLED work

* immediate cancellation for manual/coordinated physical sleep, safe startup ordering, cold/deep boot reset and deferred presentation of a new retained wake EVENT

Validation completed before the September 27 stopping point:

* `bash tests/host/run.sh` — PASS for Bubu and Dudu
* `platformio run -e bubu` — PASS
* `platformio run -e dudu` — PASS
* `git diff --check` — PASS
* complete checkpoint diff review — PASS

These are software results only. No physical 9B LED test is claimed by this entry.

### End-of-Session Working State

The communication and power architecture is essentially complete. Committed practical firmware on `feature/sleep-execution` includes reliable bidirectional ESP-NOW, application ACKs, same-ID bounded retries, duplicate handling, coordinated sleep, ADXL345 activity/inactivity, deep-sleep motion wake, CC1101 peer wake, retained CC1101 packet recovery and awake CC1101 application transport.

It also includes RSSI observation, movement settlement, bounded ESP-NOW proximity probes, median-of-three sampling, provisional CLOSE/FAR hysteresis, automatic application transport selection, ESP-NOW failure fallback to CC1101, evidence-based recovery back to ESP-NOW, peer-return proximity recovery without movement and live OLED diagnostics. Coordinated sleep/wake was physically revalidated with OLED enabled. The wire message remains eight bytes, application retries remain 300 ms / two retries, and sleep controls remain ESP-NOW.

Committed feature HEAD is `4a2d912970ebc21a978dee3a0fe810dfe9aedc3a`. The final intended heartbeat LED layer exists as uncommitted Checkpoint 9B work above that HEAD, awaiting physical validation. The five 9B files and unrelated `.vscode/extensions.json` change remain in the original feature worktree, unstaged and preserved.

`main` still intentionally does not contain the practical firmware. This continuation is a DEVLOG-only update prepared in a separate temporary `main` worktree, without checking out main in the original worktree, moving firmware changes, merging branches or altering stashes. `chore/repository-polish` remains untouched.

### Continuation Git Commits

The additional committed practical checkpoints, all verified in `feature/sleep-execution` history:

* `135e382519acb6e3cf17f4421c0363d446f48df0` — `feat: add bounded ESP-NOW proximity probing`
* `95f3f6d1e672551f3bd7c3d714ac44818e9b9f88` — `feat: add conservative proximity classification`
* `c57daf7f5ea9fe5116583775e68f9c028bde7bdf` — `feat: add automatic application transport selection`
* `5ca9beff4d7bbd1af74828d7ecc88824e6b4c5ce` — `feat: add ESP-NOW failure fallback to CC1101`
* `4a2d912970ebc21a978dee3a0fe810dfe9aedc3a` — `feat: add live OLED status and peer recovery`

Checkpoint 9B has no commit yet.

### Next Step After Travel

Physically validate Checkpoint 9B before committing it or starting battery work or another major architecture checkpoint. Begin with the simplest test:

```text
both devices awake and close
    -> normal ESP-NOW application traffic
    -> receive one NEW remote Heartbeat EVENT
    -> softer red pulse -> short gap -> stronger red pulse -> OFF
    -> serial communication continues during animation
```

Then complete the remaining bounded physical checks:

* verify the reaction in both Bubu-to-Dudu and Dudu-to-Bubu directions
* verify received EVENT behavior on both ESP-NOW and CC1101
* verify duplicate retry re-ACKs without replaying the animation
* verify a fresh new EVENT restarts an active heartbeat
* verify movement, proximity checks, OLED updates and communication continue during animation
* verify sleep cancels an active heartbeat cleanly
* verify cold/deep startup and the heartbeat for a newly recovered retained wake packet

All physical 9B checks remain pending. Resume with those checks after travel; do not begin another implementation during this documentation checkpoint.

## 2026-09-29

### Completed

Continued from the verified Checkpoint 9B firmware on `feature/sleep-execution`:

`e5331e8659d1cec029691458a5dcb0eed47cc8c7` — `feat: finalize heartbeat motion and proximity behavior`

The earlier September 27 entry stopped with uncommitted heartbeat work above `4a2d912970ebc21a978dee3a0fe810dfe9aedc3a`. Checkpoint 9B was subsequently physically tested, refined and committed. Its final behavior is summarized below before the new automatic-sleep milestone.

The remaining product gap was autonomous sleep. The coordinated handshake, transport drain, physical entry and motion-to-peer wake already existed, but normal inactivity did not yet drive the complete product sequence. This checkpoint connected real local activity to that existing architecture, then completed the final sleep display and product wake-source policy.

This entry records the September 29 engineering and physical testing. Final sequential software validation and Git closure completed after midnight on September 30. No firmware optimization, OLED-anomaly investigation or new hardware flashing was performed during closure.

### Final Shared Heartbeat Behavior — Checkpoint 9B

The WS2812B heartbeat remains a non-blocking, loop-owned double pulse. The physically accepted animation is now:

```text
first rise:   130 ms, red 0 -> 180
first fall:   130 ms, red 180 -> 0
OFF gap:       70 ms
second rise:  185 ms, red 0 -> 255
second fall:  185 ms, red 255 -> 0
total:        700 ms, ending OFF
```

A newly created normal Heartbeat EVENT requests the sender's local animation once. A newly accepted remote Heartbeat EVENT requests the receiving peer's animation once. Bubu and Dudu remain independent EVENT sources. The synchronized presentation reuses the same EVENT exchange; it does not add a new synchronization protocol.

Retries retain their message ID, payload and transport without creating another local heartbeat. Duplicate receives are acknowledged again without replaying the animation. Both ESP-NOW and CC1101 application receive paths share this behavior. Movement, settlement, proximity checks, OLED work and protocol processing continue while the LED animates. Physical sleep cancels the animation immediately and forces the LED OFF.

The accepted CLOSE automatic heartbeat interval is 2500 ms. FAR uses 4000 ms, based on `proximityClassification == FAR`, not merely on selected CC1101 transport. ESP-NOW failure fallback while classification remains CLOSE does not falsely slow the emotional cadence. The interval applies when scheduling the next normal EVENT after the current transaction completes; a classification change does not rewrite an in-flight EVENT or its retry timing.

UNKNOWN is now intentionally silent: it does not generate normal automatic Heartbeat EVENTs. A bounded startup UNKNOWN proximity check supplies its own ESP-NOW probe/reply traffic, so discovering proximity does not depend on a heartbeat that UNKNOWN suppresses. Three fresh distinct samples, median filtering and the existing 12-second absolute check timeout remain unchanged.

The synchronized ESP-NOW animation, 700 ms timing and 2500 ms CLOSE cadence passed physical acceptance. The dedicated FAR/4000 ms visual cadence check was previously waived and was not specifically physically observed. Its scheduling policy is covered by host tests; this entry does not claim a physical four-second FAR cadence demonstration.

### Bounded Motion Startup and Live OLED Status

The intermittent ADXL345 startup I2C failure was reproduced during physical testing. The bounded initialization retry recovered the same failure that previously left Motion unavailable:

```text
[Wire.cpp] requestFrom(): i2cWriteReadNonStop returned Error -1
MOTION INIT | retry 1/2
MOTION INIT | OK (DEVID=0xE5)
```

Other boots succeeded on the first attempt. Initialization permits three total attempts with 20 ms between failed attempts, for at most 40 ms of added retry waiting. It does not introduce background retries or change awake/sleep configuration, thresholds or retained startup interrupt behavior.

The new retry path also exposed an interrupt-lifecycle diagnostic: startup attempted to remove a GPIO ISR before that Motion instance had attached it. Attachment ownership is now tracked so a never-attached ISR is not removed, failed attempts remain unattached, and successful initialization attaches once. Normal pause/resume, repeated initialization and sleep restoration retain their existing ownership rules.

Awake and sleep activity thresholds remain 12 and 48. Motion remains the owner of the shared GPIO0/GPIO1 I2C initialization. The OLED's normal awake screen uses the live STATUS and MOTION fields, with no sensor polling or competing Wire initialization in Display.

These heartbeat, Motion and proximity refinements were already part of the verified `e5331e8` starting checkpoint. They were preserved while automatic sleep was added.

### Automatic Product Inactivity Policy

Normal coordinated sleep is now initiated after 35,000 ms since the last meaningful local physical activity.

Motion Activity updates `lastMeaningfulActivity` and rearms one future automatic attempt. ADXL345 Inactivity does not start another 35-second timer. Sensor inactivity and the existing one-second settlement remain movement/proximity behavior, not a second product inactivity countdown.

Automatic Heartbeats explicitly do not reset inactivity. Neither local nor received periodic Heartbeats, duplicate receipts, ACKs, retries, callbacks, probes, RSSI observations, OLED work nor transport housekeeping count as meaningful local activity.

The clock uses unsigned elapsed-time subtraction and remains safe across millis rollover. Successful cold/deep-wake runtime starts a fresh inactivity episode rather than inheriting an old timestamp.

At the local coordinator boundary:

```text
local inactivity < 35000 ms
    -> remain awake; do not initiate

local inactivity >= 35000 ms
    -> wait for existing eligibility guards
    -> automatic IDLE
    -> one coordinated sleep attempt
```

Motion must be available and movement must be READY rather than MOVING or WAITING. No proximity check may be active. Runtime, transaction, cooldown and transport-drain guards must also pass.

Temporary busy conditions defer the opportunity without consuming it or allocating another sleep request. Once an automatic attempt starts, that inactivity episode is consumed. Refusal, retry exhaustion, phase/hard timeout or physical-entry failure does not create loop-based automatic retries. Cooldown expiry alone does not rearm the policy; new genuine Motion Activity permits the next attempt.

Movement is serviced before queued controls can admit or advance sleep and again after transport drain before physical execution. Activity can cancel negotiation or revoke a pending execution decision through the existing PowerManager behavior. Sleep does not override newly observed movement.

### Participant Admission and the Staggered-Clock Race

Fresh incoming SLEEP_REQUEST admission initially required the recipient's own full 35 seconds. Physical activity clocks can be slightly different on the two boards, which exposed a policy deadlock:

```text
Bubu last activity at t=0
Dudu last activity at t=1 s

t=35 s: Bubu requests; Dudu age=34 s refuses
         Bubu's attempt is consumed; 3 s cooldown begins

t=36 s: Dudu requests; Bubu is still in cooldown and refuses
         Dudu's attempt is also consumed
```

Added `AUTOMATIC_SLEEP_PEER_GRACE_MS = 3000` for participant admission only. The threshold is derived from the 35,000 ms product timeout:

```text
local coordinator initiation: 35000 ms
fresh peer admission:         35000 - 3000 = 32000 ms
```

Only a fresh peer request may use that earlier inactivity threshold. Motion availability, movement/settlement, proximity, runtime, transaction, cooldown and transport guards still apply. Accepted participation does not rewrite the local activity clock or rearm a consumed automatic opportunity.

The grace matches the existing three-second cooldown. Slightly staggered clocks can now complete the first request rather than each consuming an attempt through reciprocal refusal. A peer that was active too recently still refuses; after it reaches its own coordinator threshold, the earlier peer can participate once its cooldown has expired.

Focused host models cover offsets of 0, 500, 1000, 2999 and 3000 ms completing on the first request. A 4000 ms offset refuses the first request, then completes the reverse request after cooldown without a second automatic attempt by either endpoint. Local 34,999/35,000 ms boundaries, peer 31,999/32,000 ms boundaries, rollover, guard refusal and absence of automatic rearming remain explicit tests.

The local coordinator threshold, retry count, ACK timeout, cooldown, deadlines and simultaneous-coordinator arbitration were not changed. No protocol message or state was added.

### Existing Coordinated Protocol and Physical Entry

The automatic policy reuses the already verified power flow:

```text
ACTIVE
    -> 35 s local inactivity and safe eligibility
    -> IDLE
    -> SLEEP_REQUEST
    -> SLEEP_READY
    -> SLEEP_COMMIT
    -> SLEEP_ACK
    -> transport drain
    -> final movement check
    -> Motion sleep arm
    -> CC1101 sleep/wake arm
    -> physical ESP32 deep sleep
```

Sleep controls remain on ESP-NOW. The packed eight-byte `Protocol::Message`, 300 ms application ACK timeout, maximum two retries, duplicate handling, phase/hard deadlines and DeviceId collision arbitration remain unchanged.

Semantic SLEEPING is still not sufficient for physical entry. Required application/control traffic, ESP-NOW TX callbacks, active RX callbacks, queued receives and CC1101 activity must drain. The one-shot execution decision and existing failure path remain responsible for bounded entry and returned/aborted sleep.

### Final Deep-Sleep OLED Presentation

The normal awake screen was previously retained after the CPU entered sleep, leaving stale ONLINE, CLOSE, ESP-NOW, ACTIVE and STILL values visible. Added a dedicated presentation-only `Display::showDeepSleep()` frame:

```text
BUBU

STATUS:
DEEP SLEEP

WAKE:
MOTION / PEER
```

Dudu displays DUDU in the identity row. The framebuffer is cleared first. The existing 6x10 font uses baselines at 10, 25, 35, 50 and 60 pixels, fitting the 128x64 SH1106 without stale awake fields.

The final frame is not drawn for IDLE, negotiation, a sent REQUEST/COMMIT, or semantic SLEEPING while transport still drains. It is written once at the physical-entry boundary after Motion preparation, CC1101 arm, wake-source setup, RTC save and final entry checks have succeeded.

Motion and transport/GDO guards are checked again after the framebuffer transfer, immediately before deep sleep. Preparation failures do not draw the frame. If activity arrives during the transfer or deep-sleep entry unexpectedly returns, normal awake rendering resumes at its next safe boundary; the old display snapshot cannot suppress that repaint.

No display delay, new display FSM, sensor polling or additional Wire initialization was added. The framebuffer transfer itself completes the final presentation. Normal awake STATUS/MOTION rendering is unchanged.

### Product Wake Sources and Bench Safety Timer

Physical testing showed that the old integration safety timer was still waking both devices approximately 30 seconds after successful sleep. The diagnostic explicitly reported `timer=30s INTEGRATION SAFETY TIMER`.

That safety mechanism has now been removed from coordinated product sleep only. The shared entry helper clears previous wake sources and enables:

* GPIO3 / ADXL345 INT1: HIGH-level Motion wake

* GPIO4 / CC1101 GDO0: HIGH-level peer wake

* combined GPIO mask: `0x18`

For coordinated product entry, timer wake is not enabled and the arm diagnostic reports `timer=OFF`. Product sleep therefore persists until a real Motion or peer GPIO wake occurs.

Diagnostic bench `x` still uses the same shared physical-entry implementation with `coordinated == false` and retains the existing 30-second safety timer. Wake setup was not duplicated.

GPIO3/GPIO4 behavior, ADXL configuration, retained CC1101 packet recovery and the existing one-shot Motion-to-peer wake mechanism were preserved. No new wake protocol was introduced.

### Physical Automatic Sleep and Wake Validation

The current firmware passed real two-board product testing:

* normal awake communication continued to work

* after approximately 35 seconds of inactivity, sleep negotiation began automatically without manual `i`, `s` or `x`

* REQUEST / READY / COMMIT / ACK completed, required transport drained, and Motion and CC1101 arm checks succeeded

* both devices entered actual ESP32 deep sleep and displayed the final DEEP SLEEP presentation

* both remained asleep beyond the old 30-second timer point, with no spontaneous product timer wake

* moving one sleeping device woke it through ADXL345 / GPIO3

* the existing CC1101 peer-wake transaction woke the other sleeping device through GPIO4

* both devices recovered and resumed normal runtime and communication

The demonstrated product chain is now:

```text
AWAKE
    -> 35 s inactivity
    -> coordinated sleep
    -> persistent deep sleep
    -> move one device
    -> local Motion wake
    -> CC1101 peer wake
    -> both awake
```

This physical evidence is separate from host fault-injection coverage. No additional firmware behavior was added after this validation to close the checkpoint.

### Known Initialization / OLED Anomaly

On one Bubu initialization, the OLED briefly showed inconsistent or stale-looking runtime values, including unexpected `RADIO: CC1101` and an odd ACTIVE/IDLE-looking presentation.

Moving Bubu caused the display to correct itself, after which normal operation continued. The observation did not prevent communication, automatic sleep, persistent sleep, Motion wake or peer wake.

This is a known unresolved issue. It has not yet been systematically reproduced, and no root cause or fix is claimed. It is non-blocking for the current verified sleep/wake milestone, but remains a candidate for later investigation before final firmware integration. No investigation or firmware change for this anomaly was attempted during checkpoint closure.

### Host Tests and Build Verification

The complete Bubu and Dudu host suites retain the existing transport, wake recovery, Motion, proximity, heartbeat and OLED regressions. Focused automatic-sleep coverage includes:

* exact local 34,999/35,000 ms initiation and peer 31,999/32,000 ms admission boundaries

* rollover-safe activity timing, background traffic exclusion and busy-state deferral

* one attempt per inactivity episode, bounded failures, unchanged cooldown and genuine-activity rearming

* participant-only grace, staggered-clock exchanges and unchanged simultaneous arbitration

* movement priority before control processing and at the final physical-execution boundary

* no premature DEEP SLEEP display, preparation-failure suppression, exactly-once rendering, post-transfer races and awake restoration

* unchanged awake OLED layout, final-frame text/bounds, no new Wire initialization, polling or delay

* coordinated GPIO3/GPIO4-only wake, no product timer wake, and retained bench timer behavior

* existing retained CC1101 wake packets, local Motion wake, one-shot peer wake and normal recovery

The first closure attempt stopped when the Bubu build reported:

```text
FileNotFoundError: .pio/build/bubu/.sconsign311.tmp
```

Bubu and Dudu PlatformIO builds had accidentally overlapped. Concurrent SCons temporary-state interference was a possible cause, not a confirmed firmware compile/link defect. No source change, staging, commit, push or DEVLOG update followed that failed validation.

The isolated Bubu retry passed without any source modification. Complete final validation was then repeated strictly sequentially, with no overlapping PlatformIO builds:

* isolated `platformio run -e bubu` retry — PASS

* `bash tests/host/run.sh` — PASS for the full Bubu and Dudu suites

* AddressSanitizer — PASS

* UndefinedBehaviorSanitizer — PASS

* `platformio run -e bubu` — PASS; completed before Dudu was started

* `platformio run -e dudu` — PASS

* `git diff --check` — PASS

* complete cumulative diff review against `e5331e8659d1cec029691458a5dcb0eed47cc8c7` — PASS

* `git diff --cached --check` and exact staged-content review — PASS

The host runner enables both sanitizers and treats compiler warnings as errors. No sanitizer diagnostics were reported. The verified firmware files remained unchanged during closure; no source fix was made merely to address the transient SCons failure.

### Current Working State

Verified firmware is committed and pushed on `feature/sleep-execution` at:

`549d3fef9ad79d340157dd60491b5d47e5b28ca3` — `feat: add automatic coordinated sleep and peer wake`

Local and origin feature refs match. Automatic sleep, persistent deep sleep, local Motion wake, CC1101 peer wake and return to normal runtime have been physically validated.

The firmware has NOT been merged or cherry-picked into `main`. Main still does not contain the new practical firmware and receives only this development record. Further optimization and debugging remain planned on the feature branch before another integration review.

The 35,000 ms local threshold, 3,000 ms participant-only grace, one-shot policy, existing protocol/retries/deadlines, thresholds 12/48, 700 ms heartbeat, classification-based cadence, UNKNOWN silence, proximity, fallback/recovery and retained wake behavior remain in the verified feature tree. The Bubu startup OLED anomaly remains open.

The original worktree remains on `feature/sleep-execution` with only the unrelated `.vscode/extensions.json` modification, unchanged and unstaged. Documentation was prepared in a separate clean `main` worktree. `chore/repository-polish` and existing stashes remain untouched.

### Problems Solved

* requiring manual commands to initiate otherwise verified coordinated sleep

* periodic Heartbeats and other background work preventing product inactivity

* repeated automatic attempts after a consumed inactivity episode

* treating temporary movement/proximity/transport busy conditions as a consumed opportunity

* slightly staggered inactivity clocks refusing each other during the existing cooldown

* allowing sleep progress to outrank newly observed local movement

* retaining misleading awake OLED values during actual deep sleep

* applying the old 30-second integration timer to coordinated product sleep

* closing the demonstrated automatic sleep -> persistent sleep -> Motion wake -> peer wake -> normal runtime chain

The startup OLED anomaly is not included as solved.

### Git Commits

The starting verified Checkpoint 9B commit was:

`e5331e8659d1cec029691458a5dcb0eed47cc8c7` — `feat: finalize heartbeat motion and proximity behavior`

The new automatic-sleep/product-sleep checkpoint was committed and pushed as:

`549d3fef9ad79d340157dd60491b5d47e5b28ca3` — `feat: add automatic coordinated sleep and peer wake`

Only these firmware/test files were included:

* `include/PowerManager.h`

* `include/CC1101WakeRecovery.h`

* `include/Display.h`

* `src/PowerManager.cpp`

* `src/CC1101WakeRecovery.cpp`

* `src/Display.cpp`

* `src/main.cpp`

* `tests/host/sleep_handshake_test.cpp`

* `tests/host/cc1101_wake_test.cpp`

* `tests/host/display_status_test.cpp`

* `tests/host/wake/esp_sleep.h`

DEVLOG and the protected editor setting were excluded from the feature commit. The feature commit was verified on `origin/feature/sleep-execution` before this documentation update.

The separate `main` documentation commit is `docs: record automatic sleep and peer wake milestone`. Its only changed file is `DEVLOG.md`; it does not merge, cherry-pick or copy firmware from the feature branch.

### Next Step

Continue optimization and testing on `feature/sleep-execution`, particularly reproduce and investigate the Bubu startup OLED anomaly if possible. Preserve the verified automatic sleep/wake chain while narrowing that observation to a reproducible case.

Then perform another final integration review before deciding whether to merge firmware into `main`. The next step is not an immediate firmware merge, a new feature or repository-polish work.

## 2026-10-01

### Completed

Prepared `integration/final-firmware` from the existing working firmware checkpoint:

`fece5af0de8cec8aed57234af49ecd44d72d4115` — `feat: add button-triggered partner heartbeat and deep-sleep wake`

That checkpoint already contained the physical-button heartbeat, GPIO5 deep-sleep wake, bounded peer-wake handoff, retained user animation and concurrent CC1101 EVENT-forwarding correction. These are prior checkpoint capabilities, summarized below from the committed `BUTTON_HEARTBEAT.md`; they were not all implemented during this October 1 session.

The integration preparation preserved the working firmware and established a current acceptance record and automated-check workflow. Subsequent work removed the manual serial bench interface while preserving automatic operation. Software validation was reported passing, but the user could not physically test the cleanup before stopping. The complete unfinished change was therefore saved in a dedicated stash instead of being committed to the firmware branch.

Session closure returned the checkout to the existing `main` branch for this development-log-only update. No firmware was merged into main, and no hardware was flashed during closure.

### Prior Working Checkpoint — Button Heartbeat and Wake

The committed button feature adds `UserHeartbeat = 2` within the existing version-1, eight-byte message format. Both devices must use firmware that understands the new event value. The physical GPIO5-to-GND button retains 30 ms press/release debounce and one bounded pending intent. Button work takes priority over periodic Heartbeats and proximity measurement after existing in-flight work drains. UNKNOWN proximity routes the request through CC1101 without fabricating a CLOSE or FAR classification.

A new received UserHeartbeat invokes the receiver's priority 700 ms double pulse once. It does not add a sender animation. Duplicate retries receive the normal receipt ACK without another pulse, including retries interleaved with background or newer user events. After the user pulse, normal background output resumes only when current CLOSE and power eligibility permit it. The existing application outbox, immutable in-flight message, 300 ms ACK timeout and two same-ID retries remain authoritative.

Coordinated sleep uses GPIO3/GPIO4 HIGH and GPIO5 LOW, combined mask `0x38`, with the product timer OFF. Earliest GPIO5 wake evidence preserves one intent even if the button is released before initialization finishes. A stable release is required before another press can be accepted. Raw LOW and pending button work block sleep, preventing repeated immediate wakeups from a held button.

After retained-radio recovery and runtime initialization, wake-origin intent uses one bounded CC1101 peer-wake episode. Only a matching peer wake ACK together with confirmed local RX readiness releases the intent into the reliable UserHeartbeat path with a fresh ID. The wake episode retains three attempts and at most one RX recovery; failure clears the held request and reports unconfirmed delivery. It does not begin another episode every loop. A later stable release and new press can request another bounded attempt. Button wake counts as local activity and rearms the normal inactivity timer.

The same checkpoint repairs incoming traffic during the synchronous peer-wake ACK wait. Valid concurrent Heartbeat/UserHeartbeat EVENTs enter an eight-packet deferred queue. Application delivery and receipt transmission wait until wake TX releases radio ownership, then use the existing consumer and same-radio ACK path. These packets do not satisfy the wake ACK or extend its deadline. Queue exhaustion stops the wake episode before consuming the next FIFO packet, preserving bounded storage and the unread packet.

A newly processed retained UserHeartbeat records one deferred visual obligation before its receipt ACK. That obligation is consumed once after LED initialization, without replaying application delivery or allocating another EVENT. These retained and concurrent delivery paths were preserved by the serial cleanup.

### Final Firmware Integration Preparation

The branch audit found no missing active subsystem requiring a merge from the historical feature branches. Existing history already supplied the current communication, Motion, display, LED, power and wake paths. Older header moves, task-based animation, sensor profiles and radio experiments were not imported over the working implementations.

Two local preparation commits were created above `fece5af`:

* `960d5f58b9c9d9f1ce06cfa6c10ff947de61bbb7` — `docs: preserve development history on final firmware branch`

* `97135b2bff2042c7f0e473ccc8f0456861d89f0d` — `ci: prepare final firmware acceptance checks`

Main's development history through `4defa2e` and `simulations/battery_indicator_v1.txt` were preserved exactly on the integration branch. The battery simulation remains historical design material, not measured battery validation. The reviewed GitHub Actions workflow was incorporated, with host tests followed by explicitly sequential Bubu and Dudu firmware builds. `FINAL_FIRMWARE_TEST.md` records the candidate acceptance sequence and open evidence.

The preparation commits did not change firmware sources, headers, host tests, PlatformIO configuration or port-selection tooling relative to `fece5af`. Closure verified that relationship and the preserved history/simulation directly from the committed trees.

GitHub Actions has not been verified for this candidate. The workflow belongs to the integration branch; publishing this DEVLOG-only main update does not publish that workflow or the feature firmware.

### Physical Radio Observations Before the Cleanup

Physical logs supplied before the serial cleanup showed successful application delivery over both ESP-NOW and CC1101, bounded fallback and recovery, and automatic return to ESP-NOW after an eligible CLOSE classification.

The user reported unexpected behavior after sequential flashing and normal behavior after resetting. Bubu also switched back after movement and settlement before its own reset. That observation matters: neither a reset requirement nor an "only after flashing" cause has been established. The startup/radio-selection behavior remains unresolved, and no investigation or fix for it was included in the serial-command task.

These observations provide useful evidence for the existing checkpoint, but do not pass the complete final acceptance checklist. Earlier button/partner animation, GPIO5-only wake (`mask=0x20`) and return to coordinated sleep (`mask=0x38`, timer OFF) remain prior physical evidence. Repeated CC1101 wake requests and subsequent user events also succeeded in the same runtime. Missing startup output during USB reconnection prevents claiming that every wake handoff detail was captured.

The exact physical `DEFERRED EVENT` overlap remains uncaptured despite deterministic host coverage of the real waiter and forwarding path. An earlier unsuccessful overlap attempt exhausted bounded wake/user retries without a captured received packet; its cause remains unresolved. The short press-and-release limitation during the final sleep-entry polling/SDK critical interval is also unchanged. No physical validation of the new serial cleanup has occurred.

### Unfinished Serial-Command Cleanup

The cleanup removes the firmware input dispatcher for:

```text
e c p x w i s a h d ?
```

Command help, command-only status/responses and the obsolete `Sleep handshake bench` startup banner are removed. The manual application-radio selector, forced-IDLE helper, heartbeat pause toggle, one-second control-delay toggle and bench-only 30-second sleep timer are removed where they were used solely by those commands. No replacement or hidden command interface and no new debug-build configuration were added.

`servicePowerTest()` previously performed both command handling and the regular `PowerManager::update(now)`. Removing the dispatcher therefore retained that power update explicitly at the same point in the normal loop, before queued control work and protocol-readiness early return. Sleep deadlines and ordinary power-state progress continue even when serial input is present.

The automatic peer-wake helper remains because Motion and button wake use it. Automatic radio selection, fallback/recovery, reliable delivery, receipt ACKs, bounded retries, duplicate suppression, physical button behavior, receiver user animation, Motion/proximity processing and OLED/LED servicing remain in the production flow. Coordinated sleep keeps its arming/drain/final-entry guards and GPIO-only wake policy. Useful automatic diagnostics remain available through serial output. Hardware wiring was unchanged.

Host tests were retained and adapted to explicit harness setup or production inputs instead of command injection. Delayed-control collision, timeout and cancellation tests still exercise the real queue/FSM, with delay injected by the host callback. Periodic scheduling is isolated by the harness where necessary; cadence tests exercise the production schedule. Former manual-selector tests now verify that serial input cannot override automatic policy or consume pending fallback decisions.

New inertness coverage compares the real loop with and without every former command character and line ending across twelve scenarios for both identities. It checks runtime state, packets, message IDs, retries, power deadlines, animation and physical input behavior, including due heartbeats, negotiation, sleep drain, pending wake work and runtime failure. The input remains unread and cannot drive firmware behavior.

### Host Tests and Build Verification

Before this closure, Codex reported the following results for the unfinished cleanup:

* complete `bash tests/host/run.sh` suite — PASS, including Bubu and Dudu and the real CC1101 wake-waiter/application-forwarding regression

* AddressSanitizer and UndefinedBehaviorSanitizer — enabled by the existing runner; no reported errors

* `pio run -e bubu -j 1` — PASS, 89.68 seconds

* `pio run -e dudu -j 1` — PASS, 83.66 seconds; started only after Bubu completed

* `git diff --check` and complete eleven-file correction review — PASS

These are the earlier cleanup validation results, not a new hardware result or a rerun on main. Physical validation of the cleanup is still pending. This closure did not rerun the firmware host suites or builds after switching to main.

Closure checked that all eleven current cleanup files matched the recorded validated content hashes before stashing. The saved stash was then compared byte-for-byte with the captured cleanup diff, and its file contents matched those same hashes. Documentation diff checks, exact staged-file review, protected-file checks and stash/ref preservation checks were performed for this documentation update. Those checks establish preservation and documentation scope; they do not add RF, USB, LED, wake or power evidence.

### Current Working State

The firmware candidate remains on `integration/final-firmware` at:

`97135b2bff2042c7f0e473ccc8f0456861d89f0d`

The uncommitted cleanup is preserved in a new stash named:

`On integration/final-firmware: wip: remove serial bench commands; physical validation pending - 2026-10-01`

Its full object ID is:

`e2472ca16d9a1329a8ca526037905540b88453dd`

Its base commit is:

`97135b2bff2042c7f0e473ccc8f0456861d89f0d`

The stash contains exactly these eleven changed files:

* `include/CC1101WakeRecovery.h`

* `include/CC1101WakeTx.h`

* `include/PowerManager.h`

* `include/RtcState.h`

* `src/CC1101WakeRecovery.cpp`

* `src/CC1101WakeTx.cpp`

* `src/PowerManager.cpp`

* `src/main.cpp`

* `tests/host/cc1101_wake_forward_test.cpp`

* `tests/host/cc1101_wake_test.cpp`

* `tests/host/sleep_handshake_test.cpp`

The cleanup is stashed, not committed or merged. The five pre-existing stash objects remain unchanged; adding the new stash shifted their numeric indices. Restore by the full object ID rather than relying on `stash@{0}`. The new stash excludes `.vscode/extensions.json` and has no staged changes in its index snapshot.

The checkout is on main. Before this update, local main and the live origin branch both matched `4defa2e46d4977a67f4e0191228c3ac3ec4fbae8`, so no synchronization was required. Main receives only this appended DEVLOG entry and still does not contain the practical feature firmware. All previous DEVLOG entries are preserved exactly.

The original `.vscode/extensions.json` modification remains outside the stash, unchanged and unstaged, with SHA-256:

`b14aaff9d2eaeb2c2d6e0893007079d33676ab6a1e8f9fc2a4bbfb706e14f846`

The integration and historical feature refs, `chore/repository-polish`, synced `sources/` files and unrelated work were preserved. This closure does not push the integration branch or create a release tag. Presentation work remains deferred.

Startup/radio-selection observations and the earlier Bubu startup/OLED anomaly remain unresolved. The uncaptured physical overlap, incomplete final acceptance sequence and cleanup physical test remain open. Battery hardware, the incoming power module, current consumption and runtime have not been validated; no battery result is inferred from the preserved simulation or USB bench operation.

### Problems Solved

* ambiguity about whether historical feature branches needed to be merged over the current working firmware

* risk of losing main's development history and battery design reference during integration preparation

* serial characters overriding automatic behavior through legacy bench commands, addressed in the stashed cleanup pending physical validation

* risk of deleting the regular power-management update together with the command handler

* host regressions depending on a production manual interface instead of explicit test setup

* risk of losing the unfinished cleanup or mixing the protected editor modification into its stash

* ambiguity about the firmware checkpoint, stash restoration target and remaining validation at the next session

The startup/radio-selection cause, physical deferred-EVENT overlap, complete candidate acceptance and battery behavior are not included as solved.

### Git Commits

The prior working firmware checkpoint is:

`fece5af0de8cec8aed57234af49ecd44d72d4115` — `feat: add button-triggered partner heartbeat and deep-sleep wake`

The local integration preparation commits are:

* `960d5f58b9c9d9f1ce06cfa6c10ff947de61bbb7` — `docs: preserve development history on final firmware branch`

* `97135b2bff2042c7f0e473ccc8f0456861d89f0d` — `ci: prepare final firmware acceptance checks`

There is no serial-cleanup firmware commit. Stash `e2472ca16d9a1329a8ca526037905540b88453dd` preserves that unfinished work above `97135b2`.

The separate main documentation commit is `docs: record final firmware preparation and pending cleanup`. Its only changed file is `DEVLOG.md`; it does not merge, cherry-pick or copy the firmware or integration workflow into main.

### Next Step

Resume the unfinished cleanup on its existing integration branch. First verify the branch/ref state and preserve the protected editor modification. Then restore the recorded stash without dropping it:

```bash
git switch integration/final-firmware
git stash apply e2472ca16d9a1329a8ca526037905540b88453dd
```

Confirm the eleven-file cleanup is restored on its recorded base and `.vscode/extensions.json` remains unchanged and unstaged. If the checkout or base has changed, review the difference before proceeding; do not use a blanket restore or discard to force the old state.

Upload matching Bubu and Dudu builds strictly sequentially, then perform one manageable physical cleanup check with paired serial logs at 115200 baud:

```text
both updated devices awake
    -> type ecpxwisahd? into the serial monitor
    -> no command response or manual behavior override
    -> press and release Bubu's physical button once
    -> Dudu receives the UserHeartbeat and plays one user pulse
    -> leave both stationary with buttons released
    -> normal coordinated sleep, mask=0x38, timer=OFF
```

Normal automatic diagnostics may continue while characters are typed. Observe the partner LED as well as the matching receipt; an ACK alone is not proof that the intended animation ran. Allow the existing 35-second inactivity policy and normal movement/proximity/transport guards to determine sleep eligibility.

Validate the cleanup physically before committing it. Retain the stash while reviewing the restored work and results. Complete remaining candidate acceptance deliberately afterward; do not treat this documentation closure as firmware integration, a verified GitHub Actions run, a release or the start of presentation work.

## 2026-10-03

### Completed

This entry closes the hardware and debugging session that began on October 2 and continued into October 3, Europe/Berlin time.

Assembled battery power for both devices and resumed the original manual-command cleanup on `integration/final-firmware`. Subsequent investigation expanded into radio/peer status, proximity, mounted-sensor sensitivity and OLED presentation. The combined experimental changes passed software checks, but physical observations exposed regressions. Those experiments were abandoned and backed up, and the original cleanup candidate was restored.

The user then requested the existing double-pulse heartbeat in FAR at a six-second repetition interval. Further work on that request was immediately stopped for this session closure. Existing uncommitted FAR edits were found and preserved separately from the restored baseline; the requested change remains pending physical acceptance. No additional firmware implementation or firmware validation was performed during this documentation-only closure.

Returned the checkout safely to main after preserving the integration work. Main receives only this appended development record; previous entries remain byte-for-byte unchanged.

### Battery Power Assembly and Physical Observations

The assembled battery supply uses:

* protected Superfire 18650 cells

* USB-C charger/protection boards

* Adafruit TPS61023 MiniBoost modules

The user measured approximately 3.9 V per cell and 4.99–5.02 V at the booster output. Both Bubu and Dudu operated from battery power.

These are user-reported physical measurements and operation, not a characterized power budget. Current consumption, battery runtime and charging under load were not validated. The earlier battery simulation remains design material and does not supply those missing measurements.

### Firmware Investigation and Abandoned Experiments

The session investigated the following reported behavior:

* CC1101 peer-status flicker

* incorrect sleeping status

* overly permissive CLOSE classification

* excessive sensitivity with the sensor mounted in the device

* OLED presentation

A combined experimental patch passed software checks. Physical testing then found excessive sensitivity and later incorrect LED behavior. A sensitivity/display correction was also attempted. These changes were subsequently abandoned; they are not completed fixes in the restored firmware.

The abandoned work remains recoverable in the existing stash:

`b0f09dadaa8c1f36935b14e9c90c1cdedd5369d7`

That stash and its restoration backups were preserved without alteration. The reported radio/status, classification, sensitivity and OLED issues remain observations requiring separate investigation. No successful resolution is inferred from the experimental host tests or builds.

### Restored Candidate and Stopped FAR Heartbeat

The restored candidate is the integration base:

`97135b2bff2042c7f0e473ccc8f0456861d89f0d`

plus the original manual-command cleanup stash:

`e2472ca16d9a1329a8ca526037905540b88453dd`

The original cleanup stash has that exact base. Closure compared the saved restoration archive against the cleanup stash: all 62 tracked files outside `.vscode/extensions.json` matched byte-for-byte. The subsequent experimental radio, peer-state, motion, proximity and OLED fixes are absent from this candidate.

Inspection of the live integration checkout found only two additional files differing from the original cleanup snapshot:

* `src/main.cpp`

* `tests/host/sleep_handshake_test.cpp`

Those differences belong to the stopped FAR-heartbeat attempt: the 6000 ms interval, FAR background eligibility, shared outgoing/incoming animation guard and associated regression changes. They were preserved in the new recovery stash and archive, not adopted as an accepted replacement for the restored candidate. Every other tracked file outside the editor modification still matched the original cleanup snapshot.

The restored baseline retains the existing 2500 ms CLOSE interval, 4000 ms FAR message interval and CLOSE-only normal background-animation condition. The requested FAR change is still the next isolated task. No explicit post-restoration physical LED acceptance result was supplied, and no physical acceptance of the stopped FAR edits is claimed.

### Host Tests and Build Verification

Restoration evidence and backups remain under:

`/Users/mohamedsellami/bubududu-restoration-backups/pre-bugfix-baseline-20261002T233708+0200`

The saved `validation-results.json`, `host-suite.log`, `build-bubu.log` and `build-dudu.log` record this order for the restored candidate:

* complete `bash tests/host/run.sh` suite — PASS

* AddressSanitizer and UndefinedBehaviorSanitizer — PASS

* Bubu PlatformIO build — PASS, 20.435 seconds

* Dudu PlatformIO build — PASS, 18.733 seconds; started after Bubu completed

Closure used that saved evidence and verified the restoration archive checksums. The firmware host suite and builds were not rerun for this DEVLOG-only task. These software results do not establish post-restoration physical LED acceptance, resolve the abandoned experiments or validate battery current, runtime or charging under load.

Documentation checks cover the append-only DEVLOG diff, preservation of all earlier entry bytes, an empty staged diff, unchanged editor content and preserved branch/stash/backup state. They do not add hardware evidence.

### Current Working State

The integration branch remains at `97135b2bff2042c7f0e473ccc8f0456861d89f0d`. All uncommitted integration work present at closure, including the original cleanup and stopped FAR-heartbeat edits, is preserved in the new stash:

`On integration/final-firmware: wip: session closure; restored cleanup and stopped FAR-heartbeat edits - 2026-10-03 Europe/Berlin`

Its full object ID is:

`683948af4c6d8a916791d4b180be00fe034df098`

The stash's complete eleven-file integration diff and all 62 tracked file contents outside the editor setting were verified against the captured worktree. Its base is `97135b2bff2042c7f0e473ccc8f0456861d89f0d`; its index snapshot contains no staged change. The seven earlier stashes remain intact. Numeric stash positions have shifted, so recovery uses full object IDs.

A separate verified archive, complete integration patch, stopped-FAR-only patch and preservation manifest are saved under:

`/Users/mohamedsellami/bubududu-restoration-backups/session-closure-20261003T004354+0200`

The archive is `integration-worktree.tar.gz`. `integration-work.patch` captures all integration changes above the base; `stopped-far-heartbeat.patch` captures only the two-file difference above the original cleanup. Existing restoration backups were retained unchanged.

The checkout is now on main at `bfe599fc3cfa697237f7dad8a777d9ff6644623c`, with only this appended `DEVLOG.md` entry and the original editor modification. `.vscode/extensions.json` was excluded from the recovery stash and archive and remains unchanged and unstaged, with SHA-256:

`b14aaff9d2eaeb2c2d6e0893007079d33676ab6a1e8f9fc2a4bbfb706e14f846`

All existing branches and stashes are preserved. Main contains no imported integration firmware. The original cleanup, post-restoration LED acceptance, FAR heartbeat and remaining candidate acceptance are not declared physically complete.

### Problems Solved

* battery supply assembly and basic battery-powered operation demonstrated on both devices

* separation of the abandoned combined experiments from the restored original cleanup candidate

* verification of the restored candidate against its exact base, cleanup stash and saved validation evidence

* preservation of stopped FAR edits without carrying firmware changes into main or losing the original baseline

The peer/status observations, classification and mounted-sensor concerns, OLED presentation, post-restoration LED acceptance, FAR heartbeat acceptance and battery power characterization remain open.

### Git Commits

No new firmware commit, push or merge resulted from this work. The original cleanup, abandoned experiments and stopped FAR edits remain recoverable as uncommitted work in their separate stashes and backups.

This closure creates no documentation commit either. Nothing was flashed, staged, committed, pushed or merged during closure. The only new main content is the unstaged DEVLOG append.

### Next Step

Proceed one change at a time. The next requested implementation is only the six-second FAR heartbeat, preserving CLOSE behavior, followed by physical testing before any further change. Do not restore the abandoned experimental fixes.

The current main DEVLOG append is uncommitted. Preserve it separately before switching branches; leave `.vscode/extensions.json` outside that stash. To recover the exact integration work stopped at this closure, use:

```bash
git stash push -m "wip: preserve 2026-10-03 DEVLOG closure" -- DEVLOG.md
git switch integration/final-firmware
git rev-parse HEAD
```

Verify HEAD is `97135b2bff2042c7f0e473ccc8f0456861d89f0d` and only the protected editor modification remains before applying:

```bash
git stash apply 683948af4c6d8a916791d4b180be00fe034df098
```

This restores the original cleanup plus the stopped FAR edits. Review the two-file FAR difference before continuing; recovery itself is not acceptance. Keep the stash rather than popping it.

To resume from the restored cleanup baseline instead, use the following apply command in place of the new recovery stash, on the same verified base with no integration changes applied:

```bash
git stash apply e2472ca16d9a1329a8ca526037905540b88453dd
```

These are alternative recovery choices; do not apply both stashes on top of one another. The stopped FAR patch remains available separately for review. If the base or worktree differs, preserve and inspect the difference before proceeding rather than forcing a restore.

For the FAR change, retain the existing double pulse's colour, brightness, fades and 700 ms duration. Six seconds is the repetition interval, using the existing scheduling mechanism. CLOSE stays at 2500 ms. Outgoing and incoming background triggers, retries and duplicates must not add or repeatedly restart FAR animations. Physical-button priority, UNKNOWN and sleep behavior must remain unchanged.

Keep OLED presentation, motion thresholds, proximity classification, radio selection, peer-state logic and reliability timeouts outside that change. After focused regression coverage and the host suite, build Bubu and Dudu sequentially. Then physically verify the FAR rhythm and preserved CLOSE/button behavior before proceeding to any other issue. No physical result is supplied by this closure.

## 2026-10-03 — Final integration and software validation

### Scope and preserved baseline

Finalized the authorized integration on `integration/final-firmware`, starting at
`2d79328697d2b03c47d54fe58fb8a5628c160394`. The actual starting
files were 2,072 lines in `src/main.cpp` and 6,295 lines in the sleep-handshake test.
The existing uncommitted work comprised 17 tracked integration files and the new
`tests/host/i2c_startup_test.cpp`; the separate `.vscode/extensions.json` change was
excluded. No applicable AGENTS.md was present in the project/ancestor directories.

The included work was inventoried before restructuring: ACTIVE-only awake power
state, evidence-gated failed-sleep recovery with deferred proximity, pending-
fallback peer recovery, the current Motion threshold, stopped-radio display
admission and Dudu I2C/OLED diagnostics, plus their regression tests and current
policy documentation. The prior `2d79328` six-second FAR/manual-command-cleanup
checkpoint was retained. No abandoned stash was applied.

Recoverable baseline and validation evidence are under:

`/Users/mohamedsellami/bubududu-restoration-backups/finalize-20261003T184817+0200`

This includes a worktree archive, file SHA-256 manifest, complete Git bundle,
working/index patches, refs and stash reflog, current-main DEVLOG copy, validation
logs/results and integration-review inventory. All earlier stashes/backups were
left intact. Protected `.vscode/extensions.json`, ignored `c_cpp_properties.json`
and `launch.json` were preserved byte-for-byte and kept outside commits.

Fetched main was `325a9d26df0475341e54fcfa426e3dca24d4cd56`. Its newer development
record was inspected before integrating. The sole merge conflict was DEVLOG's
append: the integration version was verified to be an exact prefix of main's
version. The complete main version was retained, with this entry appended after
it. Earlier historical claims and instructions were not rewritten.

### Included functionality and structure

`src/main.cpp` now contains only the readable startup/ordered cooperative loop.
Five modules in `src/app` own their private state: `RadioRuntime` (queues, message
IDs, retries/dedup, transport/fallback and RTC packet history), `MotionRuntime`
(sensor/movement/proximity), `ButtonRuntime` (debounce and wake/user intent),
`SleepRuntime` (boot routing, inactivity, drain and physical entry), and
`Presentation` (display, diagnostics and LED/retained/FAR animation). No new task,
duplicate state machine or global-state header was introduced.

Preserved ordering includes earliest wake capture, retained FIFO recovery before
I2C, Motion before OLED before LED, RX queue before callback registration, motion
and button activity before sleep, received ACKs before retries, button work before
periodic events, and bounded diagnostic work at the end of the loop. Existing
radio transaction IDs, receipt matching, retries, duplicate handling, deferred
wake EVENT forwarding/FIFO safety, inactivity safeguards, FAR/CLOSE and user
animation behavior remain in their original production paths.

The large test was split into 15 responsibility suites under `tests/host/suites`.
All 92 original test functions and their 1,558 assertions were retained unchanged,
including parameterized CLOSE/FAR paths and rollover/failure coverage. Shared
production inclusion, hardware doubles, LED observation, reset and scenario
helpers live in `tests/host/fixtures`. Each suite runs as its own process for
Bubu and Dudu; each case also resets its fixture. RTC is cleared between cases,
while intentionally retained during simulated reboots inside a case. The real
CC1101 driver/wake-waiter/application-forwarding tests remain in the complete
runner, alongside Motion, OLED, I2C, RTC and ESP-NOW driver coverage.

Removed syntax-narrating/tutorial comments and repetitive banners. Electrical,
register, timing, callback ownership, retained-data, protocol and failure-handling
reasoning was preserved. Executable token comparisons verified that comment and
format cleanup did not change code; diagnostic strings remain equivalent.
Current architecture, button, README and testing documentation now match the
modules and suites. The dated acceptance and sleep-handshake records retain their
historical content, with a current acceptance addendum identifying changed policy.

CI uses GitHub Actions on pull requests and branch pushes, including integration
and main. It checks the relevant diff, calls the complete host runner with ASan
and UBSan (fatal findings), builds Bubu then Dudu with `-j 1`, and verifies tracked
files remain unchanged. PlatformIO Core 6.2.0, SCons 4.41101.0, Python 3.11.14,
Espressif32 7.1.2, Arduino 2.0.17/IDF 4.4.7 packages, the RISC-V toolchain, esptool,
filesystem tools, NeoPixel 1.15.5 and U8g2 2.36.18 use the verified pins recorded in
configuration/testing docs. GitHub Actions references are verified commit pins;
CI selects Clang 18 on Ubuntu 24.04. The hosted runner image can still receive
updates; this is not a claim of a hermetic, bit-identical build environment.

`BUBUDUDU_BUILD_ONLY=1` skips USB discovery and `ports.ini` updates. A host test
executes the real port-selection script with USB access forbidden, validates
board-free build targets and rejects upload/monitor targets. Normal local port
selection remains available when that variable is unset. No device was flashed,
no release/tag was created, and no backup was deleted.

### Commit checkpoints and validation

- `51dee39` — preserve the existing behavioral integration and diagnostics.
- `b5858a3` — merge current main history, retaining all of its DEVLOG content.
- `23d5684` — extract the five orchestration modules, with host and both firmware builds passing.
- `d1e86f9` — split the production-loop suites with shared isolated fixtures.
- `6753b3c` — current architecture/testing documentation and comment cleanup.
- `9d61eaf` — pinned CI, complete shared runner and build-only port guard.
- `3e54d66` — release host receive queues at the simulated reboot boundary.

Before restructuring, the original complete host suite passed with ASan/UBSan
(13.11 s), followed by Bubu (76.87 s) then Dudu (76.02 s), both using `-j 1`.
The extracted modules passed the original suite and separate-translation-unit
firmware builds; the split suite passed for both identities. Final local validation passed: complete host suite 37.55 s, then Bubu 83.16 s
and Dudu 86.58 s, with build-only mode and `-j 1`. After the host-lifecycle fix
below, the entire host suite passed again and both sequential incremental
firmware builds passed (Bubu 2.50 s, Dudu 2.07 s).

The first live push/PR checks at `9d61eaf` failed in Linux LeakSanitizer: simulated
reboots in the host harness could overwrite an allocated RX queue. This was not
reported by the local macOS sanitizer run. The failure was retained in the
validation evidence, including [PR CI run 37140057164](https://github.com/1m2s/BubuDudu/actions/runs/37140057164).
The shared host `setup()` wrapper now releases the previous RAM queue before
calling the unchanged production setup. This models actual reboot ownership,
keeps every original test body/assertion and leaves sanitizers enabled. No
firmware behavior or test selection was changed to make this check pass.

The complete integration diff was reviewed against current main, including the
DEVLOG conflict resolution. Before this DEVLOG append, the 91-path implementation/documentation/test/build
inventory excluded protected settings; 45 paths remained byte-identical to the
recoverable starting worktree. All original integration capabilities were kept;
no unresolved radio/OLED repair was bundled with the structural work.

### Verified remote CI and merge handoff

[PR #1](https://github.com/1m2s/BubuDudu/pull/1) contains the final integration and
reviewable checkpoints. GitHub Actions [push run 37140223488](https://github.com/1m2s/BubuDudu/actions/runs/37140223488)
passed on `3e54d66ba81147f792b7ee73ca891367969f9cb7`: the complete Linux Clang
18.1.3 host suite with ASan/UBSan (including LeakSanitizer), whitespace checks,
Bubu then Dudu `-j 1` builds from the pinned packages without connected boards,
and the tracked-file clean-tree check. This is live service evidence, not YAML
inspection alone.

This DEVLOG append is committed and pushed as `docs: record final integration
checkpoints and verified CI`. It changes documentation only after the validated
code checkpoint. Its final push/PR checks must pass before the authorized merge;
remote main and the resulting main CI are then verified separately. The task's
final report supplies that resulting main SHA and live run link. At the initial
repository audit, main had no branch protection/rules or required review; those
requirements are rechecked at merge time. No check or review requirement is
bypassed.

### Hardware evidence and remaining issues

No new physical experiment was performed in this finalization. Earlier DEVLOG
observations (including battery assembly and prior sleep/wake/button milestones)
remain historical hardware evidence; host doubles and firmware compilation do
not add physical passes. Existing FAR/CLOSE and button behavior was preserved,
without claiming new post-refactor physical acceptance.

- The initial `NOT_IN_RX` cause is unresolved; no root cause is established here.
- Persistent CC1101 `Stopped` remains a known defect/limitation. The awake driver
  still stops after its bounded failure path; no permanent radio repair was added.
- Rare failed-sleep hardware recovery remains unverified. New-event/matching-ACK
  evidence gating and deferred proximity are covered deterministically on host;
  that does not prove recovery of the reported intermittent hardware case.
- OLED visibility remains unresolved. Dudu address/decision/attempt diagnostics
  and stopped-radio redraw eligibility are preserved; ACKs, initialization return
  values or attempted framebuffer transfers do not prove a visible panel.
- Awake Motion threshold 10 (0.625 g) remains the preserved trial value, with
  physical validation pending; sleep threshold 48 (3 g) and existing settling
  timing were unchanged.
- Exact physical deferred-traffic overlap, the short button pulse window at final
  sleep entry, proximity calibration, and battery current/runtime/charging-under-
  load evidence remain open. The dormant older CC1101 driver's documented receive
  bound concern is outside the active application path and was not repaired.

Merging this integration is a software checkpoint, not a fully hardware-validated
release.

## 2026-10-03 — Main checkpoint and remaining acceptance work

### Completed today

The final integration described above is now merged into `main`. Local history
confirms PR #1 merged at `3f6ae82`, followed by documentation/evidence commit
`5ce0ec1`. The earlier merge-handoff wording is historical; this entry records
the resulting main checkpoint without rewriting earlier development history.

The shared firmware now has the five runtime modules, split host regression
suites and pinned automated host/build checks documented in the preceding
entry. The six-second FAR heartbeat and preserved CLOSE/button behavior are
part of this software checkpoint. No new feature or firmware repair was made
during this closing review.

The presentation pass refreshed `README.md`, `REQUIREMENTS.md` and
`INTERFACES.md`, and added three selected images with provenance under
`docs/images/`: a historical CC1101 SPI capture, an I2C debugging capture and a
Falstad battery-indicator simulation. These are useful existing evidence, but
they are not new physical acceptance results. The README already links to the
architecture, host testing, acceptance record and development log.

### Current working state and validation

The checkout was already on `main` at `5ce0ec1` when this update began. The
preceding entry records passing local host checks, sanitizer checks, both
firmware builds and the verified integration CI run. Those results remain
software evidence for their recorded checkpoints; no firmware tests, builds,
uploads or physical experiments were repeated for this DEVLOG-only update.

The complete current firmware still needs a fresh physical acceptance run on
both devices. Cases 1–10 in `FINAL_FIRMWARE_TEST.md` remain Pending. Earlier
hardware milestones do not establish a pass for the integrated firmware now
on main. The acceptance document's dated October 1 baseline/upload guidance
must not be mistaken for today's firmware version.

### Known issues and remaining scope

- Initial `NOT_IN_RX` has no established root cause; persistent CC1101
  `Stopped` remains unresolved.
- Rare failed-sleep recovery has host coverage for the intended behavior but
  still lacks complete verification of the reported hardware failure case.
- OLED visibility remains unresolved/unverified. Initialization, address ACKs
  and attempted transfers alone do not establish a readable screen.
- The awake motion threshold of 0.625 g remains a trial setting requiring
  physical validation. Proximity classification is not calibrated distance.
- Gesture detection and measured battery current/runtime characterization
  remain unfinished. Battery assembly and earlier battery-powered operation
  do not establish runtime or charging-under-load performance.
- Historical `RadioTask` and `CC1101Radio` remain dormant in the source tree.
  The older receive routine's documented FIFO-length-bound concern remains;
  inspect dependencies before removing this code or ever re-enabling it.
- Exact physical deferred-traffic overlap and the short button-pulse window
  around final sleep entry remain evidence/behavior limitations.

The review identified physical verification as the highest-priority remaining
work. Further portfolio improvements can include a finished-device photo,
heartbeat/OLED demonstration, an architecture graphic and measured power
results. Existing historical bus screenshots should retain their limited
claims. Subjective project ratings are not validation evidence.

### Files, commits and hardware

- This closing update changes only `DEVLOG.md` on `main`; earlier entries are
  preserved. The existing `.vscode/extensions.json` modification is unrelated
  and remains outside this update.
- Today's integration commit sequence and its software checks are listed in
  the preceding entry. Main additionally records merge `3f6ae82` and
  documentation/evidence commit `5ce0ec1`.
- No wiring, component, battery configuration or flashed firmware changed
  during this closing update. No additional hardware problem is declared
  solved by the repository merge or documentation work.

### Exact next step

Resume physical acceptance with case 1 only: both devices boot, Motion
initializes and settles, and both OLEDs visibly show live status. First record
the source commit and confirm which matching Bubu/Dudu builds are actually
installed; reconcile the historical acceptance instructions with that version
before testing. Then capture paired serial logs and direct observations of
both screens using the existing verified bench wiring and power arrangement.
Record pass, fail or missing evidence explicitly before moving to case 2 or
making another firmware change.

## 2026-10-04 — Demonstration freeze and reported transition failures

### Completed and current state

Local main and the live GitHub main head were checked at
`ecd9d4fb9920d876e0acea3a3579429e222af718`. The user then reported flashing both
devices and testing them. Upload transcripts and paired serial logs were not
supplied; the installed-source attribution follows that report. No host test or
firmware build was rerun for these documentation changes.

The user froze further firmware development ahead of the October 5 professor
demonstration. Both devices work partially: LEDs and motion were reported
working, sleep was reported functioning, and CC1101 peer wake was described as
reliable nearby, including a successful wake with a door between the devices.
There are no trial counts or measured range/reliability claims.

The main reported failure is proximity/radio transition behavior. After physical
separation and return, the other device can retain its old classification and
stay on CC1101 until it is moved. One simultaneous observation was Dudu at
FAR / CC1101 and Bubu at CLOSE / ESP-NOW with a fast heartbeat. The user described
transitions as frequently failing; no numerical failure rate was measured.

### Interpretation and acceptance

Source inspection found independent local classifications, samples gated by an
active check, event-triggered checks without general periodic refresh, no local
check triggered merely by answering a peer probe, and retention of the last
classification after timeout/cancellation. This is consistent with the observed
movement-dependent refresh, but paired traces are needed to establish the exact
failed path. No root cause or firmware repair is declared complete.

The latest observation section in `FINAL_FIRMWARE_TEST.md` records these failures
and evidence limits. Cases 2 and 5 have reported transition failures; the full
procedures remain incomplete. No whole acceptance case has been promoted to
PASS. Door-separated wake does not prove ESP-NOW was unavailable or demonstrate
automatic radio fallback. Earlier radio startup/Stopped, OLED, rare sleep-entry,
brief-button-pulse, calibration and power-measurement issues remain open.

### Files, commits and hardware

- Updated `README.md`, `FINAL_FIRMWARE_TEST.md` and this appended DEVLOG entry.
- Firmware, build settings and tests are unchanged; the tested firmware source
  remains `ecd9d4f` despite this later documentation checkpoint.
- The user performed the reported uploads; the agent performed no upload or
  hardware operation. No wiring or power-configuration change was reported.
- The unrelated `.vscode/extensions.json` modification remains excluded.
- Documentation checkpoint: `docs: record demo observations and transition limitations`.
  No bug fix, release tag or fully accepted release is implied.

### Exact next step

Keep the firmware frozen. Repeat the same previously successful sleep/peer-wake
demonstration a small counted number of times and record the local trigger,
power arrangement, successes/failures and whether any reset was needed. Use that
bounded result to support tomorrow's demonstration, with proximity/automatic
radio transitions explicitly presented as unresolved.
