# BubuDudu — feature/adxl345

Historical checkpoint `01c63ee`. Standalone ADXL345 motion and deep-sleep checkpoint. The Motion module reports activity/inactivity and wakes the ESP32-C3 through GPIO3. The application has no peer communication or coordinated sleep.

## Things learnt

- Keep the ISR to an event flag; read I²C interrupt status in normal program execution.
- Read the latched startup event before reconfiguring the sensor after a deep-sleep reboot.
- Recheck INT1 before sleep so motion arriving during the transition can cancel entry.

[Motion driver](src/Motion.cpp) · [Sleep experiment](src/main.cpp) · [Development log](DEVLOG.md)

[Current main](https://github.com/1m2s/BubuDudu/tree/main)
