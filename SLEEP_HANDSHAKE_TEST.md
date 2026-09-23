# ESP-NOW sleep-handshake checkpoint

Both ESP32s remain physically awake, including in simulated `SLEEPING`.
Only ESP-NOW and PowerManager are active in this experiment. Flash both matching
builds together when ready; an older firmware does not understand these controls.

## Wire format and ownership

`Protocol::Message` is still eight bytes. EVENT=1 and packet ACK=2 are unchanged.
Message types 3..7 are SLEEP_REQUEST, SLEEP_READY, SLEEP_COMMIT, SLEEP_ACK,
and SLEEP_CANCEL. Controls set `event=None` and use `ackForMessageId` as `sleepId`.
REQUEST's `messageId` equals `sleepId`; other controls have their own delivery
message IDs. IDs come from the existing allocator, with no separate sleep counter.

An ordinary ACK acknowledges a packet ID, including a stale control that was
received successfully. SLEEP_ACK is a separate semantic acceptance of COMMIT.
A receipt ACK cannot advance a sleep phase or establish agreement about sleep.

The Wi-Fi callback still only copies into the eight-entry RX queue. In `loop()`:

1. PowerManager checks deadlines and serial activity.
2. Received packets are drained, bounded to eight per iteration. Control packets
   receive a packet ACK, then PowerManager validates transaction/role/phase.
3. Cancelled or superseded controls are discarded. The four-entry control outbox
   and EVENTs share one reliable pending packet: 300 ms timeout, two retries,
   same message ID. Matching semantic replies can supersede an earlier control
   whose receipt ACK was lost (e.g. READY proves REQUEST was received).
4. Controls have priority. New automatic heartbeats are permitted only in
   ACTIVE/IDLE and only when the transport is free. RX/ACK/retry processing stays
   active in simulated SLEEPING. An already pending EVENT is allowed to finish.

PowerManager owns all policy and semantic state. Coordinator waits for READY,
then for SLEEP_ACK. Participant waits for COMMIT, schedules SLEEP_ACK, and closes
successfully only when the radio accepts the SLEEP_ACK send request. The final
packet still gets bounded delivery retries after local simulated sleep begins.

Phase limit: **3 seconds**. Hard transaction limit: **5 seconds**. Failure/activity
cooldown: **3 seconds**. No retries, duplicates, or collision can extend the hard
limit. Nothing automatically restarts a closed transaction. Simulated activity
wake completes WAKING -> ACTIVE after 250 ms.

Freshness is checked for REQUEST as well as replies. Repeated current REQUESTs
can resend READY without resetting deadlines. Duplicate READY can resend COMMIT.
Repeated responses coalesce with an outstanding packet without resetting its
retry budget. A duplicate COMMIT during the transaction schedules the same
response. After successful completion, only an **exact duplicate of the accepted
COMMIT packet** can replay SLEEP_ACK while still simulated SLEEPING. This receipt
cache cannot create a transaction or repeat a transition; waking clears it.
Other closed/mismatched/out-of-phase controls are rejected.

Simultaneous WAIT_READY coordinators use lower numeric DeviceId as tie-break.
The loser drops its old transport work, adopts the winning ID as participant,
and retains its original hard deadline. It sends no CANCEL for the losing ID:
opposite devices can independently allocate equal numeric IDs. No role takeover
is permitted after COMMIT. Either device can initiate ordinary transactions.

## Commands and preparation

Open both serial monitors at **115200 baud**. Commands are lowercase; Enter is
optional. State names in `p` are semantic states, not measurements of CPU power.

| Command | Meaning |
| --- | --- |
| `p` | Power state, transaction/deadlines, transport pending/queued status |
| `i` | ACTIVE -> IDLE; does not bypass negotiation or cooldown |
| `s` | Request a real handshake; requires IDLE, no cooldown and drained transport |
| `a` | Cancel active negotiation and become ACTIVE; or wake simulated SLEEPING |
| `h` | Toggle automatic heartbeats, without stopping pending retries/ACKs |
| `d` | Toggle a non-blocking 1-second initial delay on newly queued controls |
| `?` | Help |

The former `f`/`z` simulation shortcuts are removed. `d` does not change packet
retry intervals or phase/hard deadlines; CANCEL and packet ACK are never delayed.

First check ordinary two-way EVENT/ACK traffic. Then send `h` on **both** boards;
`p` must show `auto_heartbeats=PAUSED`. Wait for `pending=0 queued=0` on both.
This avoids a leftover heartbeat cancelling the test as genuine activity.

**Put both boards into IDLE before a successful test.** The participant acceptance
rule requires IDLE; leaving the receiving board ACTIVE will reject REQUEST and
send a best-effort CANCEL. Do not interpret that as a radio failure.

Between successful tests: send `a` on both, wait at least 250 ms, check ACTIVE with
`p`, then `i` on both. Between cancelled/failed tests: wait for `COOLDOWN_LEFT_MS=0`
before `i`/`s`. The `h`/`d` settings persist until toggled or reset; always check `p`.

## Physical tests

### 1. Bubu initiates

With heartbeats paused, controls undelayed, transport drained, and both IDLE,
send `s` on Bubu. Look for this order (delivery ACK logs interleave):

```text
Bubu: POWER TX SLEEP_REQUEST | sleepId=N ...
Dudu: POWER RX SLEEP_REQUEST | sleepId=N ...
Dudu: POWER TX SLEEP_READY   | sleepId=N ...
Bubu: POWER RX SLEEP_READY   | sleepId=N ...
Bubu: POWER TX SLEEP_COMMIT  | sleepId=N ...
Dudu: POWER RX SLEEP_COMMIT  | sleepId=N ...
Dudu: POWER TX SLEEP_ACK     | sleepId=N ...
Dudu: POWER: SLEEP_NEGOTIATING -> SLEEPING | HANDSHAKE_COMPLETE
Bubu: POWER RX SLEEP_ACK     | sleepId=N ...
Bubu: POWER: SLEEP_NEGOTIATING -> SLEEPING | HANDSHAKE_COMPLETE
```

Use `p` on both: LOCAL=SLEEPING, PEER=SLEEPING, TXN_ACTIVE=0. Closed transaction
fields reset to NONE/0; use the preceding logs to compare the accepted sleepId.
After its final receipt ACK, the participant should also show pending=0.

### 2. Dudu initiates

Wake both simulated states with `a`, wait 250 ms, then `i` on both and `s` on Dudu.
Expect the same exchange with Dudu coordinating and Bubu participating.

### 3. Simultaneous initiation

Prepare both IDLE with heartbeats paused. Enable `d` on both; check
`control_delay_ms=1000`. Enter `s` on both within approximately half a second.
This gives both time to become coordinators before either REQUEST is sent.
Expect explicit `simultaneous requests` logs: DeviceId Bubu=1 beats Dudu=2.
Dudu adopts Bubu's sleepId; both finish with that single winning transaction.
If one board became participant before its `s`, it will refuse that command;
that was an ordinary successful handshake, not proof of collision handling.
Repeat if needed, then toggle `d` off on both after the test.

### 4. Participant missing

Turn off one board. Leave automatic heartbeats paused on the remaining board,
set it IDLE, and send `s`. Expect initial REQUEST plus at most two same-message-ID
retries, then CONTROL_RETRIES_EXHAUSTED, IDLE, peer OFFLINE and cooldown. Normally
this occurs around 900 ms after the initial send, before the phase timeout.
Wait ten seconds; `p` must still show no active transaction. An `s` attempted
during cooldown must be refused. No endless retry or automatic restart is allowed.
Reset both boards before the next test if the powered-off peer restarted.

### 5. Activity during negotiation

Enable `d` on both and prepare both IDLE. Start with `s` on Bubu. When Dudu logs
RX SLEEP_REQUEST (about one second later), enter `a` on Bubu **before COMMIT**.
Expect Bubu's CANCEL, immediate ACTIVE, closed transaction and cooldown. Dudu
receives CANCEL and returns IDLE with cooldown. Wait past both deadlines; neither
may enter SLEEPING from delayed controls. Repeat with activity on Dudu if useful.
If CANCEL is lost, the other board must leave negotiation by its own deadline.
Toggle `d` off afterward. Host tests also inject a late COMMIT after cancellation.

### 6. Stale packet protection (host)

Run `bash tests/host/run.sh`. Tests inject mismatched READY/COMMIT/SLEEP_ACK/CANCEL,
wrong sender/phase, closed REQUEST, and late COMMIT after activity. They assert
that none can advance a transaction or create a new one. No unsafe arbitrary
packet injection command is exposed on the physical boards.

### 7. Duplicate controls (host)

The same script repeats REQUEST and COMMIT, checks READY/SLEEP_ACK responses,
tests the completed-COMMIT receipt replay, and asserts there is only one final
transition. It also verifies pending responses retain their message ID and retry
budget. Physical RF loss may show the same duplicate logs, but host injection
makes the cases deterministic without modifying the working protocol.

Finally send `a` on both, wait 250 ms, and restore automatic heartbeats with `h`
on both. Verify normal ACK matching and bidirectional communication again.

## Limits to record, not hide

- This four-message exchange cannot guarantee agreement under permanent packet
  loss. If all SLEEP_ACK transmissions are lost, participant may be simulated
  SLEEPING while coordinator times out to IDLE/peer UNKNOWN. If all final packet
  receipts are lost, participant reports peer UNKNOWN. Both cases are bounded;
  no additional commit/ack protocol or radio fallback is hidden in this test.
- CANCEL is best effort, and only applies to an active matching transaction.
  An activity race after the other device already completed can leave different
  simulated states. Do not infer real-sleep safety from a clean happy-path run.
- New application EVENTs during negotiation cancel it. Duplicate old EVENTs are
  re-ACKed without executing activity again. Late receipt ACKs for COMMIT/CANCEL
  do not overwrite UNKNOWN or SLEEPING with ONLINE.
- REQUEST freshness uses 16-bit serial-number ordering, assuming fewer than
  32768 allocated message IDs between observed requests. There is no session
  nonce or reboot negotiation in this unchanged-size checkpoint. A rebooted
  peer's reset counter, very long gaps, and counter reuse need a later explicit
  policy; reset both boards for repeatable bench sessions.
- Host tests run the production FSM and the actual queue/loop code with fake
  time/radio, for both device identities, under address/undefined sanitizers.
  They do not measure RF loss, USB timing, scheduling latency or actual power.

No flashing, commits, pushes, stashes, or branch operations are performed by the
host test script. `pio run -e bubu -e dudu` builds firmware without uploading.
