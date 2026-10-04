# Button heartbeat

Press one device's button and its partner plays a double pulse.
[Final results](FINAL_FIRMWARE_TEST.md) record what was tested on hardware.

## Messages and awake input

Both devices use the same eight-byte message and must run matching firmware.
Adding UserHeartbeat did not change the message size or protocol version.
Older firmware rejects that event value.

| Event | Use |
| --- | --- |
| `Heartbeat = 1` | Background pulse or partner wake. |
| `UserHeartbeat = 2` | Button request; only the receiver gets the extra animation. |

The button connects GPIO5 to ground. A LOW level lasting 30 ms counts as a press;
this filters switch bounce. Only one request can wait. It cancels a proximity
check, waits for current radio work and sends. UNKNOWN proximity uses CC1101.

A reply (ACK) must arrive within 300 ms. Otherwise the sender retries up to twice
with the same ID. ACKs and retries do not create extra animations.

## Receiver animation and duplicates

The user pulse lasts 700 ms: 130 ms up, 130 ms down, a 70 ms gap, then 185 ms up
and 185 ms down. It replaces background output. Background requests during it
are dropped; a new, distinct user event restarts it. The sender's background
animation continues normally.

Repeated event copies get another ACK but no repeated action. While awake, the
receiver remembers eight recent user IDs for ten seconds. That cache clears on
reboot; separate RTC memory keeps the latest event history through deep sleep.

After the user pulse, normal background output uses the current state: 2500 ms
in CLOSE, 6000 ms in FAR, none in UNKNOWN. FAR sends/receives share one pulse
clock. Animation updates leave time for input and radio work. Sleep turns it off.

## Button wake and peer handoff

GPIO5 LOW wakes the ESP32. Startup saves that wake reason, so an early button
release does not lose the request. A held button does not count twice; a stable
HIGH for 30 ms allows the next press. Button wake resets the 35-second inactivity
clock. [Interfaces](INTERFACES.md) lists all wake pins and their fixed levels.

After startup, the request waits up to 3 seconds for radio work and any received
user pulse to finish. It then tries to wake the partner over CC1101 using a plain
Heartbeat with a new ID. There are three attempts, 300 ms reply waits and at most
one receive-mode recovery attempt.

Only a matching reply **and** a working local receiver allow the UserHeartbeat
to be sent with another new ID. The wake reply does not confirm user-event delivery.
If wake fails, times out or leaves the receiver unavailable, the request clears.
Another attempt needs a release and new press. A stopped radio needs a reboot.
Combined motion/button wake uses one wake attempt sequence; radio/button wake
handles incoming packets first.

## Other packets during wake

While waiting for the wake reply, other valid events wait in an eight-packet
queue. They do not extend the deadline or count as that reply. After the wake
attempt, the loop handles them one at a time and sends their ACKs. A full queue
stops the attempt before another packet is read; unread data stays in the radio.

A saved event can still be handled if receive-mode recovery fails, though its
ACK may fail. Queued events block sleep and new wake attempts until handled.
A user event found after deep sleep saves one LED request before its ACK, then
plays once after LED setup. Duplicates or rejected RTC history add no animation.

## Sleep guard and known limit

A held button or waiting button work blocks sleep. A complete press and release
between the final sleep checks can be missed, including during the SDK's short
interrupt-disabled entry period. There is no extra capture mechanism for that gap.

## Implementation and verification

[ButtonRuntime](src/app/ButtonRuntime.cpp) handles input;
[RadioRuntime](src/app/RadioRuntime.cpp) handles delivery;
[Presentation](src/app/Presentation.cpp) controls output;
[SleepRuntime](src/app/SleepRuntime.cpp) handles startup and sleep checks.
See [software tests](tests/host/README.md) and the
[old physical test plan](docs/history/2026-10-01-acceptance-prep.md#physical-acceptance-sequence).
