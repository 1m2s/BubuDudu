# BubuDudu — feature/button-heartbeat

Historical checkpoint `fece5af`. Button interaction and automatic sleep checkpoint, sharing commit fece5af with feature/sleep-execution. A debounced press requests a receiver-only 700 ms heartbeat; GPIO5 wake preserves a pending press through reboot and a bounded peer-wake handoff.

## Things learnt

- Latch GPIO5 wake evidence before initialization so an early button release does not lose the request.
- A peer-wake ACK plus local RX readiness permits handoff; the subsequent UserHeartbeat still needs its own application ACK.
- Defer concurrent EVENTs while the wake transmitter owns SPI, then process them through normal deduplication and receipt handling.

[Button behavior and limitations](BUTTON_HEARTBEAT.md) · [Application](src/main.cpp) · [Host checks](tests/host/run.sh) · [Development log](DEVLOG.md)

[Current main](https://github.com/1m2s/BubuDudu/tree/main)
