# BubuDudu — build/platformio

Historical checkpoint `eedb2f5`. Shared PlatformIO foundation for two ESP32-C3 identities. The application only prints the selected device name over Serial; peripheral and radio behavior are later work.

## Things learnt

- One source tree can select Bubu or Dudu through per-environment compiler flags.
- Matching a board by MAC address avoids depending on changing USB port names.
- Native USB CDC settings and a 115200-baud monitor are part of the initial bring-up configuration.

[Build environments](platformio.ini) · [Port selection](tools/pio_select_port.py) · [Development log](DEVLOG.md)

[Current main](https://github.com/1m2s/BubuDudu/tree/main)
