# BubuDudu — feature/ws2812

Historical checkpoint `a4ea899`. Local Motion + WS2812B heartbeat checkpoint. A dedicated LED module drives one pixel on GPIO21; the main loop calls its delay-based animation and enters deep sleep after inactivity.

## Things learnt

- Two brightness ramps with different peaks produce the local double-pulse heartbeat.
- Delay-based animation occupies the main loop between Motion checks.
- Clear and transmit the LED buffer before sleep; the external pixel otherwise retains its last output.

[LED implementation](src/LED.cpp) · [Application](src/main.cpp) · [Development log](DEVLOG.md)

[Current main](https://github.com/1m2s/BubuDudu/tree/main)
