#pragma once
#include <cstdint>
using esp_err_t = int;
constexpr int ESP_OK = 0;
enum esp_sleep_wakeup_cause_t { ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_GPIO,
                              ESP_SLEEP_WAKEUP_TIMER, ESP_SLEEP_WAKEUP_ALL };
constexpr int ESP_GPIO_WAKEUP_GPIO_HIGH = 1;
constexpr int ESP_GPIO_WAKEUP_GPIO_LOW = 0;
namespace WakePlatform
{
    extern bool deepReset, sources, held, deepHeld, failSetup, failRelease;
    extern bool returnFromSleep;
    extern esp_sleep_wakeup_cause_t cause;
    extern uint64_t mask;
    extern uint64_t enabledGpioMask, timerUs;
    extern uint64_t highWakeMask, hardwareGpioMask, hardwareHighMask;
    extern bool timerEnabled;
    extern unsigned gpioWakeCalls, failGpioWakeCall;
    extern unsigned timerCalls, sleepCalls;
    struct Entered {};
}
inline auto esp_sleep_get_wakeup_cause() -> esp_sleep_wakeup_cause_t { return WakePlatform::cause; }
inline uint64_t esp_sleep_get_gpio_wakeup_status()
{ return WakePlatform::cause == ESP_SLEEP_WAKEUP_GPIO ? WakePlatform::mask : 0; }
inline esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t source)
{
    assert(source == ESP_SLEEP_WAKEUP_ALL);
    // IDF 4.4.7 clears trigger enables, not stored GPIO masks/modes or duration.
    WakePlatform::sources = WakePlatform::timerEnabled = false;
    return ESP_OK;
}
inline esp_err_t esp_deep_sleep_enable_gpio_wakeup(uint64_t mask, int mode)
{
    if ((mask & ~0x3FULL) || (mode != ESP_GPIO_WAKEUP_GPIO_HIGH && mode != ESP_GPIO_WAKEUP_GPIO_LOW))
        return -1;
    ++WakePlatform::gpioWakeCalls;
    if (WakePlatform::failSetup || WakePlatform::gpioWakeCalls == WakePlatform::failGpioWakeCall) return -1;
    // The public API ORs the mask; the driver updates only selected pin polarities.
    WakePlatform::enabledGpioMask |= mask;
    WakePlatform::hardwareGpioMask |= mask;
    WakePlatform::hardwareHighMask = (WakePlatform::hardwareHighMask & ~mask) |
        (mode == ESP_GPIO_WAKEUP_GPIO_HIGH ? mask : 0);
    // The inspected LOW setter uses ~(mode << pin), where LOW is zero:
    // it cannot clear a previously HIGH stored pull-mode bit in this SDK.
    if (mode == ESP_GPIO_WAKEUP_GPIO_HIGH) WakePlatform::highWakeMask |= mask;
    WakePlatform::sources = true;
    return ESP_OK;
}
inline esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us)
{ assert(us == 30000000); ++WakePlatform::timerCalls; WakePlatform::timerUs = us; WakePlatform::timerEnabled = true; return ESP_OK; }
inline void esp_deep_sleep_start()
{ ++WakePlatform::sleepCalls; if (!WakePlatform::returnFromSleep) throw WakePlatform::Entered{}; }
