#include "webui.h"
#include "config.h"
#include "storage.h"
#include "wifi_manager.h"
#include "api_providers.h"
#include "radar_display.h"
#include "web_assets_gz.h"
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

static AsyncWebServer server(WEB_CONFIG_PORT);

#define JSON_BODY_MAX 4096   // cap request bodies; larger payloads are rejected

typedef std::function<void(AsyncWebServerRequest*, JsonDocument&)> JsonHandler;

static ArBodyHandlerFunction jsonBody() {
    return [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        if (total > JSON_BODY_MAX) {
            if (index == 0 && request->_tempObject) {
                delete (String*)request->_tempObject;
                request->_tempObject = nullptr;
            }
            return;
        }
        if (index == 0) {
            if (request->_tempObject) delete (String*)request->_tempObject;
            request->_tempObject = new String();
        }
        String* body = (String*)request->_tempObject;
        if (body && len > 0) {
            body->concat((const char*)data, len);
        }
    };
}

static void handleJsonRequest(AsyncWebServerRequest* request, JsonHandler handler) {
    if (!request->_tempObject) {
        request->send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }
    String* body = (String*)request->_tempObject;
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, *body);
    delete body;
    request->_tempObject = nullptr;
    if (err) {
        request->send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }
    handler(request, doc);
}

static void sendOk(AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"ok\":true}");
}

static void registerStatusRoutes() {
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        DynamicJsonDocument doc(512);
        WifiState ws = WifiManager::getState();
        const char* stateStr =
            ws == WIFI_STATE_AP_MODE ? "ap_mode" :
            ws == WIFI_STATE_CONNECTING ? "connecting" :
            ws == WIFI_STATE_CONNECTED ? "connected" : "failed";
        doc["wifiState"] = stateStr;
        doc["apSsid"] = WifiManager::getApSsid();
        doc["apIp"] = WifiManager::getApIp();
        doc["staIp"] = WifiManager::getStaIp();
        doc["lastError"] = WifiManager::getLastError();
        doc["mdns"] = String(MDNS_NAME) + ".local";
        doc["fwName"] = FW_NAME;
        doc["fwVersion"] = FW_VERSION;
        doc["time"] = WifiManager::getClockDateTime();

        ApiProviders::Weather wx = ApiProviders::getWeather();
        if (wx.valid) {
            doc["tempC"] = wx.tempC;
            doc["feelsC"] = wx.feelsC;
            doc["windKt"] = wx.windKt;
            doc["gustKt"] = wx.gustKt;
            doc["windDeg"] = wx.windDirDeg;
            doc["humidity"] = wx.humidityPct;
            doc["pressure"] = wx.pressureHpa;
            doc["cloud"] = wx.cloudPct;
            doc["precip"] = wx.precipMm;
            doc["wmo"] = wx.wmoCode;
        }

        ApiProviders::Status pst = ApiProviders::getStatus();
        doc["fetchInProgress"] = pst.fetchInProgress;
        doc["lastFetchOk"] = pst.lastFetchOk;

        Storage::lock();
        AppSettings& s = Storage::settings();
        int primIdx = 0;
        for (int i = 0; i < PROVIDER_COUNT; i++) {
            if (s.providerPriority[i] == 0) { primIdx = i; break; }
        }
        Storage::unlock();
        const char* pNames[PROVIDER_COUNT] = {"OpenSky", "adsb.lol", "airplanes.live"};
        doc["primaryProvider"] = pNames[primIdx];
        doc["lastProvider"] = pst.lastProviderUsed.isEmpty() ? pNames[primIdx] : pst.lastProviderUsed;
        doc["lastSuccessMs"] = pst.lastSuccessMs;

        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    // Async Wi-Fi scan: 1st request starts it (returns scanning:true),
    // subsequent requests poll until done. Never blocks this thread.
    // ponytail: scan state lives in WifiManager via WiFi.scanComplete();
    // no extra server-side session state.
    server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest* request) {
        ScannedNetwork nets[20];
        int n = WifiManager::pollScan(nets, 20);
        if (n == WifiManager::SCAN_RUNNING || n == WifiManager::SCAN_STARTED) {
            request->send(200, "application/json", "{\"scanning\":true}");
            return;
        }
        if (n == WifiManager::SCAN_FAILED) {
            // Start scan on failure / initial poll
            WifiManager::startScanAsync();
            request->send(200, "application/json", "{\"scanning\":true}");
            return;
        }
        DynamicJsonDocument doc(3072);
        JsonArray arr = doc.to<JsonArray>();
        for (int i = 0; i < n; i++) {
            JsonObject o = arr.createNestedObject();
            o["ssid"] = nets[i].ssid;
            o["rssi"] = nets[i].rssi;
            o["secure"] = nets[i].secure;
        }
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.on("/api/aircraft", HTTP_GET, [](AsyncWebServerRequest* request) {
        static AircraftPoint planesBuffer[MAX_PLANES];
        int count = 0;
        ApiProviders::getLatest(planesBuffer, count);
        ApiProviders::Status pst = ApiProviders::getStatus();

        AppSettings s = Storage::getSnapshot();
        int zoomLevel = s.zoomLevel;
        double lat = s.lat, lon = s.lon;

        float curRangeKm = ApiProviders::ZOOM_KM[zoomLevel >= 0 && zoomLevel < ApiProviders::ZOOM_LEVEL_COUNT ? zoomLevel : 1];

        String out;
        out.reserve(12288);
        out += "{\"count\":"; out += count;
        out += ",\"provider\":\""; out += (pst.lastProviderUsed.isEmpty() ? "" : pst.lastProviderUsed); out += "\"";
        out += ",\"lastFetchOk\":"; out += (pst.lastFetchOk ? "true" : "false");
        out += ",\"fetchInProgress\":"; out += (pst.fetchInProgress ? "true" : "false");
        out += ",\"lastSuccessMs\":"; out += (int)pst.lastSuccessMs;
        out += ",\"rangeKm\":"; out += (int)curRangeKm;
        out += ",\"lat\":"; out += String(lat, 6);
        out += ",\"lon\":"; out += String(lon, 6);
        out += ",\"aircraft\":[";

        bool first = true;
        for (int i = 0; i < count && i < MAX_PLANES; i++) {
            if (!planesBuffer[i].valid) continue;
            if (!first) out += ",";
            first = false;
            out += "{\"hex\":\""; out += planesBuffer[i].icaoHex; out += "\"";
            const char* fl = planesBuffer[i].flight[0] ? planesBuffer[i].flight : planesBuffer[i].icaoHex;
            out += ",\"flight\":\""; out += fl; out += "\"";
            out += ",\"type\":\""; out += planesBuffer[i].aircraftType; out += "\"";
            out += ",\"desc\":\""; out += planesBuffer[i].desc; out += "\"";
            out += ",\"reg\":\""; out += planesBuffer[i].registration; out += "\"";
            out += ",\"op\":\""; out += planesBuffer[i].operatorName; out += "\"";
            out += ",\"alt\":"; out += planesBuffer[i].altitudeFt;
            out += ",\"spd\":"; out += (int)planesBuffer[i].speedKt;
            out += ",\"track\":"; out += (int)planesBuffer[i].trackDeg;
            out += ",\"dst\":"; out += (int)planesBuffer[i].distanceKm;
            out += ",\"brg\":"; out += (int)planesBuffer[i].bearingDeg;
            out += ",\"sqk\":\""; out += planesBuffer[i].squawk; out += "\"";
            out += ",\"gnd\":"; out += (planesBuffer[i].onGround ? "true" : "false");
            out += "}";
        }
        out += "]}";

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", out);
        response->addHeader("Connection", "close");
        request->send(response);
    });

    server.on("/api/refresh", HTTP_POST, [](AsyncWebServerRequest* request) {
        ApiProviders::requestRefresh();
        sendOk(request);
    });
}

static void rebootDeferred(uint32_t delayMs = 400) {
    xTaskCreate([](void* p) {
        uint32_t ms = (uint32_t)(uintptr_t)p;
        vTaskDelay(pdMS_TO_TICKS(ms));
        ESP.restart();
        vTaskDelete(NULL);
    }, "rebootTask", 2048, (void*)(uintptr_t)delayMs, 1, NULL);
}

static void connectDeferred(const String& ssid, const String& pass, uint32_t delayMs = 500) {
    struct ConnectArgs {
        String ssid;
        String pass;
        uint32_t delayMs;
    };
    ConnectArgs* args = new ConnectArgs{ssid, pass, delayMs};
    xTaskCreate([](void* p) {
        ConnectArgs* a = (ConnectArgs*)p;
        vTaskDelay(pdMS_TO_TICKS(a->delayMs));
        WifiManager::startConnect(a->ssid, a->pass);
        delete a;
        vTaskDelete(NULL);
    }, "connTask", 3072, args, 1, NULL);
}

static void registerWifiRoutes() {
    auto handleWifiClear = [](AsyncWebServerRequest* request) {
        Storage::clearWifi();
        sendOk(request);
        rebootDeferred(400);
    };
    server.on("/api/wifi/disconnect", HTTP_POST, handleWifiClear);
    server.on("/api/wifi/clear", HTTP_POST, handleWifiClear);

    server.on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        DynamicJsonDocument doc(512);
        WifiState st = WifiManager::getState();
        String stateStr = "unknown";
        switch (st) {
            case WIFI_STATE_AP_MODE: stateStr = "ap_mode"; break;
            case WIFI_STATE_CONNECTING: stateStr = "connecting"; break;
            case WIFI_STATE_CONNECTED: stateStr = "connected"; break;
            case WIFI_STATE_FAILED: stateStr = "failed"; break;
        }
        doc["state"] = stateStr;
        doc["apActive"] = WifiManager::isApActive();
        doc["apRemainingSec"] = WifiManager::getApRemainingSec();
        doc["staIp"] = WifiManager::getStaIp();
        doc["apIp"] = WifiManager::getApIp();
        doc["ssid"] = WifiManager::getPendingSsid();
        doc["error"] = WifiManager::getLastError();
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.on("/api/wifi/connect", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleJsonRequest(request, [](AsyncWebServerRequest* request, JsonDocument& doc) {
            String ssid = doc["ssid"] | "";
            String pass = doc["password"] | "";
            if (ssid.isEmpty()) {
                request->send(400, "application/json", "{\"error\":\"ssid required\"}");
                return;
            }
            connectDeferred(ssid, pass, 100);
            request->send(200, "application/json", "{\"status\":\"connecting\"}");
        });
    }, nullptr, jsonBody());
}

static void registerSettingsRoutes() {
    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* request) {
        Storage::lock();
        AppSettings& s = Storage::settings();
        DynamicJsonDocument doc(1024);
        doc["staSsid"] = s.staSsid;
        doc["lat"] = s.lat;
        doc["lon"] = s.lon;
        doc["zoomLevel"] = s.zoomLevel;
        doc["labelsMode"] = s.labelsMode;
        doc["aircraftIcon"] = s.aircraftIcon;
        doc["showSweepAnim"] = s.showSweepAnim;
        doc["brightness"] = s.brightness;
        doc["theme"] = s.theme;
        doc["showCompass"] = s.showCompass;
        doc["showRangeLabels"] = s.showRangeLabels;
        doc["showTrail"] = s.showTrail;
        doc["autoRange"] = s.autoRange;
        JsonArray en = doc.createNestedArray("providerEnabled");
        JsonArray pr = doc.createNestedArray("providerPriority");
        int primIdx = 1;
        for (int i = 0; i < PROVIDER_COUNT; i++) {
            en.add(s.providerEnabled[i]);
            pr.add(s.providerPriority[i]);
            if (s.providerPriority[i] == 0) primIdx = i;
        }
        doc["primaryProvider"] = primIdx;
        doc["refreshInterval"] = s.refreshInterval;
        doc["openSkyClientId"] = s.openSkyClientId;
        doc["timezone"] = s.timezone.isEmpty() ? DEFAULT_TZ : s.timezone;
        Storage::unlock();

        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.on("/api/settings/timezone", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleJsonRequest(request, [](AsyncWebServerRequest* request, JsonDocument& doc) {
            String tz = doc["timezone"] | "";
            if (!tz.isEmpty()) {
                Storage::saveTimezone(tz);
                WifiManager::applyTimezone(tz);
            }
            sendOk(request);
        });
    }, nullptr, jsonBody());

    server.on("/api/settings/opensky", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleJsonRequest(request, [](AsyncWebServerRequest* request, JsonDocument& doc) {
            String clientId = doc["clientId"] | "";
            String clientSecret = doc["clientSecret"] | "";
            Storage::saveOpenSkyCredentials(clientId, clientSecret);
            ApiProviders::requestRefresh();
            sendOk(request);
        });
    }, nullptr, jsonBody());

    server.on("/api/settings/location", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleJsonRequest(request, [](AsyncWebServerRequest* request, JsonDocument& doc) {
            if (!doc.containsKey("lat") || !doc.containsKey("lon")) {
                request->send(400, "application/json", "{\"error\":\"lat and lon required\"}");
                return;
            }
            double lat = doc["lat"];
            double lon = doc["lon"];
            if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
                request->send(400, "application/json", "{\"error\":\"lat/lon out of range\"}");
                return;
            }
            Storage::saveLocation(lat, lon);
            ApiProviders::requestRefresh();
            sendOk(request);
        });
    }, nullptr, jsonBody());

    server.on("/api/settings/providers", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleJsonRequest(request, [](AsyncWebServerRequest* request, JsonDocument& doc) {
            if (doc.containsKey("clientId") && doc.containsKey("clientSecret")) {
                Storage::saveOpenSkyCredentials(doc["clientId"], doc["clientSecret"]);
            }
            if (doc.containsKey("primaryProvider")) {
                int p = doc["primaryProvider"].as<int>();
                if (p >= 0 && p < PROVIDER_COUNT) {
                    uint8_t pr[PROVIDER_COUNT];
                    bool en[PROVIDER_COUNT];
                    // Preserve the user's per-provider enable/disable choices —
                    // switching the primary must not silently re-enable everything.
                    Storage::lock();
                    AppSettings& s = Storage::settings();
                    for (int i = 0; i < PROVIDER_COUNT; i++) en[i] = s.providerEnabled[i];
                    Storage::unlock();
                    pr[p] = 0;
                    int nextRank = 1;
                    for (int i = 0; i < PROVIDER_COUNT; i++) {
                        if (i != p) pr[i] = nextRank++;
                    }
                    Storage::saveProviderConfig(en, pr);
                    const char* pNames[PROVIDER_COUNT] = {"OpenSky", "adsb.lol", "airplanes.live"};
                    ApiProviders::setPrimaryProvider(pNames[p]);
                }
            } else if (doc.containsKey("enabled") || doc.containsKey("priority")) {
                bool en[PROVIDER_COUNT]; uint8_t pr[PROVIDER_COUNT];
                JsonArray enArr = doc["enabled"].as<JsonArray>();
                JsonArray prArr = doc["priority"].as<JsonArray>();
                for (int i = 0; i < PROVIDER_COUNT; i++) {
                    en[i] = (i < (int)enArr.size()) ? enArr[i].as<bool>() : true;
                    pr[i] = (i < (int)prArr.size()) ? prArr[i].as<uint8_t>() : i;
                }
                Storage::saveProviderConfig(en, pr);
                ApiProviders::requestRefresh();
            }
            if (doc.containsKey("refreshInterval")) {
                Storage::saveRefreshInterval(doc["refreshInterval"]);
            }
            sendOk(request);
        });
    }, nullptr, jsonBody());

    server.on("/api/settings/display", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleJsonRequest(request, [](AsyncWebServerRequest* request, JsonDocument& doc) {
            // Snapshot current values under the lock, merge the provided keys,
            // then save — saveDisplay takes the mutex itself, so we must not
            // hold it here (non-recursive mutex would self-deadlock).
            Storage::lock();
            AppSettings& s = Storage::settings();
            int zl = doc.containsKey("zoomLevel") ? doc["zoomLevel"].as<int>() : s.zoomLevel;
            uint8_t lm = doc.containsKey("labelsMode") ? doc["labelsMode"].as<uint8_t>() : s.labelsMode;
            uint8_t ai = doc.containsKey("aircraftIcon") ? doc["aircraftIcon"].as<uint8_t>() : s.aircraftIcon;
            bool sw = doc.containsKey("showSweepAnim") ? doc["showSweepAnim"].as<bool>() : s.showSweepAnim;
            uint8_t th = doc.containsKey("theme") ? doc["theme"].as<uint8_t>() : s.theme;
            uint8_t br = doc.containsKey("brightness") ? doc["brightness"].as<uint8_t>() : s.brightness;
            bool cmp = doc.containsKey("showCompass") ? doc["showCompass"].as<bool>() : s.showCompass;
            bool rlbl = doc.containsKey("showRangeLabels") ? doc["showRangeLabels"].as<bool>() : s.showRangeLabels;
            bool trl = doc.containsKey("showTrail") ? doc["showTrail"].as<bool>() : s.showTrail;
            bool ar = doc.containsKey("autoRange") ? doc["autoRange"].as<bool>() : s.autoRange;
            // A manual zoom pick overrides auto-range
            if (doc.containsKey("zoomLevel") && ar) ar = false;
            Storage::unlock();
            Storage::saveDisplay(zl, lm, ai, sw, th, cmp, rlbl, trl, br);
            RadarDisplay::setBrightness(br);
            if (ar != s.autoRange) Storage::saveAutoRange(ar);
            sendOk(request);
        });
    }, nullptr, jsonBody());

    server.on("/api/factory-reset", HTTP_POST, [](AsyncWebServerRequest* request) {
        sendOk(request);
        Storage::factoryReset();
        rebootDeferred(400);
    });
}

static void registerCaptivePortalRoutes() {
    auto redirectToRoot = [](AsyncWebServerRequest* request) {
        request->redirect("/");
    };
    server.on("/generate_204", HTTP_GET, redirectToRoot);
    server.on("/gen_204", HTTP_GET, redirectToRoot);
    server.on("/hotspot-detect.html", HTTP_GET, redirectToRoot);
    server.on("/library/test/success.html", HTTP_GET, redirectToRoot);
    server.on("/connecttest.txt", HTTP_GET, redirectToRoot);
    server.on("/ncsi.txt", HTTP_GET, redirectToRoot);

    server.onNotFound([](AsyncWebServerRequest* request) {
        if (WifiManager::getState() == WIFI_STATE_AP_MODE) {
            if (request->url() == "/") {
                request->send(404, "text/plain", "Setup UI missing - please upload LittleFS data");
            } else {
                request->redirect("/");
            }
        } else {
            request->send(404, "text/plain", "Not found");
        }
    });
}

void WebUI::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
    }

    registerStatusRoutes();
    registerWifiRoutes();
    registerSettingsRoutes();
    registerCaptivePortalRoutes();

    // High-speed pre-compressed GZIP web cockpit served directly from Flash PROGMEM.
    // Reduces payload by >77% (109 KB -> 25 KB) with instant sub-50ms browser response.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        response->addHeader("Pragma", "no-cache");
        response->addHeader("Expires", "0");
        response->addHeader("Connection", "close");
        request->send(response);
    });

    server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        response->addHeader("Pragma", "no-cache");
        response->addHeader("Expires", "0");
        response->addHeader("Connection", "close");
        request->send(response);
    });

    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/css", STYLE_CSS_GZ, STYLE_CSS_GZ_LEN);
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        response->addHeader("Pragma", "no-cache");
        response->addHeader("Expires", "0");
        response->addHeader("Connection", "close");
        request->send(response);
    });

    server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "application/javascript", APP_JS_GZ, APP_JS_GZ_LEN);
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        response->addHeader("Pragma", "no-cache");
        response->addHeader("Expires", "0");
        response->addHeader("Connection", "close");
        request->send(response);
    });

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    server.begin();
}