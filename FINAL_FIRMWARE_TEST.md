# v1 final findings and acceptance record

## v1 closure — 4 October 2026

**Partially working integrated prototype; development paused.** I am closing
this development period with incomplete two-device acceptance. No whole
acceptance case is marked PASS. Physical testing and firmware development are
over for v1; remaining investigations belong to a possible v2.
The [README version map](README.md#which-code-was-tested) identifies the uploaded
source, release tag and software checks.

## Evidence levels

| Label | Meaning |
| --- | --- |
| **Observed** | Behavior or measurements I recorded during physical use. Conditions and missing measurements are stated with the observation. |
| **Host-tested** | Software exercised on a computer with simulated hardware or a successful firmware build. It does not establish physical behavior. |
| **Source-only** | Behavior inferred from source inspection or a design/simulation record, without a corresponding physical acceptance result. |
| **Not done** | A test, measurement or feature remains incomplete. |

A demonstration is an observation, not a full acceptance pass. A full pass would
need the complete case, matching firmware, stated conditions and recorded results.

## October 4 demonstration checkpoint

I flashed both devices from the source identified in the README. I did not save
upload logs, and the firmware has no version readback. The observations below
come from informal use, without paired serial logs, trial counts, measured
separation or timing; I did not record the exact power source and wake trigger
for each episode.

| Observation | Expected behavior | Observed result |
| --- | --- | --- |
| Devices separate and return | Useful proximity/radio state on both devices without moving the stationary partner. | Extremely unreliable. The stationary partner often stays on CC1101 until I move it. |
| Simultaneous state | Each device's display and heartbeat reflect useful current local state. | Dudu showed FAR / CC1101 while Bubu showed CLOSE / ESP-NOW and a fast heartbeat. |
| LED and motion | Visible LED output and response to movement. | Both worked in informal use. I did not separately measure the pulse or settling timing. |
| Coordinated sleep and peer wake | A local wake can wake the sleeping peer. | Sleep and nearby CC1101 peer wake worked, including one wake through a closed door. I did not count trials or capture matching ACK logs. |

The proximity failures affect cases 2 and 5 of the
[archived acceptance plan](docs/history/2026-10-01-acceptance-prep.md#physical-acceptance-sequence).
Sleep/wake observations provide partial evidence for cases 7–9. None of those
procedures was completed as a whole-case pass. A door does not establish that
ESP-NOW was unavailable, so that wake does not demonstrate automatic fallback.

### Source interpretation, separate from physical evidence

**Source-only:** each device stores its own classification.
`MotionRuntime::sampleProximity()` accepts samples only during an active check.
Startup, local movement followed by settling, certain peer-contact/recovery
events and failed-sleep recovery can trigger checks. There is no general
periodic refresh of a known classification. Answering a peer's probe does not
start a local check. Timeout or cancellation keeps the last completed result;
it does not expire it to UNKNOWN.

`RadioRuntime::serviceAutomaticTransportSelection()` uses fresh local
classification completion to request a transport change, subject to fallback
and busy-state guards. This is consistent with the stationary device retaining
old state while its moved partner updates. It is a plausible explanation,
not a confirmed diagnosis of every failure. RSSI calibration and state freshness
are separate questions; brief differences between local estimates are expected,
whereas the persistent stale behavior is a product limitation.

## Remaining limitations

| Evidence | Limitation |
| --- | --- |
| Observed | Unreliable proximity and radio switching; stationary-peer stale state. Earlier initial `NOT_IN_RX`, persistent CC1101 `Stopped`, intermittent failed-sleep behavior and OLED anomalies remain unresolved. |
| Host-tested | Modeled recovery, ACK/retry, button, sleep and retained-packet paths pass software checks. The [test guide](tests/host/README.md#limits) explains the single-device model, test substitutes and retained-classification policy. |
| Source-only | A complete button press/release between final entry polls can be missed. Legacy diagnostic text omits button wake or refers to simulated sleep; the [architecture](ARCHITECTURE.md#legacy-names-and-visible-diagnostics) explains the actual behavior. |
| Source-only | Dormant `RadioTask`/`CC1101Radio` are not started. The older driver lacks an explicit 64-byte FIFO read bound and ignores some RX-restart results; it must be audited before reuse. |
| Not done | Full button/sleep/wake acceptance; controlled automatic fallback under demonstrated ESP-NOW failure; physical capture of deferred-traffic overlap; reliable OLED visibility and rare failed-entry recovery. |
| Not done | Validation of the trial 0.625 g awake motion threshold, proximity calibration, current consumption, battery runtime and charging under load. The sleep motion threshold remains 3 g. |
| Not done | Gestures, battery telemetry, additional UI fields/animations, MOSFET power gating and an assembled comparator battery indicator. |

Battery assembly and basic operation are **Observed** in the
[3 October record](DEVLOG.md#2026-10-03), including approximately 3.9 V per cell
and 4.99–5.02 V at the booster output. The Falstad battery indicator remains a
simulation. Neither supplies the missing current/runtime measurements.

## Historical records and future work

The [October 1 preparation](docs/history/2026-10-01-acceptance-prep.md) and
[September simulated-sleep procedure](docs/history/2026-09-23-sleep-handshake-procedure.md)
are archived. Their commands, counts, dependency warnings and policy descriptions
belong to older checkpoints. Current [interfaces](INTERFACES.md),
[architecture](ARCHITECTURE.md) and [requirements](REQUIREMENTS.md) take precedence.

The [audit disposition](docs/V1_AUDIT_RESOLUTION.md) records what was corrected
without changing firmware behavior and what requires future code/hardware work.
The [saved restart point](docs/V1_REFLECTION.md#saved-restart-point) begins with
the stationary-peer return problem. No further physical test is required for
this closure.
