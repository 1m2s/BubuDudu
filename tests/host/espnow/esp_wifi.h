#pragma once
#include "esp_now.h"
constexpr int WIFI_SECOND_CHAN_NONE = 0;
inline esp_err_t esp_wifi_set_channel(int, int) { return ESP_OK; }
inline esp_err_t esp_wifi_get_mac(int, uint8_t* mac)
{ for (unsigned i = 0; i < 6; ++i) mac[i] = 0; return ESP_OK; }
