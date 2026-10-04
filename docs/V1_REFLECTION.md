# v1 reflection and possible v2

**Closed development period: 4 October 2026 (Europe/Berlin).** v1 is a partially
working integrated prototype. Development is paused until free time is available,
possibly during a holiday; no v2 date is promised. The [final physical findings](../FINAL_FIRMWARE_TEST.md)
are the acceptance record. Calling this milestone v1 does not convert missing
evidence into passes, and no further physical tests are required to close it.

## Learning context

I am an Electrical Engineering & IT student at RWU Ravensburg-Weingarten,
entering semester 4. I completed an embedded-systems course from the University
of Colorado Boulder and tried to apply what I learned, to the best of my
understanding. I am not claiming that this project was assessed by that
university or that I am affiliated with it.

Development was AI-assisted, including implementation and documentation work.
The source, commits and tests show what exists and what was checked; they do not
prove that I independently understand every C++ detail. I want to become better
at explaining, tracing and modifying the implementation myself. A useful next
exercise is to follow one button request from input to queue, packet, ACK and
receiver animation, then explain each timeout and failure path without relying
on generated explanations.

I estimate approximately **14 active working days** during limited summer
availability. This is my estimate, not a verified activity count or an hours
claim. The DEVLOG spans September 7 to October 4, and Git activity records
checkpoints rather than time worked. The limited window helps explain the pause
and scope choices; the unresolved defects still matter.

## Concepts I tried to put into practice

| Concept | Concrete repository example and learning value |
| --- | --- |
| Shared firmware configuration | [PlatformIO identities](../platformio.ini) and [Config.h](../include/Config.h) build Bubu and Dudu from the same source. Identity differences need not become two diverging programs. |
| Hardware interfaces | [Interfaces](../INTERFACES.md) maps shared I²C, CC1101 SPI and GPIO wake inputs. Motion initializes I²C once before the OLED uses it. A bus ACK alone cannot establish correct application behavior. |
| Structured messages and reliability | [Protocol.h](../include/Protocol.h) defines an eight-byte message. [RadioRuntime](../src/app/RadioRuntime.cpp) matches ACKs, limits retries and keeps a retry's ID stable; duplicates can be acknowledged without replaying an action. Failure remains possible after the budget. |
| Callbacks and queues | Wi-Fi callbacks copy packets into a bounded queue; the cooperative loop processes them and owns protocol state. This separates arrival from decisions about retries, peer state and output. |
| State machines and non-blocking output | [PowerManager](../src/PowerManager.cpp) separates sleep agreement from execution. [LED.cpp](../src/LED.cpp) advances animation with time so normal loop servicing can continue. The active application does not create FreeRTOS application tasks. |
| Sleep/wake and retained state | [SleepRuntime](../src/app/SleepRuntime.cpp), [RtcState](../include/RtcState.h) and CC1101 wake modules distinguish boot evidence, retained messages, semantic agreement and guarded physical entry. Waking the peer and delivering its user event require different acknowledgements. |
| Debugging, version control and testing | Serial output, bus captures, host fault injection, sanitizer checks, builds and dated Git checkpoints answer different questions. A screenshot or a passing suite is useful only within that evidence's limits. |

The [architecture](../ARCHITECTURE.md) describes the final runtime ownership;
the [host guide](../tests/host/README.md) maps tests to responsibilities.

## Practices worth keeping

The September 22 [DEVLOG entry](../DEVLOG.md#2026-09-22) records a return to
incremental work after integration became difficult to debug. Moving protocol
work out of callbacks is a concrete improvement: one execution context can
reason about the pending packet and its ACK. Bounded retries and sleep deadlines
also turn missing responses into explicit outcomes rather than endless waiting.
They constrain failure; they do not guarantee delivery or repair a stopped radio.

Preserving working versions made it possible to back away from regressions.
The [October 3 session record](../DEVLOG.md#2026-10-03) retains abandoned
experiments in stashes/backups and distinguishes them from the restored baseline.
Historical branches remain useful evidence of subsystem bring-up, even when
their procedures no longer match main. They should be inspected at their recorded
commit, not blindly reapplied over the integrated firmware.

The [final integration record](../DEVLOG.md#2026-10-03--final-integration-and-software-validation)
documents isolated host suites and a Linux sanitizer failure in the simulated
reboot harness that local macOS checks had not reported. Fixing the harness's
queue lifetime while keeping sanitizers enabled is more informative than hiding
the failing check. Recording physical observations separately is equally useful:
the later transition failures remain failures despite successful CI.

## What I would approach differently

The scope was ambitious for the available time: two radios, local and peer state,
motion, proximity, user output and coordinated sleep all interact. This is
supported by the [September 20–21 integration record](../DEVLOG.md#2026-09-20-to-2026-09-21),
which describes combining too many subsystems before responsibilities were clear,
and the October 3 record of software-passing experiments with physical regressions.
Those examples suggest smaller integration steps and fewer simultaneous changes.

Transition testing and calibration were insufficient to establish reliable
integrated behavior. The final separation-and-return observation exposes an
especially important scenario: only one device moves, but both devices need
useful current state. Proximity is locally owned, refresh is event-triggered,
and timed-out checks retain the old classification. That is consistent with stale
state, not a confirmed diagnosis of every failure. I need a clearer policy for
who owns each fact, how old it may become, what refreshes it, and how uncertainty
is shown. Motion thresholds and RSSI boundaries are still provisional.

Passing host tests show expected behavior for modeled scenarios; they do not
establish reliable RF, actual scheduling, sensor calibration or visible OLED
output. The gap between those tests and physical behavior is a central v1 lesson.
Adding more components or abstractions is not automatically better engineering.
An extra radio, sensor or module is worthwhile only if it solves a defined problem
with understandable interfaces and evidence that the resulting system improves.

## Electronics that remain unfinished

![Falstad model of a candidate battery indicator](images/falstad-battery-indicator-simulation.png)

*Simulation evidence: dividers, comparator stages, ideal reference sources and
logic driving LED states. This is not an assembled, measured indicator.*

The [September 19 record](../DEVLOG.md#2026-09-19) and
[Falstad export](../simulations/battery_indicator_v1.txt) preserve the analog
experiment. The screenshot includes feedback experimentation; the log records
deferring final hysteresis. Neither establishes real comparator behavior or a
finished analog power subsystem. The physical comparator indicator, reference
generation and MOSFET power gating were not completed.

Battery supply assembly and basic battery-powered operation were separately
reported on October 3, with about 3.9 V per cell and 4.99–5.02 V at the booster
output. Those historical user measurements do not supply current consumption,
battery runtime or charging-under-load characterization. The simulation cannot
fill that evidence gap.

## Aspirations for a possible v2

| Direction | Purpose and evidence needed when work resumes |
| --- | --- |
| Evaluate ultra-wideband (UWB) ranging | Hopefully purchase suitable hardware and evaluate it for more precise distance measurement. No hardware choice, purchase or accuracy is claimed. A new sensor alone would not fix state ownership or refresh logic. |
| Strengthen programming understanding and state design | Trace the C++ paths, define state ownership and freshness, improve recovery behavior, and add integration scenarios for stationary-peer return and asymmetric state. |
| Purposeful MOSFET switching or subsystem power gating | Identify a useful load to control and account for wake availability and electrical behavior before selecting an implementation. More switching circuitry is not a goal by itself. |
| Build a simple comparator battery indicator | Start from the Falstad idea, then select real components and examine reference generation, input/output limits, tolerances, hysteresis and LED loading in an assembled, measured circuit. |
| Measure power and runtime | Record current in defined active/sleep/radio states and measure battery runtime under a stated workload. Characterize charging under load separately. |
| Reconsider optional features | Gestures, battery telemetry and interface changes remain ideas; implement them only when they serve a clear purpose. |

None of these items is already implemented or purchased as part of this closure.

## Saved restart point

1. Read [final findings](../FINAL_FIRMWARE_TEST.md) and the final
   [DEVLOG entry](../DEVLOG.md#2026-10-04--v1-prototype-closure-and-development-pause).
   Start from the v1 documentation checkpoint on `main`. The reported installed
   firmware target is `ecd9d4f`; `edd622f` and this closure are later documentation
   changes with the same firmware sources. Upload transcripts/version readback
   were not supplied.
2. Inspect the then-current Git status and remote history before doing new work.
   Preserve the unrelated editor setting, historical branches, stashes, backups,
   original images and simulation. Do not restore an abandoned patch merely
   because it is available. Create an isolated v2 branch when development resumes.
3. Choose one question first: why does the stationary partner fail to refresh
   after separation and return? Record the actual matching firmware on both
   devices and, when testing resumes, pair serial traces with screen/LED
   observations and stated conditions. Compare local check triggers, accepted
   samples, timeout/cancellation and selected transport before choosing a fix.
4. Re-establish the existing host/build baseline from the [testing guide](../tests/host/README.md).
   Define one transition/recovery change with a focused integration case, then
   compare software results with repeated physical observations. Keep calibration,
   state refresh and radio recovery as distinct questions.

These are instructions for a future return, not unfinished tasks for this
closure. The previous request for three more wake tests, and the earlier
immediate repeat-demonstration instruction, are superseded by the development
pause. No additional test, hardware purchase or firmware change is required now.
