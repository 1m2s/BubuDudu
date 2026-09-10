#pragma once

#if defined(DEVICE_BUBU)
constexpr char DEVICE_NAME[] = "BUBU";

#elif defined(DEVICE_DUDU)
constexpr char DEVICE_NAME[] = "DUDU";

#else
#error "Device identity is not defined"
#endif
