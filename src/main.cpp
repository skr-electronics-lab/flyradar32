#include <Arduino.h>
#include "config.h"
#include "storage.h"
#include "buttons.h"
#include "radar_display.h"
#include "wifi_manager.h"
#include "api_providers.h"
#include "webui.h"
#include <lvgl.h>

static AppScreen currentScreen = SCR_BOOT;

static AircraftPoint planes[MAX_PLANES];
static int planeCount = 0;

static int sweepAngle = 0;
static int selectedPlaneIndex = -1;
static int detailPlaneIndex = -1;
static char detailPlaneHex[8] = "";   // ICAO of the plane shown on the detail screen
static int detailScrollY = 0;
static int listSelectedIndex = 0;

static int settingsMainIndex = 0;
static int settingsMainScroll = 0;
static int settingsDisplayIndex = 0;
static int settingsLabelsIndex = 0;
static int settingsIconIndex = 0;
static int settingsProvidersIndex = 0;
static int settingsProvidersScroll = 0;

static unsigned long lastPeriodicRefreshMs = 0;
static unsigned long lastDrawMs = 0;
static unsigned long wifiConnectedScreenShownAt = 0;

static const char* THEME_NAMES[THEME_COUNT] = {"GREEN", "CYAN", "AMBER"};

// ---------------------------------------------------------------
// Settings menu layout.
// ---------------------------------------------------------------
#define SETTINGS_MAIN_COUNT 17
static const char* settingsMainLabels[SETTINGS_MAIN_COUNT] = {
    "Radar Range",
    "Auto Range",
    "Brightness",
    "Animation",
    "Labels",
    "Aircraft Icon",
    "Theme",
    "Compass",
    "Range Labels",
    "Trail",
    "Data Providers",
    "Aircraft List",
    "Weather Report",
    "Refresh Now",
    "System Info",
    "Factory Reset",
    "Back to Radar"
};

#define SETTINGS_DISPLAY_COUNT 4
static const char* settingsDisplayLabels[SETTINGS_DISPLAY_COUNT] = {
    "50 km",
    "100 km",
    "150 km",
    "Back"
};

#define SETTINGS_LABELS_COUNT 4
static const char* settingsLabelsLabels[SETTINGS_LABELS_COUNT] = {
    "Disabled",
    "Selected Only",
    "All Aircraft",
    "Back"
};

#define SETTINGS_ICON_COUNT 4
static const char* settingsIconLabels[SETTINGS_ICON_COUNT] = {
    "Dots",
    "Arrow",
    "Aeroplane",
    "Back"
};

#define SETTINGS_PROVIDERS_COUNT 4
static const char* settingsProvidersLabels[SETTINGS_PROVIDERS_COUNT] = {
    "OpenSky",
    "adsb.lol",
    "airplanes.live",
    "Back"
};

static int calcScrollOffset(int selected, int total, int visible) {
    if (total <= visible) return 0;
    if (selected < 0) return 0;
    if (selected >= total) return total - visible;
    int offset = selected - visible / 2;
    if (offset < 0) offset = 0;
    if (offset > total - visible) offset = total - visible;
    return offset;
}

// Effective display zoom index (0..2) for this cycle. When autoRange is on,
// pick the smallest fixed zoom that contains the 10th-closest valid aircraft,
// with 2-cycle hysteresis so traffic sitting near a boundary doesn't flap.
// planes[] must already be valid-filled (not necessarily bearing-sorted).
static int effectiveZoom() {
    AppSettings& s = Storage::settings();
    if (!s.autoRange) return s.zoomLevel;

    // distances of valid planes (insertion sort, we only need order)
    float d[MAX_PLANES];
    int n = 0;
    for (int i = 0; i < planeCount && n < MAX_PLANES; i++) {
        if (planes[i].valid) {
            int j = n++;
            while (j > 0 && d[j-1] > planes[i].distanceKm) { d[j] = d[j-1]; j--; }
            d[j] = planes[i].distanceKm;
        }
    }
    if (n == 0) return s.zoomLevel;

    float ref = d[n >= 10 ? 9 : n - 1];   // 10th closest, or closest if fewer
    int want = 0;
    while (want < 2 && ref > ApiProviders::ZOOM_KM[want]) want++;

    static int lastWant = -1;
    static int stableCount = 0;
    static int applied = -1;  // -1 = uninitialised; set to saved zoom on first call
    if (applied < 0) applied = Storage::settings().zoomLevel;
    if (want == lastWant) {
        if (stableCount < 2) stableCount++;
        if (stableCount >= 2 && applied != want) applied = want;   // commit
    } else {
        stableCount = 0;
        lastWant = want;
    }
    return applied;
}

static void clampSelection() {
    if (planeCount == 0) { selectedPlaneIndex = -1; return; }

    float maxVisibleDist = ApiProviders::ZOOM_KM[effectiveZoom()];

    if (selectedPlaneIndex >= 0 && selectedPlaneIndex < planeCount) {
        if (!planes[selectedPlaneIndex].valid || planes[selectedPlaneIndex].distanceKm > maxVisibleDist) {
            selectedPlaneIndex = -1;
        }
    }

    if (selectedPlaneIndex < 0) {
        for (int i = 0; i < planeCount; i++) {
            if (planes[i].valid && planes[i].distanceKm <= maxVisibleDist) {
                selectedPlaneIndex = i;
                break;
            }
        }
    }
}

// Sort planes by compass bearing (N->E->S->W clockwise). Called once per
// new fetch so UP/DOWN walking the array = walking clockwise around the
// scope, and the plane list shows compass order too.
static void sortByBearing() {
    char anchorHex[8] = "";
    if (selectedPlaneIndex >= 0 && selectedPlaneIndex < planeCount) {
        strncpy(anchorHex, planes[selectedPlaneIndex].icaoHex, 7);
        anchorHex[7] = '\0';
    }
    for (int i = 1; i < planeCount; i++) {
        AircraftPoint key = planes[i];
        int j = i - 1;
        while (j >= 0 && planes[j].bearingDeg > key.bearingDeg) {
            planes[j + 1] = planes[j];
            j--;
        }
        planes[j + 1] = key;
    }
    // Re-anchor the cursor to the same aircraft it had selected
    if (anchorHex[0] != '\0') {
        selectedPlaneIndex = -1;
        for (int i = 0; i < planeCount; i++) {
            if (planes[i].valid && strncmp(planes[i].icaoHex, anchorHex, 7) == 0) {
                selectedPlaneIndex = i;
                break;
            }
        }
        if (selectedPlaneIndex < 0) clampSelection();
    }
    // Re-anchor detailPlaneIndex (shown on SCR_PLANE_DETAIL) by hex so
    // the detail card never shows the wrong aircraft after a sort.
    if (detailPlaneHex[0] != '\0') {
        detailPlaneIndex = -1;
        for (int i = 0; i < planeCount; i++) {
            if (planes[i].valid && strncmp(planes[i].icaoHex, detailPlaneHex, 7) == 0) {
                detailPlaneIndex = i;
                break;
            }
        }
    }
}

static void moveSelection(int delta) {
    if (planeCount == 0) { selectedPlaneIndex = -1; return; }
    
    float maxVisibleDist = ApiProviders::ZOOM_KM[effectiveZoom()];
    int startIdx = selectedPlaneIndex;
    if (startIdx < 0) startIdx = 0; // If nothing selected, start from 0

    int nextIdx = startIdx;
    for (int i = 0; i < planeCount; i++) {
        nextIdx = (nextIdx + delta + planeCount) % planeCount;
        if (planes[nextIdx].valid && planes[nextIdx].distanceKm <= maxVisibleDist) {
            selectedPlaneIndex = nextIdx;
            return;
        }
    }
    // If no valid planes in range, deselect
    selectedPlaneIndex = -1;
}

// Persists every field currently in AppSettings that belongs to the
// "display" NVS namespace. Centralized here so every settings toggle
// below only has to mutate `s` and call this one helper.
static void persistDisplay(AppSettings& s) {
    Storage::saveDisplay(s.zoomLevel, s.labelsMode, s.aircraftIcon, s.showSweepAnim,
                          s.theme, s.showCompass, s.showRangeLabels, s.showTrail, s.brightness);
}

static void handleRadarButtons(ButtonEvent ev, ButtonId which) {
    if (ev == BTN_EVENT_SHORT_PRESS) {
        if (which == BTN_ID_UP || which == BTN_ID_DOWN) {
            if (planeCount > 0) {
                moveSelection(which == BTN_ID_UP ? -1 : 1);
            }
        } else if (which == BTN_ID_SELECT) {
            if (selectedPlaneIndex >= 0) {
                detailPlaneIndex = selectedPlaneIndex;
                strncpy(detailPlaneHex, planes[selectedPlaneIndex].icaoHex, 7);
                detailPlaneHex[7] = '\0';
                detailScrollY = 0;
                currentScreen = SCR_PLANE_DETAIL;
            } else {
                currentScreen = SCR_WEATHER;
            }
        }
    } else if (ev == BTN_EVENT_REPEAT) {
        if ((which == BTN_ID_UP || which == BTN_ID_DOWN) && planeCount > 0) {
            moveSelection(which == BTN_ID_UP ? -1 : 1);
        }
    } else if (ev == BTN_EVENT_LONG_PRESS) {
        if (which == BTN_ID_SELECT) {
            settingsMainIndex = 0; settingsMainScroll = 0;
            currentScreen = SCR_SETTINGS_MAIN;
        } else if (which == BTN_ID_UP) {
            currentScreen = SCR_WEATHER;
        } else if (which == BTN_ID_DOWN) {
            listSelectedIndex = 0;
            currentScreen = SCR_PLANE_LIST;
        }
    } else if (ev == BTN_EVENT_DUAL_LONG_PRESS) {
        currentScreen = SCR_WEATHER;   // UP+DOWN held together
    }
}

static void handleDetailButtons(ButtonEvent ev, ButtonId which) {
    if (planeCount == 0) { currentScreen = SCR_RADAR; return; }
    if (ev == BTN_EVENT_SHORT_PRESS || ev == BTN_EVENT_REPEAT) {
        if (which == BTN_ID_UP) {
            detailScrollY = max(0, detailScrollY - 12);
        } else if (which == BTN_ID_DOWN) {
            detailScrollY = min(125, detailScrollY + 12);
        } else if (which == BTN_ID_SELECT && ev == BTN_EVENT_SHORT_PRESS) {
            selectedPlaneIndex = detailPlaneIndex;
            currentScreen = SCR_RADAR;
        }
    } else if (ev == BTN_EVENT_LONG_PRESS && which == BTN_ID_SELECT) {
        selectedPlaneIndex = detailPlaneIndex;
        currentScreen = SCR_RADAR;
    }
}

static void handleListButtons(ButtonEvent ev, ButtonId which) {
    if (planeCount <= 0) {
        listSelectedIndex = 0;
        if (which == BTN_ID_SELECT && (ev == BTN_EVENT_SHORT_PRESS || ev == BTN_EVENT_LONG_PRESS)) {
            currentScreen = SCR_RADAR;
        }
        return;
    }
    if (ev == BTN_EVENT_SHORT_PRESS) {
        if (which == BTN_ID_UP) listSelectedIndex = max(0, listSelectedIndex - 1);
        else if (which == BTN_ID_DOWN) listSelectedIndex = min(planeCount - 1, listSelectedIndex + 1);
        else if (which == BTN_ID_SELECT) {
            detailPlaneIndex = listSelectedIndex;
            strncpy(detailPlaneHex, planes[listSelectedIndex].icaoHex, 7);
            detailPlaneHex[7] = '\0';
            detailScrollY = 0;
            currentScreen = SCR_PLANE_DETAIL;
        }
    } else if (ev == BTN_EVENT_REPEAT) {
        if (which == BTN_ID_UP) listSelectedIndex = max(0, listSelectedIndex - 1);
        else if (which == BTN_ID_DOWN) listSelectedIndex = min(planeCount - 1, listSelectedIndex + 1);
    } else if (ev == BTN_EVENT_LONG_PRESS && which == BTN_ID_SELECT) {
        currentScreen = SCR_RADAR;
    }
}

static void handleSettingsButtons(ButtonEvent ev, ButtonId which) {
    AppSettings& s = Storage::settings();

    switch (currentScreen) {

    case SCR_SETTINGS_MAIN: {
        if (ev == BTN_EVENT_SHORT_PRESS || ev == BTN_EVENT_REPEAT) {
            if (which == BTN_ID_UP) {
                settingsMainIndex = (settingsMainIndex - 1 + SETTINGS_MAIN_COUNT) % SETTINGS_MAIN_COUNT;
                settingsMainScroll = calcScrollOffset(settingsMainIndex, SETTINGS_MAIN_COUNT, 7);
            } else if (which == BTN_ID_DOWN) {
                settingsMainIndex = (settingsMainIndex + 1) % SETTINGS_MAIN_COUNT;
                settingsMainScroll = calcScrollOffset(settingsMainIndex, SETTINGS_MAIN_COUNT, 7);
            } else if (which == BTN_ID_SELECT && ev == BTN_EVENT_SHORT_PRESS) {
                switch (settingsMainIndex) {
                    case 0: settingsDisplayIndex = s.zoomLevel; currentScreen = SCR_SETTINGS_DISPLAY; break;
                    case 1: // Auto Range direct toggle
                        s.autoRange = !s.autoRange;
                        Storage::saveAutoRange(s.autoRange);
                        break;
                    case 2: // Brightness
                        currentScreen = SCR_SETTINGS_BRIGHTNESS;
                        break;
                    case 3:
                        s.showSweepAnim = !s.showSweepAnim;
                        persistDisplay(s);
                        RadarDisplay::forceLVGLRefresh();
                        break;
                    case 4:
                        settingsLabelsIndex = s.labelsMode;
                        currentScreen = SCR_SETTINGS_LABELS;
                        break;
                    case 5:
                        settingsIconIndex = s.aircraftIcon;
                        currentScreen = SCR_SETTINGS_ICON;
                        break;
                    case 6: // Theme: cycle 0 -> 1 -> 2 -> 0
                        s.theme = (s.theme + 1) % THEME_COUNT;
                        persistDisplay(s);
                        RadarDisplay::forceLVGLRefresh();
                        break;
                    case 7: // Compass toggle
                        s.showCompass = !s.showCompass;
                        persistDisplay(s);
                        RadarDisplay::forceLVGLRefresh();
                        break;
                    case 8: // Range labels toggle
                        s.showRangeLabels = !s.showRangeLabels;
                        persistDisplay(s);
                        RadarDisplay::forceLVGLRefresh();
                        break;
                    case 9: // Trail toggle
                        s.showTrail = !s.showTrail;
                        persistDisplay(s);
                        RadarDisplay::forceLVGLRefresh();
                        break;
                    case 10: settingsProvidersIndex = 0; settingsProvidersScroll = 0; currentScreen = SCR_SETTINGS_PROVIDERS; break;
                    case 11: listSelectedIndex = 0; currentScreen = SCR_PLANE_LIST; break;
                    case 12: currentScreen = SCR_WEATHER; break;
                    case 13: ApiProviders::requestRefresh(); currentScreen = SCR_RADAR; break;
                    case 14: currentScreen = SCR_SYSTEM_INFO; break;
                    case 15: currentScreen = SCR_FACTORY_RESET_CONFIRM; break;
                    case 16: currentScreen = SCR_RADAR; break;
                }
            }
        } else if (ev == BTN_EVENT_LONG_PRESS && which == BTN_ID_SELECT) {
            currentScreen = SCR_RADAR;
        }
        break;
    }

    case SCR_SETTINGS_BRIGHTNESS: {
        if (ev == BTN_EVENT_SHORT_PRESS || ev == BTN_EVENT_REPEAT) {
            if (which == BTN_ID_UP) {
                if (s.brightness <= 90) s.brightness += 10;
                else s.brightness = 100;
                RadarDisplay::setBrightness(s.brightness);
            } else if (which == BTN_ID_DOWN) {
                if (s.brightness >= 20) s.brightness -= 10;
                else s.brightness = 10;
                RadarDisplay::setBrightness(s.brightness);
            } else if (which == BTN_ID_SELECT && ev == BTN_EVENT_SHORT_PRESS) {
                persistDisplay(s);
                currentScreen = SCR_SETTINGS_MAIN;
            }
        } else if (ev == BTN_EVENT_LONG_PRESS && which == BTN_ID_SELECT) {
            persistDisplay(s);
            currentScreen = SCR_SETTINGS_MAIN;
        }
        break;
    }

    case SCR_SETTINGS_DISPLAY: {
        if (ev == BTN_EVENT_SHORT_PRESS || ev == BTN_EVENT_REPEAT) {
            if (which == BTN_ID_UP) settingsDisplayIndex = (settingsDisplayIndex - 1 + SETTINGS_DISPLAY_COUNT) % SETTINGS_DISPLAY_COUNT;
            else if (which == BTN_ID_DOWN) settingsDisplayIndex = (settingsDisplayIndex + 1) % SETTINGS_DISPLAY_COUNT;
            else if (which == BTN_ID_SELECT && ev == BTN_EVENT_SHORT_PRESS) {
                if (settingsDisplayIndex < 3) {
                    s.zoomLevel = settingsDisplayIndex;
                    // Manual pick overrides auto â€”switch it off
                    if (s.autoRange) { s.autoRange = false; Storage::saveAutoRange(false); }
                    persistDisplay(s);
                    RadarDisplay::forceLVGLRefresh();
                } else {
                    currentScreen = SCR_SETTINGS_MAIN;
                }
            }
        } else if (ev == BTN_EVENT_LONG_PRESS && which == BTN_ID_SELECT) {
            currentScreen = SCR_SETTINGS_MAIN;
        }
        break;
    }

    case SCR_SETTINGS_LABELS: {
        if (ev == BTN_EVENT_SHORT_PRESS || ev == BTN_EVENT_REPEAT) {
            if (which == BTN_ID_UP) settingsLabelsIndex = (settingsLabelsIndex - 1 + SETTINGS_LABELS_COUNT) % SETTINGS_LABELS_COUNT;
            else if (which == BTN_ID_DOWN) settingsLabelsIndex = (settingsLabelsIndex + 1) % SETTINGS_LABELS_COUNT;
            else if (which == BTN_ID_SELECT && ev == BTN_EVENT_SHORT_PRESS) {
                if (settingsLabelsIndex < 3) {
                    s.labelsMode = settingsLabelsIndex;
                    persistDisplay(s);
                    RadarDisplay::forceLVGLRefresh();
                } else {
                    currentScreen = SCR_SETTINGS_MAIN;
                }
            }
        } else if (ev == BTN_EVENT_LONG_PRESS && which == BTN_ID_SELECT) {
            currentScreen = SCR_SETTINGS_MAIN;
        }
        break;
    }

    case SCR_SETTINGS_ICON: {
        if (ev == BTN_EVENT_SHORT_PRESS || ev == BTN_EVENT_REPEAT) {
            if (which == BTN_ID_UP) settingsIconIndex = (settingsIconIndex - 1 + SETTINGS_ICON_COUNT) % SETTINGS_ICON_COUNT;
            else if (which == BTN_ID_DOWN) settingsIconIndex = (settingsIconIndex + 1) % SETTINGS_ICON_COUNT;
            else if (which == BTN_ID_SELECT && ev == BTN_EVENT_SHORT_PRESS) {
                if (settingsIconIndex < 3) {
                    s.aircraftIcon = settingsIconIndex;
                    persistDisplay(s);
                    RadarDisplay::forceLVGLRefresh();
                } else {
                    currentScreen = SCR_SETTINGS_MAIN;
                }
            }
        } else if (ev == BTN_EVENT_LONG_PRESS && which == BTN_ID_SELECT) {
            currentScreen = SCR_SETTINGS_MAIN;
        }
        break;
    }

    case SCR_SETTINGS_PROVIDERS: {
        if (ev == BTN_EVENT_SHORT_PRESS || ev == BTN_EVENT_REPEAT) {
            if (which == BTN_ID_UP) {
                settingsProvidersIndex = (settingsProvidersIndex - 1 + SETTINGS_PROVIDERS_COUNT) % SETTINGS_PROVIDERS_COUNT;
                settingsProvidersScroll = calcScrollOffset(settingsProvidersIndex, SETTINGS_PROVIDERS_COUNT, 7);
            } else if (which == BTN_ID_DOWN) {
                settingsProvidersIndex = (settingsProvidersIndex + 1) % SETTINGS_PROVIDERS_COUNT;
                settingsProvidersScroll = calcScrollOffset(settingsProvidersIndex, SETTINGS_PROVIDERS_COUNT, 7);
            } else if (which == BTN_ID_SELECT && ev == BTN_EVENT_SHORT_PRESS) {
                if (settingsProvidersIndex < PROVIDER_COUNT) {
                    s.providerEnabled[settingsProvidersIndex] = !s.providerEnabled[settingsProvidersIndex];
                    Storage::saveProviderConfig(s.providerEnabled, s.providerPriority);
                    RadarDisplay::forceLVGLRefresh();
                } else {
                    currentScreen = SCR_SETTINGS_MAIN;
                }
            }
        } else if (ev == BTN_EVENT_LONG_PRESS && which == BTN_ID_SELECT) {
            currentScreen = SCR_SETTINGS_MAIN;
        }
        break;
    }

    case SCR_SYSTEM_INFO: {
        if ((ev == BTN_EVENT_SHORT_PRESS && which == BTN_ID_SELECT) ||
            (ev == BTN_EVENT_LONG_PRESS && which == BTN_ID_SELECT)) {
            currentScreen = SCR_SETTINGS_MAIN;
        }
        break;
    }

    case SCR_FACTORY_RESET_CONFIRM: {
        if (ev == BTN_EVENT_SHORT_PRESS && (which == BTN_ID_UP || which == BTN_ID_DOWN)) {
            currentScreen = SCR_SETTINGS_MAIN;          // matches the on-screen "UP/DOWN to cancel"
        } else if (ev == BTN_EVENT_SHORT_PRESS && which == BTN_ID_SELECT) {
            Storage::factoryReset();
            delay(100);
            ESP.restart();
        } else if (ev == BTN_EVENT_LONG_PRESS && which == BTN_ID_SELECT) {
            currentScreen = SCR_SETTINGS_MAIN;
        }
        break;
    }

    default: break;
    }
}

void setup() {
    Serial.begin(115200);

    Storage::begin();
    Buttons::begin();
    RadarDisplay::begin();

    RadarDisplay::showBootStatus("STARTING...", "");
    for(int i=0; i<5; i++) { lv_timer_handler(); delay(10); }
    
    RadarDisplay::showBootStatus("CONNECTING WIFI", "or starting setup AP");
    for(int i=0; i<5; i++) { lv_timer_handler(); delay(10); }
    
    WifiManager::begin();

    WebUI::begin();
    ApiProviders::begin();

    Serial.printf("[WiFi] State: %d, STA IP: %s, AP IP: %s\n",
        (int)WifiManager::getState(),
        WifiManager::getStaIp().c_str(),
        WifiManager::getApIp().c_str());

    if (WifiManager::getState() == WIFI_STATE_CONNECTED) {
        RadarDisplay::showConnectedScreen(WifiManager::getStaIp());
        for(int i=0; i<100; i++) { lv_timer_handler(); delay(15); } // wait 1.5s
        currentScreen = SCR_RADAR;
        ApiProviders::requestRefresh();
    } else {
        currentScreen = SCR_WIFI_SETUP;
    }
    lastPeriodicRefreshMs = millis();

    // Always end setup at 100% brightness — re-assert after all init is done.
    RadarDisplay::assertBacklight();
}

void loop() {
    WifiManager::loop();

    if (currentScreen == SCR_WIFI_SETUP) {
        static bool setupScreenDrawn = false;
        static bool connectedScreenDrawn = false;
        static WifiState lastWs = (WifiState)-1;
        
        WifiState ws = WifiManager::getState();
        if (ws != lastWs) {
            lastWs = ws;
            if (ws != WIFI_STATE_CONNECTED) {
                setupScreenDrawn = false;
                connectedScreenDrawn = false;
            }
        }
        if (ws == WIFI_STATE_CONNECTED) {
            if (!connectedScreenDrawn) {
                RadarDisplay::showConnectedScreen(WifiManager::getStaIp());
                connectedScreenDrawn = true;
                wifiConnectedScreenShownAt = millis();
            }
            if (wifiConnectedScreenShownAt > 0 && millis() - wifiConnectedScreenShownAt > 2000) {
                currentScreen = SCR_RADAR;
                ApiProviders::requestRefresh();
            }
        } else {
            if (!setupScreenDrawn) {
                RadarDisplay::showWifiSetupScreen(WifiManager::getApSsid(), WifiManager::getApIp());
                setupScreenDrawn = true;
            }
        }
        
        lv_timer_handler();
        delay(20);
        return;
    }

    ButtonId which;
    ButtonEvent ev = Buttons::poll(which);
    if (ev != BTN_EVENT_NONE) {
        switch (currentScreen) {
            case SCR_RADAR:                handleRadarButtons(ev, which); break;
            case SCR_PLANE_DETAIL:         handleDetailButtons(ev, which); break;
            case SCR_PLANE_LIST:           handleListButtons(ev, which); break;
            case SCR_WEATHER:
                // any SELECT press (short or long) returns to the radar
                if (which == BTN_ID_SELECT) currentScreen = SCR_RADAR;
                else if (which == BTN_ID_UP || which == BTN_ID_DOWN) {
                    RadarDisplay::toggleWeatherPage();
                }
                break;
            case SCR_SETTINGS_MAIN:
            case SCR_SETTINGS_DISPLAY:
            case SCR_SETTINGS_LABELS:
            case SCR_SETTINGS_ICON:
            case SCR_SETTINGS_PROVIDERS:
            case SCR_SYSTEM_INFO:
            case SCR_FACTORY_RESET_CONFIRM: handleSettingsButtons(ev, which); break;
            default: break;
        }
    }

    AppSettings& s = Storage::settings();

    uint32_t intervalMs = (s.refreshInterval < 10 ? 10 : s.refreshInterval) * 1000UL;
    if (millis() - lastPeriodicRefreshMs > intervalMs) {
        lastPeriodicRefreshMs = millis();
        ApiProviders::requestRefresh();
    }

    if (millis() - lastDrawMs >= 30) {
        lastDrawMs = millis();

        ApiProviders::getLatest(planes, planeCount);
        // New fetch arrived? Sort by compass bearing (once) so cursor/list
        // walk clockwise from here on.
        static unsigned long lastSortedSuccessMs = 0;
        ApiProviders::Status pst = ApiProviders::getStatus();
        if (planeCount > 0 && pst.lastSuccessMs != 0 && pst.lastSuccessMs != lastSortedSuccessMs) {
            lastSortedSuccessMs = pst.lastSuccessMs;
            sortByBearing();
        }
        RadarDisplay::sampleTrailHistory(planes, planeCount);
        clampSelection();
        if (listSelectedIndex >= planeCount || listSelectedIndex < 0) listSelectedIndex = planeCount > 0 ? constrain(listSelectedIndex, 0, planeCount - 1) : 0;

        // Re-anchor the detail view by ICAO hex
        if (currentScreen == SCR_PLANE_DETAIL && detailPlaneHex[0] != '\0') {
            int idx = -1;
            for (int i = 0; i < planeCount; i++) {
                if (planes[i].valid && strncmp(planes[i].icaoHex, detailPlaneHex, 7) == 0) { idx = i; break; }
            }
            if (idx < 0) currentScreen = SCR_RADAR;
            else detailPlaneIndex = idx;
        } else if (detailPlaneIndex >= planeCount) {
            detailPlaneIndex = planeCount > 0 ? planeCount - 1 : -1;
        }

        switch (currentScreen) {
            case SCR_RADAR: {
                int ez = effectiveZoom();
                RadarDisplay::renderRadar(planes, planeCount, sweepAngle, selectedPlaneIndex,
                                           ApiProviders::ZOOM_KM[ez], s.labelsMode, s.showSweepAnim,
                                           ApiProviders::getStatus());
                if (s.showSweepAnim) sweepAngle = (sweepAngle + 4) % 360;
                break;
            }
            case SCR_PLANE_DETAIL:
                if (detailPlaneIndex >= 0 && detailPlaneIndex < planeCount) {
                    RadarDisplay::renderPlaneDetail(planes[detailPlaneIndex], detailScrollY);
                } else {
                    currentScreen = SCR_RADAR;
                }
                break;
            case SCR_PLANE_LIST:
                RadarDisplay::renderPlaneList(planes, planeCount, listSelectedIndex);
                break;
            case SCR_SETTINGS_MAIN: {
                char labels[SETTINGS_MAIN_COUNT][24];
                const char* ptrs[SETTINGS_MAIN_COUNT];
                for (int i = 0; i < SETTINGS_MAIN_COUNT; i++) ptrs[i] = settingsMainLabels[i];
                snprintf(labels[0], 24, "Range: %d km%s", (int)ApiProviders::ZOOM_KM[s.zoomLevel], s.autoRange ? " A" : "");
                snprintf(labels[1], 24, "Auto Range: %s", s.autoRange ? "ON" : "OFF");
                snprintf(labels[2], 24, "Brightness: %d%%", s.brightness);
                snprintf(labels[3], 24, "Anim: %s", s.showSweepAnim ? "ON" : "OFF");
                const char* lm = s.labelsMode == 0 ? "OFF" : (s.labelsMode == 1 ? "SELECTED" : "ALL");
                snprintf(labels[4], 24, "Labels: %s", lm);
                const char* iconName = s.aircraftIcon == 0 ? "DOTS" : (s.aircraftIcon == 1 ? "ARROW" : "PLANE");
                snprintf(labels[5], 24, "Icon: %s", iconName);
                snprintf(labels[6], 24, "Theme: %s", THEME_NAMES[s.theme]);
                snprintf(labels[7], 24, "Compass: %s", s.showCompass ? "ON" : "OFF");
                snprintf(labels[8], 24, "Range Labels: %s", s.showRangeLabels ? "ON" : "OFF");
                snprintf(labels[9], 24, "Trail: %s", s.showTrail ? "ON" : "OFF");
                for (int i = 0; i <= 9; i++) ptrs[i] = labels[i];
                RadarDisplay::renderScrollMenu("SETTINGS", ptrs, SETTINGS_MAIN_COUNT, settingsMainIndex, settingsMainScroll);
                break;
            }
            case SCR_SETTINGS_BRIGHTNESS:
                RadarDisplay::renderBrightnessMenu(s.brightness);
                break;
            case SCR_SETTINGS_DISPLAY:
                RadarDisplay::renderScrollMenu("RADAR RANGE", settingsDisplayLabels, SETTINGS_DISPLAY_COUNT, settingsDisplayIndex, 0);
                break;
            case SCR_SETTINGS_LABELS:
                RadarDisplay::renderScrollMenu("LABELS", settingsLabelsLabels, SETTINGS_LABELS_COUNT, settingsLabelsIndex, 0);
                break;
            case SCR_SETTINGS_ICON:
                RadarDisplay::renderScrollMenu("AIRCRAFT ICON", settingsIconLabels, SETTINGS_ICON_COUNT, settingsIconIndex, 0);
                break;
            case SCR_SETTINGS_PROVIDERS: {
                char labels[SETTINGS_PROVIDERS_COUNT][24];
                const char* ptrs[SETTINGS_PROVIDERS_COUNT];
                for (int i = 0; i < SETTINGS_PROVIDERS_COUNT; i++) {
                    if (i < PROVIDER_COUNT) {
                        snprintf(labels[i], 24, "%s [%s]", settingsProvidersLabels[i], s.providerEnabled[i] ? "ON" : "OFF");
                        ptrs[i] = labels[i];
                    } else {
                        ptrs[i] = settingsProvidersLabels[i];
                    }
                }
                RadarDisplay::renderScrollMenu("PROVIDERS", ptrs, SETTINGS_PROVIDERS_COUNT, settingsProvidersIndex, settingsProvidersScroll);
                break;
            }
            case SCR_SYSTEM_INFO:
                RadarDisplay::renderSystemInfo(
                    WifiManager::getState() == WIFI_STATE_CONNECTED ? WifiManager::getStaIp() : WifiManager::getApIp(),
                    s.staSsid.length() > 0 ? s.staSsid : "(setup mode)"
                );
                break;
            case SCR_WEATHER:
                RadarDisplay::renderWeatherScreen();
                break;
            case SCR_FACTORY_RESET_CONFIRM:
                RadarDisplay::renderFactoryResetConfirm();
                break;
            default: break;
        }
    }

    // Radar screen is canvas/SPI directly — LVGL only needs servicing on the
    // menu/detail/weather screens.
    if (currentScreen != SCR_RADAR) {
        lv_timer_handler();
    }
    delay(5);
}