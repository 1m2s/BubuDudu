# v1 audit disposition — 4 October 2026

This checklist records the documentation cleanup against findings F1–F22.
**v1 firmware development and physical testing are closed.** Changes to C++
files are comments only. Executable code, identifiers, strings, pin values,
protocol, timing, tests and build settings are preserved. The v1 tag is fixed.

“Corrected” below describes documentation or comments. “Deferred” means the
underlying code or hardware issue still exists and requires a future development
cycle; documenting it does not resolve it.

| Finding | Disposition |
| --- | --- |
| F1 — Author voice | Corrected current README, findings, reflection and media notes to project/first-person language. Dated DEVLOG entries retain their historical wording. |
| F2 — Version map | Centralized source equivalence, upload attribution, v1 tag target and verified CI in [README](../README.md#which-code-was-tested); removed repeated provenance headers. Subsequent source changes are explicitly comment-only. |
| F3 — Historical guidance | Moved both old procedures to [history](history/README.md), with current-document links. Preserved their bodies except relative link targets. Added current 3 s phase / 5 s overall / 3 s cooldown limits to Architecture. |
| F4 — RSSI and callbacks | Corrected Architecture and source comments: RSSI feeds proximity/transport policy, ESP-NOW callbacks log metadata, and the separate RSSI callback does not log. The frozen `diagnostics only` runtime string is explained in Architecture; changing that string is deferred. |
| F5 — Working-day estimate | Removed the unverifiable 14-day estimate. Retained the documented development date span without inventing a reason for Git activity on more days. |
| F6 — Course attribution | Used one plain sentence about the completed University of Colorado Boulder course. No title, year, credential or concept-to-course attribution was invented. |
| F7 — Dormant drivers | Identified `RadioTask` and `CC1101Radio` as inactive in README, Architecture and their headers. Driver deletion and linker/map inspection are deferred; no claim about discarded binary size is made. |
| F8 — Ownership/names | Architecture maps names to actual responsibilities, including proximity in MotionRuntime, awake transport/deep sleep in CC1101WakeRecovery, read-only inspection and EVENT-capable `startAck`. Code renames/module moves are deferred. |
| F9 — Scattered pins | Named Interfaces the single current pin reference and corrected Config's scope comment. Consolidating definitions and replacing the GPIO4 literal remain deferred source changes. |
| F10 — Legacy comments/text | Corrected timer, deep-wake, power-owner and EVENT comments. Architecture explains frozen `SIMULATED_*` names/strings, the enum gap and OLED's omitted button-wake caption. Identifier/string/enum changes are deferred. |
| F11 — Retained-event flag and test hook | Documented `sendReceipt=false` and disabled fault injection. An explicit event-origin enum, independent dedup-cache capacity and removal of the injection hook are deferred code changes. |
| F12 — Button reference | Rewrote [button behavior](../BUTTON_HEARTBEAT.md) in present tense with delivery, wake, queue, animation and polling limits; removed repeated change-history phrasing and inline physical-test instructions. |
| F13 — Hardware/upload | Added components, recorded rails, ADXL345 CS, LED resistor, GDO2, CS hold, battery evidence, macOS upload/monitor commands and both MAC locations to Interfaces. Final assembly verification and shared/generated MAC definitions are deferred. |
| F14 — Test claims | Corrected the 15-suite/92-case count, listed doubles and explained white-box coupling, single-device scope and the retained-classification expectation. Test redesign, two-device scenarios and shorter PASS messages are deferred. |
| F15 — Private paths/process narration | Removed local Desktop/Downloads paths and internal selection/shell narration from current-facing media/test docs. Kept media hashes. The archived preparation body and dated DEVLOG remain historical records, including old workflow details. |
| F16 — Repeated caveats | Defined Observed / Host-tested / Source-only / Not done once in [final findings](../FINAL_FIRMWARE_TEST.md#evidence-levels); shortened repeated caveats and linked to that record. |
| F17 — Dense architecture | Defined semantic agreement, queues, FIFO, callbacks and state machines; simplified failed-sleep recovery and separated actual module roles. |
| F18 — Source presentation | Simplified Motion header banners and the receive-handler comment. A repository-wide formatter pass is deferred to avoid mixing broad source churn into the frozen checkpoint. |
| F19 — CC1101 primitives | Replaced commit-hash narration in active radio comments with what the preserved sequence does. Sharing primitives and introducing register/state constants remain deferred. |
| F20 — DEVLOG voice | Added a current-guidance note; previous dated entries remain intact. This cleanup receives its own appended checkpoint. |
| F21 — GitHub description | Updated to: “Two ESP32-C3 companion devices with shared firmware: button-triggered heartbeats over ESP-NOW and CC1101, motion wake and coordinated deep sleep. v1 prototype, paused.” |
| F22 — GPIO21 boot activity | Documented the UART0 TX/ROM-print basis and a possible boot-time LED flash as an unverified hardware question, with an Espressif source. No flash is claimed observed and no wiring is changed. |

## Deferred work if development resumes

Start with the [saved restart point](V1_REFLECTION.md#saved-restart-point), not
with a broad refactor. The first design question is how both devices obtain
useful fresh state when only one moves away and returns.

1. Define classification freshness, expiry/UNKNOWN behavior, peer refresh and
   radio selection. Add a two-instance scenario with independent clocks, one
   stationary partner, separation, return, lost probes and timed-out checks.
   The expected result must be a deliberate product decision, then validated on
   physical devices. Existing tests preserve v1's old-result-on-timeout policy.
2. Characterize radio `NOT_IN_RX`/`Stopped`, rare failed-entry recovery and OLED
   visibility separately from proximity calibration. Measure current and runtime
   before making power-efficiency claims. Recheck assembled wiring only when
   hardware work resumes, including the possible GPIO21 boot flash.
3. When behavior is understood, move proximity into its own module; rename the
   CC1101 link/inspection/packet-TX operations and reconsider deep-sleep ownership.
   Preserve read-only inspection, one SPI owner, bounded deadlines and unread FIFO
   contents through each change.
4. Centralize pin and board-MAC definitions, remove the GPIO4 literal and dormant
   drivers, and replace `sendReceipt` with an explicit live/retained origin.
   Preserve retained history/deduplication before Wi-Fi startup and one deferred
   user animation. Give the dedup cache its own capacity; remove fault injection.
5. Name register/state values, share CC1101 TX/bus helpers without changing FIFO
   safety, update legacy strings and identifiers, then apply a consistent source
   format. Reduce test dependence on internals and shorten verbose PASS output.

The ACK/retry identity rules, RTC checksum and invalidate-before-write,
fixed-polarity wake masks, single I²C owner and ordered cooperative loop are
intentional safeguards to preserve. None of this deferred work is required to
finish the v1 documentation cleanup.

## Cleanup verification

- Compiler token comparison across every `src/` and `include/` C++ file:
  all 28,052 non-comment tokens match the pre-cleanup `d5acad1` source.
  Seventeen source/header files have comment-only changes.
- The complete existing host suite passed with AddressSanitizer and
  UndefinedBehaviorSanitizer, including both identities and standalone suites.
- Bubu and Dudu build-only firmware builds passed sequentially. USB discovery,
  upload and monitoring were disabled for those builds.
- Local Markdown links/headings, gallery hashes and diff whitespace were checked.
  Tests, build configuration, tools, CI workflow, images and simulation are
  unchanged. Historical procedure bodies and prior DEVLOG entries are preserved
  as described above.

These are software/documentation checks. The physical findings remain those
recorded before this cleanup.
