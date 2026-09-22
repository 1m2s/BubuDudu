# CC1101 remote-wake bench test

The ESP32-C3 **light-sleep** chain has been physically verified. A separate
**deep-sleep** command now tests the same wiring across an MCU reboot; its
physical verification is pending. CC1101 remains powered and listening in RX
until it receives a packet. WOR duty cycling is not enabled. RadioTask continues
to own all SPI and protocol work.

## Wiring and configuration

The owner confirmed **GDO0 -> GPIO4 on both Bubu and Dudu** for this test.
The older `design/pin-map` documentation says GDO is unconnected; it predates
this confirmation. Check the module's GDO0 label, not a guessed connector order.

- CC1101 supply: 3.3 V; common ground with ESP32.
- Existing SPI: SCK6, MOSI7, MISO20, CS10, unchanged.
- GDO0 -> GPIO4; ESP32 input with weak pulldown.
- `IOCFG0 (0x02) = 0x07`: active-high CRC-valid packet indication, held until
  the first RX FIFO byte is read. Register readback is checked at initialization
  and after bounded recovery. This is not the short sync-word pulse (`0x06`).
- Frequency, modulation, CRC, packet format, and RX/TX state settings are unchanged.
- ESP32 wake: `gpio_wakeup_enable(GPIO_NUM_4, GPIO_INTR_HIGH_LEVEL)` plus
  `esp_sleep_enable_gpio_wakeup()`, then `esp_light_sleep_start()`.
- A separate 30-second timer wake bounds a failed experiment. Timer wake is not
  reported as successful CC1101 wake.

GPIO4 is also deep-sleep-wake capable on ESP32-C3. Light sleep is chosen first
because execution and protocol state resume without a reboot or resetting the
radio before reading the wake packet. WOR would duty-cycle the CC1101 itself;
it is independent of the GDO connection and ESP32 wake-source configuration.

## Commands (115200 baud, lower case; Enter is optional)

| Command | Meaning |
| --- | --- |
| `?` | Print help |
| `t` | Pause automatic heartbeats; receive, ACKs, retries and recovery continue |
| `g` | Print GPIO level, radio state, IOCFG0, pending-ACK flag, and saved wake report |
| `s` | Arm GPIO4 and enter light sleep, only in manual mode with no pending outgoing event |
| `d` | Separate deep-sleep experiment; GPIO4 HIGH wake plus 30-second timer; reboot preserves/inspects external radio before recovery |
| `w` | Send an ordinary eight-byte heartbeat immediately, only in manual mode with no pending event |
| `n` | Resume normal periodic heartbeats |

`w` uses the existing 300 ms ACK timeout and maximum two same-ID retries.
There is no WOR listening window to overlap, so no separate wake-burst protocol
is needed. Any CRC-valid matching-PHY packet can assert GDO; sender/version
validation still happens in software after waking.

## Procedure

1. Build with `pio run -e bubu -e dudu`. After separately authorizing uploads,
   flash the matching environment to each board and open both serial monitors.
   This change does not perform uploads automatically.
2. Confirm normal two-way EVENT/ACK traffic. Enter `t` on **both** boards.
   Wait for any outstanding ACK/retry transaction to finish. Enter `g` on each:
   expect `mode=manual radio=ready pendingAck=0`, idle GDO0=0,
   `MARCSTATE=0x0D` and `IOCFG0=0x07`. A snapshot during packet reception can differ.
3. Enter `s` on Bubu. Expect:

   ```text
   WAKE ARMED: GDO0 -> GPIO4 HIGH; CC1101 RX; ESP32 LIGHT SLEEP; timer=30s
   ```

4. Wait approximately two seconds, then enter `w` on Dudu. Expect Dudu's normal
   `TX EVENT ... waiting for ACK` and subsequently `ACK MATCHED`.
5. On Bubu expect normal received-event/ACK output and:

   ```text
   WAKE: result=0 cause=GPIO(...) GDO0_at_return=1 CC1101_GDO_WAKE=YES elapsed_ms=...
   WAKE TEST: communication resumed; manual mode remains active (n restores auto)
   ```

   The wake report captures GDO **before** FIFO handling clears it. `g` afterward
   normally shows current GDO low and reprints the saved high-at-return report.
   A matched peer event plus its ACK verifies the communication portion of the chain.
6. Repeat at least ten times, then reverse the roles. Record failures, elapsed
   sleep time, retries, wake cause and register snapshots; do not count an
   immediately cancelled or rejected sleep as a successful wake test.
7. Negative control: enter `s` but do not send `w`. With both boards in manual
   mode and no other RF traffic, expect `cause=TIMER(...) CC1101_GDO_WAKE=NO`
   after about 30 seconds. Then verify `w` still works normally.
8. Enter `n` on both boards. Confirm sustained bidirectional automatic traffic,
   same-ID retries when needed, and ordinary recovery diagnostics.

## Interpretation and limits

- ESP32 native USB Serial/JTAG is unavailable during sleep. A monitor may pause
  or require reopening after wake; do not reset the board merely to retrieve
  the result, since that clears it. `g` reprints the saved report after reconnect.
- `SLEEP REFUSED` means mode/radio/pending traffic is not suitable. `SLEEP CANCELLED`
  means a packet asserted GDO before entry; the normal loop will process it.
- GPIO wake plus captured high GDO is electrical wake evidence. Use the peer's
  matching event/ACK and a logic analyzer on GPIO4 if diagnosing false triggers.
- The existing RXOFF=IDLE behavior is retained. A CRC-rejected packet can leave
  CC1101 idle without asserting GDO; the timer then wakes the MCU and normal RX
  handling resumes. This checkpoint is not a noise-hardened unattended receiver.
- CC1101 RX current remains present; this is not a low-power WOR implementation.
- If RF traffic never arrives or GDO is miswired, a timer wake is a failed remote
  wake test, not success. Persistent radio faults still use the existing three-
  attempt recovery cutoff; commands cannot bypass it.
- Build and host tests cannot validate electrical wiring, real wake latency,
  USB behavior, or current consumption. Record physical results before committing.

References: [TI CC1101 datasheet, Table 41](https://www.ti.com/lit/ds/symlink/cc1101.pdf),
[ESP32-C3 sleep modes](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32c3/api-reference/system/sleep_modes.html).

## Separate deep-sleep experiment

Entry requires manual mode, radio ready, no pending ACK, GDO0 LOW,
MARCSTATE RX (`0x0D`), and IOCFG0 `0x07`. `d` does not write CC1101 configuration
or flush FIFO. ESP32 uses
`esp_deep_sleep_enable_gpio_wakeup(1ULL << 4, ESP_GPIO_WAKEUP_GPIO_HIGH)`,
`esp_sleep_enable_timer_wakeup(30000000)`, and `esp_deep_sleep_start()`.
This is the ESP32-C3 deep-sleep GPIO API, separate from the light-sleep API.

CC1101 must remain powered. CS10 is held HIGH with `gpio_hold_en()` and
`gpio_deep_sleep_hold_en()` through sleep. MCU SPI setup restores CS HIGH before
releasing the hold; it sends no radio commands. Verify CS with a logic analyzer
if FIFO/configuration unexpectedly disappears during reboot.

### Startup ordering

1. The first operation in `setup()` captures whether this is a deep-sleep reset,
   the wake cause, wake GPIO mask, and GDO level (after enabling the MCU input).
   Boot ROM/Arduino startup necessarily precede `setup()`.
2. Serial starts, but the normal 1.5-second startup delay is skipped on deep wake.
3. RadioTask restores the saved next message ID and last-peer duplicate history
   from a small RTC-memory checkpoint. Manual mode remains active. No outgoing
   transaction existed at entry; ACK/retry and peer-online timers are not restored.
4. RadioTask initializes only MCU pins/SPI and records GDO, MARCSTATE, raw RXBYTES
   (overflow bit 7, count bits 6:0), and IOCFG0. No reset, IDLE, RX restart, FIFO
   flush, or configuration write precedes this snapshot.
5. The existing receive function copies the packet before restarting RX. The
   existing protocol handler validates it, applies duplicate detection, and sends
   the usual ACK. Return values now record copied/processed/ACK-transmitted
   outcomes; they do not change protocol actions.
6. A healthy retained radio continues operating without a full reset. Inspection
   or RX failures enter the same bounded three-attempt recovery after inspection.
   Packet delivery remains independent of RX readiness; if restart fails, the
   event can be processed while its ACK is suppressed until the sender retries.
7. `g` reprints the saved pre-FIFO snapshot and result after USB reconnects.
   A cold reset uses normal reset/configuration/startReceive initialization and
   discards the RTC checkpoint. Do not press Reset to reconnect USB.

### Physical procedure and acceptance

1. After reviewing this change, flash the matching Bubu/Dudu builds yourself.
   Open both monitors at 115200 baud. Check normal traffic and repeat one `s`/`w`
   light-sleep test first.
2. Send `t` on both boards. Wait for outstanding transactions to finish. Use `g`:
   require `mode=manual radio=ready pendingAck=0`, GDO0=0, MARCSTATE=0x0D,
   IOCFG0=0x07.
3. Send `d` on Bubu. Expect:

   ```text
   DEEP WAKE ARMED: GDO0 -> GPIO4 HIGH; CC1101 RX; ESP32 DEEP SLEEP; timer=30s
   ```

4. Wait two seconds, send `w` on Dudu, and allow Bubu to reboot. Reopen Bubu's
   USB monitor without resetting it if necessary; send `g` to recover evidence.
   Typical successful Bubu output (normal EVENT/ACK lines may precede this):

   ```text
   DEEP WAKE: cause=GPIO(...) GPIO_mask=0x10 GDO0_at_boot=1 GDO0_before_FIFO=1 checkpoint=restored
   DEEP RADIO BEFORE FIFO: MARCSTATE=0x01 RXBYTES=0x09 IOCFG0=0x07
   ELECTRICAL DEEP-WAKE SUCCESS=YES
   DEEP PACKET: copied=1 peer_event_processed=1 ack_tx=1
   FULL PROTOCOL DEEP-WAKE: receiver steps passed; require sender ACK MATCHED
   ```

   The FIFO contains one length byte plus the eight-byte application message.
   Register snapshots can differ on failed/noisy tests; retain the actual output.
   Dudu should print its ordinary `TX EVENT`, possibly `RETRY 1/2` or `RETRY 2/2`,
   then `RX ACK` and `ACK MATCHED` for that wake event's message ID.
5. Count **electrical success** only for deep-sleep GPIO4 wake. Count **full
   protocol success** only when the receiver reports the preserved/processed
   event and transmitted ACK AND Dudu reports the matching `ACK MATCHED`.
   `ack_tx=1` alone does not prove the peer received the ACK within its timeout.
6. Repeat ten times, then swap sleeping/awake roles and repeat. Record message ID,
   register snapshot, retries, and sender outcome. Check a subsequent normal `w`
   in both directions after each wake; same-ID retransmissions must remain
   `RX DUPLICATE` with an ACK, not a second new event.
7. Negative control: `d` with neither board transmitting. After about 30 seconds,
   expect the following (MARCSTATE=0x0D, RXBYTES=0x00 in a quiet RF environment):

   ```text
   DEEP WAKE: cause=TIMER(...) GPIO_mask=0x0 GDO0_at_boot=0 GDO0_before_FIFO=0 checkpoint=restored
   ELECTRICAL DEEP-WAKE SUCCESS=NO
   DEEP PACKET: copied=0 peer_event_processed=0 ack_tx=0
   FULL PROTOCOL DEEP-WAKE: NOT VERIFIED
   ```

   Timer wake is never counted as CC1101 wake, even if a packet arrives just
   after the timeout. Verify communication afterward with `w`.
8. Finish with `n` on both boards and verify sustained bidirectional traffic.

### Remaining physical questions

- Software/host tests verify ordering, not electrical retention: CC1101 supply,
  CS hold through boot, GDO level, and real FIFO survival need the boards.
- ROM/Arduino/USB startup and radio turnaround may exceed the sender's existing
  300 ms timeout and two retries. The explicit 1.5-second delay is skipped, but
  no deadline guarantee is claimed. If the sender gives up before a late ACK,
  record electrical success and protocol failure separately; do not extend the
  protocol timing to hide the result.
- Native USB disconnect/re-enumeration can hide early logs; the saved `g` report
  remains available until another reset. It does not record RF-to-ACK latency.
- CRC-rejected traffic can still leave CC1101 IDLE without GDO wake, requiring
  timer fallback. Continuous CC1101 RX current remains. Neither issue is changed
  by this deep-sleep experiment.
