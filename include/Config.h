#pragma once

// Confirmed physical wiring on both boards: CC1101 GDO0 -> ESP32-C3 GPIO4.
constexpr unsigned int CC1101_GDO0_GPIO = 4;

#if defined(DEVICE_BUBU)
constexpr char DEVICE_NAME[] = "BUBU";

#elif defined(DEVICE_DUDU)
constexpr char DEVICE_NAME[] = "DUDU";

#else
#error "Device identity is not defined"
#endif
