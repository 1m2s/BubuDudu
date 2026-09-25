# Build, host tests and bench procedure

Use matching `bubu` and `dudu` builds from the same firmware checkpoint. The current baseline is `1b10419`; [validation](validation.md) records what was actually observed. The old root [handshake guide](../SLEEP_HANDSHAKE_TEST.md) documents simulated sleep and is not the procedure for this firmware.

## PlatformIO setup and builds

The repository uses Arduino on the PlatformIO `espressif32` platform, with `esp32-c3-devkitm-1` as the build profile for the physical ESP32-C3 Super Mini boards. Device identity is selected by `DEVICE_BUBU` / `DEVICE_DUDU` in [platformio.ini](../platformio.ini).

Use the PlatformIO VS Code terminal, or install the locally verified Core version in a virtual environment outside the checkout:

```bash
python3 -m venv "$HOME/.venvs/bubududu"
source "$HOME/.venvs/bubududu/bin/activate"
python -m pip install 'platformio==6.2.0'
cd ~/bubududu
pio run -e bubu
pio run -e dudu
```

If the extension's `pio` is not on PATH, its existing macOS/Linux installation can be invoked directly:

```bash
"$HOME/.platformio/penv/bin/pio" run -e bubu
"$HOME/.platformio/penv/bin/pio" run -e dudu
```

Build-only commands do not upload, reset or open a monitor. The extra script enumerates USB metadata and may write ignored `.pio/ports.ini`; a connected device is not required. The platform and library dependencies in `platformio.ini` are currently unpinned. Pinning Core alone does not make the whole toolchain reproducible; record resolved versions alongside any future validation.

## Upload and monitor: hardware actions

These commands alter or interact with physical boards; use them only when deliberately running a bench session:

```bash
pio run -e bubu -t upload
pio run -e dudu -t upload
# Use separate terminals when watching both devices:
pio run -e bubu -t monitor_auto
pio run -e dudu -t monitor_auto
```

The [port-selection script](../tools/pio_select_port.py) chooses the board by configured MAC and USB serial number, checks whether its port is busy, and runs the monitor at 115200 baud. Its automatic hardware discovery currently expects macOS `/dev/cu.*` ports. Linux/Windows build support does not imply portable automatic upload/monitor support. Do not add guessed `upload_port` values to the project: this script rejects an explicit project upload port.

If a candidate USB device exposes no serial number, upload/monitor selection may fall back to an esptool MAC probe that resets that board. Close competing monitors first. Deep sleep can disconnect USB; reconnect after wake. `p` reprints the wake report, not every earlier TX log. A missing early USB log is not proof that the operation never happened.

## Host tests

```bash
bash tests/host/run.sh
```

The script uses `${CXX:-c++}`, C++11, `-Wall -Wextra -Werror`, AddressSanitizer, UndefinedBehaviorSanitizer and frame pointers. Bash and a compatible host compiler/runtime are required. It compiles into a temporary directory printed at the end; no flashing or Git operations occur.

Local validation uses Apple Clang 17; CI selects Clang through the existing `CXX` option. Ubuntu GCC 13 currently rejects three pre-existing single-line `for`/`assert` test statements under `-Werror=misleading-indentation`. Source formatting was left unchanged in this pass. To use the CI compiler family locally, run `CXX=clang++ bash tests/host/run.sh`.

These tests include production implementation files with deterministic platform substitutes. They exercise decisions and boundaries, not physical RF, sleep current, actual USB reconnection or scheduling latency.

| Test source | What it checks | Identities |
| --- | --- | --- |
| [sleep_handshake_test.cpp](../tests/host/sleep_handshake_test.cpp) | Actual FSM/application loop, EVENT/ACK, duplicate/stale controls, collision, phase/hard deadlines, cancellation, ID/time rollover, RTC restart, transport drain, physical-entry gates, one-shot motion peer wake | Bubu + Dudu |
| [cc1101_sleep_arm_test.cpp](../tests/host/cc1101_sleep_arm_test.cpp) | Readiness profile, bounded SPI/state waits, FIFO/GDO preservation | Shared helper |
| [rtc_state_test.cpp](../tests/host/rtc_state_test.cpp) | Magic/version/flags/checksum, save/load/invalidate, ID edges | Shared helper |
| [cc1101_wake_test.cpp](../tests/host/cc1101_wake_test.cpp) | Retained packet inspection, malformed/overflow rejection, ACK/RX faults, awake duplicate re-ACK, wake masks, entry abort cleanup | Bubu-defined helper fixture |
| [cc1101_wake_tx_test.cpp](../tests/host/cc1101_wake_tx_test.cpp) | ACK matching, same-ID/payload retries, timeout cutoff, one recovery budget, retained-FIFO refusal | Bubu + Dudu |
| [espnow_drain_test.cpp](../tests/host/espnow_drain_test.cpp) | Actual wrapper accounting for early/late/failed callbacks and rejected sends | Bubu + Dudu |
| [motion_sleep_test.cpp](../tests/host/motion_sleep_test.cpp) | Activity-only preparation, source clearing, stuck HIGH, readback/I2C failures, abort restoration | Shared driver |

The full suite must pass before interpreting a build as a verified checkpoint. A build alone checks compilation/linkage; the host suite cannot replace the board tests below.

## Current serial controls

| Command | Effect |
| --- | --- |
| `p` | Power/transaction/transport status; repeat deep-wake report if applicable |
| `h` | Toggle automatic ESP-NOW heartbeats; does not stop pending work |
| `i` | Request IDLE from ACTIVE |
| `s` | Initiate coordinated sleep negotiation from eligible IDLE |
| `a` | Inject activity/cancel negotiation while CPU is awake; cannot wake a physically sleeping CPU |
| `d` | Toggle non-blocking 1 s delay on newly queued controls; retry/deadline values unchanged |
| `x` | Manual local deep-sleep entry after runtime/drain guards and hardware preparation |
| `w` | One existing bounded CC1101 wake transaction from an eligible awake board |
| `?` | Help |

`h` and `d` are RAM-only bench flags and reset on reboot. One stale startup banner still describes the earlier simulated-sleep checkpoint: successful current handshakes **enter real deep sleep**. [Audit details](repository-audit.md).

## Physical regression sequence

Record commit, board roles, wiring, supply, command order and both serial streams. Keep the 30-second timer enabled. Settle both boards so movement does not race sleep entry. Before each manual `x`/`w` scenario, restore any powered-off peer unless absence is the test, pause heartbeats with `h` again after reboot, and use `p` to confirm ACTIVE/IDLE with `pending=0 queued=0`. These commands refuse busy or ineligible states.

1. **Normal transport:** start both boards; confirm bidirectional EVENT/ACK and matching IDs. Enter `h` on both, check `auto_heartbeats=PAUSED`, then wait for `pending=0 queued=0`.
2. **Coordinated sleep:** enter `i` on both, then `s` on Bubu. Expect REQUEST/READY/COMMIT/SLEEP_ACK with packet receipts interleaved, transport drain, one execution attempt per board, Motion/CC1101 ready and deep-sleep entry. Let both timer-wake to ACTIVE. Repeat with Dudu coordinating, pausing heartbeats again after reboot.
3. **Simultaneous requests:** pause/drain both, enter `i` on both and enable `d`. Send `s` on both within about half a second. Confirm both initially coordinate, Bubu's lower DeviceId wins, Dudu adopts that transaction, and both sleep. A run where one was already a participant is not evidence of collision arbitration. After reboot the delay flag resets.
4. **Cancellation and missing participant:** with `d` enabled, use `a` before COMMIT; expect cancellation and bounded exit with no execution. Separately turn the peer off and request sleep from IDLE: initial REQUEST plus at most two same-ID retries, then failure/cooldown without sleep or automatic re-negotiation. Wait for cooldown before the next test.
5. **CC1101 wake:** Bubu `x`, then awake Dudu `w`; expect Bubu GPIO4/`source=CC1101`, retained EVENT processing, ACK and RX ready. Dudu must match the ACK. Repeat reversed. The receiving board must not print a motion-triggered return wake.
6. **Motion propagates wake:** put both into sleep using `x`, then deliberately move only Bubu before either timer expires. Expect Bubu `source=MOTION`, one `MOTION PEER WAKE | one-shot request`, and a bounded CC1101 transaction. Dudu should wake through GPIO4, process and ACK. Both become ACTIVE; Dudu sends no automatic return wake. Repeat with only Dudu moved.
7. **Timer exclusion:** put one board to sleep several seconds before the other; do not move or transmit. When the first timer fires, it must report TIMER and emit no automatic CC1101 wake. The second should stay asleep until its own timer or another real source.
8. **Unavailable peer:** power the peer off, sleep the local board with `x`, then move it. Expect one wake transaction, attempts `0/1/2` using the same ID, then `GIVE_UP ... retries=2`. In the verified case `RX_READY=1`, local state stayed ACTIVE and Serial remained responsive. Ordinary ESP-NOW heartbeats may resume after reboot; distinguish them from a second CC1101 transaction.

The lost-first-ACK receiver path was physically proved using temporary deterministic suppression, then that firmware instrumentation was removed. Keep it removed for normal operation. Its permanent host tests reproduce a lost receipt through mocks; random RF loss is not a controlled replacement for that earlier experiment.

## CI and evidence

[GitHub Actions](../.github/workflows/firmware-ci.yml) runs the host suite and both firmware builds on push/pull request. It does not flash boards or establish physical validation. Check the actual run result; a workflow file alone is not a passing CI run.

Save future real captures according to [assets guidance](assets/README.md), and update the [validation record](validation.md) with the tested revision and limitations. Do not edit firmware simply to make a host or CI check green.
