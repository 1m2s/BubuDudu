# How the firmware works

Both devices run the same code. `DEVICE_BUBU` and `DEVICE_DUDU` select the name
and partner. [main.cpp](src/main.cpp) runs each part in a fixed order inside
`loop()`. The application creates no FreeRTOS tasks.

## What each module does

| Module | Job |
| --- | --- |
| [RadioRuntime](src/app/RadioRuntime.cpp) | Sends and receives messages, tracks replies and retries, ignores duplicates, and chooses the radio. |
| [MotionRuntime](src/app/MotionRuntime.cpp) | Reads motion **and handles proximity**: signal-strength samples, check messages and CLOSE/FAR results. |
| [ButtonRuntime](src/app/ButtonRuntime.cpp) | Filters button bounce, keeps one waiting request and wakes the partner when needed. |
| [SleepRuntime](src/app/SleepRuntime.cpp) | Reads the wake reason, tracks inactivity and checks whether it is safe to sleep. |
| [Presentation](src/app/Presentation.cpp) | Updates the OLED and LED, including user heartbeats and FAR pulse timing. |
| [PowerManager](src/PowerManager.cpp) | Tracks power/partner states and the sleep agreement. It makes no hardware calls. |

Each module keeps its own changing data. Headers expose the functions other
modules need. `AppIdentity.h` holds device names and IDs.

| Driver/helper | Job, including names that can be misleading |
| --- | --- |
| `ESPNowRadio` | Wi-Fi callbacks, send tracking and signal-strength samples. |
| `CC1101SleepArm` | Starts the radio at a cold boot. `prepareForSleep()` only reads its status and is also used while awake. |
| `CC1101WakeRecovery` | Reads packets left in the radio after sleep, handles awake sends/receives and enters deep sleep. `startAck()` sends EVENTs too. |
| `CC1101WakeTx` | Wakes the partner and saves other incoming events until that attempt ends. |
| `CC1101Bus` | Shared SPI pins, settings and access helpers. |
| `Motion`, `Display`, `LED` | Motion sensor, OLED and LED control. |
| `RtcState` | Saves message history in memory that survives deep sleep. A checksum detects bad data; the old record is marked invalid before writing a new one. |
| `RadioTask`, `CC1101Radio` | Older code, kept for reference. The application does not start it. |

Only one path uses CC1101's SPI bus at a time. Unread packets in its FIFO
(the radio's byte buffer) must not be erased during recovery.

## Startup and loop order

Startup reads the wake reason first, then restores saved message history and
checks packets left in CC1101. Motion starts the shared I²C bus before the OLED.
LED and button setup follow. A user heartbeat received during wake is saved
until the LED is ready. The receive queue exists before Wi-Fi callbacks start.
A successful startup then starts the first proximity check and inactivity clock.

Each loop:

1. Updates the LED, reads motion/button input and checks power deadlines.
2. Handles up to eight Wi-Fi packets, services CC1101 and saved events, then
   checks reply timeouts. Replies are handled before deciding to retry.
3. Checks sleep conditions, sends sleep messages and tries to sleep after both
   devices agree and radio work has finished.
4. Handles a waiting button request, radio choice and background heartbeats.
5. Sends proximity probes, reads up to two signal-strength samples, updates the
   screen if needed and waits 10 ms.

A callback is a function the driver calls when something happens. ESP-NOW's
receive callback logs packet details and puts bytes in a queue for `loop()`.
The send callback logs the result and updates the send count. The separate RSSI
callback queues samples without logging. RSSI means received signal strength;
the loop uses it for proximity and radio choice.

While waking the partner, CC1101 saves other incoming events. The loop handles
those events after the wake sender releases the radio.

## Sleep and timing

Awake devices stay ACTIVE. After 35 seconds without meaningful local activity,
the device can ask its partner to sleep. Motion, a held button, waiting work,
a running proximity check or cooldown can delay this. There is 3 seconds of
peer grace. The sleep agreement allows **3 seconds per phase, 5 seconds total
and a 3-second cooldown after failure**. Retries do not extend the total limit.
Receiving an application event alone does not cancel the sleep agreement.

Both devices agreeing to sleep is separate from entering deep sleep. If entry
fails, the device returns to ACTIVE. It checks proximity again only after the
partner replies to a new event sent after that failure; movement and unfinished
radio work can still delay the check. A returning partner can also be detected
while a switch from ESP-NOW to CC1101 is waiting.

| Setting | Value |
| --- | --- |
| CLOSE / FAR background heartbeat | 2500 / 6000 ms; FAR sends/receives share one animation clock |
| User heartbeat | 700 ms, with priority over background output |
| UNKNOWN proximity | No background heartbeat |
| Awake / sleep motion threshold | 0.625 g trial setting / 3 g |
| Sensor inactivity / software settling | 3 s / 1 s |
| Wake inputs | GPIO3/4 HIGH, GPIO5 LOW, mask `0x38`; timer off |

Wake restarts the ESP32. See [hardware](INTERFACES.md) for pins and
[button behavior](BUTTON_HEARTBEAT.md) for input details.

## Why proximity can get stuck

Each device keeps its own CLOSE/FAR result. It checks again after startup,
movement/settling or certain partner/recovery events. There is no regular refresh
of a known result. Answering the partner's probe does not start a local check.
A timeout or cancelled check keeps the old result.

This could explain why the stationary device stays FAR after its partner
returns. It is a likely cause to investigate, not a confirmed fix. See
[observations](FINAL_FIRMWARE_TEST.md#october-4-demonstration-checkpoint) and
[test limits](tests/host/README.md#limits).

## Legacy names and visible diagnostics

Some old names and screen messages remain in the frozen firmware:

| Name/text | What to know |
| --- | --- |
| RSSI `diagnostics only` | Old wording. These samples also affect proximity and radio choice. |
| `SIMULATED_WAKE_*` / `SIMULATED_ACTIVITY_WAKE` | Old names for a software state change. Product sleep is real deep sleep. |
| OLED `WAKE: MOTION / PEER` | Button wake also works; the caption leaves it out. |
| Missing `LocalState` value 1 | It belonged to the removed IDLE state. |
| `sendReceipt=false` | Processes a packet saved through sleep. It records history without sending the normal reply or starting the LED immediately. |
| `DROP_FIRST_ACK_FOR_TEST` | A disabled test switch left in the code. |

[Future cleanup](docs/V1_AUDIT_RESOLUTION.md) · [Old procedures](docs/history/README.md)
