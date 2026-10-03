# BubuDudu — feature/cc1101

Historical checkpoint `ece6879`. CC1101 messaging and wake bench using a dedicated RadioTask. The branch records a light-sleep wake check and adds a separate retained-packet deep-sleep experiment. Its test document still marks physical deep-sleep verification pending.

## Things learnt

- GDO0 packet-ready level and an ordinary packet ACK provide different evidence: waking alone does not prove delivery.
- Inspect the external radio FIFO after reboot before resetting or flushing the radio.
- A 30-second timer bounds the wake experiment; a timer wake is not a successful radio wake.

[Wake procedure and evidence limits](CC1101_WAKE_TEST.md) · [Radio task](src/RadioTask.cpp) · [Development log](DEVLOG.md)

[Current main](https://github.com/1m2s/BubuDudu/tree/main)
