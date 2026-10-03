# BubuDudu — design/pin-map

Historical checkpoint `bebd87c`. Documentation checkpoint for GPIO allocation, with recorded subsystem bring-up notes. This branch contains no firmware. Its later CC1101 notes describe polled SPI with GDO0/GDO2 unconnected; GPIO4 is reserved but unused at this checkpoint.

## Things learnt

- Sharing GPIO0/1 between ADXL345 and OLED conserves pins while their I²C addresses remain distinct.
- The proposed map uses all ten preferred GPIOs after avoiding strapping pins GPIO2/8/9; MOSFET control has no allocation.
- Motion wake needs a suitable wake pin and a powered sensor: the recorded ADXL345 arrangement uses INT1 on GPIO3.

[Pin map and recorded observations](PIN_MAP.md) · [Pin constraints](PIN_CONSTRAINTS.md) · [Branch history](BRANCH_NOTES.md)

[Current main](https://github.com/1m2s/BubuDudu/tree/main)
