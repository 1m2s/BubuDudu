# BubuDudu System Architecture

Both ESP32-C3 identities run the same cooperative firmware. `DEVICE_BUBU` and
`DEVICE_DUDU` select identity and peer configuration. [main.cpp](src/main.cpp)
contains startup and the ordered loop; it creates no FreeRTOS application tasks.

A **state machine** records the current state and the events allowed to change
it. **Semantic agreement** means both devices have agreed to sleep, even if
physical sleep has not started yet. A **queue** holds work until its owner can
process it; a hardware **FIFO** is a buffer read in the order bytes arrived.

## Runtime ownership

| Module name | Actual role | Owned state |
| --- | --- | --- |
| `src/app/RadioRuntime.*` | Protocol delivery and transport selection | RX queue, message IDs, one reliable pending packet, sleep-control outbox, retries, duplicate history, selected transport and deferred fallback; protocol history saved/restored through RTC memory. |
| `src/app/MotionRuntime.*` | Motion **and proximity** | Motion driver, movement/settling, RSSI measurement and samples, correlated ESP-NOW probes and classification completion. Proximity currently lives here despite the motion-only name. |
| `src/app/ButtonRuntime.*` | Button request and wake handoff | Debounce, one pending UserHeartbeat, latched wake intent and bounded peer-wake handoff. |
| `src/app/SleepRuntime.*` | Boot policy and sleep orchestration | Wake evidence, retained-packet routing, inactivity clock, automatic sleep admission, transport drain and physical-entry orchestration. |
| `src/app/Presentation.*` | OLED and LED presentation | Display/LED instances, display snapshot, Dudu diagnostics, retained user animation and shared FAR animation cadence. |
| `src/PowerManager.cpp` | Hardware-independent power and peer state machines | Sleep negotiation, cooldown, replay history and failed-entry recovery evidence. It makes no hardware calls. |

Each runtime module keeps mutable state in its implementation. Headers expose
operations and read-only queries. `AppIdentity.h` supplies identity constants
and diagnostic names.

| Lower-level name | Actual role |
| --- | --- |
| `ESPNowRadio` | Wi-Fi callbacks, outstanding TX accounting and a bounded RSSI observation queue. |
| `CC1101SleepArm` | Cold-boot configuration and retained bus attachment; `prepareForSleep()` only inspects radio readiness and is also called while awake. |
| `CC1101WakeRecovery` | Retained-packet recovery **and awake radio transport**, boot capture and MCU deep-sleep entry. Its internal `startAck()` transmits EVENTs as well as ACKs. |
| `CC1101WakeTx` | Bounded synchronous peer wake and a deferred queue for application EVENTs received during its ACK wait. |
| `CC1101Bus` | Shared SPI pins/settings and bus access primitives. |
| `Motion`, `Display`, `LED` | Sensor configuration/interrupts, OLED frames and non-blocking LED animation. |
| `RtcState` | Validated protocol checkpoint in RTC memory, a memory area retained through deep sleep; checksum and invalidate-before-write protect restoration. |
| `RadioTask`, `CC1101Radio` | Earlier task-based radio implementation, compiled as source but not started by the application. No linked-size claim is made. |

The active CC1101 paths keep inspection, retained-FIFO recovery and SPI ownership
separate. Only one path uses SPI at a time; unread radio packets must survive
recovery and competing wake traffic.

## Startup and loop order

Startup captures wake evidence before Serial/SPI/I²C. PowerManager initializes;
RTC history is restored and retained CC1101 packets are inspected/ACKed before
sensor work. Motion alone initializes shared I²C, then OLED and LED initialize.
A retained user animation is consumed before button initialization. The RX queue
exists before ESP-NOW callbacks are registered. After successful runtime startup,
the one-shot motion peer wake, bootstrap proximity check and fresh inactivity
interval can begin. Dudu's startup address checks observe bus behavior.

The loop order is deliberate:

1. Update LED, observe proximity/power boundaries, consume motion and button
   input, then update power deadlines. Local activity wins over sleep admission.
2. Drain at most eight ESP-NOW packets, service awake CC1101 and deferred wake
   packets, discard obsolete controls, then handle ACK timeouts. Received ACKs
   are processed before retransmission decisions.
3. Evaluate automatic sleep, send the next control, then attempt physical sleep
   after semantic agreement and transport drain. Recheck input before entry.
4. Service button intent, apply radio selection, schedule a periodic Heartbeat
   and service the shared FAR animation clock.
5. Service proximity probes, drain at most two RSSI observations, redraw eligible
   display changes and yield for 10 ms.

A **callback** is a function the Wi-Fi driver calls when an event occurs.
The ESP-NOW receive callback logs packet metadata and forwards bytes to the
bounded application queue; the send callback logs its result and updates TX
accounting. Protocol decisions happen in `loop()`. The separate RSSI observer
queues observations without logging. The loop uses those observations for both
diagnostics and proximity classification, which can change transport selection.

Synchronous peer-wake transmission owns SPI until it returns. Its bounded queue
forwards deferred EVENTs through the normal consumer afterward, without a nested
receipt transmission while the wake sender owns the radio.

## Product policy and hardware constraints

Awake devices stay ACTIVE; there is no IDLE state. The 35-second interval since
meaningful local activity admits automatic negotiation, with 3-second peer grace.
Motion, button intent, proximity, transport work and cooldown are separate guards.
Sleep negotiation has a **3-second per-phase limit, 5-second overall limit and
3-second cooldown after failure**. Duplicate packets and retries cannot extend
the overall deadline. Application EVENT reception updates peer visibility;
it does not itself cancel sleep agreement. Local activity follows its own path.

If physical sleep entry fails, the device returns to ACTIVE. It re-measures
proximity only after the peer acknowledges the next EVENT it sends, so the
measurement uses evidence gathered after the failure. Movement and transport
drain still decide when that measurement can run. An OFFLINE peer can also
recover while ESP-NOW fallback is pending.

CLOSE background Heartbeats use 2500 ms; FAR uses 6000 ms. Incoming and outgoing
FAR requests share a visual clock. User requests take priority with a 700 ms
receiver pulse; UNKNOWN suppresses background animation. See [button behavior](BUTTON_HEARTBEAT.md).

The trial awake motion threshold is 0.625 g, with a separate 3 g sleep threshold,
3-second sensor inactivity and 1-second software settle. I²C runs on GPIO0/1;
the OLED shares the bus at 100 kHz. Sleep uses GPIO3/4 HIGH and GPIO5 LOW
(mask `0x38`), with no timer wake. Successful deep sleep restarts the MCU on wake;
a return from entry means refusal/failure and restores awake sensing.
[Interfaces](INTERFACES.md) is the single current pin reference.

## Classification freshness and the v1 limitation

Each device retains its own RAM-only CLOSE/FAR classification. Active checks
accept fresh eligible RSSI observations. Startup, local movement/settling and
specific peer/recovery events can trigger checks. There is no periodic refresh
of a known result. Answering a peer's probe does not start a local check.
Cancellation or timeout keeps the previous classification. A completed result
can request transport selection, subject to fallback and busy-state guards.

This is consistent with the observed stationary partner staying FAR on CC1101
after its peer returns. [Final observations](FINAL_FIRMWARE_TEST.md#october-4-demonstration-checkpoint)
remain separate from this source-based explanation. The [test limits](tests/host/README.md#limits)
and [v2 restart point](docs/V1_REFLECTION.md#saved-restart-point) describe the gap.

## Legacy names and visible diagnostics

Source comments describe current ownership. Some identifiers and device strings
remain frozen with v1 and need care when reading code or logs:

| Name or text | Current meaning / limit |
| --- | --- |
| RSSI `OBSERVER READY` with `diagnostics only` | Legacy banner; RSSI also feeds proximity classification and radio selection. |
| `SIMULATED_WAKE_MS`, `SIMULATED_ACTIVITY_WAKE`, `SIMULATED_WAKE_COMPLETE` | Legacy names for PowerManager's software WAKING transition; physical product sleep is real deep sleep. |
| OLED `WAKE: MOTION / PEER` | Incomplete caption: GPIO5 button wake is also enabled. |
| `LocalState` numeric gap | Value 1 belonged to removed IDLE; retained values do not imply an active IDLE state. |
| `handleEvent(..., sendReceipt=false, ...)` | Retained-wake path: records history/deduplication while suppressing immediate receipt, live LED dispatch and fault injection. Retained user animation is delivered after LED initialization. |
| `DROP_FIRST_ACK_FOR_TEST` | Disabled compile-time fault injection left in the source, not a serial command. |

Renames, driver removal, shared pin/register definitions, formatting and test
redesign are recorded in the [audit disposition](docs/V1_AUDIT_RESOLUTION.md).
[Historical procedures](docs/history/README.md) and the [DEVLOG](DEVLOG.md)
preserve earlier designs without redefining current behavior.
