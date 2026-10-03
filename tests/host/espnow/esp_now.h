#pragma once
#include "Arduino.h"
using esp_err_t = int;
constexpr int ESP_OK = 0, WIFI_IF_STA = 1;
enum esp_now_send_status_t { ESP_NOW_SEND_SUCCESS, ESP_NOW_SEND_FAIL };
using esp_now_send_cb_t = void (*)(const uint8_t*, esp_now_send_status_t);
using esp_now_recv_cb_t = void (*)(const uint8_t*, const uint8_t*, int);
struct esp_now_peer_info_t { uint8_t peer_addr[6]; int channel, ifidx; bool encrypt; };
extern esp_now_send_cb_t sentCallback;
extern esp_now_recv_cb_t receivedCallback;
inline esp_err_t esp_now_init() { return ESP_OK; }
inline esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb) { sentCallback = cb; return ESP_OK; }
inline esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb) { receivedCallback = cb; return ESP_OK; }
inline esp_err_t esp_now_add_peer(const esp_now_peer_info_t*) { return ESP_OK; }
esp_err_t esp_now_send(const uint8_t*, const uint8_t*, size_t);
