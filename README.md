# BubuDudu — feature/sleep-handshake

Historical checkpoint `baea754`. Coordinated sleep protocol checkpoint over ESP-NOW: REQUEST → READY → COMMIT → SLEEP_ACK. Both CPUs remain physically awake even when the FSM reports SLEEPING.

## Things learnt

- A packet ACK confirms receipt; the separate SLEEP_ACK accepts the sleep commit.
- Simultaneous coordinators need deterministic arbitration without extending the original hard deadline.
- Duplicate controls must preserve retry budgets and avoid repeating transitions; permanent packet loss can still leave different peer states.

[Bench procedure and host checks](SLEEP_HANDSHAKE_TEST.md) · [Power state machine](src/PowerManager.cpp) · [Development log](DEVLOG.md)

[Current main](https://github.com/1m2s/BubuDudu/tree/main)
