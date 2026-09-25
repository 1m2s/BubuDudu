# Repository audit

Scope: the active call paths and tracked text at firmware checkpoint `1b1041976b4200668c3090d27c8138dc80aa53f2`. This polish pass changes documentation and adds CI; source, headers, tests, PlatformIO configuration and the port-selection script remain byte-for-byte unchanged. No feature branch was merged or deleted.

## Active, dormant and historical material

| Material | Classification | Decision |
| --- | --- | --- |
| `main.cpp`, Protocol, PowerManager, ESPNowRadio | Active application and semantic policy | Document ownership, delivery and sleep boundaries; preserve implementation |
| CC1101Bus, CC1101SleepArm, CC1101WakeRecovery, CC1101WakeTx | Active radio wake path | Document retained-state inspection and bounded recovery; preserve implementation |
| Motion, RtcState | Active sensor preparation and retained history | Document startup order and narrow retained-state scope |
| RadioTask and CC1101Radio | Dormant subsystem prototype in this application | Still compiled, but `main.cpp` does not start the task or call this driver; keep for history and possible future comparison |
| Display and LED | Dormant, historically tested drivers | Not initialized/called by current `main.cpp`; keep without claiming integrated UI behavior |
| Historical feature branches | Separate checkpoints, not the current architecture | Keep; do not import their implementation wholesale |

Dormant does not mean safely deletable. The intended future role of the older modules is uncertain. Their headers/callers still compile, so there is no demonstrated interface break that warrants cleanup here.

One concrete dormant-code concern remains: [`CC1101Radio::receivePacket()`](../src/CC1101Radio.cpp) masks RXBYTES to seven bits and reads that count into `raw[64]` without an explicit upper-bound check. Several restart paths also ignore the result of `startReceive()`. These findings describe the older file **in this integration tree**, not the separately verified `feature/cc1101` implementation or the active wake helpers. They are not evidence of an active-runtime overflow, and they were not repaired during documentation work. Re-audit before enabling this driver.

## Documentation reconciled

| Document | Treatment |
| --- | --- |
| [README](../README.md) | Current entry point, status and navigation |
| [ARCHITECTURE](../ARCHITECTURE.md) | Original early product sketch retained, marked historical; current model in [docs/architecture.md](architecture.md) |
| [INTERFACES](../INTERFACES.md) | Original interface budget retained, marked historical; verified assignments in [hardware.md](hardware.md) |
| [REQUIREMENTS](../REQUIREMENTS.md) | Product intent retained with an implementation-status map |
| [SLEEP_HANDSHAKE_TEST](../SLEEP_HANDSHAKE_TEST.md) | Original simulated-only procedure retained and marked historical; current real-sleep procedure in [testing.md](testing.md) |
| [DEVLOG](../DEVLOG.md) | Documentation history through September 25 copied exactly from `main` at `06ac661`, extending the older log on the firmware base; no source merge or history rewrite |

Some source comments and the serial startup banner still refer to simulated sleep or manual-only RTC saving. Those descriptions are obsolete; the current call path executes real coordinated deep sleep. The old `design/pin-map` GPIO4-unused/GDO0-unconnected assumption is also superseded by confirmed wiring. Current docs call out these discrepancies rather than changing protected firmware strings or comments in this pass.

## Build and repository hygiene

The existing [`.gitignore`](../.gitignore) excludes `.pio` and generated VS Code files. It intentionally does not hide documentation assets or the tracked editor recommendation file. No ignore changes or generated build artifacts are needed in the commits.

The new [CI workflow](../.github/workflows/firmware-ci.yml) runs host tests and build-only PlatformIO commands, with read-only repository permissions and official Actions pinned by commit. PlatformIO Core is pinned to `6.2.0`; firmware platform/library versions in `platformio.ini` remain unpinned. Download caching is not a dependency lock.

A local archived checkout built both environments without an initial project `.pio` directory or connected-board discovery. The existing script generated its ignored `ports.ini`. That check reused installed global PlatformIO packages; it was not a fresh-machine or Linux-runner test. Remote CI success must be checked on the actual run, not inferred from workflow syntax validation.

Automatic upload/monitor discovery currently assumes macOS `/dev/cu.*` devices. Build portability does not establish upload portability. See [testing.md](testing.md) for the supported commands and USB reset/probe caveat. No firmware/tooling changes were made to satisfy CI.

## Security and personal-data review

A heuristic review of current tracked text found no obvious API keys, private keys, tokens, passwords, Wi-Fi credentials or personal absolute filesystem paths. This was not a full Git-history scan or security certification. Intentional board MAC addresses remain in configuration and documentation because they identify the two devices; they are not authentication secrets.

The current radio protocol is not authenticated. ESP-NOW peer encryption is disabled, and application checks use the payload sender ID rather than authenticating callback source MAC. CC1101 validates expected message fields but does not provide application authentication. The limited duplicate/freshness history is also not a security replay defense. These boundaries belong in [protocol.md](protocol.md), not hidden behind a reliability claim.

## Deliberately left for later

- Archive real hardware photographs, paired logs and analyzer captures with test metadata.
- Evaluate dependency pinning and upload portability as separate tooling changes.
- Review stale source comments/banner and dormant drivers before their next relevant code checkpoint.
- Test combined-pin electrical wake and longer-run/current/RF behavior before expanding validation claims.
- Keep awake-motion sensitivity, proximity/UI behavior, battery integration and general radio fallback as separate firmware work.
