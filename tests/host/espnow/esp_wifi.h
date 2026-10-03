#pragma once
#include "esp_now.h"
constexpr int WIFI_SECOND_CHAN_NONE = 0;
inline esp_err_t esp_wifi_set_channel(int, int) { return ESP_OK; }
inline esp_err_t esp_wifi_get_mac(int, uint8_t* mac)
{
    const uint8_t local[] = {0xE8, 0xF6, 0x0A, 0x12,
#ifdef DEVICE_BUBU
        0x4C, 0xA4
#else
        0x5B, 0x84
#endif
    };
    for (unsigned i = 0; i < 6; ++i) mac[i] = local[i];
    return ESP_OK;
}

struct wifi_pkt_rx_ctrl_t { int8_t rssi; uint16_t sig_len; uint8_t rx_state; };
struct wifi_promiscuous_pkt_t { wifi_pkt_rx_ctrl_t rx_ctrl; uint8_t payload[0]; };
enum wifi_promiscuous_pkt_type_t { WIFI_PKT_MGMT, WIFI_PKT_CTRL, WIFI_PKT_DATA, WIFI_PKT_MISC };
using wifi_promiscuous_cb_t = void (*)(void*, wifi_promiscuous_pkt_type_t);
constexpr uint32_t WIFI_PROMIS_FILTER_MASK_MGMT = 1;
struct wifi_promiscuous_filter_t { uint32_t filter_mask; };
extern wifi_promiscuous_cb_t promiscuousCallback;
extern unsigned observerFailure, observerSetupCalls;
extern bool promiscuousEnabled;
inline esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t* filter)
{
    ++observerSetupCalls; assert(filter->filter_mask == WIFI_PROMIS_FILTER_MASK_MGMT);
    return observerFailure == 1 ? -1 : ESP_OK;
}
inline esp_err_t esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t cb)
{
    ++observerSetupCalls;
    if (observerFailure == 2) return -2;
    promiscuousCallback = cb; return ESP_OK;
}
inline esp_err_t esp_wifi_set_promiscuous(bool enabled)
{
    ++observerSetupCalls;
    if (enabled && observerFailure == 3) return -3;
    promiscuousEnabled = enabled; return ESP_OK;
}
