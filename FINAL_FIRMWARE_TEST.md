# Final results and known problems

## v1 closure — 4 October 2026

v1 is a partly working prototype. Development and device testing are paused.
The full two-device test plan was not completed. The
[README](README.md#which-code-was-tested) identifies the flashed code and software checks.

## Why testing was delayed

For much of development, 18650 batteries and a suitable 5 V DC-DC boost module
were unavailable locally, and online delivery would not have arrived in time.
Without that portable power setup, I could not carry out realistic, repeated
distance-and-return tests, including FAR/CLOSE transitions, radio fallback and
recovery. Both devices ran from batteries on 3 October, but there was little
time left to investigate the failures and repeat the tests before the pause.

I have ideas for fixes, but the university semester is starting and I need to
prioritize my studies. The fallback and recovery behavior remains unvalidated
in the full two-device test plan. The [reflection](docs/V1_REFLECTION.md#why-physical-validation-came-late)
records the lesson about planning power hardware and physical tests early.

## Evidence levels

| Label | Meaning |
| --- | --- |
| **Observed** | Something I saw or measured on the devices. |
| **Host-tested** | Checked on a computer with hardware substitutes, or built successfully. |
| **Source-only** | Read from the code or a design/simulation. |
| **Not done** | Still missing or unfinished. |

A demo shows one result. It does not prove every planned test passed.

## October 4 demonstration checkpoint

I flashed both devices from the code listed in the README. I did not save upload
logs or paired serial logs, count trials or measure range/timing. The power source
and wake trigger were not recorded for each demo.

| What I tried | What I saw |
| --- | --- |
| Move one device away and bring it back | Very unreliable. The stationary device often stayed on CC1101 until I moved it. |
| Compare both screens | Dudu showed FAR / CC1101 while Bubu showed CLOSE / ESP-NOW and a fast heartbeat. |
| LED output and motion | Both worked in informal use. I did not measure the exact timing. |
| Sleep and peer wake | Worked nearby, including once through a closed door. I did not measure a success rate. |

A door does not prove ESP-NOW had failed, so that demo does not establish automatic
fallback. The proximity failures relate to cases 2 and 5 of the
[old test plan](docs/history/2026-10-01-acceptance-prep.md#physical-acceptance-sequence).
Sleep/wake demos cover parts of cases 7–9, not complete passes.

### What the code suggests

Each device keeps its own proximity result. It takes signal-strength samples
only during a check. Movement, startup and some partner/recovery events can
start checks, but known results have no regular refresh. Answering a partner's
probe does not start a local check. A timeout keeps the old result.

That could explain the stationary device's stale state. It is not a confirmed
diagnosis of every failure. Signal-strength calibration and refreshing old
results are separate problems. [Architecture](ARCHITECTURE.md) explains the path.

## Remaining limitations

- **Observed:** proximity/radio switching failures, earlier `NOT_IN_RX` and
  `Stopped` radio errors, occasional sleep failures and OLED issues.
- **Host-tested:** reply/retry, button, sleep and recovery cases pass in the
  software model. [Test limits](tests/host/README.md#limits) explain what it misses.
- **Source-only:** a very short button press between the last sleep checks can
  be missed. Some [old messages](ARCHITECTURE.md#legacy-names-and-visible-diagnostics)
  are misleading. The unused `CC1101Radio` driver lacks a 64-byte FIFO read check
  and ignores some receive-restart failures; review it before reuse.
- **Not done:** full button/sleep/wake tests, controlled radio fallback, hardware
  logs of overlapping wake traffic, reliable OLED checks and rare sleep recovery.
- **Not done:** motion/proximity calibration, current draw, battery life and
  charging-under-load measurements. Motion thresholds remain 0.625 g awake and
  3 g asleep.
- **Not done:** gestures, battery readings, extra UI features, MOSFET switching
  and a physical comparator battery indicator.

On [3 October](DEVLOG.md#2026-10-03), both devices ran from batteries. I measured
about 3.9 V per cell and 4.99–5.02 V at the booster. The Falstad battery indicator
is only a simulation; there are no current or runtime measurements.

[Old test records](docs/history/README.md) · [Cleanup checklist](docs/V1_AUDIT_RESOLUTION.md) ·
[Future restart](docs/V1_REFLECTION.md#saved-restart-point)
