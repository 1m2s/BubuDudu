# v1 cleanup checklist

The October 4 audit found 22 items. Documentation and comments were cleaned up;
firmware behavior stayed the same. **Code changes and physical checks are saved
for a future version.** “Later” below means the issue has not been fixed.

| Item | Done / later |
| --- | --- |
| F1 — Writing | Current docs use plain project language and first person for my observations. |
| F2 — Versions | [README](../README.md#which-code-was-tested) holds the version map and verified CI link. |
| F3 — Old instructions | Moved both old procedures to [history](history/README.md). Added current sleep deadlines to Architecture. |
| F4 — RSSI/callbacks | Fixed comments and docs: signal strength affects radio choice; Wi-Fi callbacks log. The old `diagnostics only` output string stays for now. |
| F5 — Time estimate | Removed the unsupported 14-day estimate. |
| F6 — Coursework | Kept the Colorado course reference simple. Added RWU subjects and links to project concepts from the supplied course materials. |
| F7 — Unused drivers | Marked `RadioTask`/`CC1101Radio` as unused. Removal and binary-size checks come later. |
| F8 — Module names | Explained each module's real job. Renaming or moving code comes later. |
| F9 — Pins | [Interfaces](../INTERFACES.md) is the current map. One shared pin file and replacing the literal GPIO4 come later. |
| F10 — Old wording | Fixed comments and explained old names, simulated-wake strings and the OLED's missing button-wake label. Code/output changes come later. |
| F11 — Event handling | Explained the saved-packet path. Replacing its flag, separating cache size and removing the disabled test switch come later. |
| F12 — Button guide | Rewrote it around current behavior, timing and limits. |
| F13 — Hardware/upload | Added parts, supplies, resistor, GDO2, CS hold and upload/MAC details. Rechecking wiring and sharing MAC definitions come later. |
| F14 — Tests | Corrected counts and explained fake hardware and the missing two-device case. New tests and shorter test output come later. |
| F15 — Private paths | Removed local paths and work-session narration from current media/test guides. Kept file hashes and dated history. |
| F16 — Repetition | Put evidence labels in [final results](../FINAL_FIRMWARE_TEST.md#evidence-levels) and cut repeated caveats. |
| F17 — Hard wording | Shortened explanations of sleep, recovery and module jobs. |
| F18 — Formatting | Simplified header comments. A full code-format pass comes later. |
| F19 — Radio helpers | Explained the radio sequence in comments. Shared helpers and named register values come later. |
| F20 — DEVLOG | Added a short reading guide and new checkpoints. Original dated entries stay as history. |
| F21 — GitHub description | Describes the device and demo without claiming measured power efficiency. |
| F22 — Boot LED flash | Recorded a possible GPIO21 boot-output issue. It has not been observed or tested. |

## Future code and hardware work

Start with the [one-moving-device test](V1_REFLECTION.md#saved-restart-point).
Decide when proximity expires, becomes UNKNOWN or gets checked again. Use two
independent device models, then test the chosen rule on hardware.

Other work can follow in small steps:

- Investigate radio errors, sleep recovery and OLED problems. Check the wiring
  and possible boot LED flash; measure current and runtime.
- Give proximity its own module and give the CC1101 functions clearer names.
- Share pin/MAC definitions and remove unused drivers.
- Use a clear live/saved event type, give the duplicate cache its own size and
  remove the disabled test switch.
- Share radio helpers, name register values, update old strings, format the code
  and reduce tests' dependence on internal variable names.

Keep the protections that matter: fixed retry IDs, checked saved state, fixed
wake levels, one I²C owner, one SPI owner at a time and no erased unread packets.

## Cleanup verification

The comment cleanup kept all **28,052 non-comment C++ tokens unchanged** across
`src/` and `include/`. Seventeen files had comment edits only. The full host suite
and both firmware builds passed, including [GitHub CI](https://github.com/1m2s/BubuDudu/actions/runs/37229553752).

Later documentation edits leave all firmware, tests and build settings unchanged.
Links, headings and media hashes are checked. No new physical result is claimed;
those remain in [final results](../FINAL_FIRMWARE_TEST.md).
