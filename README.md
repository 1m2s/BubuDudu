# BubuDudu

BubuDudu is a pair of wireless companion devices built around the ESP32-C3.
Both identities share firmware for physical-button input, ADXL345 motion,
ESP-NOW/CC1101 delivery, proximity experiments, LED heartbeat animations,
OLED diagnostics and coordinated deep sleep with motion/button/radio wake.

The integration retains a cooperative loop and bounded protocol retries. Awake
devices stay ACTIVE, meaningful inactivity qualifies sleep, and CLOSE/FAR
background pulses share priority with receiver-only user animations.

- [Architecture and ownership](ARCHITECTURE.md)
- [Host suites, CI and sequential firmware builds](tests/host/README.md)
- [Button and heartbeat behavior](BUTTON_HEARTBEAT.md)
- [Hardware acceptance and remaining evidence](FINAL_FIRMWARE_TEST.md)
- [Development history](DEVLOG.md)

Software validation is separate from physical acceptance. The initial
`NOT_IN_RX` cause, persistent CC1101 `Stopped` condition, rare failed-sleep
hardware recovery and OLED visibility remain unresolved or unverified. The
current 0.625 g awake Motion threshold is a preserved trial setting. Gesture
detection and battery runtime characterization are not completed capabilities.
