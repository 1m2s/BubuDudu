# Host tests and firmware builds

From the repository root, run the complete suite with Bash and a C++11 compiler:

```sh
CXX=clang++ bash tests/host/run.sh
```

The runner enables AddressSanitizer and UndefinedBehaviorSanitizer, treats warnings
as errors and stops on sanitizer findings. These tools detect invalid memory
access and undefined program operations in the exercised paths. The runner
supports macOS Bash 3.2 and Linux Bash. Binaries are saved in the temporary
directory printed at completion.
Python 3 runs the port-selection guard test. GitHub Actions uses the same runner
with `clang++-18`, with no separate CI test selection.

## Production-loop suites

Every `suites/*_test.cpp` is discovered and run for **both identities**. There are
15 suites containing 92 cases, each run for both identities. Parameterized
CLOSE/FAR cases exercise both classifications. Standalone suites below are
additional to that count.

| Suite | Coverage |
| --- | --- |
| `sleep_fsm` | Semantic agreement, stale/duplicate controls, collisions, hard/phase deadlines, one-shot decisions and rollover |
| `protocol_delivery` | Actual RX queue/loop, transport routing, ACK matching, same-ID retries and awake wake service |
| `sleep_execution` | Drain, receipt gates, Motion/radio arm failure, physical-entry ordering and abort cleanup |
| `boot_wake` | RTC restart/dedup, boot routing, Motion initialization, peer-wake guards and retained user animation |
| `automatic_sleep` | Inactivity/activity bookkeeping, deferral, admission, staggered clocks, collisions and motion priority |
| `failed_sleep_recovery` | Post-abort EVENT/ACK evidence, stale evidence rejection, deferred checks, movement/drain, fallback and supersession |
| `motion_runtime` | Awake diagnostics, settle boundaries, cancellation, isolation and rollover |
| `proximity_measurement` | Fresh/distinct samples, deadlines, correlated probes, classification and isolation |
| `proximity_startup` | UNKNOWN bootstrap/silence, first contact, first cadence, timeout and known-distance rechecks |
| `radio_selection` | Automatic cadence, fresh selection evidence, retry immutability and policy guards |
| `peer_recovery` | ESP-NOW exhaustion, pending fallback, peer return, precedence and timeout |
| `button_runtime` | Debounce, wake intent/handoff/failure, priority, transports, sleep guards and LED ownership |
| `display_runtime` | Startup, snapshots, guards, stopped-radio presentation, Dudu diagnostics and sleep frames |
| `led_runtime` | Production waveform, FAR shared cadence, overlap, rollover, protocol isolation and sleep cancellation |
| `serial_input` | Former serial commands remain inert across twelve real-loop scenarios |

`fixtures/production_app.h` includes the actual extracted `.cpp` implementations
and `main.cpp`. The tests use **white-box assertions**: they inspect and set
internal state directly, so internal renames can require test changes. Firmware
compiles those modules separately.

`runtime_doubles.h` replaces `ESPNowRadio`, `CC1101SleepArm`,
`CC1101WakeRecovery`, `CC1101WakeTx`, `Motion` and `Display` with deterministic
**test doubles**, substitutes whose responses the test controls. GPIO, time,
queues, Serial and peripheral libraries also use host substitutes. The application
loop/policy, PowerManager, RTC logic and LED state machine execute real code;
this is not a hardware simulation of the entire device.

`runtime_doubles.h` records interactions;
`runtime_fixture.h` owns reset and real-loop scheduling helpers;
`scenario_helpers.h` contains helpers used by multiple suites. Helpers used by
only one suite stay beside its cases. `observed_led.h` counts requests while
executing the real LED state machine.

Each suite is a separate process and every case begins with `runCase` reset.
RAM resets also clear pending packet bytes and history slots. RTC is invalidated
between cases, but intentionally survives simulated reboots *within* a case.
The simulated reboot wrapper releases the old RAM receive queue before running
the unchanged production `setup()`, so Linux LeakSanitizer can check every suite
without leaked host allocations. Most protocol fixtures postpone periodic
traffic without a firmware test switch;
cadence tests explicitly execute the normal production schedule.

## Driver and hardware-boundary suites

The runner also retains every standalone suite:

- Both identities: `cc1101_wake`, `cc1101_wake_tx`, `cc1101_wake_forward`,
  `espnow_drain` and `i2c_startup`.
- Identity-independent: `cc1101_sleep_arm`, `rtc_state`, `motion_sleep` and
  `display_status`.
- Python: `port_selection_test.py` executes the real PlatformIO extra script with
  USB enumeration forbidden. Build-only targets cannot discover devices or write
  `ports.ini`; accidental upload/monitor targets fail before accessing hardware.

The CC1101 tests execute the real sleep-arm, wake recovery and wake transmitter
against the SPI model. Forwarding coverage also executes the real application
consumer and LED, including queue exhaustion, preserved unread FIFO packets,
failed RX recovery and a stopped awake driver. Motion and display driver tests
execute their real implementations against Wire/U8g2/GPIO recorders.

## Limits

The production-loop harness runs one device against a scripted peer. It does
not model two independently running devices where only one moves away and
returns. `testKnownHeartbeatRechecks` in `proximity_startup_test.cpp` explicitly
expects a timed-out check to retain the old classification. Passing that case
confirms the implemented policy; it does not establish that the policy keeps
stationary-peer state useful. That is the central gap exposed by the
[physical findings](../../FINAL_FIRMWARE_TEST.md).

Real-driver suites exercise additional boundaries, but host results cannot
establish RF reliability, hardware scheduling, motion/RSSI calibration, visible
OLED/LED behavior or power consumption. No test changes are part of the v1
documentation cleanup.

## Reproducible build inputs and CI

The pinned working versions are PlatformIO Core 6.2.0, Espressif32 7.1.2,
Arduino package `4.20017.260907+sha.dcc1105b` (Arduino 2.0.17 / IDF 4.4.7),
RISC-V toolchain `8.4.0+2021r2-patch5`, esptool package `2.41100.260830`,
Adafruit NeoPixel 1.15.5 and U8g2 2.36.18. `platformio.ini` also pins the installed
filesystem tools. CI pins Python 3.11.14, Core/SCons and GitHub Actions commits;
the Ubuntu 24.04 runner image itself still receives service updates.
PlatformIO's [exact platform pins](https://docs.platformio.org/en/latest/projectconf/sections/env/options/platform/platform.html)
and [package overrides](https://docs.platformio.org/en/latest/projectconf/sections/env/options/platform/platform_packages.html)
keep the firmware on those verified inputs.

After installing PlatformIO Core 6.2.0, run builds sequentially:

```sh
BUBUDUDU_BUILD_ONLY=1 pio run -e bubu -j 1
BUBUDUDU_BUILD_ONLY=1 pio run -e dudu -j 1
git diff --check
```

`BUBUDUDU_BUILD_ONLY=1` skips automatic USB port discovery and `ports.ini` updates;
no board, USB port, upload or monitor is needed. Ordinary local port-selection
behavior remains available when the variable is unset. The ignored
`.pio/ports.ini` is optional on a clean checkout and is never supplied by CI.

The [workflow](../../.github/workflows/firmware-ci.yml) runs on pull requests and
all branch pushes, including integration and main. It checks the relevant diff,
runs the entire host suite, builds Bubu then Dudu with `-j 1`, and checks that
builds left tracked files unchanged. Download caches do not contain port settings
or compiled firmware. The [README version map](../../README.md#which-code-was-tested) links the verified
v1 CI run. [Upload and monitor instructions](../../INTERFACES.md#upload-and-serial-monitor)
describe the separate hardware workflow.
