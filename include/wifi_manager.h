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

    // Non-blocking scan. Call 1: starts it (returns SCAN_STARTED).
    // Then poll: SCAN_RUNNING / SCAN_FAILED / n>=0 results (call again to
    // re-poll; results are consumed and freed on the first n>=0 return).
    enum ScanResult { SCAN_FAILED = -2, SCAN_RUNNING = -1, SCAN_STARTED = -3 };
    int  startScanAsync();
    int  pollScan(ScannedNetwork out[], int maxResults);

    WifiState getState();
    String getApSsid();
    String getApIp();
    String getStaIp();
    String getLastError();

    // NTP: "HH:MM" ("" if never synced) and "YYYY-MM-DD HH:MM" for detail rows
    bool  timeSynced();
    String getClockTime();       // HH:MM
    String getClockDateTime();   // YYYY-MM-DD HH:MM
}
