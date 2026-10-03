# BubuDudu — feature/sleep-execution

Historical checkpoint `fece5af`. Automatic sleep and button checkpoint, sharing commit fece5af with feature/button-heartbeat. Product sleep follows 35 seconds of meaningful inactivity and transport/motion guards, with GPIO3/4 HIGH and GPIO5 LOW wake and no product timer.

## Things learnt

- Semantic sleep agreement must be followed by transport drain, sensor preparation and final input checks before physical entry.
- Button wake preserves one request, but final polling cannot guarantee capture of every brief press-and-release.
- Keep wake acknowledgement separate from user-event delivery, and drain deferred radio events before another sleep or wake episode.

[Button wake and final-entry limits](BUTTON_HEARTBEAT.md) · [Application](src/main.cpp) · [Host checks](tests/host/run.sh) · [Development log](DEVLOG.md)

[Current main](https://github.com/1m2s/BubuDudu/tree/main)
