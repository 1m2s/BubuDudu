# BubuDudu System Architecture

**v1 source map, 4 October 2026.** Development is paused with incomplete physical
acceptance. The reported upload target is `ecd9d4f`; later documentation changes
leave its firmware intact. See [final findings](FINAL_FIRMWARE_TEST.md).

Both ESP32-C3 identities run the same cooperative firmware. `DEVICE_BUBU` and
`DEVICE_DUDU` select identity and peer configuration. `src/main.cpp` contains
startup and the ordered loop; it does not create FreeRTOS application tasks.

## Runtime ownership

| Module | Responsibility and owned state |
| --- | --- |
| `src/app/RadioRuntime.*` | Application RX queue, shared message-ID allocator, one reliable pending packet, sleep-control outbox, retries, duplicate history, selected transport and deferred fallback. Saves/restores packet history at the RTC boundary. |
| `src/app/MotionRuntime.*` | Motion driver instance, movement/settling state, one proximity measurement, sample history and correlated ESP-NOW probes. Completed classifications supply fresh radio-selection evidence. |
| `src/app/ButtonRuntime.*` | Debounce, one pending UserHeartbeat intent, latched wake intent and bounded peer-wake handoff. |
| `src/app/SleepRuntime.*` | Earliest boot evidence, retained-wake routing, meaningful-activity clock, automatic sleep admission, transport drain and physical sleep execution. |
| `src/app/Presentation.*` | Display/LED instances, display snapshot and Dudu diagnostics, retained user animation and shared FAR animation cadence. |
| `src/PowerManager.cpp` | The single semantic power/peer FSM, sleep negotiation, cooldown, replay history and evidence-gated failed-entry recovery. No hardware calls. |

Each runtime module keeps mutable state in its own implementation. Headers expose
operations and read-only queries, not a shared collection of globals.
`AppIdentity.h` provides identity constants and diagnostic names. Radio retries
and sleep negotiations remain in their existing state machines.

The low-level modules retain their responsibilities: `ESPNowRadio` owns Wi-Fi
callbacks, outstanding TX accounting and bounded RSSI observations;
`CC1101SleepArm`, `CC1101WakeRecovery`, `CC1101WakeTx` and `CC1101Bus` own the
active CC1101 path, including retained FIFO safety and deferred EVENT forwarding.
`Motion`, `Display`, `LED` and `RtcState` provide the existing hardware/retention
services. Historical `RadioTask` and `CC1101Radio` remain in the tree but are not
started by the application.

## Startup and loop order

Startup captures wake evidence before Serial/SPI/I2C. PowerManager initializes,
then RTC history is restored and retained CC1101 packets are inspected/ACKed
before sensor work. Motion alone initializes the shared I2C bus; OLED then LED
initialize, a retained user animation is consumed, and the button is initialized.
The RX queue exists before ESP-NOW callbacks are registered. Only after successful
runtime initialization can the one-shot motion peer wake, bootstrap proximity
check and fresh inactivity interval begin. Dudu's existing startup address checks
remain after Motion/OLED initialization; they are observations, not repairs.

The loop order is deliberate:

1. Update LED, observe proximity/power boundaries, consume motion and button input,
   then update power deadlines. New local activity wins over sleep admission.
2. Drain at most eight queued ESP-NOW packets, service awake CC1101 and deferred
   wake packets, discard obsolete controls, then handle ACK timeouts. Received
   ACKs run before retransmission decisions.
3. Evaluate automatic sleep, send the next control, then attempt physical sleep
   only after semantic agreement and transport drain. Recheck input before entry.
4. Service pending button intent, apply automatic radio policy, then schedule a
   periodic Heartbeat and service the shared FAR animation clock.
5. Service proximity probes, drain at most two RSSI observations, redraw eligible
   display changes, and yield for 10 ms.

The Wi-Fi callback only copies bytes into the bounded application queue; the
loop owns protocol/policy state. Synchronous peer-wake transmission owns SPI
until it returns. Its bounded deferred queue forwards EVENTs through the normal
consumer afterward; it cannot overwrite an unread FIFO packet or send a nested
receipt while the wake transmitter owns the radio.

## Product policy and hardware constraints

Awake devices stay ACTIVE; there is no IDLE state. The 35-second meaningful-local-
activity interval admits one automatic negotiation, with 3-second peer grace.
Motion, button intent, proximity, transport work and cooldown remain independent
guards. Failed entry returns ACTIVE. Only a newly started post-failure application
EVENT and its exact matching ACK authorize a deferred recovery measurement;
movement and drain guards still control when it runs. An OFFLINE peer can also
recover while ESP-NOW fallback is pending.

CLOSE Heartbeats use 2500 ms; FAR uses 6000 ms. Outgoing and incoming FAR background
requests share a visual clock. The priority 700 ms receiver user pulse, retries,
deduplication and UNKNOWN suppression are unchanged. Proximity uses provisional
RSSI hysteresis, not calibrated distance.

Current Motion settings are the preserved trial awake threshold 10 (0.625 g),
sleep threshold 48 (3 g), 3-second sensor inactivity and 1-second software settle.
The trial awake threshold still needs physical validation. Motion owns GPIO0/1
I2C initialization, and OLED uses the same bus at 100 kHz. Sleep uses GPIO3/4 HIGH
and GPIO5 LOW (mask `0x38`), with the product timer OFF. A successful deep sleep
reboots; returning from the entry function always means failure and restores
awake sensing. Display attempts do not establish physical OLED visibility.

## Classification freshness and the v1 limitation

Each device retains its own RAM-only CLOSE/FAR classification. An active check
accepts fresh eligible RSSI observations; startup, local movement/settling and
specific peer/recovery events can trigger checks. There is no general periodic
refresh of a known result. Answering a peer's proximity probe does not itself
start a local check. Cancellation or timeout clears the measurement but keeps
the previous classification. A completed classification can request radio
selection, subject to fallback and busy-state guards.

This source behavior is consistent with the reported stationary partner staying
FAR on CC1101 after its peer returns. It is not a confirmed diagnosis of every
transition failure. The [final observations](FINAL_FIRMWARE_TEST.md#october-4-demonstration-checkpoint)
remain separate from that interpretation. See the [system graphic](docs/images/v1-system-flow.svg)
for a compact overview and [v2 restart point](docs/V1_REFLECTION.md#saved-restart-point)
for future investigation.

See [host testing](tests/host/README.md) for the suite map and
[acceptance/evidence](FINAL_FIRMWARE_TEST.md) for unresolved hardware observations.
The [DEVLOG](DEVLOG.md) preserves historical checkpoints separately from current
architecture and software verification.
