# Final firmware acceptance

## October 4 demonstration checkpoint

**Firmware target: `main` at `ecd9d4fb9920d876e0acea3a3579429e222af718`.**
The user reports flashing both Bubu and Dudu after the October 4 source check.
The source tree was still at that commit when this report was recorded; no
upload transcripts or embedded version readback were supplied. Attribution is
therefore based on the user's upload report. Development is frozen for the
October 5 demonstration; this update changes documentation only.

These are user-reported physical observations, not an instrumented acceptance
run. Paired serial logs, trial counts, measured separation, timing, power source
and exact wake trigger were not supplied. No complete acceptance case is marked
passed by this report.

| Observation | Expected behavior | Reported result / evidence limit |
| --- | --- | --- |
| Devices move apart and return near one another | Eligible fresh proximity evidence should restore appropriate radio selection without requiring the stationary partner to be moved. | Unreliable. The other device can keep its old classification and remain on CC1101; moving it is reported to be needed for refresh. |
| Simultaneous state observation | Each device's classification and heartbeat should remain useful under the current physical conditions. | Dudu showed FAR / CC1101 while Bubu showed CLOSE / ESP-NOW and a fast heartbeat. Duration and logs were not supplied. |
| LED and motion behavior | Visible LED output and response to movement. | Reported working. Exact user-pulse timing, receiver-only behavior, sensor initialization and settling sequence were not separately recorded. |
| Sleep and CC1101 peer wake | A local wake can wake the sleeping peer. | Sleep was reported functioning and nearby peer wake described as reliable; a wake with a door between the devices also succeeded. No counted success rate or matching-ACK trace was supplied. |

**Acceptance impact:** proximity transitions and automatic return to ESP-NOW
failed in the reported use (relevant to cases 2 and 5). The full procedures for
those cases remain incomplete. Sleep/wake observations provide partial evidence
for cases 7–9, not full passes. Other unobserved requirements remain pending.
The sequence below retains its historical procedure; current observations here
take precedence over an unqualified reading of its Pending statuses.

### Source interpretation, separate from physical evidence

At the tested source checkpoint, each device stores its own classification.
`MotionRuntime::sampleProximity()` accepts classification samples only while a
check is active. Checks start on eligible startup, local movement followed by
settling, certain first-contact/peer-recovery events, or failed-sleep recovery.
There is no general periodic refresh of an already-known classification.
Responding to the peer's proximity probe does not itself start a local check.
Timeout or cancellation retains the last completed classification; it does not
expire that result to UNKNOWN. `RadioRuntime::serviceAutomaticTransportSelection()`
uses fresh local classification completion to request a selection change, with
separate fallback and busy-state guards.

This provides a plausible explanation for a stationary device retaining an old
result while its moved partner updates. It is not a confirmed diagnosis of all
reported transition failures. RSSI is received signal strength, not measured
distance; calibration quality and failure to refresh are separate questions.
A temporary difference between the two local estimates alone does not prove a
defect, but the reported persistent stale behavior is a product limitation.

The door-separated wake is useful physical evidence. A door alone does not
establish that ESP-NOW was unavailable or isolate radio propagation. The source
uses CC1101 for the peer-wake transaction; this observation does not prove
automatic ESP-NOW failure fallback, proximity accuracy or measured range.

For the demonstration, describe LED/motion/sleep/wake observations alongside
the unreliable proximity and automatic radio transitions. Preserve the tested
firmware. The next small evidence step is a count of repeat attempts of the
same previously successful sleep/peer-wake demonstration, recording trigger,
power arrangement, successes, failures and any reset needed.

> **2026-10-03 integration update.** The implementation now uses the runtime
> modules in [Architecture](ARCHITECTURE.md), and the complete split host suite
> and build workflow are described in [Testing](tests/host/README.md). Awake
> state is ACTIVE (IDLE removed); CLOSE/FAR cadence is 2500/6000 ms; former
> serial bench controls are inert. The preserved 0.625 g awake threshold is a
> trial setting. Failed-entry evidence gating, pending-fallback recovery and
> Dudu diagnostics have host coverage, not new physical acceptance.
> Initial `NOT_IN_RX`, persistent CC1101 `Stopped`, rare failed-sleep hardware
> recovery and OLED visibility remain unresolved/unverified. No device was
> flashed for this finalization. Consult the appended DEVLOG for final commit
> and remote CI evidence. The dated preparation record below is historical;
> its build/ref/stash counts and documentation-only claims describe October 1.

Prepared on 2026-10-01. Candidate branch: `integration/final-firmware`.
Firmware baseline: `fece5af0de8cec8aed57234af49ecd44d72d4115`.
Physical acceptance for this candidate is **pending**. Previous observations
remain useful evidence, but are not new passes of this acceptance sequence.

## Integration decisions

Local refs and live GitHub heads were checked before preparation:
`feature/sleep-execution` and `feature/button-heartbeat` both pointed to
`fece5af`; `main` pointed to `4defa2e`; `chore/repository-polish` to `d4b3e57`.
The candidate branch did not already exist locally or remotely.

| Source | Audit result and treatment |
| --- | --- |
| `build/platformio`, `feature/oled`, `feature/espnow`, `feature/system-integration`, `feature/power-fsm`, `feature/sleep-handshake`, `feature/adxl345-motion-wake`, `feature/button-heartbeat` | Ancestors of the verified firmware. No extra merge needed. |
| `feature/adxl345`, `feature/ws2812`, `feature/freertos` | Separate historical threshold/header moves. Current headers already live in `include/`; Motion has newer awake/sleep profiles; LED uses the current non-blocking loop-driven implementation. Do not restore old task-based animation or sensor profiles. |
| `feature/cc1101` | Its three separate commits concern the older radio driver and wake experiment. The active application uses `CC1101Bus`, `CC1101SleepArm`, `CC1101WakeRecovery` and `CC1101WakeTx`, with bounded reads/recovery and retained-packet handling. `main.cpp` does not start `RadioTask` or call `CC1101Radio`. No old-driver merge needed for active behavior. |
| `design/pin-map` | Historical design evidence. Its later text calls GPIO4 unused, which is obsolete. Use the source-verified table below. No wholesale import. |
| `main` | Preserve `DEVLOG.md` through `4defa2e` and `simulations/battery_indicator_v1.txt` exactly. The simulation is a historical design reference, not battery validation. No firmware imported from main. |
| `chore/repository-polish` | Reuse its read-only GitHub Actions host-test/build workflow, making the two firmware build steps explicitly sequential. Reuse the historical-procedure warning for `SLEEP_HANDSHAKE_TEST.md`, pointing to this current record. Other presentation documents describe old firmware `1b10419` and need later reconciliation; do not import their outdated capability claims or older DEVLOG. |

No active subsystem was found missing from the requested firmware baseline.
Firmware source, headers, host tests, PlatformIO configuration and port-selection
tool remain unchanged. Historical branches and all five existing stashes are
preserved. The unrelated `.vscode/extensions.json` modification remains unstaged
with SHA-256 `b14aaff9d2eaeb2c2d6e0893007079d33676ab6a1e8f9fc2a4bbfb706e14f846`.

## Wiring checked against source

| Signal | GPIO | Source |
| --- | --- | --- |
| Shared ADXL345/OLED SDA, SCL | 0, 1 | `include/Config.h`, Motion-owned I2C initialization |
| ADXL345 INT1 | 3, wake HIGH | `include/Config.h`, `src/CC1101WakeRecovery.cpp` |
| Button to GND, internal pull-up | 5, wake LOW | `include/Config.h`, `src/main.cpp`, `src/CC1101WakeRecovery.cpp` |
| WS2812B data | 21 | `src/LED.cpp` |
| CC1101 SCK, MOSI, MISO, CSN | 6, 7, 20, 10 | `src/CC1101Bus.h` |
| CC1101 GDO0 | 4, wake HIGH | `src/CC1101Bus.h`, `src/CC1101WakeRecovery.cpp` |

The coordinated product wake mask is `0x38`; its timer is OFF.
No wiring changes are part of this preparation.

## Software verification

Local verification on 2026-10-01:

| Check | Result |
| --- | --- |
| Full `CXX=clang++ bash tests/host/run.sh` suite | PASS, including both identities and the real wake-waiter/application-forwarding regressions |
| AddressSanitizer and UndefinedBehaviorSanitizer | Enabled by the host runner; no reported errors |
| `platformio run -e bubu` | PASS |
| `platformio run -e dudu`, started after Bubu completed | PASS |
| Firmware source/headers/tests/configuration/tools against `fece5af` | No differences |
| Main development log and simulation against `main` at `4defa2e` | Byte-for-byte identical |
| Workflow YAML syntax and acceptance-document relative links | PASS |
| `git diff --check` | PASS |

Host checks exercise production logic with simulated inputs; they do not
establish physical RF, OLED, LED, USB or power behavior. The GitHub workflow has
not been pushed or run remotely for this candidate. No new physical result is
claimed here.

## Physical acceptance sequence

Run one manageable case at a time. Before each case, agree its exact actions
and expected observations. Use matching Bubu/Dudu firmware from the same source
checkpoint, existing verified USB power and wiring, and paired serial logs at
115200 baud. Record device, firmware commit, conditions, observed result and any
missing evidence. Logs and visual observations have different jobs: a successful
display initialization return does not prove a panel is physically working.

| Case | Coverage and acceptance target | Status |
| --- | --- | --- |
| 1 | Both identities boot, Motion initializes, OLED shows live status, movement settles on both boards. | Pending |
| 2 | After settling, CLOSE classification and normal heartbeat agree with current motion/power conditions. RSSI is not a calibrated distance. | Pending |
| 3 | Button delivery both ways while stationary, moving and checking proximity; UNKNOWN can send via CC1101. One request per stable press. | Pending |
| 4 | User pulse belongs to the receiver; no additional sender pulse; receiver background beating pauses and resumes only when conditions permit. | Pending |
| 5 | CC1101 application delivery, actual ESP-NOW failure fallback, and automatic return to ESP-NOW after eligible CLOSE classification. Manual selection alone does not prove automatic fallback. | Pending |
| 6 | Simultaneous requests, bounded retries, duplicate suppression, and concurrent wake/user traffic. Record exact deferred-path overlap only if captured. | Pending |
| 7 | After normal inactivity and drain, coordinated sleep; LED off, OLED deep-sleep frame, GPIO mask `0x38`, timer OFF. | Pending |
| 8 | Motion wake and button wake, including brief press/release during startup; preserve one button request. | Pending |
| 9 | Bounded peer wake, matching wake acknowledgement and local RX readiness, fresh-ID user delivery, then eventual return to coordinated sleep. | Pending |
| 10 | Peer unavailable: bounded attempts, unconfirmed delivery on failure, cleared intent, and another attempt only after release/new stable press. | Pending |

### Case 1: boot, Motion and OLED

Use PlatformIO environments `bubu` and `dudu`. The user performs any necessary
uploads, sequentially. If both boards already contain `fece5af`, no reflash is
required for the documentation/CI-only candidate. Open both serial monitors at
115200 baud and reset both boards without pressing the application buttons.
Use the existing USB bench arrangement with the boards separated as in the
previous working tests.

Expected boot observations on each board:

- Correct `BUBU` or `DUDU` identity on its OLED.
- `BOOT | COLD`, `CC1101 ARM INIT | READY`, `MOTION INIT | OK (DEVID=0xE5)`
  and `ESP-NOW startup successful.` where startup logs are captured.
- A readable OLED with PEER, DIST, RADIO, STATUS and MOTION rows. Initial
  UNKNOWN distance is allowed before a completed proximity measurement.
- No repeated reboot or initialization-failure sequence.

Move both devices normally, one at a time, then set both down for about
5-10 seconds. Expected logs on each device include `MOTION AWAKE | MOVING`,
then `MOTION AWAKE | INACTIVITY` and `MOVEMENT | SETTLED | state=READY`.
The OLED should show movement and return to STILL; a brief SETTLING transition
may be missed between display updates. Confirm both visual behavior and logs.
Do not require a CLOSE result to pass this first case; that is case 2.

Complete the observation before extended inactivity: automatic sleep becomes
eligible after 35 seconds of local inactivity when the other conditions permit.
Do not use serial bench toggles during this case. Startup USB reconnects can
hide early output; missing lines alone are not proof of initialization failure.
If startup OLED values look inconsistent, record the values before moving the
device and note whether/when they correct themselves.

Send the paired logs and a short description of each OLED's behavior before
continuing to case 2.

## Open evidence and limitations

- Exact physical `DEFERRED EVENT` overlap has deterministic host coverage but
  no physical capture in the supplied checkpoint.
- One unsuccessful overlap attempt exhausted wake/user retries without a
  captured received packet. Its cause is unresolved; no forwarding regression
  is established by that observation.
- USB reconnection can hide early deep-wake handoff logs.
- The documented very short button pulse limitation around final sleep entry
  remains; see `BUTTON_HEARTBEAT.md`.
- The earlier Bubu startup OLED anomaly in main's DEVLOG has no established
  root cause or reproduced fix; explicitly observe it in case 1.
- Earlier nearby-radio observations do not establish a reproducible selection
  bug. Judge fresh evidence from paired logs and current proximity state.
- Dormant `CC1101Radio::receivePacket()` still lacks an explicit 64-byte bound
  before its FIFO read and ignores some RX-restart results. It is not called by
  this runtime. Re-audit before ever re-enabling that historical driver.
- The startup banner still contains obsolete simulated-sleep wording; actual
  coordinated sleep is real. The older root README/design sketches also need
  the later presentation pass; use this record for candidate status.
- Dependencies are not fully pinned. A locally successful build and the added
  workflow do not establish a successful GitHub Actions run.
- Batteries, the incoming power module, current consumption and runtime are
  unvalidated and require a separate measured hardware step.

No merge to main, final-branch push, release tag or agent-performed flash is
authorized by this preparation. Review all physical results and limitations
before deciding whether to merge. Repository presentation and real evidence
archiving follow firmware acceptance.
