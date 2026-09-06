#pragma once
#include <Arduino.h>

enum WifiState {
    WIFI_STATE_AP_MODE,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED
};

struct ScannedNetwork {
    String ssid;
    int32_t rssi;
    bool secure;
};

namespace WifiManager {
    void begin();
    void loop();

    void startConnect(const String& ssid, const String& password);
    int  scanNetworks(ScannedNetwork out[], int maxResults);

    WifiState getState();
    String getApSsid();
    String getApIp();
    String getStaIp();
    String getLastError();
}
