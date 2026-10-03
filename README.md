# BubuDudu — chore/repository-polish

Historical checkpoint `d4b3e57`. Documentation and CI checkpoint around firmware 1b10419. Motion wake can trigger one bounded CC1101 peer-wake transaction after retained recovery. This application reports events through Serial; the later button, live display, proximity and fallback integration is absent.

## Things learnt

- Only a pure motion wake starts the automatic peer-wake episode; GPIO4 participation suppresses an automatic return wake.
- Deep-sleep reboot requires preserving message history and inspecting the external FIFO before normal initialization.
- Host tests, board builds and physical observations answer different questions and need separate evidence records.

[Architecture](docs/architecture.md) · [Testing and setup](docs/testing.md) · [Validation record](docs/validation.md) · [Development log](DEVLOG.md)

Build each identity and run the host suite (no upload):

```sh
pio run -e bubu
pio run -e dudu
bash tests/host/run.sh
```

Host tests need Bash and a C++11 compiler with ASan/UBSan support.

[Current main](https://github.com/1m2s/BubuDudu/tree/main)
