# BubuDudu — feature/oled

Historical checkpoint `9cc2089`. Local display checkpoint combining Motion, the FreeRTOS heartbeat task and an SH1106 OLED. The screen shows device identity, ACTIVE/INACTIVE state and normal/motion boot reason; it does not show the later peer/radio dashboard.

## Things learnt

- ADXL345 at 0x53 and OLED at 0x3C can share GPIO0/1; a bus scan helps distinguish their addresses.
- U8g2 takes the OLED address shifted left by one bit.
- Keep boot reason separate from live activity state so a later motion event does not rewrite the origin of the boot.

[Display implementation](src/Display.cpp) · [Application and bus scan](src/main.cpp) · [Development log](DEVLOG.md)

[Current main](https://github.com/1m2s/BubuDudu/tree/main)
