# BubuDudu Requirements

v1 firmware status at the user-reported upload target `ecd9d4f`, unchanged by
the later documentation checkpoints. Development is paused. “Implemented”
describes the code; physical acceptance remains incomplete. [Architecture](ARCHITECTURE.md) explains
runtime ownership, [testing](tests/host/README.md) preserves build/host commands,
and [hardware acceptance](FINAL_FIRMWARE_TEST.md) records the remaining checks.

## Implemented behavior

| Area | Current behavior |
| --- | --- |
| Device symmetry | Bubu and Dudu share one source tree. PlatformIO identity flags select local/peer configuration; either device can send and receive. |
| Button interaction | A debounced press queues one UserHeartbeat. The receiver plays a priority 700 ms double pulse; the sender gets no additional user animation. Duplicate events are re-ACKed without repeating the action. |
| Event delivery | The eight-byte protocol has message types, IDs, matching ACKs, a 300 ms timeout and at most two same-ID retries. Receive callbacks queue packets; the cooperative loop handles protocol state, peer status and simultaneous traffic. Delivery can fail after the bounded attempts. |
| Radio selection | Application traffic uses ESP-NOW for eligible CLOSE state and CC1101 for FAR, with deferred CC1101 fallback after ESP-NOW event retries exhaust. Fresh eligible CLOSE evidence permits return to ESP-NOW. Pending packets keep their selected transport; sleep controls stay on ESP-NOW. |
| Motion and proximity | ADXL345 activity/inactivity drives movement/settling and motion wake. Proximity uses fresh ESP-NOW RSSI observations and provisional CLOSE/FAR hysteresis. The last completed classification persists while a new check is pending; UNKNOWN remains possible. |
| Power | Awake devices stay ACTIVE. After 35 seconds of meaningful local inactivity, sleep admission still requires motion, button, proximity, transport and cooldown guards. REQUEST → READY → COMMIT → SLEEP_ACK establishes semantic agreement; physical entry additionally requires drain and sensor/radio preparation. Product sleep has no timer wake. |
| Wake and recovery | GPIO3 motion, GPIO4 CC1101 and GPIO5 button can wake the MCU. Startup preserves RTC protocol history and inspects retained radio packets. Motion/button-origin peer wake is bounded; button handoff requires a peer ACK and local RX readiness before user-event delivery. Failed sleep entry returns ACTIVE; its recovery measurement requires a new post-failure EVENT and matching ACK. |
| Visual feedback | Loop-driven WS2812B animation is non-blocking. CLOSE/FAR background cadence is 2500/6000 ms, subject to current eligibility and user-animation priority. OLED frames contain identity, peer, distance classification, selected radio, status and motion, plus a sleep frame. |

[Button behavior](BUTTON_HEARTBEAT.md) describes debounce, wake intent,
receiver-only animation and the brief-pulse limitation at final sleep entry.
[Interfaces](INTERFACES.md) records the current GPIO and bus assignments.

## Deferred defects and missing evidence

- Proximity classification and automatic radio transitions were reported as
  extremely unreliable. The stationary partner can remain FAR on CC1101 after
  the moved device returns; moving the stale device may be needed for refresh.
  This is an observed product limitation, not only a future calibration task.
- Resolve or characterize initial `NOT_IN_RX`, persistent CC1101 `Stopped`, rare
  failed-sleep hardware recovery and OLED visibility. Host recovery tests and
  attempted framebuffer transfers do not close these hardware questions.
- Physically evaluate the trial awake Motion threshold of 0.625 g; the separate
  sleep threshold remains 3 g. Calibrate proximity against repeatable conditions;
  CLOSE/FAR is not measured distance.
- The paired-device acceptance sequence remains incomplete, including radio
  fallback, button/wake behavior and exact deferred-traffic overlap. LED/motion
  and nearby sleep/peer-wake observations provide partial evidence, with no
  whole-case passes or measured success rate. Retain their dates and conditions.
- Measure battery current, runtime and charging under load. No battery-life or
  charging-performance claim follows from the existing assembly notes or simulation.

These items are deferred, not prerequisites for closing v1. Current results and
known implementation limits are in [final findings](FINAL_FIRMWARE_TEST.md);
future priorities are in the [v2 restart plan](docs/V1_REFLECTION.md#saved-restart-point).

## Optional unfinished features

The original requirements proposed gesture recognition (double tap, shake and
orientation), battery telemetry, additional OLED RSSI/message-ID/ACK fields,
and a broader set of LED status animations. These remain ideas rather than
completed capabilities. The implemented activity detector is not a gesture
classifier. Optional MOSFET power gating is also unimplemented.

ESP-NOW delivery, CC1101 fallback, heartbeat animation and motion wake have moved
from those original plans into firmware. The dated [DEVLOG](DEVLOG.md) and older
branch documents retain the development sequence and historical hardware results;
their future-tense plans do not redefine the current implementation.
