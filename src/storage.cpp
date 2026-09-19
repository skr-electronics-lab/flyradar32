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
    cache.timezone = prefs.getString("tz", DEFAULT_TZ);
    prefs.end();

    prefs.begin(NVS_NS_API, true);
    for (int i = 0; i < PROVIDER_COUNT; i++) {
        String enKey = "en" + String(i);
        String prKey = "pr" + String(i);
        // Default: all providers enabled. Default priority: adsb.lol(1)=0, OpenSky(0)=1, airplanes.live(2)=2
        // This makes adsb.lol the first-tried provider (no auth needed, HTTP, fastest).
        uint8_t defPriority = (i == PROVIDER_ADSB_LOL) ? 0 : (i == PROVIDER_OPENSKY) ? 1 : 2;
        cache.providerEnabled[i]  = prefs.getBool(enKey.c_str(), true);
        cache.providerPriority[i] = prefs.getUChar(prKey.c_str(), defPriority);
    }
    cache.refreshInterval     = prefs.getInt("refInt", 10);
    // Empty defaults: system will use anonymous/unauthenticated OpenSky if no credentials set.
    cache.openSkyClientId     = prefs.getString("osId", "");
    cache.openSkyClientSecret = prefs.getString("osSecret", "");
    prefs.end();

    // When OpenSky credentials are not configured, ensure anonymous OpenSky is not primary (priority 0)
    // to prevent immediate rate-limiting (429). Keep priorities distinct [0, 1, 2].
    if (cache.openSkyClientId.isEmpty() && cache.providerPriority[PROVIDER_OPENSKY] == 0) {
        // Swap OpenSky with whichever provider had priority 1
        int swapIdx = (cache.providerPriority[PROVIDER_ADSB_LOL] == 1) ? PROVIDER_ADSB_LOL : PROVIDER_AIRPLANES_LIVE;
        cache.providerPriority[PROVIDER_OPENSKY] = 1;
        cache.providerPriority[swapIdx] = 0;
    }

    // Open READ-WRITE so stale keys can be removed (read-only mode
    // silently ignores writes — was the root cause of a reboot loop).
    prefs.begin(NVS_NS_DISPLAY, false);
    cache.zoomLevel      = prefs.getInt("zoom", 1);
    cache.labelsMode     = prefs.getUChar("lblMode", 2);
    cache.aircraftIcon   = prefs.getUChar("acIcon", AIRCRAFT_ICON_DOT);
    cache.showSweepAnim  = prefs.getBool("swpAnim", true);
    cache.brightness     = prefs.getUChar("bright", 100);
    cache.theme          = prefs.getUChar("theme", 0);
    cache.showCompass    = prefs.getBool("cmp", true);
    cache.showRangeLabels= prefs.getBool("rlbl", true);
    cache.showTrail      = prefs.getBool("trail", true);
    cache.autoRange      = prefs.getBool("autoRng", false);
    prefs.end();

    if (cache.brightness < 10) cache.brightness = 10;
    if (cache.brightness > 100) cache.brightness = 100;

    // Defensive clamping in case NVS holds stale/out-of-range values
    // from a previous firmware version.
    if (cache.aircraftIcon >= AIRCRAFT_ICON_COUNT) cache.aircraftIcon = AIRCRAFT_ICON_DOT;
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

AppSettings Storage::getSnapshot() {
    lock();
    AppSettings copy = cache;
    unlock();
    return copy;
}

void Storage::lock() {
    if (settingsMutex) xSemaphoreTake(settingsMutex, portMAX_DELAY);
}

void Storage::unlock() {
    if (settingsMutex) xSemaphoreGive(settingsMutex);
}

void Storage::saveWifi(const String& ssid, const String& pass) {
    lock();
    prefs.begin(NVS_NS_WIFI, false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
    cache.staSsid = ssid;
    cache.staPassword = pass;
    unlock();
}

void Storage::clearWifi() {
    lock();
    prefs.begin(NVS_NS_WIFI, false);
    prefs.clear();
    prefs.end();
    cache.staSsid = "";
    cache.staPassword = "";
    unlock();
}

void Storage::saveLocation(double lat, double lon) {
    lock();
    prefs.begin(NVS_NS_LOC, false);
    prefs.putDouble("lat", lat);
    prefs.putDouble("lon", lon);
    prefs.end();
    cache.lat = lat;
    cache.lon = lon;
    unlock();
}

void Storage::saveProviderConfig(const bool enabled[PROVIDER_COUNT], const uint8_t priority[PROVIDER_COUNT]) {
    lock();
    prefs.begin(NVS_NS_API, false);
    for (int i = 0; i < PROVIDER_COUNT; i++) {
        prefs.putBool(("en" + String(i)).c_str(), enabled[i]);
        prefs.putUChar(("pr" + String(i)).c_str(), priority[i]);
        cache.providerEnabled[i] = enabled[i];
        cache.providerPriority[i] = priority[i];
    }
    prefs.end();
    unlock();
}

void Storage::saveOpenSkyCredentials(const String& clientId, const String& clientSecret) {
    lock();
    prefs.begin(NVS_NS_API, false);
    prefs.putString("osId", clientId);
    prefs.putString("osSecret", clientSecret);
    prefs.end();
    cache.openSkyClientId = clientId;
    cache.openSkyClientSecret = clientSecret;
    unlock();
}

void Storage::saveRefreshInterval(int seconds) {
    if (seconds < 5) seconds = 5;
    if (seconds > 300) seconds = 300;
    lock();
    prefs.begin(NVS_NS_API, false);
    prefs.putInt("refInt", seconds);
    prefs.end();
    cache.refreshInterval = seconds;
    unlock();
}

void Storage::saveTimezone(const String& tz) {
    lock();
    prefs.begin(NVS_NS_LOC, false);
    prefs.putString("tz", tz);
    prefs.end();
    cache.timezone = tz;
    unlock();
}

void Storage::saveDisplay(int zoomLevel, uint8_t labelsMode, uint8_t aircraftIcon, bool showSweepAnim,
                           uint8_t theme, bool showCompass, bool showRangeLabels, bool showTrail, uint8_t brightness) {
    if (zoomLevel < 0 || zoomLevel > 2) zoomLevel = 1;
    if (labelsMode > 2) labelsMode = 2;
    if (aircraftIcon >= AIRCRAFT_ICON_COUNT) aircraftIcon = AIRCRAFT_ICON_DOT;
    if (theme >= THEME_COUNT) theme = 0;
    if (brightness < 10) brightness = 10;
    if (brightness > 100) brightness = 100;

    lock();
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
    unlock();
}

void Storage::saveBrightness(uint8_t brightness) {
    if (brightness < 10) brightness = 10;
    if (brightness > 100) brightness = 100;
    lock();
    prefs.begin(NVS_NS_DISPLAY, false);
    prefs.putUChar("bright", brightness);
    prefs.end();
    cache.brightness = brightness;
    unlock();
}

void Storage::saveAutoRange(bool on) {
    lock();
    prefs.begin(NVS_NS_DISPLAY, false);
    prefs.putBool("autoRng", on);
    prefs.end();
    cache.autoRange = on;
    unlock();
}

void Storage::factoryReset() {
    lock();
    const char* namespaces[] = {NVS_NS_WIFI, NVS_NS_LOC, NVS_NS_API, NVS_NS_DISPLAY};
    for (auto ns : namespaces) {
        prefs.begin(ns, false);
        prefs.clear();
        prefs.end();
    }
    loadAll();
    unlock();
}