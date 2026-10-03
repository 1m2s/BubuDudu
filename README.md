# BubuDudu — feature/freertos

Historical checkpoint `1fd1b35`. Scheduling experiment that moves the existing delay-based heartbeat into a FreeRTOS LED task. The Arduino loop continues to handle local Motion events and deep-sleep entry; wireless event delivery is not active.

## Things learnt

- A separate LED task lets the main loop check Motion without waiting for an entire heartbeat.
- Task separation does not make the animation itself non-blocking: LED brightness ramps still use delays.
- Output ownership matters at sleep entry: the main loop calls led.off() while the LED task can also write the pixel.

[Task and sleep ownership](src/main.cpp) · [LED implementation](src/LED.cpp) · [Development log](DEVLOG.md)

[Current main](https://github.com/1m2s/BubuDudu/tree/main)
