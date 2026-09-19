#pragma once
#include <Arduino.h>
#include "config.h"
#include "api_providers.h"

enum AppScreen {
    SCR_BOOT,
    SCR_WIFI_SETUP,
    SCR_RADAR,
    SCR_PLANE_LIST,
    SCR_PLANE_DETAIL,
    SCR_SETTINGS_MAIN,
    SCR_SETTINGS_DISPLAY,
    SCR_SETTINGS_LABELS,
    SCR_SETTINGS_ICON,
    SCR_SETTINGS_PROVIDERS,
    SCR_SYSTEM_INFO,
    SCR_WEATHER,
    SCR_FACTORY_RESET_CONFIRM
};

namespace RadarDisplay {
    void begin();
    void assertBacklight(); // Re-assert full 100% backlight – call after all init

    void showBootStatus(const char* line1, const char* line2 = "");
    void showWifiSetupScreen(const String& apSsid, const String& apIp);
    void showConnectedScreen(const String& staIp);

    // labelsMode/aircraftIcon/theme/compass/rangeLabels/trail are read live
    // from Storage::settings() inside the implementation, so they always
    // reflect the latest saved value without bloating this call signature.
    void renderRadar(const AircraftPoint planes[], int count, int sweepAngle,
                     int selectedIndex, float rangeKm, uint8_t labelsMode,
                     bool showSweepAnim, const ApiProviders::Status& status);

    void renderPlaneList(const AircraftPoint planes[], int count, int selectedIndex);
    void renderPlaneDetail(const AircraftPoint& p, int scrollY = 0);

    // Feed the breadcrumb-trail history from the current snapshot. Call from
    // the main loop on every new fetch so trails keep recording while the
    // user sits in menus (renderRadar only draws what's already recorded).
    void sampleTrailHistory(const AircraftPoint planes[], int count);

    void renderScrollMenu(const char* title, const char* items[], int itemCount, int selectedIndex, int scrollOffset);

    void renderSystemInfo(const String& ip, const String& wifiSsid);
    void renderFactoryResetConfirm();

    // Full weather card screen (entered via UP+DOWN long-press).
    void renderWeatherScreen();
    void toggleWeatherPage();

    void forceLVGLRefresh();
}