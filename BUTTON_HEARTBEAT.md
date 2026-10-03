# Awake button heartbeat

Both identities use the shared eight-byte `Protocol::Message` and its existing
EVENT/ACK reliability rules. The EVENT enum now explicitly distinguishes:

| `event` | Meaning | Awake LED behavior |
| --- | --- | --- |
| `Heartbeat = 1` | Background heartbeat or existing peer wake | Background output while locally CLOSE/FAR and ACTIVE |
| `UserHeartbeat = 2` | Debounced physical button request | No added sender animation; receiver plays the priority 700 ms double pulse |

This is an additive event value within protocol version 1. Message size, message
types, IDs, and `ackForMessageId` semantics are unchanged. Both firmware identities
must be updated together: older firmware rejects the new event value and cannot
acknowledge a button request. ESP-NOW and awake/retained CC1101 validation accept
both event values. Motion/button peer wake still sends `Heartbeat = 1`; retained
wake processing records a new user animation obligation before its receipt ACK,
then invokes the animation once after LED initialization.

The button uses the existing outbox, 300 ms ACK timeout and two same-ID retries.
It retains one pending intent and preempts measurement/background application
work after existing in-flight work drains. UNKNOWN uses the existing awake CC1101
path without asserting CLOSE/FAR. Neither a receipt ACK nor a retry creates an
animation or a reply EVENT.

LED ownership is local to the receiver. A received `UserHeartbeat` supersedes
the current background pulse. Background animation requests during the user
pulse are discarded, not queued. Another distinct user EVENT restarts the pulse
on the next update; there is one animation slot and no animation queue. Duplicate
EVENTs are re-ACKed through the existing deduplication path before LED requests.
An awake cache of eight recent user IDs also rejects user retries interleaved
with background or newer user EVENTs; the existing retained latest-EVENT history
is unchanged. Entries expire after ten seconds, beyond the 900 ms retry episode,
so allocator wrap cannot collide with indefinitely cached user IDs. The cache
stores no animations or packets and resets on reboot.
Animation updates remain non-blocking, and radio/input/motion/proximity/power
servicing continues. Sleep still cancels LED output immediately.

After completion, no old proximity state or missed background request is restored.
The next normally scheduled background request checks current proximity and
ACTIVE eligibility. CLOSE keeps its 2500 ms cadence. FAR uses 6000 ms and one
shared background visual clock across outgoing/incoming EVENTs; duplicates,
retries and offset peer traffic cannot add or restart FAR pulses. UNKNOWN
suppresses background output. Button sends do not interrupt or restart the sender's active
background animation. Application messages still share the existing single
outbox and ACK-driven cadence.

Physical check: establish CLOSE beating on both updated, awake devices. Press
only Bubu once for at least 30 ms, then release. Bubu must continue its background
pulse without an added/restarted pulse. Dudu must log one `PARTNER LED | USER
HEARTBEAT` for the received EVENT ID, substitute the 700 ms double pulse, then
return to normal CLOSE beating. ACKs/duplicate retries must not add a pulse or
echo a user EVENT. This behavior is symmetric for Dudu's button.

## Button deep-sleep wake

The shared sleep entry now makes two checked, fixed-polarity calls: GPIO3/4 HIGH
(`0x18`) and GPIO5 LOW (`0x20`), combined mask `0x38`. Product sleep still has no
timer; the former serial bench sleep controls and their timer are removed. Existing Motion/CC1101
arming, retained FIFO inspection, and final guards remain in place. The installed
C3 IDF 4.4.7 SDK accumulates GPIO masks and configures per-pin wake polarity.
Disabling all sources clears trigger enables, not stored GPIO masks/modes. Its
LOW setter cannot clear a previously HIGH internal pull-mode bit, so these pins
must keep their disjoint, fixed polarities. The host stub models those behaviors.
The installed SDK enables and holds the GPIO5 sleep pull-up; this step needs no
hardware change to the existing GPIO5-to-GND button.

The earliest captured GPIO wake mask seeds one pending UserHeartbeat even if the
button is already HIGH when button initialization runs. Wake evidence seeds the
debounced pressed state; a stable 30 ms HIGH rearms subsequent awake presses.
A held wake cannot create a second press. Release/repress uses the existing
awake debounce and coalesces into the same bounded pending slot while wake work
is held. Ordinary awake button requests retain their reliable send behavior.

Wake-origin intent counts as local activity and rearms the existing 35 s
inactivity timer. After retained-radio recovery and runtime initialization, it
waits at most 3 s for existing transport/control work and any received user pulse
to finish. It then runs one episode through the existing CC1101 peer-wake sender,
using a plain Heartbeat and its own fresh ID. GPIO3+GPIO5 uses this same button
episode, rather than also starting a motion wake. GPIO4+GPIO5 still recovers
incoming traffic before the local request is considered.

Only the actual `Acked` result **and** `rxReady=true` release the held intent.
An idle/not-busy radio or attempted send does not establish success. The released
intent uses the existing reliable UserHeartbeat outbox with another fresh ID;
UNKNOWN still routes through CC1101, without inventing CLOSE/FAR. In-flight
messages, application ACK/retry rules and receiver-only user animation remain
unchanged. No sender animation is added. Peer-wake ACK is not a UserHeartbeat
delivery ACK; the HANDOFF log explicitly says application acknowledgement is
still required.

During the synchronous peer-wake ACK wait, valid concurrent Heartbeat or
UserHeartbeat EVENTs are length/version/peer/type/event validated and copied
into an eight-packet deferred queue. They never satisfy the wake ACK or reset
its deadline. No application callback or receipt transmitter runs while wake TX
owns the radio. After `send()` returns, and on subsequent loop boundaries, the
queue drains through the normal CC1101 application consumer one packet at a time,
waiting for each asynchronous receipt TX to finish. Duplicate retries therefore
receive normal same-radio ACKs without another user animation. A copied event
still delivers if RX restoration fails; its failed receipt logs explicitly and
the sender retains its existing retry/give-up policy. The queue blocks sleep and
new wake episodes until drained. If full, the episode stops BUSY before reading
the next complete FIFO packet, preserving it for normal receive service. No
uncopied packet is acknowledged, no queued packet is overwritten, and no new wake
episode or additional recovery is automatically started.

The wake episode retains three same-ID attempts, 300 ms ACK waits, and at most
one RX recovery. There is no loop-based new wake episode. Any unsuccessful wake
result, even with healthy local RX, clears the held/pending request and logs
GIVE_UP with delivery unconfirmed. A peer ACK with failed final RX readiness also
clears it, without sending UserHeartbeat or extending the recovery budget. The
3 s drain timeout and runtime/stopped-radio failures clear it too. Only a later
stable release and new debounced press arm another bounded episode; held LOW
cannot retry. Existing stopped-radio cutoff remains authoritative until reboot,
and a new press cannot fabricate radio readiness. An application UserHeartbeat
that reaches the outbox keeps its existing 300 ms/two-retry failure policy.

Both raw LOW and pending button work block product sleep admission and final
entry. A confirmed held button stays awake while LOW, preventing repeated
immediate GPIO5 wakeups. After success or terminal failure clears the wake hold,
and the button is released and transport work has finished, ordinary coordinated
sleep is eligible again under the existing inactivity/motion/proximity rules.
Block reasons log on change rather than on every loop.

A newly processed retained UserHeartbeat owns one bounded deferred animation
obligation (one flag and event ID) before its receipt ACK. After `led.begin()`,
that slot is consumed once with `requestUserHeartbeat()` and the event ID is
logged. Application delivery is not replayed and no EVENT/extra ID is allocated.
Duplicates and invalid-RTC rejection cannot create an obligation. The LED request
is consumed before later runtime-init failure returns, so it can still animate
in the loop. A local button episode waits for this received user pulse to finish
before running the synchronous peer-wake sender. User priority and current CLOSE
background resumption use the existing LED state machine.

Final entry still polls. LOW spanning a final guard is refused; LOW spanning
actual sleep triggers wake. A complete press-and-release between polls,
including during the SDK's interrupt-disabled entry interval, can be missed.
This step adds no interrupt capture and makes no guarantee for every such pulse.

Physical check: both updated devices enter coordinated deep sleep with the
buttons released; verify `mask=0x38 | timer=OFF`. Briefly press only Bubu and
release, including an early release before startup completes. Bubu must report
GPIO5 wake, one preserved intent, one bounded peer-wake episode, and
`peer_ack=1 | RX_READY=1`. Dudu must wake through CC1101. Bubu then sends one
fresh-ID UserHeartbeat via CC1101 while UNKNOWN, and Dudu logs one `PARTNER LED |
USER HEARTBEAT` for that user ID and visibly plays the 700 ms double pulse. If
received during startup, its deferred log precedes that single invocation log.
ACKs/retries must not add a pulse or another wake episode. Leave both stationary
with buttons released: after the existing 35 s inactivity and normal drain/
measurement guards, both must coordinate sleep again with `mask=0x38 | timer=OFF`.

## Runtime and test locations

`src/app/ButtonRuntime.*` owns debounce and pending/wake intent; `RadioRuntime.*`
owns delivery, IDs, retries and deduplication; `Presentation.*` owns LED requests
and retained animation; `SleepRuntime.*` owns sleep guards and startup policy.
The real loop preserves their service order. See [host suites](tests/host/README.md)
for the button, boot/wake, LED, automatic-sleep and real CC1101 forwarding tests.
