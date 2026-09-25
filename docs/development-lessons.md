# Development lessons

These are engineering consequences of the recorded work, not a claim that every failure mode is solved. The full experiments and checkpoints are in [DEVLOG](../DEVLOG.md).

## Smaller checkpoints made failures attributable

The abandoned large integration changed too many subsystems together. A radio timeout could then be caused by concurrency, startup ordering, sleep policy or hardware. The replacement process preserved a verified firmware checkpoint before adding one behavior at a time: sleep decision, arm readiness, RTC history, retained recovery, execution, Motion, then peer wake. [September 22–24](../DEVLOG.md#2026-09-22).

Historical branches remain useful evidence but are not an architecture to merge wholesale. A driver file existing in the tree is not evidence it runs; [the call-path audit](repository-audit.md) makes that distinction explicit.

## Ownership and bounded time are design constraints

The ESP-NOW receive callback originally risked sharing protocol state with application code. Copying eight-byte packets to a FreeRTOS queue let the loop own interpretation and state transitions. TX/RX callback counts separately track driver work that is invisible to the application outbox.

Retries reuse the ID of the original semantic event, and duplicates receive another receipt without repeating the application action. This is a bounded cache with documented limits, not universal exactly-once delivery. A sleep transaction has phase and hard deadlines; duplicate packets cannot keep it alive indefinitely. Sleeping and offline peers carry different meanings. [Protocol](protocol.md), [power management](power-management.md).

## Forward progress needs its own phase

The simultaneous-request hardware test delayed outgoing controls. A participant accepted COMMIT but remained under its old WAIT_COMMIT deadline while SLEEP_ACK waited to be submitted. That was real progress governed by the wrong timer.

`WAIT_SLEEP_ACK_TX` gives the final send a new bounded phase deadline while preserving the overall hard deadline. Duplicate COMMITs and retries do not extend either budget. This corrected the phase model without hiding the problem behind larger timeouts. [September 23](../DEVLOG.md#2026-09-23).

## Sleep agreement is not physical readiness

Semantic SLEEP_ACK submission is different from delivery and from completion of the final receipt callback. Hardware tests exposed an execution-ready log appearing before TX callback completion. Physical entry now waits for protocol work and callbacks to drain and rechecks at the final RTC/save boundary.

The system can still disagree under permanent loss or races after one side sleeps. Bounded failure and explicit uncertainty are more honest than treating a clean happy-path test as proof of distributed agreement. [September 24](../DEVLOG.md#2026-09-24).

## A reboot can leave important state outside the MCU

Deep sleep resets ESP32 runtime while CC1101 retains the received packet. Normal radio initialization would erase that evidence. The startup contract therefore restores history, attaches SPI without radio reset, inspects/copies FIFO data, processes and ACKs it, and only then continues peripheral initialization.

Likewise, the ADXL345 interrupt is latched external state. Wake cause must use the actual GPIO mask: GPIO3 and GPIO4 are not interchangeable. [Deep sleep and wake](deep-sleep-wake.md).

## An ACK can be lost after the intended action already happened

A sleeping receiver could wake and process an EVENT successfully, lose its first ACK, then ignore the sender's retry because it was now awake. The fix was narrow awake service for that accepted EVENT's retained receipt, without a second application delivery.

Temporary deterministic ACK suppression proved the path in both directions. The suppression code was removed before committing; permanent host mocks preserve the regression. Test instrumentation must not silently become product behavior. [September 25](../DEVLOG.md#2026-09-25).

## Hardware intermittency deserves evidence before workarounds

Dudu intermittently failed the ADXL345 DEVID read despite I2C-like GPIO traffic. A later bounded scan found the OLED at 0x3C and ADXL345 at 0x53, and DEVID 0xE5 returned. The evidence pointed toward connection/supply intermittency rather than a deterministic firmware defect; it did not identify a unique physical contact.

The temporary scanner was removed. Arbitrary delays, retries or a new framework would have obscured that distinction. The next awake-motion/UI work remains separate from the now-verified sleep wake path.
