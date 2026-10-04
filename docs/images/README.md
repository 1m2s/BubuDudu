# Evidence gallery and image provenance

Four unchanged screenshots record bus debugging, simulation and early tool
setup. They illustrate those stages of development. Current physical results
and [evidence levels](../../FINAL_FIRMWARE_TEST.md#evidence-levels) are recorded
separately. Original filenames and SHA-256 hashes below identify each image.

## cc1101-spi-debug-capture.png

![Historical SPI decoding with chip-select, clock and MOSI/MISO transfers](cc1101-spi-debug-capture.png)

Historical SPI debugging: decoded MOSI/MISO exchanges, not RF delivery.
Original filename: `01_cc1101_spi_register_transactions.png`;
matching capture: `Screenshot 2026-09-17 at 3.06.32 PM.png`
(the original timestamp uses a narrow no-break space before `PM`).

SHA-256: `8632da3b05c6833399c39da907297aadfd2c30223874b98fa65e28c4620754ab`

## i2c-address-69-debug-capture.png

![Historical I²C decode labelled Address read: 69 followed by Read and ACK](i2c-address-69-debug-capture.png)

Historical I²C decode labelled “Address read: 69” with ACK. It does not establish
ADXL345/OLED operation at their current addresses. Original filename:
`03_i2c_read_ack_capture.png`; matching capture:
`Screenshot 2026-09-25 at 1.31.58 PM.png` (with a narrow no-break space before `PM`).

SHA-256: `d303dc592e9d61729ed5b1ef71c42be195bb2716fbcfb85f7282e7fb35265ed7`

## falstad-battery-indicator-simulation.png

![Falstad candidate with dividers, comparator stages, reference sources, logic and LEDs](falstad-battery-indicator-simulation.png)

Simulation-only battery-indicator candidate. Original filename:
`04_falstad_circuit_candidate.png`; matching source:
`falstad_circuit_simulation.png`. The screenshot includes feedback experiments;
the [saved export](../../simulations/battery_indicator_v1.txt) is a separate
historical design record. Final hysteresis and a physical comparator indicator
were not completed. See the [electronics reflection](../V1_REFLECTION.md#electronics-that-remain-unfinished).

SHA-256: `64d64c6fc5193f5a179eaedda53701134db19e505f5d5edac50f553d78e611b8`

## platformio-bringup-environment.png

![Early VS Code PlatformIO environment and 115200-baud serial monitor](platformio-bringup-environment.png)

Historical environment on `design/pin-map`, with the monitor printing
`BubuDudu PlatformIO test`. For the final dual-identity configuration use the
[build guide](../../tests/host/README.md). Original filename:
`platformio_vscode_firmware_environment.png`.

SHA-256: `6c39cad20eb41c72a815409e330c98d3a95127d19714e293b7810203e8ed99e3`

## v1-system-flow.svg

![Shared firmware, separate local state, two radio paths and callback-to-loop flow](v1-system-flow.svg)

Source-only overview of [architecture](../../ARCHITECTURE.md) and
[main.cpp](../../src/main.cpp). It describes communication paths and ownership,
not a wiring schematic or a physical result. The SVG is its editable source and
has no external image dependency.
