#pragma once
#include <cstdint>
using esp_err_t = int;
constexpr int ESP_OK = 0;
enum esp_sleep_wakeup_cause_t { ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_GPIO,
                              ESP_SLEEP_WAKEUP_TIMER, ESP_SLEEP_WAKEUP_ALL };
constexpr int ESP_GPIO_WAKEUP_GPIO_HIGH = 1;
namespace WakePlatform
{
    extern bool deepReset, sources, held, deepHeld, failSetup, failRelease;
    extern bool returnFromSleep;
    extern esp_sleep_wakeup_cause_t cause;
    extern uint64_t mask;
    struct Entered {};
}
inline auto esp_sleep_get_wakeup_cause() -> esp_sleep_wakeup_cause_t { return WakePlatform::cause; }
inline uint64_t esp_sleep_get_gpio_wakeup_status() { return WakePlatform::mask; }
inline esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t)
{ WakePlatform::sources = false; return ESP_OK; }
inline esp_err_t esp_deep_sleep_enable_gpio_wakeup(uint64_t mask, int mode)
{
    assert(mask == ((1ULL << 4) | (1ULL << 3)) && mode == ESP_GPIO_WAKEUP_GPIO_HIGH);
    WakePlatform::sources = true;
    return WakePlatform::failSetup ? -1 : ESP_OK;
}
inline esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us) { assert(us == 30000000); return ESP_OK; }
inline void esp_deep_sleep_start()
{ if (!WakePlatform::returnFromSleep) throw WakePlatform::Entered{}; }
