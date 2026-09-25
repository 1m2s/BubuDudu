# Protocol

This describes the firmware checkpoint `1b1041976b4200668c3090d27c8138dc80aa53f2`. The wire definition is [Protocol.h](../include/Protocol.h); application processing and ESP-NOW reliability live in [main.cpp](../src/main.cpp). Sleep-control policy is documented in [power management](power-management.md).

## Packet layout

`Protocol::Message` is packed and statically checked to be exactly eight bytes. The firmware transmits its memory representation directly and receives it with `memcpy`; the 16-bit fields are little-endian on the ESP32-C3. There is no explicit portable serializer or session nonce.

| Byte offset | Size | Field | C++ type | Meaning |
| --- | --- | --- | --- | --- |
| 0 | 1 | `version` | `uint8_t` | `1` |
| 1 | 1 | `type` | `MessageType : uint8_t` | Packet kind below |
| 2 | 2 | `messageId` | `uint16_t` | This packet's ID |
| 4 | 1 | `sender` | `DeviceId : uint8_t` | Bubu `1`, Dudu `2` |
| 5 | 1 | `event` | `EventType : uint8_t` | None `0`, Heartbeat `1` |
| 6 | 2 | `ackForMessageId` | `uint16_t` | Receipt target or sleep transaction ID |

| Value | Type | `event` emitted | `ackForMessageId` emitted |
| --- | --- | --- | --- |
| 1 | `Event` | `Heartbeat` | `0` |
| 2 | `Ack` | `None` | Received packet's `messageId` |
| 3 | `SleepRequest` | `None` | Its own `messageId`, the new `sleepId` |
| 4 | `SleepReady` | `None` | Request's `sleepId` |
| 5 | `SleepCommit` | `None` | Request's `sleepId` |
| 6 | `SleepAck` | `None` | Request's `sleepId` |
| 7 | `SleepCancel` | `None` | Request's `sleepId` |

One loop-owned `uint16_t` allocator supplies outgoing IDs, initially `1`. Every new EVENT, receipt ACK and control response gets a new packet ID; REQUEST uses the ID already allocated for its transaction. Retries reuse the original packet and ID. Rollover includes valid IDs `65535` and `0`; zero is not an invalid-ID sentinel. [RTC history](power-management.md#history-across-deep-sleep) preserves the allocator across a valid deep wake.

ESP-NOW sends these eight bytes as the payload. The CC1101 wake path uses the same payload with a one-byte length prefix of `8`: a retained FIFO packet is nine bytes under the current profile, which disables appended status bytes. That prefix is radio framing, not a ninth protocol field. See [CC1101 wake recovery](../src/CC1101WakeRecovery.cpp) and [wake transmission](../src/CC1101WakeTx.cpp).

## Submission, receipts and semantic acknowledgment

| Observation | What it means | What it does not establish |
| --- | --- | --- |
| ESP-NOW send returns accepted | Driver accepted this submission | Peer receipt or semantic agreement |
| ESP-NOW send callback completes | The driver reported completion/success or failure for that submission | A matching application receipt or permission to sleep |
| `MessageType::Ack` | Peer application emitted a receipt for `ackForMessageId` | Acceptance of a sleep-control transition |
| `MessageType::SleepAck` | Participant accepted COMMIT for the referenced `sleepId` | Completion of every outstanding radio callback |

A valid sleep-control envelope receives an ordinary `Ack` **before** the PowerManager evaluates it. Stale or wrong-phase controls may therefore receive a packet receipt while being rejected semantically. Receipt ACKs are never ACKed themselves. `SleepAck` is a reliable control packet and does receive an ordinary receipt ACK.

The participant completes its semantic transaction when its `SleepAck` submission is accepted; the coordinator completes on receiving the matching `SleepAck` in `WAIT_ACK`. Merely queueing `SleepAck`, receiving the ordinary ACK for COMMIT, or seeing a successful driver callback cannot substitute for those transitions. Physical sleep has additional drain and failure gates.

## ESP-NOW delivery and ownership

[ESPNowRadio.cpp](../src/ESPNowRadio.cpp) configures station mode, channel `1`, Wi-Fi sleep disabled, TX power `8.5 dBm`, and the opposite board as an unencrypted peer. The callback logs the source MAC and forwards bytes; application sender validation uses the payload `DeviceId`, not a comparison with the callback's source MAC. These checks are not authentication.

The Wi-Fi receive callback copies only correctly sized messages into an eight-entry FreeRTOS queue without blocking. It does not change the ID allocator, deduplication history, retry state or power FSM. A full queue drops the message without a receipt, leaving the sender's retry budget to handle loss. `setup()` initializes state; `loop()` owns subsequent protocol and power-state changes.

Each loop handles serial activity/deadlines first, then at most eight queued receive packets, then obsolete-control removal, ACK timeout handling and the next queued control. Processing queued ACKs before timeout work avoids retrying a packet whose receipt is already waiting. The bounded batch leaves time for outgoing work.

There is one reliable pending packet and a four-entry control outbox. EVENTs and REQUEST/READY/COMMIT/SLEEP_ACK share the existing delivery mechanism:

- Wait `300 ms` for a matching ordinary ACK.
- Retry at most twice, with the same bytes and ID: at most three submissions.
- Restart the receipt timer on each submission attempt, including a rejected submission.
- Recheck whether a control is still needed at its actual send boundary, including retries.

Repeated responses with the same control type and `sleepId` reuse an already pending or queued packet, including its timer and retry budget. Superseded controls are retired. CANCEL bypasses that outbox as one best-effort send. Ordinary receipts are also sent directly, without their own reliable pending slot.

`ESPNowRadio::send()` increments an atomic in-flight count before calling the driver, rolls it back on rejection, and decrements it at the end of the send callback. This covers callbacks that arrive before the driver call returns. An independent atomic counter covers the receive callback. These are drain observations; they do not move the power FSM. See the [sleep-entry contract](power-management.md#physical-sleep-entry).

## Receive checks and duplicates

The ESP-NOW application path checks exact length, protocol version and the opposite `DeviceId`. Sleep controls additionally require `event == None`; REQUEST requires `ackForMessageId == messageId`. PowerManager independently checks those control-envelope conditions, then transaction freshness, role and phase. Unknown message types are logged and ignored.

The current ESP-NOW EVENT/ACK dispatch does not additionally enforce every emitted field convention in the table: for example, it does not require received EVENTs to have `Heartbeat`/zero `ackForMessageId`, or received ACKs to have `event == None`. CC1101 wake reception is stricter: only the expected peer's version-1 Heartbeat EVENT with zero `ackForMessageId` is delivered, and the wake transmitter checks ACK version, sender, type, event and target ID.

EVENT deduplication remembers only the most recently accepted peer EVENT ID. An equal ID is ACKed again without executing the event or cancelling a negotiation again. A different ID is treated as new; this is not a general replay window. Current EVENT execution is a serial log and power-policy notification, with LED/application forwarding still a future integration point.

Sleep REQUEST freshness instead uses 16-bit serial arithmetic: after a prior request, a new ID must advance by `1..32767`. Equal or older/ambiguous IDs are rejected. The watermark is recorded even when a fresh request cannot be accepted in the current state. An exact active-participant REQUEST is handled as a duplicate without advancing deadlines. An exact completed COMMIT can replay SLEEP_ACK only while semantic `SLEEPING` and only when both its `sleepId` and COMMIT packet ID match the retained completion record. Details are in [power management](power-management.md#arbitration-freshness-and-cancellation).

Cold resets have no wire session identifier and discard volatile history. One peer restarting its ID sequence can conflict with the other peer's freshness history; reset both peers together for that bench case. RTC retention addresses normal deep-wake continuity, not arbitrary cold-reset coordination.

## Evidence

[Host tests](../tests/host/run.sh) compile the production FSM/application code with deterministic time and radio substitutes. [sleep_handshake_test.cpp](../tests/host/sleep_handshake_test.cpp) exercises EVENT/ACK matching, same-ID retries, deduplication, controls, ID rollover and RTC restart behavior for both identities. [espnow_drain_test.cpp](../tests/host/espnow_drain_test.cpp) exercises the actual radio wrapper with early, late, failed and rejected-send callback cases.

Physical bidirectional ESP-NOW delivery, deliberately lost receipts, duplicate re-ACK and bounded retry behavior are recorded in the [September 18 development log](../DEVLOG.md#2026-09-18). Later physical handshake and wake checkpoints are recorded on [September 23](../DEVLOG.md#2026-09-23), [September 24](../DEVLOG.md#2026-09-24) and [September 25](../DEVLOG.md#2026-09-25). Host fault injection is separate evidence from those board observations; neither establishes delivery or agreement under every RF-loss condition.
