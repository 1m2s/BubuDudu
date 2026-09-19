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
