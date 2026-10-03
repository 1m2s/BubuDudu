# BubuDudu — feature/espnow

Historical checkpoint `d24d72c`. ESP-NOW reliability checkpoint with receive processing moved into the Arduino loop. The application sends periodic events and reports delivery over Serial; the later power handshake and visual interaction are not integrated.

## Things learnt

- The Wi-Fi callback copies packets into an eight-entry queue instead of changing protocol state.
- Drain queued ACKs before checking timeouts to avoid retrying an already acknowledged event.
- Bound each receive batch to eight packets so continuous input cannot starve outgoing work.

[Queue and protocol loop](src/main.cpp) · [Protocol](include/Protocol.h) · [Development log](DEVLOG.md)

[Current main](https://github.com/1m2s/BubuDudu/tree/main)
