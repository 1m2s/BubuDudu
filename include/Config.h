#pragma once

#include <stdint.h>

// Confirmed wiring on both boards.
constexpr uint8_t MOTION_SDA_PIN = 0, MOTION_SCL_PIN = 1, MOTION_INT1_PIN = 3;

#if defined(DEVICE_BUBU)
constexpr char DEVICE_NAME[] = "BUBU";

#elif defined(DEVICE_DUDU)
constexpr char DEVICE_NAME[] = "DUDU";

#else
#error "Device identity is not defined"
#endif
