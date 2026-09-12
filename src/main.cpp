#include <Arduino.h>
#include "config.h"
#include "storage.h"
#include "buttons.h"
#include "radar_display.h"
#include "wifi_manager.h"
#include "api_providers.h"
#include "webui.h"
#include <lvgl.h>
#include <esp_task_wdt.h>

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
static int settingsBrightnessIndex = 0;
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
//
// Items 0,1,3,4,9,10 open a dedicated sub-screen (range/brightness/
// labels/icon/providers/aircraft list). Item 11 is a one-shot action
// (force a data refresh). Items 2,5,6,7,8 are single-press direct
// toggles/cycles handled right here in the main list, exactly like
// the original "Animation" entry â€” this keeps the menu tree flat
// and avoids adding a pile of near-identical new AppScreen states
// just to flip one boolean each. Items 12,13,14 are info/reset/back.
// ---------------------------------------------------------------
#define SETTINGS_MAIN_COUNT 15
static const char* settingsMainLabels[SETTINGS_MAIN_COUNT] = {
    "Radar Range",
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

#define SETTINGS_BRIGHTNESS_COUNT 5
static const char* settingsBrightnessLabels[SETTINGS_BRIGHTNESS_COUNT] = {
    "25%",
    "50%",
    "75%",
    "100%",
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

static void clampSelection() {
    if (planeCount == 0) { selectedPlaneIndex = -1; return; }
    
    float maxVisibleDist = ApiProviders::ZOOM_KM[Storage::settings().zoomLevel];

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

static void moveSelection(int delta) {
    if (planeCount == 0) { selectedPlaneIndex = -1; return; }
    
    float maxVisibleDist = ApiProviders::ZOOM_KM[Storage::settings().zoomLevel];
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
    Storage::saveDisplay(s.zoomLevel, s.labelsMode, s.aircraftIcon, s.showSweepAnim, s.brightness,
                          s.theme, s.showCompass, s.showRangeLabels, s.showTrail);
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
        }
    }
}

static void handleDetailButtons(ButtonEvent ev, ButtonId which) {
    if (planeCount == 0) { currentScreen = SCR_RADAR; return; }
    if (ev == BTN_EVENT_SHORT_PRESS || ev == BTN_EVENT_REPEAT) {
        if (which == BTN_ID_UP) {
            detailScrollY = max(0, detailScrollY - 12);
        } else if (which == BTN_ID_DOWN) {
            detailScrollY = min(90, detailScrollY + 12);
        }
    } else if (ev == BTN_EVENT_LONG_PRESS && which == BTN_ID_SELECT) {
        selectedPlaneIndex = detailPlaneIndex;
        currentScreen = SCR_RADAR;
    }
}

static void handleListButtons(ButtonEvent ev, ButtonId which) {
    if (ev == BTN_EVENT_SHORT_PRESS) {
        if (which == BTN_ID_UP) listSelectedIndex = max(0, listSelectedIndex - 1);
        else if (which == BTN_ID_DOWN) listSelectedIndex = min(planeCount - 1, listSelectedIndex + 1);
        else if (which == BTN_ID_SELECT && planeCount > 0) {
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
                    case 1:
                        if (s.brightness <= 64) settingsBrightnessIndex = 0;
                        else if (s.brightness <= 128) settingsBrightnessIndex = 1;
                        else if (s.brightness <= 192) settingsBrightnessIndex = 2;
                        else settingsBrightnessIndex = 3;
                        currentScreen = SCR_SETTINGS_BRIGHTNESS;
                        break;
                    case 2:
                        s.showSweepAnim = !s.showSweepAnim;
                        persistDisplay(s);
                        RadarDisplay::forceLVGLRefresh();
                        break;
                    case 3:
                        settingsLabelsIndex = s.labelsMode;
                        currentScreen = SCR_SETTINGS_LABELS;
                        break;
                    case 4:
                        settingsIconIndex = s.aircraftIcon;
                        currentScreen = SCR_SETTINGS_ICON;
                        break;
                    case 5: // Theme: cycle 0 -> 1 -> 2 -> 0
                        s.theme = (s.theme + 1) % THEME_COUNT;
                        persistDisplay(s);
                        RadarDisplay::forceLVGLRefresh();
                        break;
                    case 6: // Compass toggle
                        s.showCompass = !s.showCompass;
                        persistDisplay(s);
                        RadarDisplay::forceLVGLRefresh();
                        break;
                    case 7: // Range labels toggle
                        s.showRangeLabels = !s.showRangeLabels;
                        persistDisplay(s);
                        RadarDisplay::forceLVGLRefresh();
                        break;
                    case 8: // Trail toggle
                        s.showTrail = !s.showTrail;
                        persistDisplay(s);
                        RadarDisplay::forceLVGLRefresh();
                        break;
                    case 9: settingsProvidersIndex = 0; settingsProvidersScroll = 0; currentScreen = SCR_SETTINGS_PROVIDERS; break;
                    case 10: listSelectedIndex = 0; currentScreen = SCR_PLANE_LIST; break;
                    case 11: ApiProviders::requestRefresh(); currentScreen = SCR_RADAR; break;
                    case 12: currentScreen = SCR_SYSTEM_INFO; break;
                    case 13: currentScreen = SCR_FACTORY_RESET_CONFIRM; break;
                    case 14: currentScreen = SCR_RADAR; break;
                }
            }
        } else if (ev == BTN_EVENT_LONG_PRESS && which == BTN_ID_SELECT) {
            currentScreen = SCR_RADAR;
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

    case SCR_SETTINGS_BRIGHTNESS: {
        if (ev == BTN_EVENT_SHORT_PRESS || ev == BTN_EVENT_REPEAT) {
            if (which == BTN_ID_UP) settingsBrightnessIndex = (settingsBrightnessIndex - 1 + SETTINGS_BRIGHTNESS_COUNT) % SETTINGS_BRIGHTNESS_COUNT;
            else if (which == BTN_ID_DOWN) settingsBrightnessIndex = (settingsBrightnessIndex + 1) % SETTINGS_BRIGHTNESS_COUNT;
            else if (which == BTN_ID_SELECT && ev == BTN_EVENT_SHORT_PRESS) {
                if (settingsBrightnessIndex < 4) {
                    uint8_t vals[] = {64, 128, 192, 255};
                    s.brightness = vals[settingsBrightnessIndex];
                    persistDisplay(s);
                    RadarDisplay::applyBrightness();
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

    // Hardware watchdog: if loop() ever hangs (LVGL deadlock, stuck menu
    // handler), the ESP reboots instead of becoming a paperweight.
    // Verified: worst legit loop() pass (full redraw + snapshot copy) ~50 ms.
    esp_task_wdt_config_t wdtCfg = {};
    wdtCfg.timeout_ms = WDT_TIMEOUT_S * 1000;
    wdtCfg.idle_core_mask = 0;      // don't watch the idle tasks
    wdtCfg.trigger_panic = true;    // abort + reboot on expiry
    esp_task_wdt_init(&wdtCfg);
    esp_task_wdt_add(NULL);
    Serial.println("[wdt] watchdog armed");
}

void loop() {
    esp_task_wdt_reset();
    WifiManager::loop();

    if (currentScreen == SCR_WIFI_SETUP) {
        static bool setupScreenDrawn = false;
        static bool connectedScreenDrawn = false;
        
        WifiState ws = WifiManager::getState();
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
            case SCR_SETTINGS_MAIN:
            case SCR_SETTINGS_DISPLAY:
            case SCR_SETTINGS_BRIGHTNESS:
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
        RadarDisplay::sampleTrailHistory(planes, planeCount);
        clampSelection();
        if (listSelectedIndex >= planeCount) listSelectedIndex = planeCount > 0 ? planeCount - 1 : 0;

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
                RadarDisplay::renderRadar(planes, planeCount, sweepAngle, selectedPlaneIndex,
                                           ApiProviders::ZOOM_KM[s.zoomLevel], s.labelsMode, s.showSweepAnim,
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
                snprintf(labels[0], 24, "Range: %d km", (int)ApiProviders::ZOOM_KM[s.zoomLevel]);
                snprintf(labels[1], 24, "Brightness: %d%%", (int)((s.brightness * 100) / 255));
                snprintf(labels[2], 24, "Anim: %s", s.showSweepAnim ? "ON" : "OFF");
                const char* lm = s.labelsMode == 0 ? "OFF" : (s.labelsMode == 1 ? "SELECTED" : "ALL");
                snprintf(labels[3], 24, "Labels: %s", lm);
                const char* iconName = s.aircraftIcon == 0 ? "DOTS" : (s.aircraftIcon == 1 ? "ARROW" : "PLANE");
                snprintf(labels[4], 24, "Icon: %s", iconName);
                snprintf(labels[5], 24, "Theme: %s", THEME_NAMES[s.theme]);
                snprintf(labels[6], 24, "Compass: %s", s.showCompass ? "ON" : "OFF");
                snprintf(labels[7], 24, "Range Labels: %s", s.showRangeLabels ? "ON" : "OFF");
                snprintf(labels[8], 24, "Trail: %s", s.showTrail ? "ON" : "OFF");
                for (int i = 0; i <= 8; i++) ptrs[i] = labels[i];
                RadarDisplay::renderScrollMenu("SETTINGS", ptrs, SETTINGS_MAIN_COUNT, settingsMainIndex, settingsMainScroll);
                break;
            }
            case SCR_SETTINGS_DISPLAY:
                RadarDisplay::renderScrollMenu("RADAR RANGE", settingsDisplayLabels, SETTINGS_DISPLAY_COUNT, settingsDisplayIndex, 0);
                break;
            case SCR_SETTINGS_BRIGHTNESS:
                RadarDisplay::renderScrollMenu("BRIGHTNESS", settingsBrightnessLabels, SETTINGS_BRIGHTNESS_COUNT, settingsBrightnessIndex, 0);
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
            case SCR_FACTORY_RESET_CONFIRM:
                RadarDisplay::renderFactoryResetConfirm();
                break;
            default: break;
        }
    }

    if (currentScreen != SCR_RADAR) {
        lv_timer_handler();
    }
    delay(5);
}