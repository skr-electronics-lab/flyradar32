#include "api_providers.h"
#include "storage.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>

// ---------------------------------------------------------------
// Shared state, protected by dataMutex. NEVER touch these fields
// from outside this file without holding the mutex.
// ---------------------------------------------------------------
static SemaphoreHandle_t dataMutex = nullptr;
static AircraftPoint sharedPlanes[MAX_PLANES];
static int sharedCount = 0;
static bool everSucceeded = false;

static bool refreshRequested = true;
static ApiProviders::Status status = {false, false, "", 0, 0};

// Per-provider consecutive-failure counters for simple backoff.
static int failStreak[PROVIDER_COUNT] = {0, 0};
#define BACKOFF_SKIP_THRESHOLD 3   // after this many fails in a row, skip provider for a while
#define BACKOFF_SKIP_CYCLES    5

static int skipCyclesRemaining[PROVIDER_COUNT] = {0, 0};

// ---------------------------------------------------------------
// Geo helpers
// ---------------------------------------------------------------
static float distanceKm(double lat1, double lon1, double lat2, double lon2) {
    const double Rearth = 6371.0;
    double dLat = (lat2 - lat1) * PI / 180.0;
    double dLon = (lon2 - lon1) * PI / 180.0;
    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(lat1 * PI / 180.0) * cos(lat2 * PI / 180.0) * sin(dLon / 2) * sin(dLon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return Rearth * c;
}

static float bearingDeg(double lat1, double lon1, double lat2, double lon2) {
    double dLon = (lon2 - lon1) * PI / 180.0;
    double lat1Rad = lat1 * PI / 180.0;
    double lat2Rad = lat2 * PI / 180.0;
    double y = sin(dLon) * cos(lat2Rad);
    double x = cos(lat1Rad) * sin(lat2Rad) - sin(lat1Rad) * cos(lat2Rad) * cos(dLon);
    double brng = atan2(y, x) * 180.0 / PI;
    if (brng < 0) brng += 360.0;
    return brng;
}

// ---------------------------------------------------------------
// Provider: airplanes.live  (no auth, ADS-B Exchange v2-compatible schema)
// ---------------------------------------------------------------
static bool fetchAdsbSchemaProvider(const char* host, AircraftPoint temp[MAX_PLANES], int& tempCount,
                                     double homeLat, double homeLon, float maxRangeKm) {
    tempCount = 0;
    int radiusNm = (int)(maxRangeKm / 1.852) + 1;
    String url = String("https://") + host + "/v2/point/" + String(homeLat, 4) + "/" +
                 String(homeLon, 4) + "/" + String(radiusNm);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    http.setTimeout(10000);
    http.setConnectTimeout(8000);
    if (!http.begin(client, url)) return false;

    int code = http.GET();
    bool ok = false;
    if (code == HTTP_CODE_OK) {
        static StaticJsonDocument<256> filter;
        static bool filterInit = false;
        if (!filterInit) {
            filter["ac"][0]["lat"] = true;
            filter["ac"][0]["lon"] = true;
            filter["ac"][0]["flight"] = true;
            filter["ac"][0]["alt_baro"] = true;
            filter["ac"][0]["gs"] = true;
            filter["ac"][0]["track"] = true;
            filter["ac"][0]["hex"] = true;
            filter["ac"][0]["type"] = true;
            filter["ac"][0]["desc"] = true;
            filter["ac"][0]["squawk"] = true;
            filter["ac"][0]["r"] = true;
            filter["ac"][0]["ownOp"] = true;
            filterInit = true;
        }

        // 12KB comfortably holds 15 filtered aircraft records (which include
        // long strings like desc/ownOp); 6KB was silently truncating busy skies.
        static DynamicJsonDocument doc(12288);
        doc.clear();
        DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        if (!err) {
            JsonArray arr = doc["ac"].as<JsonArray>();
            for (JsonObject ac : arr) {
                if (tempCount >= MAX_PLANES) break;
                if (!ac["lat"].is<float>() || !ac["lon"].is<float>()) continue;

                double lat = ac["lat"];
                double lon = ac["lon"];
                float d = distanceKm(homeLat, homeLon, lat, lon);
                if (d > maxRangeKm) continue;

                AircraftPoint& p = temp[tempCount];
                p.valid = true;
                p.distanceKm = d;
                p.bearingDeg = bearingDeg(homeLat, homeLon, lat, lon);
                p.speedKt = ac["gs"] | 0.0f;
                p.trackDeg = ac["track"] | 0.0f;
                p.onGround = false;

                if (ac["alt_baro"].is<float>()) {
                    p.altitudeFt = (int)ac["alt_baro"].as<float>(); // can be fractional, e.g. 3475.25
                } else if (ac["alt_baro"].is<int>()) {
                    p.altitudeFt = ac["alt_baro"].as<int>();
                } else if (strncmp(ac["alt_baro"], "ground", 6) == 0) {
                    p.altitudeFt = 0;
                    p.onGround = true;
                } else {
                    p.altitudeFt = 0; // missing
                }

                String flightStr = ac["flight"] | "";
                flightStr.trim();
                strncpy(p.flight, flightStr.c_str(), 9); p.flight[9] = '\0';
                String hexStr = ac["hex"] | "";
                strncpy(p.icaoHex, hexStr.c_str(), 7); p.icaoHex[7] = '\0';
                String typeStr = ac["type"] | "";
                strncpy(p.aircraftType, typeStr.c_str(), 7); p.aircraftType[7] = '\0';
                
                String descStr = ac["desc"] | "";
                descStr.trim();
                strncpy(p.desc, descStr.c_str(), 31); p.desc[31] = '\0';

                const char* sq = ac["squawk"] | "";   // API sends a string like "7700"
                strncpy(p.squawk, sq, 4); p.squawk[4] = '\0';
                
                String regStr = ac["r"] | "";
                regStr.trim();
                strncpy(p.registration, regStr.c_str(), 11); p.registration[11] = '\0';
                String opStr = ac["ownOp"] | "";
                opStr.trim();
                strncpy(p.operatorName, opStr.c_str(), 31); p.operatorName[31] = '\0';

                tempCount++;
            }
            ok = true;
        }
    }
    http.end();
    return ok;
}

static bool fetchAirplanesLive(AircraftPoint temp[MAX_PLANES], int& tempCount, double lat, double lon, float rangeKm) {
    return fetchAdsbSchemaProvider("api.airplanes.live", temp, tempCount, lat, lon, rangeKm);
}

static bool fetchAdsbLol(AircraftPoint temp[MAX_PLANES], int& tempCount, double lat, double lon, float rangeKm) {
    return fetchAdsbSchemaProvider("api.adsb.lol", temp, tempCount, lat, lon, rangeKm);
}

// ---------------------------------------------------------------
// Provider dispatch table
// ---------------------------------------------------------------
typedef bool (*ProviderFn)(AircraftPoint[MAX_PLANES], int&, double, double, float);
static ProviderFn providerFns[PROVIDER_COUNT] = {fetchAirplanesLive, fetchAdsbLol};
static const char* providerNames[PROVIDER_COUNT] = {"airplanes.live", "adsb.lol"};

// ---------------------------------------------------------------
// Background task
// ---------------------------------------------------------------
static void runFetchCycle() {
    Storage::lock();
    AppSettings& s = Storage::settings();

    // Copy what we need, then release the settings lock BEFORE the slow
    // network I/O — previously the lock was held for the whole fetch
    // (up to ~18s with both providers), freezing every settings save.
    bool wifiUp = (WiFi.status() == WL_CONNECTED);
    double homeLat = s.lat;
    double homeLon = s.lon;
    bool enabled[PROVIDER_COUNT];
    uint8_t priority[PROVIDER_COUNT];
    for (int i = 0; i < PROVIDER_COUNT; i++) {
        enabled[i] = s.providerEnabled[i];
        priority[i] = s.providerPriority[i];
    }
    Storage::unlock();

    if (!wifiUp) {
        xSemaphoreTake(dataMutex, portMAX_DELAY);
        status.fetchInProgress = false;
        status.lastFetchOk = false;
        status.lastAttemptMs = millis();
        xSemaphoreGive(dataMutex);
        return;
    }

    float maxRangeKm = 150.0f; // always pull the widest zoom's worth; drawing handles the active zoom

    // Build try-order: providers sorted by priority[], filtered to enabled ones not in backoff.
    int order[PROVIDER_COUNT];
    int orderCount = 0;
    for (int rank = 0; rank < PROVIDER_COUNT; rank++) {
        for (int i = 0; i < PROVIDER_COUNT; i++) {
            if (priority[i] == rank && enabled[i]) {
                order[orderCount++] = i;
            }
        }
    }

    AircraftPoint temp[MAX_PLANES];
    int tempCount = 0;
    bool success = false;
    const char* usedName = "";

    for (int oi = 0; oi < orderCount && !success; oi++) {
        int idx = order[oi];
        if (skipCyclesRemaining[idx] > 0) { skipCyclesRemaining[idx]--; continue; }

        for (int i = 0; i < MAX_PLANES; i++) temp[i].valid = false;
        bool ok = providerFns[idx](temp, tempCount, homeLat, homeLon, maxRangeKm);

        if (ok) {
            success = true;
            usedName = providerNames[idx];
            failStreak[idx] = 0;
        } else {
            failStreak[idx]++;
            if (failStreak[idx] >= BACKOFF_SKIP_THRESHOLD) {
                skipCyclesRemaining[idx] = BACKOFF_SKIP_CYCLES;
                failStreak[idx] = 0;
            }
        }
    }

    xSemaphoreTake(dataMutex, portMAX_DELAY);
    status.fetchInProgress = false;
    status.lastAttemptMs = millis();
    status.lastFetchOk = success;
    if (success) {
        sharedCount = tempCount;
        for (int i = 0; i < tempCount; i++) sharedPlanes[i] = temp[i];
        status.lastProviderUsed = usedName;
        status.lastSuccessMs = millis();
        everSucceeded = true;
    }
    xSemaphoreGive(dataMutex);
}

static void apiTask(void* param) {
    for (;;) {
        if (refreshRequested) {
            refreshRequested = false;
            xSemaphoreTake(dataMutex, portMAX_DELAY);
            status.fetchInProgress = true;
            xSemaphoreGive(dataMutex);

            runFetchCycle();
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------
void ApiProviders::begin() {
    dataMutex = xSemaphoreCreateMutex();
    sharedCount = 0;
    xTaskCreatePinnedToCore(apiTask, "apiTask", 20480, nullptr, 1, nullptr, 0);
}

void ApiProviders::requestRefresh() {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    refreshRequested = true;
    xSemaphoreGive(dataMutex);
}

bool ApiProviders::getLatest(AircraftPoint out[MAX_PLANES], int& count) {
    if (!dataMutex) { count = 0; return false; }
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    count = sharedCount;
    for (int i = 0; i < sharedCount; i++) out[i] = sharedPlanes[i];
    bool ret = everSucceeded;
    xSemaphoreGive(dataMutex);
    return ret;
}

ApiProviders::Status ApiProviders::getStatus() {
    if (!dataMutex) return status;
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    Status copy = status;
    xSemaphoreGive(dataMutex);
    return copy;
}
