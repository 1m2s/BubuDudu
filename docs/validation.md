# Validation record

This record applies to firmware `1b1041976b4200668c3090d27c8138dc80aa53f2`, not every historical file or subsystem branch. The [DEVLOG](../DEVLOG.md) records checkpoint-by-checkpoint outcomes. Physical results below are developer-reported bench observations preserved there; a complete archive of raw captures has not yet been committed.

## Evidence levels

- **Implemented:** visible in the active call path, not inferred from an unused module.
- **Host-tested:** deterministic tests of production code with platform substitutes.
- **Physically tested:** a stated two-board/individual-board bench scenario passed.
- **Not established:** no claim about RF range, battery life, arbitrary packet loss, electrical margins or final product readiness.

## Verified scenarios

| Scenario | Host coverage | Physical evidence / scope |
| --- | --- | --- |
| Bidirectional ESP-NOW EVENT/ACK, bounded same-ID retries and duplicate handling | Application loop and wrapper tests | Earlier ESP-NOW checkpoint; [Sept 22](../DEVLOG.md#2026-09-22) and subsequent regressions |
| Coordinator/participant sleep, both initiators | FSM and actual loop | Both boards entered real sleep and timer-woke; [Sept 24](../DEVLOG.md#2026-09-24) |
| Simultaneous sleep requests | Delayed collision and deadline tests | DeviceId arbitration and successful completion with roles converged; [Sept 23–24](../DEVLOG.md#2026-09-23) |
| Activity cancellation and missing participant | Late controls, timeout/cooldown, no restart | Bench cancellation and absent-peer failure; [Sept 24](../DEVLOG.md#2026-09-24) |
| RTC history restored after deep sleep | Invalid/version/checksum/rollover and restart tests | Restore OK after timer/radio/motion wake; not a power-loss journal |
| GPIO4 retained CC1101 packet → EVENT → ACK | FIFO/format/bounds, recovery and TX tests | Both directions, packet survives ESP32 reboot; [Sept 24–25](../DEVLOG.md#2026-09-24) |
| Lost first wake ACK → awake duplicate re-ACK | No repeated application delivery, bounded TX/RX failures | Deterministic temporary first-ACK suppression passed both directions, then removed; [Sept 25](../DEVLOG.md#2026-09-25) |
| GPIO3 motion deep wake; timer remains independent | Wake classification, Motion register/failure tests | Both boards motion-woke; timer did not fabricate radio delivery; [Sept 25](../DEVLOG.md#2026-09-25) |
| GPIO3 + GPIO4 together | Both-bit classification and safe retained recovery | Simultaneous physical stimulus not recorded |
| Motion on one sleeping board wakes its sleeping peer | Pure-motion startup policy, one allocation, no loop rearm | Bubu → Dudu and Dudu → Bubu passed; [Sept 25](../DEVLOG.md#2026-09-25) |
| CC1101-only/timer wake does not return-wake peer | Radio/both/timer/cold suppression | CC1101-only and timer exclusion passed; [Sept 25](../DEVLOG.md#2026-09-25) |
| Motion wake with peer powered off | TX cutoff and application one-shot tests | GIVE_UP after two retries; RX ready, ACTIVE/responding, later peer OFFLINE |

The current EVENT cache remembers the most recent peer EVENT; it is not unbounded exactly-once delivery across all resets or reordering. The dedicated awake wake-receipt cache handles retries of the boot-accepted radio EVENT. Tests establish these specific rules, not a general distributed agreement guarantee.

## Checkpoint anchors

| Commit | Meaning |
| --- | --- |
| `d24d72c` | ESP-NOW callback-to-FreeRTOS receive queue |
| `baea754` | Bounded coordinated sleep handshake, including final ACK-send phase |
| `d62f0fc` | Coordinated agreement connected to real deep sleep |
| `89014ad` | Motion initialization after retained radio recovery |
| `ed1d227` | Awake CC1101 retry/re-ACK without duplicate application execution |
| `4fe8171` | ADXL345 GPIO3 deep wake alongside GPIO4 and timer |
| `8ce8f23` | Explicit integration of that verified feature; tree matches `4fe8171` |
| `1b10419` | One-shot local-motion wake → bounded CC1101 peer wake |

The polish branch is based on `1b10419`. Its firmware, headers, tests, PlatformIO configuration and port-selection script are compared byte-for-byte against that base. Documentation/CI work is not a new physical firmware checkpoint.

## Repository-polish verification (September 26, 2026)

| Check | Local result |
| --- | --- |
| `bash tests/host/run.sh` | All 10 sanitizer-enabled binaries passed, including both application identities |
| `pio run -e bubu -e dudu` | Both firmware environments compiled and linked successfully |
| `git diff --check` | Passed |
| Source/header/tests/config/tool comparison with `1b10419` | No differences |
| Markdown navigation | All 170 relative links/anchors checked |
| GitHub Actions definition | YAML and actionlint checks passed; remote execution is a separate result |

The builds used PlatformIO Core `6.2.0`, Espressif32 platform `7.1.2`, Arduino package `4.20017.260907+sha.dcc1105b`, RISC-V toolchain `8.4.0+2021r2-patch5`, NeoPixel `1.15.5` and U8g2 `2.36.18`. A separate archived-project check passed without an initial `.pio` directory, while reusing installed global packages. These are build/test observations; no board was flashed or newly physically tested during repository polish.

## Representative evidence, not new captures

These excerpts are transcribed in the September 25 DEVLOG:

```text
CC1101 AWAKE RETRY | id=69 | re-ACK started
CC1101 WAKE TX | id=536 | GIVE_UP | retries=2 | reason=ACK_TIMEOUT | RX_READY=1
POWER: LOCAL=ACTIVE PEER=OFFLINE
TRANSPORT: pending=0
```

They describe different bench cases, not one continuous trace. IDs 69 and 536 are examples, not constants. Do not present these excerpts as a fresh capture or substitute them for two-sided logs.

## Remaining evidence work

Archive real wiring photos, paired serial logs and original logic-analyzer files with commit, board identity, supply, test steps and expected/observed result. No new photos or instrument traces were generated for this polish pass. [Asset guidance](assets/README.md) defines where and how to add them.

Useful future evidence includes repeated cycles over longer runs, both-pin electrical wake, measured current, supply integrity and RF performance under stated conditions. Battery-life claims, exact distance from RSSI, a finished emotional UI, and production readiness remain unsupported. Follow the [bench procedure](testing.md) before expanding any claim.
