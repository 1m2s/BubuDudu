# BubuDudu — integration/final-firmware

Historical checkpoint `72b38b5`. Final integration checkpoint merged into main. Shared firmware uses a cooperative loop with separate radio, Motion, button, sleep and presentation modules. Host/CI and build evidence is recorded; physical acceptance remains incomplete.

## Things learnt

- Separate runtime state by owner while preserving callback, receive, retry and sleep ordering.
- Simulated reboots must release host-owned queues so LeakSanitizer can distinguish fixture leaks from firmware behavior.
- Software evidence for failed-sleep recovery and display attempts does not resolve intermittent hardware recovery or visible OLED output.

[Architecture](ARCHITECTURE.md) · [Build and host tests](tests/host/README.md) · [Hardware acceptance](FINAL_FIRMWARE_TEST.md) · [Development log](DEVLOG.md)

[Current main](https://github.com/1m2s/BubuDudu/tree/main)
