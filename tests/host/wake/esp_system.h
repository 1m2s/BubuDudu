#pragma once
#include "esp_sleep.h"
constexpr int ESP_RST_DEEPSLEEP = 5;
inline int esp_reset_reason() { return WakePlatform::deepReset ? ESP_RST_DEEPSLEEP : 1; }
