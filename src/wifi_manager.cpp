#include "wifi_manager.h"
#include "config.h"
#include "storage.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <esp_mac.h>

static DNSServer dnsServer;
static WifiState state = WIFI_STATE_AP_MODE;
static String apSsid;
static unsigned long connectStartedMs = 0;
static const unsigned long CONNECT_TIMEOUT_MS = 15000;
static String lastError = "";
static bool apActive = false;
static bool mdnsStarted = false;

static void startApMode() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
    apSsid = String(AP_SSID_PREFIX) + suffix;

    WiFi.softAP(apSsid.c_str(), AP_PASSWORD);
    dnsServer.start(53, "*", WiFi.softAPIP());
    apActive = true;
    state = WIFI_STATE_AP_MODE;
}

static void startMdns() {
    if (!mdnsStarted) {
        if (MDNS.begin(MDNS_NAME)) {
            mdnsStarted = true;
            MDNS.addService("http", "tcp", WEB_CONFIG_PORT);
        }
    }
}

static void stopApMode() {
    if (apActive) {
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
        apActive = false;
    }
}

void WifiManager::begin() {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true);

    AppSettings& s = Storage::settings();
    WiFi.setHostname(MDNS_NAME);

    if (s.staSsid.length() > 0) {
        WiFi.mode(WIFI_STA);
        WifiManager::startConnect(s.staSsid, s.staPassword);
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < CONNECT_TIMEOUT_MS) {
            delay(250);
        }
        if (WiFi.status() == WL_CONNECTED) {
            state = WIFI_STATE_CONNECTED;
            stopApMode();
            startMdns();
            return;
        }
        lastError = "Could not connect to stored network";
    }
    startApMode();
}

void WifiManager::startConnect(const String& ssid, const String& password) {
    if (apActive) {
        WiFi.mode(WIFI_AP_STA);
    } else {
        WiFi.mode(WIFI_STA);
    }
    WiFi.begin(ssid.c_str(), password.c_str());
    connectStartedMs = millis();
    state = WIFI_STATE_CONNECTING;
}

void WifiManager::loop() {
    if (apActive) dnsServer.processNextRequest();

    if (state == WIFI_STATE_CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            state = WIFI_STATE_CONNECTED;
            lastError = "";
            stopApMode();
            startMdns();
        } else if (millis() - connectStartedMs > CONNECT_TIMEOUT_MS) {
            state = WIFI_STATE_FAILED;
            lastError = "Connection failed or timed out";
            if (!apActive) startApMode();
        }
    } else if (state == WIFI_STATE_FAILED || state == WIFI_STATE_AP_MODE) {
        static unsigned long nextStaRetryMs = 0;
        if (nextStaRetryMs == 0) nextStaRetryMs = millis();
        if (millis() - nextStaRetryMs > 60000UL && WiFi.softAPgetStationNum() == 0) {
            AppSettings& s = Storage::settings();
            if (s.staSsid.length() > 0) {
                startConnect(s.staSsid, s.staPassword);
            }
            nextStaRetryMs = millis();
        }
    }
}

int WifiManager::scanNetworks(ScannedNetwork out[], int maxResults) {
    int n = WiFi.scanNetworks(false, false);
    int count = 0;
    for (int i = 0; i < n && count < maxResults; i++) {
        out[count].ssid = WiFi.SSID(i);
        out[count].rssi = WiFi.RSSI(i);
        out[count].secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        count++;
    }
    WiFi.scanDelete();
    return count;
}

WifiState WifiManager::getState() { return state; }
String WifiManager::getApSsid() { return apSsid; }
String WifiManager::getApIp() { return WiFi.softAPIP().toString(); }
String WifiManager::getStaIp() { return WiFi.localIP().toString(); }
String WifiManager::getLastError() { return lastError; }
