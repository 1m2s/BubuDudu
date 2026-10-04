# BubuDudu

Bubu and Dudu are two ESP32-C3 companion devices running shared firmware.
A button press sends a heartbeat request to the other device, whose WS2812B
plays a double pulse. Motion sensing supports wake and proximity checks;
ESP-NOW and CC1101 carry events and coordinated sleep/wake traffic. RSSI-based
CLOSE/FAR estimates control background heartbeat timing and radio selection.
The OLED code presents device, peer, radio, motion and power status.

## Things learnt

- Copy received packets into a queue; let the loop own protocol state.
- Keep retry IDs stable and acknowledge duplicates without replaying their action.
- Separate packet receipts from sleep agreement and physical sleep entry.
- Inspect the retained CC1101 FIFO before reboot initialization can erase it.
- Give shared I²C one initializer; address ACKs do not prove visible OLED output.

Software checks pass at the recorded integration checkpoint. Hardware acceptance
remains incomplete: initial `NOT_IN_RX`, persistent CC1101 `Stopped`, intermittent
sleep recovery and OLED visibility remain unresolved or unverified. Awake motion
sensitivity is a trial setting; battery runtime is unmeasured.

October 4 user testing after reflashing reports unreliable proximity/radio
transitions: one device can remain FAR on CC1101 while its partner shows CLOSE
on ESP-NOW, with refresh often requiring local movement. LED and motion behavior
and CC1101 peer wake were reported working, including a wake through a door;
trial counts and paired logs were not captured in the report. See the
[current observations](FINAL_FIRMWARE_TEST.md#october-4-demonstration-checkpoint)
for evidence limits. This is a partially working prototype, not a fully accepted release.

[Architecture](ARCHITECTURE.md) · [Builds and tests](tests/host/README.md) ·
[Remaining hardware acceptance](FINAL_FIRMWARE_TEST.md) · [Development history](DEVLOG.md)

## Selected evidence

![Historical CC1101 SPI debugging capture with decoded MOSI and MISO transfers](docs/images/cc1101-spi-debug-capture.png)

*Historical CC1101 SPI debugging capture: chip-select, clock and decoded transfers.
It does not establish correct register values, RF delivery or current firmware acceptance.*

![I²C debugging capture showing Address read: 69 and an ACK](docs/images/i2c-address-69-debug-capture.png)

*Historical I²C decoder view labelled “Address read: 69,” followed by an ACK.
This is not successful ADXL345/OLED validation or proof of visible display output.*

![Falstad battery-indicator simulation with resistor dividers, comparators and LEDs](docs/images/falstad-battery-indicator-simulation.png)

*Falstad battery-indicator candidate using dividers, comparators and LEDs.
Simulation only; it does not establish assembled-circuit performance, charging behavior
or battery runtime. [Image provenance](docs/images/README.md).*
