# Debugging images

These unchanged screenshots show earlier debugging and simulation work.
[Final results](../../FINAL_FIRMWARE_TEST.md) cover the finished prototype.
SHA-256 hashes below let you check that each file is unchanged.

## cc1101-spi-debug-capture.png

![SPI clock and data capture](cc1101-spi-debug-capture.png)

CC1101 SPI traffic. This shows bus activity, not wireless delivery.
Original: `01_cc1101_spi_register_transactions.png`;
also saved as `Screenshot 2026-09-17 at 3.06.32 PM.png`.

SHA-256: `8632da3b05c6833399c39da907297aadfd2c30223874b98fa65e28c4620754ab`

## i2c-address-69-debug-capture.png

![I²C capture labelled Address read: 69](i2c-address-69-debug-capture.png)

Early I²C debug capture, not a check of the final sensor/display addresses.
Original: `03_i2c_read_ack_capture.png`;
also saved as `Screenshot 2026-09-25 at 1.31.58 PM.png`.
The two timestamp filenames use a narrow space before `PM`.

SHA-256: `d303dc592e9d61729ed5b1ef71c42be195bb2716fbcfb85f7282e7fb35265ed7`

## falstad-battery-indicator-simulation.png

![Battery-indicator simulation](falstad-battery-indicator-simulation.png)

A Falstad experiment applying ideas from my university Circuit Design class:
scaling battery voltage with resistors, comparing it with reference voltages and
using LEDs to indicate the result. [Course connection](../V1_REFLECTION.md#electronics-that-remain-unfinished).

A circuit idea, not a built indicator. The screenshot and
[saved circuit](../../simulations/battery_indicator_v1.txt) are separate experiment
records. Final switching thresholds were unfinished.
Original: `04_falstad_circuit_candidate.png`, also `falstad_circuit_simulation.png`.

SHA-256: `64d64c6fc5193f5a179eaedda53701134db19e505f5d5edac50f553d78e611b8`

## platformio-bringup-environment.png

![Early PlatformIO setup](platformio-bringup-environment.png)

Early `design/pin-map` setup. Use the [build guide](../../tests/host/README.md)
for current commands. Original: `platformio_vscode_firmware_environment.png`.

SHA-256: `6c39cad20eb41c72a815409e330c98d3a95127d19714e293b7810203e8ed99e3`

## v1-system-flow.svg

![Two devices, shared firmware and two radios](v1-system-flow.svg)

[Code overview](../../ARCHITECTURE.md), not a wiring diagram. The SVG is editable
and needs no external images.
