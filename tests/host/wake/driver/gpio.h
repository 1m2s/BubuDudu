#pragma once
#include "esp_sleep.h"
using gpio_num_t = int;
inline esp_err_t gpio_hold_en(gpio_num_t pin)
{ assert(pin == 10 && csLevel == HIGH); WakePlatform::held = true; return ESP_OK; }
inline esp_err_t gpio_hold_dis(gpio_num_t pin)
{
    assert(pin == 10 && csLevel == HIGH);
    if (WakePlatform::failRelease) return -1;
    WakePlatform::held = false; return ESP_OK;
}
inline void gpio_deep_sleep_hold_en() { WakePlatform::deepHeld = true; }
inline void gpio_deep_sleep_hold_dis() { WakePlatform::deepHeld = false; }
