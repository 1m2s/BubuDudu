# Builds and computer tests

Run the existing tests from the repository root:

```sh
CXX=clang++ bash tests/host/run.sh
```

You need Bash, Python 3 and a C++11 compiler. The runner works on macOS Bash 3.2
and Linux. It enables AddressSanitizer and UndefinedBehaviorSanitizer to catch
memory errors and invalid operations, treats warnings as errors and stops on
failures. Test programs go in the temporary directory printed at the end.
GitHub Actions uses the same runner with `clang++-18`.

## Production-loop suites

There are **15 suites and 92 cases**, run for both Bubu and Dudu.
CLOSE/FAR cases test both results. The separate driver tests below are additional.

| Suite | Checks |
| --- | --- |
| `sleep_fsm` | Sleep agreement, old/duplicate messages, simultaneous requests and deadlines |
| `protocol_delivery` | Receive queue, radio choice, matching replies and retries |
| `sleep_execution` | Finishing radio work, setup failures and sleep entry order |
| `boot_wake` | Saved history, wake reason, partner wake and saved LED requests |
| `automatic_sleep` | Inactivity, movement, input priority and two different clocks |
| `failed_sleep_recovery` | Fresh event/reply after failed sleep, delayed checks and recovery |
| `motion_runtime` | Movement, settling, cancelled checks and timer wraparound |
| `proximity_measurement` | Fresh samples, probes, deadlines and CLOSE/FAR results |
| `proximity_startup` | UNKNOWN startup, first contact, timeouts and repeated checks |
| `radio_selection` | Heartbeat timing, fresh results, radio choice and retries |
| `peer_recovery` | Failed ESP-NOW sends, waiting fallback and partner return |
| `button_runtime` | Button bounce, wake, failures, priority and receiver animation |
| `display_runtime` | Startup, screen values, radio errors and sleep screen |
| `led_runtime` | Pulse shape, shared FAR timing, overlap and sleep cancellation |
| `serial_input` | Removed serial commands stay inactive |

The tests run real `setup()`/`loop()` and application code, but read and set its
internal state. Renaming internals can therefore break tests.
`runtime_doubles.h` replaces `ESPNowRadio`, `CC1101SleepArm`,
`CC1101WakeRecovery`, `CC1101WakeTx`, `Motion` and `Display` with controlled test
versions. GPIO, time, queues and peripheral libraries also use substitutes.
PowerManager, RTC logic and the LED state machine run their real code.

Each case starts clean. Simulated reboots keep RTC data but reset normal memory
and free the old receive queue. Shared helpers live in `fixtures/`.

## Driver tests

- Both identities: `cc1101_wake`, `cc1101_wake_tx`, `cc1101_wake_forward`,
  `espnow_drain` and `i2c_startup`.
- Shared tests: `cc1101_sleep_arm`, `rtc_state`, `motion_sleep`, `display_status`.
- Python: `port_selection_test.py` checks that build-only mode never accesses
  USB or writes port settings, and rejects upload/monitor requests.

These run the real drivers against fake SPI, I²C and GPIO responses. CC1101
cases include unread packets, full queues and receive-restart failures.

## Limits

The loop tests run one device with a scripted partner. They do not run two
independent devices where only one moves away and returns.
`testKnownHeartbeatRechecks` even expects a timed-out check to keep the old
proximity result. Passing it proves the code follows that rule, not that the
rule works well on the devices. See [physical results](../../FINAL_FIRMWARE_TEST.md).

Computer tests cannot prove radio range, sensor calibration, visible screen/LED
output or battery life.

## Reproducible build inputs and CI

Use **PlatformIO Core 6.2.0**. [platformio.ini](../../platformio.ini) fixes the
platform, toolchain and library versions, including Espressif32 7.1.2,
Arduino 2.0.17 / IDF 4.4.7, NeoPixel 1.15.5 and U8g2 2.36.18.
The [CI workflow](../../.github/workflows/firmware-ci.yml) also fixes Python,
SCons and action versions; its Ubuntu 24.04 runner still receives updates.

Run the builds one after the other:

```sh
BUBUDUDU_BUILD_ONLY=1 pio run -e bubu -j 1
BUBUDUDU_BUILD_ONLY=1 pio run -e dudu -j 1
git diff --check
```

Build-only mode does not access boards or update `.pio/ports.ini`. That ignored
file is optional on a fresh checkout. CI runs the tests, builds Bubu then Dudu,
and checks that tracked files stay unchanged. See the
[verified v1 run](../../README.md#which-code-was-tested) and
[upload/monitor guide](../../INTERFACES.md#upload-and-serial-monitor).
