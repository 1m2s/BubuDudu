# BubuDudu — feature/power-fsm

Historical checkpoint `e3b849f`. Power-state foundation alongside queued ESP-NOW messaging. Serial commands exercise local simulated sleep negotiation and wake states; no peer sleep-control exchange or physical CPU/radio suspension occurs.

## Things learnt

- Local power state and peer availability need separate state variables.
- Phase and hard deadlines bound a negotiation independently; cooldown prevents immediate restart.
- A semantic SLEEPING state can be tested while the CPU remains awake, but it is not power-consumption evidence.

[Power state machine](src/PowerManager.cpp) · [State interface](include/PowerManager.h) · [Development log](DEVLOG.md)

[Current main](https://github.com/1m2s/BubuDudu/tree/main)
