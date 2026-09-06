#include "webui.h"
#include "config.h"
#include "storage.h"
#include "wifi_manager.h"
#include "api_providers.h"
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

static AsyncWebServer server(WEB_CONFIG_PORT);

static bool pinOk(AsyncWebServerRequest* request) {
    AppSettings& s = Storage::settings();
    if (s.configPin.isEmpty()) return true;
    if (request->hasHeader("X-Config-Pin") &&
        request->getHeader("X-Config-Pin")->value() == s.configPin) return true;
    request->send(401, "application/json", "{\"error\":\"pin required\"}");
    return false;
}

typedef std::function<void(AsyncWebServerRequest*, JsonDocument&)> JsonHandler;

static ArBodyHandlerFunction jsonBody() {
    return [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        if (index == 0) request->_tempObject = new String();
        String* body = (String*)request->_tempObject;
        body->concat((const char*)data, len);
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

        ApiProviders::Status pst = ApiProviders::getStatus();
        doc["fetchInProgress"] = pst.fetchInProgress;
        doc["lastFetchOk"] = pst.lastFetchOk;
        doc["lastProvider"] = pst.lastProviderUsed;
        doc["lastSuccessMs"] = pst.lastSuccessMs;

        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest* request) {
        ScannedNetwork nets[20];
        int n = WifiManager::scanNetworks(nets, 20);
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
}

static void registerWifiRoutes() {
    server.on("/api/wifi/disconnect", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!pinOk(request)) return;
        Storage::clearWifi();
        sendOk(request);
        delay(300);
        ESP.restart();
    });

    server.on("/api/wifi/connect", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleJsonRequest(request, [](AsyncWebServerRequest* request, JsonDocument& doc) {
            if (!pinOk(request)) return;
            String ssid = doc["ssid"] | "";
            String pass = doc["password"] | "";
            if (ssid.isEmpty()) {
                request->send(400, "application/json", "{\"error\":\"ssid required\"}");
                return;
            }
            Storage::saveWifi(ssid, pass);
            WifiManager::startConnect(ssid, pass);
            sendOk(request);
        });
    }, nullptr, jsonBody());
}

static void registerSettingsRoutes() {
    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!pinOk(request)) return;
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
        JsonArray en = doc.createNestedArray("providerEnabled");
        JsonArray pr = doc.createNestedArray("providerPriority");
        for (int i = 0; i < PROVIDER_COUNT; i++) { en.add(s.providerEnabled[i]); pr.add(s.providerPriority[i]); }
        doc["refreshInterval"] = s.refreshInterval;
        doc["pinSet"] = !s.configPin.isEmpty();
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.on("/api/settings/location", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleJsonRequest(request, [](AsyncWebServerRequest* request, JsonDocument& doc) {
            if (!pinOk(request)) return;
            double lat = doc["lat"] | DEFAULT_LAT;
            double lon = doc["lon"] | DEFAULT_LON;
            if (lat < -90 || lat > 90 || lon < -180 || lon > 180) {
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
            if (!pinOk(request)) return;
            // Only save the provider enable/priority arrays when they were
            // actually sent — otherwise a refresh-interval-only save would
            // fall back to the hardcoded defaults and silently reset them.
            if (doc.containsKey("enabled") || doc.containsKey("priority")) {
                bool en[PROVIDER_COUNT]; uint8_t pr[PROVIDER_COUNT];
                JsonArray enArr = doc["enabled"].as<JsonArray>();
                JsonArray prArr = doc["priority"].as<JsonArray>();
                for (int i = 0; i < PROVIDER_COUNT; i++) {
                    en[i] = (i < (int)enArr.size()) ? enArr[i].as<bool>() : true;
                    pr[i] = (i < (int)prArr.size()) ? prArr[i].as<uint8_t>() : i;
                }
                Storage::saveProviderConfig(en, pr);
            }
            if (doc.containsKey("refreshInterval")) {
                Storage::saveRefreshInterval(doc["refreshInterval"]);
            }
            sendOk(request);
        });
    }, nullptr, jsonBody());

    server.on("/api/settings/display", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleJsonRequest(request, [](AsyncWebServerRequest* request, JsonDocument& doc) {
            if (!pinOk(request)) return;
            AppSettings& s = Storage::settings();
            int zl = doc["zoomLevel"] | s.zoomLevel;
            uint8_t lm = doc["labelsMode"] | s.labelsMode;
            uint8_t ai = doc["aircraftIcon"] | s.aircraftIcon;
            bool sw = doc["showSweepAnim"] | s.showSweepAnim;
            uint8_t br = doc["brightness"] | s.brightness;
            uint8_t th = doc["theme"] | s.theme;
            bool cmp = doc["showCompass"] | s.showCompass;
            bool rlbl = doc["showRangeLabels"] | s.showRangeLabels;
            bool trl = doc["showTrail"] | s.showTrail;
            Storage::saveDisplay(zl, lm, ai, sw, br, th, cmp, rlbl, trl);
            sendOk(request);
        });
    }, nullptr, jsonBody());

    server.on("/api/settings/pin", HTTP_POST, [](AsyncWebServerRequest* request) {
        handleJsonRequest(request, [](AsyncWebServerRequest* request, JsonDocument& doc) {
            if (!pinOk(request)) return;
            String pin = doc["pin"] | "";
            Storage::saveConfigPin(pin);
            sendOk(request);
        });
    }, nullptr, jsonBody());

    server.on("/api/factory-reset", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!pinOk(request)) return;
        sendOk(request);
        delay(100);
        Storage::factoryReset();
        delay(100);
        ESP.restart();
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

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");

    server.begin();
}