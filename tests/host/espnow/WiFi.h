#pragma once
constexpr int WIFI_STA = 1, WIFI_POWER_8_5dBm = 2;
struct HostWiFi
{
    void mode(int) {}
    bool setSleep(bool) { return true; }
    bool setTxPower(int) { return true; }
};
extern HostWiFi WiFi;
