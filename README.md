# BubuDudu — feature/adxl345-motion-wake

Historical checkpoint `4fe8171`. Motion wake added to coordinated physical deep sleep and retained CC1101 recovery. GPIO3 and GPIO4 wake sources are distinguished. Automatic peer wake after local motion is not yet connected in setup() at this checkpoint.

## Things learnt

- Arm activity-only ADXL345 interrupts for sleep and restore the awake profile if entry is cancelled.
- Inspect retained CC1101 traffic before sensor initialization, including after motion or combined-pin wake.
- An asserted INT1 must block entry rather than cause an immediate sleep/wake cycle.

[Boot and sleep ordering](src/main.cpp) · [Motion profiles](src/Motion.cpp) · [Host checks](tests/host/run.sh) · [Development log](DEVLOG.md)

[Current main](https://github.com/1m2s/BubuDudu/tree/main)
