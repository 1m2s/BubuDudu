# Button heartbeat and wake behavior

This is the v1 behavior reference. [Final findings](FINAL_FIRMWARE_TEST.md)
records physical evidence and open limits; further device testing is deferred.

## Messages and awake input

Both devices use the eight-byte `Protocol::Message`. Its event field distinguishes:

| Event | Meaning | LED behavior |
| --- | --- | --- |
| `Heartbeat = 1` | Background event or peer wake | Background pulse when locally ACTIVE and CLOSE/FAR. |
| `UserHeartbeat = 2` | Physical button request | Receiver plays a priority 700 ms double pulse; no added sender animation. |

UserHeartbeat was added without changing message size or `Protocol::VERSION`.
Both devices must run matching firmware; earlier firmware rejects the new event
value. ESP-NOW and awake/retained CC1101 paths accept both values. Peer wake uses
plain Heartbeat and has its own acknowledgement, separate from user delivery.

GPIO5 uses an internal pull-up and a button to ground. A stable LOW press is
debounced for 30 ms; release rearms it. **Debounce** filters rapid electrical
changes so one physical press becomes one request. At most one request waits.
It cancels a running proximity check, waits for in-flight work and uses the
shared outbox: 300 ms ACK timeout, up to two retries with the same message ID.
UNKNOWN sends through CC1101 without inventing a proximity result. An ACK means
the packet was acknowledged; retries and ACKs never create an animation or echo
EVENT.

## Receiver animation and duplicates

A fresh UserHeartbeat replaces the receiver's background pulse. Its two pulses
last 700 ms total: two 130 ms phases, a 70 ms gap and two 185 ms phases.
Background requests during that animation are discarded. Another distinct user
event restarts the pulse; there is one animation slot, not a queue. The sender's
background animation continues without an extra user pulse.

Duplicate EVENTs are acknowledged again without replaying their action. An
awake cache of eight user IDs also catches retries interleaved with newer events;
entries expire after ten seconds and the cache resets on reboot. RTC memory
separately retains the latest accepted-event history through deep sleep.

Animation is non-blocking: updates advance it while input/radio work continues.
After completion, the next scheduled background request uses current state;
CLOSE cadence is 2500 ms, FAR 6000 ms, and UNKNOWN suppresses background output.
Incoming and outgoing FAR events share a visual clock. Sleep cancels LED output.

## Button wake and peer handoff

Deep-sleep wake uses GPIO3/4 HIGH and GPIO5 LOW, combined mask `0x38`, with no
timer. The pinned SDK accumulates wake masks and preserves per-pin pull modes,
so each pin keeps its fixed polarity. GPIO5's sleep pull-up supports the button
to ground. [Interfaces](INTERFACES.md) owns the complete pin reference.

The earliest wake mask seeds one UserHeartbeat even if the button is released
before initialization. A held wake cannot create another press; stable HIGH for
30 ms rearms input. Wake intent counts as local activity and resets the
35-second inactivity timer.

After retained-radio recovery and runtime initialization, the intent waits up
to 3 seconds for transport/control work and any received user pulse. It then
runs one CC1101 peer-wake episode using Heartbeat with a fresh ID. Combined
motion/button wake uses this one episode; combined radio/button wake recovers
incoming packets first.

Only an acknowledged wake **and** healthy local receive readiness release the
user request. It then enters the reliable UserHeartbeat outbox with another
fresh ID, using CC1101 while proximity is UNKNOWN. A peer-wake ACK alone does not
confirm UserHeartbeat delivery.

The wake episode has three same-ID attempts, 300 ms ACK waits and at most one
RX recovery. Failure, a 3-second drain timeout or unavailable runtime clears
the held request with delivery unconfirmed. A new episode requires release and
a new debounced press. Held LOW cannot retry, and a stopped radio stays stopped
until reboot.

## Concurrent and retained events

During the synchronous wake ACK wait, validated application EVENTs enter an
eight-packet deferred queue. They do not satisfy the wake ACK or extend its
deadline. Once wake transmission returns, normal CC1101 processing drains them
one at a time, waiting for each receipt transmission. A full queue stops the
episode before reading another complete FIFO packet. Unread packets are
preserved; no uncopied packet is acknowledged.

A copied event can still be delivered if RX restoration fails, although its
receipt may fail and the sender retains its normal retry limit. Queued work
blocks sleep and new wake episodes until drained.

A new retained UserHeartbeat records one animation obligation before its receipt
ACK. After LED initialization it is consumed once, without replaying delivery
or allocating another message ID. Duplicates and invalid-RTC rejection create
no obligation. Local button wake waits for this received pulse to finish before
starting synchronous peer wake.

## Sleep guard and known limit

Raw LOW and pending button work block sleep admission and final entry. Once
work clears and the button is released, normal inactivity and other guards
control coordinated sleep. LOW spanning an entry check prevents sleep; LOW
spanning actual sleep triggers wake. A complete press/release between the final
polls, including the SDK's interrupt-disabled interval, can be missed. There is
no interrupt capture guaranteeing every such pulse.

## Implementation and verification

[ButtonRuntime](src/app/ButtonRuntime.cpp) owns input and wake intent;
[RadioRuntime](src/app/RadioRuntime.cpp) owns delivery, retries and duplicate
history; [Presentation](src/app/Presentation.cpp) owns LED requests;
[SleepRuntime](src/app/SleepRuntime.cpp) owns boot and sleep policy.
The [host suite map](tests/host/README.md) covers those paths.
[Historical acceptance cases 3–10](docs/history/2026-10-01-acceptance-prep.md#physical-acceptance-sequence)
retain the planned physical checks; they are not additional closure tasks.
