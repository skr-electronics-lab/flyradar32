#pragma once
#include <Arduino.h>
#include "config.h"

struct AppSettings {
    String staSsid;
    String staPassword;
    double lat;
    double lon;
    bool providerEnabled[PROVIDER_COUNT];
    uint8_t providerPriority[PROVIDER_COUNT];
    int refreshInterval;
    int zoomLevel;
    uint8_t labelsMode;
    uint8_t aircraftIcon;
    bool showSweepAnim;
    uint8_t brightness;
    String configPin;
    uint8_t theme;
    bool showCompass;
    bool showRangeLabels;
    bool showTrail;
};

class Storage {
public:
    static void begin();
    static AppSettings& settings();
    
    static void lock();
    static void unlock();
    
    static void saveWifi(const String& ssid, const String& pass);
    static void clearWifi();
    
    static void saveLocation(double lat, double lon);
    static void saveProviderConfig(const bool enabled[PROVIDER_COUNT], const uint8_t priority[PROVIDER_COUNT]);
    static void saveRefreshInterval(int seconds);
    
    static void saveDisplay(int zoomLevel, uint8_t labelsMode, uint8_t aircraftIcon, bool showSweepAnim, uint8_t brightness,
                           uint8_t theme = 0, bool showCompass = true, bool showRangeLabels = true, bool showTrail = true);
                           
    static void saveConfigPin(const String& pin);
    static void factoryReset();
};
