#include "wifi_manager.h"
#include "config.h"
#include "storage.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <esp_mac.h>
#include <time.h>

static SemaphoreHandle_t wifiMutex = nullptr;
static inline void lockWifi() { if (wifiMutex) xSemaphoreTake(wifiMutex, portMAX_DELAY); }
static inline void unlockWifi() { if (wifiMutex) xSemaphoreGive(wifiMutex); }

static DNSServer dnsServer;
static WifiState state = WIFI_STATE_AP_MODE;
static String apSsid;
static unsigned long connectStartedMs = 0;
static const unsigned long CONNECT_TIMEOUT_MS = 15000;
static String lastError = "";
static bool apActive = false;
static bool mdnsStarted = false;
static String pendingSsid = "";
static String pendingPass = "";
static unsigned long stopApTimerMs = 0;

static void startApMode() {
    lockWifi();
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
    unlockWifi();
}

static void startMdns() {
    if (!mdnsStarted) {
        if (MDNS.begin(MDNS_NAME)) {
            mdnsStarted = true;
            MDNS.addService("http", "tcp", WEB_CONFIG_PORT);
        }
    }
}

// ---- NTP ----
static bool ntpStarted = false;
static void startNtp() {
    if (ntpStarted) return;
    String tz = Storage::getSnapshot().timezone;
    if (tz.isEmpty()) tz = DEFAULT_TZ;
    configTzTime(tz.c_str(), "pool.ntp.org", "time.nist.gov");
    ntpStarted = true;
}

void WifiManager::applyTimezone(const String& tz) {
    String useTz = tz.isEmpty() ? DEFAULT_TZ : tz;
    configTzTime(useTz.c_str(), "pool.ntp.org", "time.nist.gov");
    ntpStarted = true;
}

static void stopApMode() {
    lockWifi();
    if (apActive) {
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
        apActive = false;
    }
    unlockWifi();
}

void WifiManager::begin() {
    if (!wifiMutex) wifiMutex = xSemaphoreCreateMutex();
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.disconnect(true);

    AppSettings s = Storage::getSnapshot();
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
            startNtp();
            return;
        }
        lastError = "Could not connect to stored network";
    }
    startApMode();
}

void WifiManager::startConnect(const String& ssid, const String& password) {
    lockWifi();
    pendingSsid = ssid;
    pendingPass = password;
    lastError = "";
    stopApTimerMs = 0;
    if (apActive) {
        WiFi.mode(WIFI_AP_STA);
    } else {
        WiFi.mode(WIFI_STA);
    }
    WiFi.begin(ssid.c_str(), password.c_str());
    connectStartedMs = millis();
    state = WIFI_STATE_CONNECTING;
    unlockWifi();
}

void WifiManager::loop() {
    if (apActive) dnsServer.processNextRequest();

    lockWifi();
    WifiState curState = state;
    unsigned long curConnectStart = connectStartedMs;
    unlockWifi();

    if (curState == WIFI_STATE_CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            lockWifi();
            state = WIFI_STATE_CONNECTED;
            lastError = "";
            if (pendingSsid.length() > 0) {
                Storage::saveWifi(pendingSsid, pendingPass);
            }
            if (apActive) {
                stopApTimerMs = millis() + 4500;
            }
            unlockWifi();
            startMdns();
            startNtp();
        } else if (millis() - curConnectStart > CONNECT_TIMEOUT_MS) {
            lockWifi();
            state = WIFI_STATE_FAILED;
            lastError = "Incorrect password or network unreachable.";
            stopApTimerMs = 0;
            bool needAp = !apActive;
            unlockWifi();
            WiFi.disconnect(false);
            if (needAp) startApMode();
        }
    } else if (curState == WIFI_STATE_CONNECTED) {
        bool doStopAp = false;
        lockWifi();
        if (apActive && stopApTimerMs > 0 && millis() >= stopApTimerMs) {
            doStopAp = true;
            stopApTimerMs = 0;
        }
        unlockWifi();
        if (doStopAp) {
            Serial.println("[WiFi] Network confirmed, stopping setup AP now.");
            stopApMode();
            WiFi.mode(WIFI_STA);
        }
        static unsigned long downSinceMs = 0;
        if (WiFi.status() != WL_CONNECTED) {
            if (downSinceMs == 0) downSinceMs = millis();
            else if (millis() - downSinceMs > 5000UL) {
                downSinceMs = 0;
                Serial.println("[WiFi] Lost connection, attempting reconnect...");
                lockWifi();
                state = WIFI_STATE_CONNECTING;
                connectStartedMs = millis();
                unlockWifi();
                WiFi.reconnect();
            }
        } else {
            downSinceMs = 0;
        }
    } else if (curState == WIFI_STATE_FAILED || curState == WIFI_STATE_AP_MODE) {
        static unsigned long nextStaRetryMs = 0;
        AppSettings s = Storage::getSnapshot();
        if (s.staSsid.length() > 0) {
            // Only initialise the timer once; don't reset it on every loop call.
            if (nextStaRetryMs == 0) nextStaRetryMs = millis();
            if (millis() - nextStaRetryMs > 60000UL && WiFi.softAPgetStationNum() == 0) {
                startConnect(s.staSsid, s.staPassword);
                nextStaRetryMs = millis(); // reset only after an actual attempt
            }
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

// ---- Async scan (never blocks the async_tcp request thread) ----
int WifiManager::startScanAsync() {
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return SCAN_RUNNING;
    WiFi.scanNetworks(true, false);   // async = start in background
    return SCAN_STARTED;
}

int WifiManager::pollScan(ScannedNetwork out[], int maxResults) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return SCAN_RUNNING;
    if (n < 0) return SCAN_FAILED;   // -1 running handled above, other <0 = failed
    // n >= 0: scan finished, harvest results and free
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

WifiState WifiManager::getState() {
    lockWifi();
    WifiState s = state;
    unlockWifi();
    return s;
}

String WifiManager::getApSsid() {
    lockWifi();
    String s = apSsid;
    unlockWifi();
    return s;
}

String WifiManager::getApIp() { return WiFi.softAPIP().toString(); }
String WifiManager::getStaIp() { return WiFi.localIP().toString(); }

String WifiManager::getLastError() {
    lockWifi();
    String s = lastError;
    unlockWifi();
    return s;
}

bool WifiManager::timeSynced() {
    return ntpStarted && time(nullptr) > 1700000000; // post-2023 epoch = real time
}

String WifiManager::getClockTime() {
    if (!timeSynced()) return "";
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[8];
    strftime(buf, sizeof(buf), "%H:%M", &t);
    return String(buf);
}

String WifiManager::getClockDateTime() {
    if (!timeSynced()) return "";
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &t);
    return String(buf);
}

bool WifiManager::isApActive() {
    lockWifi();
    bool b = apActive;
    unlockWifi();
    return b;
}

int WifiManager::getApRemainingSec() {
    lockWifi();
    int sec = 0;
    if (apActive && stopApTimerMs > millis()) {
        sec = (int)((stopApTimerMs - millis() + 999) / 1000);
    }
    unlockWifi();
    return sec;
}

String WifiManager::getPendingSsid() {
    lockWifi();
    String s = pendingSsid;
    unlockWifi();
    return s;
}
