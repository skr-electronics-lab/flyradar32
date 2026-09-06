#include "storage.h"
#include "config.h"
#include <Preferences.h>

static Preferences prefs;
static AppSettings cache;
static SemaphoreHandle_t settingsMutex = nullptr;

static void loadAll() {
    prefs.begin(NVS_NS_WIFI, true);
    cache.staSsid     = prefs.getString("ssid", "");
    cache.staPassword = prefs.getString("pass", "");
    prefs.end();

    prefs.begin(NVS_NS_LOC, true);
    cache.lat = prefs.getDouble("lat", DEFAULT_LAT);
    cache.lon = prefs.getDouble("lon", DEFAULT_LON);
    prefs.end();

    prefs.begin(NVS_NS_API, true);
    for (int i = 0; i < PROVIDER_COUNT; i++) {
        String enKey = "en" + String(i);
        String prKey = "pr" + String(i);
        bool defEnabled = (i == PROVIDER_AIRPLANES_LIVE || i == PROVIDER_ADSB_LOL);
        cache.providerEnabled[i]  = prefs.getBool(enKey.c_str(), defEnabled);
        cache.providerPriority[i] = prefs.getUChar(prKey.c_str(), i);
    }
    cache.refreshInterval     = prefs.getInt("refInt", 10);
    prefs.end();

    prefs.begin(NVS_NS_DISPLAY, true);
    cache.zoomLevel      = prefs.getInt("zoom", 1);
    cache.labelsMode     = prefs.getUChar("lblMode", 2);
    cache.aircraftIcon   = prefs.getUChar("acIcon", AIRCRAFT_ICON_PLANE);
    cache.showSweepAnim  = prefs.getBool("swpAnim", true);
    cache.brightness     = prefs.getUChar("bright", 255);
    cache.configPin      = prefs.getString("pin", "");
    cache.theme          = prefs.getUChar("theme", 0);
    cache.showCompass    = prefs.getBool("cmp", true);
    cache.showRangeLabels= prefs.getBool("rlbl", true);
    cache.showTrail      = prefs.getBool("trail", true);
    prefs.end();

    // Defensive clamping in case NVS holds stale/out-of-range values
    // from a previous firmware version (e.g. aircraftIcon used to max
    // out at 1, theme is brand new, etc).
    if (cache.aircraftIcon >= AIRCRAFT_ICON_COUNT) cache.aircraftIcon = AIRCRAFT_ICON_PLANE;
    if (cache.theme >= THEME_COUNT) cache.theme = 0;
    if (cache.zoomLevel < 0 || cache.zoomLevel > 2) cache.zoomLevel = 1;
    if (cache.labelsMode > 2) cache.labelsMode = 2;
    if (cache.refreshInterval < 5 || cache.refreshInterval > 300) cache.refreshInterval = 10;
}

void Storage::begin() {
    settingsMutex = xSemaphoreCreateMutex();
    loadAll();
}

AppSettings& Storage::settings() {
    return cache;
}

void Storage::lock() {
    if (settingsMutex) xSemaphoreTake(settingsMutex, portMAX_DELAY);
}

void Storage::unlock() {
    if (settingsMutex) xSemaphoreGive(settingsMutex);
}

void Storage::saveWifi(const String& ssid, const String& pass) {
    prefs.begin(NVS_NS_WIFI, false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
    cache.staSsid = ssid;
    cache.staPassword = pass;
}

void Storage::clearWifi() {
    prefs.begin(NVS_NS_WIFI, false);
    prefs.clear();
    prefs.end();
    cache.staSsid = "";
    cache.staPassword = "";
}

void Storage::saveLocation(double lat, double lon) {
    prefs.begin(NVS_NS_LOC, false);
    prefs.putDouble("lat", lat);
    prefs.putDouble("lon", lon);
    prefs.end();
    cache.lat = lat;
    cache.lon = lon;
}

void Storage::saveProviderConfig(const bool enabled[PROVIDER_COUNT], const uint8_t priority[PROVIDER_COUNT]) {
    prefs.begin(NVS_NS_API, false);
    for (int i = 0; i < PROVIDER_COUNT; i++) {
        prefs.putBool(("en" + String(i)).c_str(), enabled[i]);
        prefs.putUChar(("pr" + String(i)).c_str(), priority[i]);
        cache.providerEnabled[i] = enabled[i];
        cache.providerPriority[i] = priority[i];
    }
    prefs.end();
}

void Storage::saveRefreshInterval(int seconds) {
    if (seconds < 5) seconds = 5;
    if (seconds > 300) seconds = 300;
    prefs.begin(NVS_NS_API, false);
    prefs.putInt("refInt", seconds);
    prefs.end();
    cache.refreshInterval = seconds;
}

void Storage::saveDisplay(int zoomLevel, uint8_t labelsMode, uint8_t aircraftIcon, bool showSweepAnim, uint8_t brightness,
                           uint8_t theme, bool showCompass, bool showRangeLabels, bool showTrail) {
    if (zoomLevel < 0 || zoomLevel > 2) zoomLevel = 1;
    if (labelsMode > 2) labelsMode = 2;
    if (aircraftIcon >= AIRCRAFT_ICON_COUNT) aircraftIcon = AIRCRAFT_ICON_PLANE;
    if (theme >= THEME_COUNT) theme = 0;

    prefs.begin(NVS_NS_DISPLAY, false);
    prefs.putInt("zoom", zoomLevel);
    prefs.putUChar("lblMode", labelsMode);
    prefs.putUChar("acIcon", aircraftIcon);
    prefs.putBool("swpAnim", showSweepAnim);
    prefs.putUChar("bright", brightness);
    prefs.putUChar("theme", theme);
    prefs.putBool("cmp", showCompass);
    prefs.putBool("rlbl", showRangeLabels);
    prefs.putBool("trail", showTrail);
    prefs.end();

    cache.zoomLevel = zoomLevel;
    cache.labelsMode = labelsMode;
    cache.aircraftIcon = aircraftIcon;
    cache.showSweepAnim = showSweepAnim;
    cache.brightness = brightness;
    cache.theme = theme;
    cache.showCompass = showCompass;
    cache.showRangeLabels = showRangeLabels;
    cache.showTrail = showTrail;
}

void Storage::saveConfigPin(const String& pin) {
    prefs.begin(NVS_NS_DISPLAY, false);
    prefs.putString("pin", pin);
    prefs.end();
    cache.configPin = pin;
}

void Storage::factoryReset() {
    const char* namespaces[] = {NVS_NS_WIFI, NVS_NS_LOC, NVS_NS_API, NVS_NS_DISPLAY};
    for (auto ns : namespaces) {
        prefs.begin(ns, false);
        prefs.clear();
        prefs.end();
    }
    loadAll();
}