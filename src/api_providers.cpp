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
static int failStreak[PROVIDER_COUNT] = {0, 0, 0};
#define BACKOFF_SKIP_THRESHOLD 3   // after this many fails in a row, skip provider for a while
#define BACKOFF_SKIP_CYCLES    5

static int skipCyclesRemaining[PROVIDER_COUNT] = {0, 0, 0};

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

// True zero-heap token parser for OpenSky state vectors
static bool parseOpenSkyFallback(const String& payload, AircraftPoint temp[MAX_PLANES], int& tempCount,
                                 double homeLat, double homeLon, float maxRangeKm) {
    int statesIdx = payload.indexOf("\"states\":");
    if (statesIdx < 0) return false;
    int p = payload.indexOf('[', statesIdx);
    if (p < 0) return false;
    p++; // skip outer '['

    const char* str = payload.c_str();
    int len = payload.length();

    while (p < len && tempCount < MAX_PLANES) {
        while (p < len && str[p] != '[' && str[p] != ']') p++;
        if (p >= len || str[p] == ']') break;
        p++; // skip '['

        char fields[17][36];
        for (int i = 0; i < 17; i++) fields[i][0] = '\0';
        int fCount = 0;

        while (p < len && str[p] != ']' && fCount < 17) {
            while (p < len && (str[p] == ' ' || str[p] == '\t' || str[p] == '\r' || str[p] == '\n')) p++;
            if (p >= len || str[p] == ']') break;

            int tLen = 0;
            if (str[p] == '"') {
                p++; // skip open quote
                while (p < len && str[p] != '"') {
                    if (str[p] == '\\' && p + 1 < len) p++;
                    if (tLen < 35) fields[fCount][tLen++] = str[p];
                    p++;
                }
                if (p < len && str[p] == '"') p++;
            } else {
                while (p < len && str[p] != ',' && str[p] != ']' && str[p] != ' ' && str[p] != '\r' && str[p] != '\n') {
                    if (tLen < 35) fields[fCount][tLen++] = str[p];
                    p++;
                }
            }
            fields[fCount][tLen] = '\0';
            fCount++;
            while (p < len && (str[p] == ' ' || str[p] == ',')) p++;
        }
        while (p < len && str[p] != ']') p++;
        if (p < len && str[p] == ']') p++;

        if (fCount >= 7 && fields[5][0] != '\0' && strcmp(fields[5], "null") != 0 &&
            fields[6][0] != '\0' && strcmp(fields[6], "null") != 0) {
            double lon = atof(fields[5]);
            double lat = atof(fields[6]);
            if (lat != 0.0 || lon != 0.0) {
                float d = distanceKm(homeLat, homeLon, lat, lon);
                if (d <= maxRangeKm) {
                    AircraftPoint& pt = temp[tempCount];
                    pt.valid = true;
                    pt.distanceKm = d;
                    pt.bearingDeg = bearingDeg(homeLat, homeLon, lat, lon);
                    strncpy(pt.icaoHex, fields[0], 7); pt.icaoHex[7] = '\0';

                    char callsign[10];
                    strncpy(callsign, fields[1], 9); callsign[9] = '\0';
                    int cLen = strlen(callsign);
                    while (cLen > 0 && callsign[cLen - 1] == ' ') callsign[--cLen] = '\0';
                    if (cLen == 0) strncpy(callsign, fields[0], 9);
                    strncpy(pt.flight, callsign, 9); pt.flight[9] = '\0';
                    strncpy(pt.desc, fields[2], 31); pt.desc[31] = '\0';

                    double altM = 0;
                    if (fields[7][0] != '\0' && strcmp(fields[7], "null") != 0) altM = atof(fields[7]);
                    else if (fCount > 13 && fields[13][0] != '\0' && strcmp(fields[13], "null") != 0) altM = atof(fields[13]);
                    pt.altitudeFt = (int)(altM * 3.28084);

                    pt.onGround = (fCount > 8 && strcmp(fields[8], "true") == 0);

                    float spdMps = (fCount > 9 && fields[9][0] != '\0' && strcmp(fields[9], "null") != 0) ? atof(fields[9]) : 0.0f;
                    pt.speedKt = spdMps * 1.94384f;

                    pt.trackDeg = (fCount > 10 && fields[10][0] != '\0' && strcmp(fields[10], "null") != 0) ? atof(fields[10]) : 0.0f;

                    const char* sq = (fCount > 14 && fields[14][0] != '\0' && strcmp(fields[14], "null") != 0) ? fields[14] : "";
                    strncpy(pt.squawk, sq, 4); pt.squawk[4] = '\0';

                    pt.aircraftType[0] = '\0';
                    pt.registration[0] = '\0';
                    strncpy(pt.operatorName, fields[2], 31); pt.operatorName[31] = '\0';

                    tempCount++;
                }
            }
        }
    }
    return (tempCount > 0);
}

// ---------------------------------------------------------------
// Provider: OpenSky Network (https://opensky-network.org)
// High-reliability open REST API returning state vectors in bounding box
// ---------------------------------------------------------------
static bool fetchOpenSky(AircraftPoint temp[MAX_PLANES], int& tempCount,
                         double homeLat, double homeLon, float maxRangeKm) {
    tempCount = 0;
    double dLat = (double)maxRangeKm / 111.0;
    double cosLat = cos(homeLat * DEG_TO_RAD);
    if (cosLat < 0.05) cosLat = 0.05;
    double dLon = (double)maxRangeKm / (111.0 * cosLat);

    double lamin = homeLat - dLat;
    double lamax = homeLat + dLat;
    double lomin = homeLon - dLon;
    double lomax = homeLon + dLon;

    String url = "https://opensky-network.org/api/states/all?lamin=" + String(lamin, 4) +
                 "&lomin=" + String(lomin, 4) +
                 "&lamax=" + String(lamax, 4) +
                 "&lomax=" + String(lomax, 4);

    Serial.printf("[fetch] OpenSky heap: free=%u maxAlloc=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(10);
    HTTPClient http;
    http.setTimeout(8000);
    http.setConnectTimeout(6000);
    http.setUserAgent("Mozilla/5.0 (ESP32 ADS-B Ground Station)");

    bool ok = false;
    if (http.begin(client, url)) {
        int code = http.GET();
        Serial.printf("[fetch] OpenSky url=%s code=%d\n", url.c_str(), code);
        if (code == HTTP_CODE_OK) {
            String payload = http.getString();
            // Immediately terminate connection & free TLS buffers before parsing
            http.end();
            client.stop();

            ok = parseOpenSkyFallback(payload, temp, tempCount, homeLat, homeLon, maxRangeKm);
            Serial.printf("[fetch] OpenSky parsed %d planes (ok=%d)\n", tempCount, ok);
        } else {
            http.end();
            client.stop();
        }
    } else {
        client.stop();
    }
    return ok;
}

// ---------------------------------------------------------------
// Provider: ADS-B Exchange v2-compatible schema (adsb.lol / airplanes.live)
// ---------------------------------------------------------------
static bool fetchAdsbSchemaProvider(const char* host, AircraftPoint temp[MAX_PLANES], int& tempCount,
                                     double homeLat, double homeLon, float maxRangeKm) {
    tempCount = 0;
    int radiusNm = (int)(maxRangeKm / 1.852) + 1;
    String url = String("https://") + host + "/v2/point/" + String(homeLat, 4) + "/" +
                 String(homeLon, 4) + "/" + String(radiusNm);

    Serial.printf("[fetch] %s heap: free=%u maxAlloc=%u\n", host, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(10);
    HTTPClient http;

    http.setTimeout(8000);
    http.setConnectTimeout(6000);
    http.setUserAgent("Mozilla/5.0 (ESP32 ADS-B Ground Station)");
    if (!http.begin(client, url)) {
        client.stop();
        return false;
    }

    int code = http.GET();
    Serial.printf("[fetch] %s url=%s code=%d\n", host, url.c_str(), code);
    bool ok = false;
    if (code == HTTP_CODE_OK) {
        String payload = http.getString();
        // Immediately terminate connection & free TLS buffers before JSON parsing
        http.end();
        client.stop();

        static StaticJsonDocument<384> filter;
        static bool filterInit = false;
        if (!filterInit) {
            filter["ac"][0]["lat"] = true;
            filter["ac"][0]["lon"] = true;
            filter["ac"][0]["flight"] = true;
            filter["ac"][0]["alt_baro"] = true;
            filter["ac"][0]["gs"] = true;
            filter["ac"][0]["track"] = true;
            filter["ac"][0]["hex"] = true;
            filter["ac"][0]["t"] = true;
            filter["ac"][0]["type"] = true;
            filter["ac"][0]["desc"] = true;
            filter["ac"][0]["squawk"] = true;
            filter["ac"][0]["r"] = true;
            filter["ac"][0]["ownOp"] = true;
            filterInit = true;
        }

        DynamicJsonDocument doc(4096);
        DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
        Serial.printf("[fetch] %s parse err=%s docSize=%u payloadLen=%u\n",
                      host, err.c_str(), (unsigned)doc.size(), (unsigned)payload.length());
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
                    p.altitudeFt = (int)ac["alt_baro"].as<float>();
                } else if (ac["alt_baro"].is<int>()) {
                    p.altitudeFt = ac["alt_baro"].as<int>();
                } else if (strncmp(ac["alt_baro"] | "", "ground", 6) == 0) {
                    p.altitudeFt = 0;
                    p.onGround = true;
                } else {
                    p.altitudeFt = 0;
                }

                String flightStr = ac["flight"] | "";
                flightStr.trim();
                strncpy(p.flight, flightStr.c_str(), 9); p.flight[9] = '\0';
                String hexStr = ac["hex"] | "";
                strncpy(p.icaoHex, hexStr.c_str(), 7); p.icaoHex[7] = '\0';

                // In adsb.lol, "t" is the aircraft model (e.g. A20N, B77L). "type" is the transponder mode ("adsb_icao")
                String modelStr = ac["t"] | "";
                if (modelStr.isEmpty()) {
                    String rawType = ac["type"] | "";
                    if (rawType != "adsb_icao" && rawType != "mlat" && rawType != "tisb") {
                        modelStr = rawType;
                    }
                }
                strncpy(p.aircraftType, modelStr.c_str(), 7); p.aircraftType[7] = '\0';
                
                String descStr = ac["desc"] | "";
                descStr.trim();
                strncpy(p.desc, descStr.c_str(), 31); p.desc[31] = '\0';

                const char* sq = ac["squawk"] | "";
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
    } else {
        http.end();
        client.stop();
    }
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
static ProviderFn providerFns[PROVIDER_COUNT] = {fetchOpenSky, fetchAdsbLol, fetchAirplanesLive};
static const char* providerNames[PROVIDER_COUNT] = {"OpenSky", "adsb.lol", "airplanes.live"};

// ---------------------------------------------------------------
// Background task
// ---------------------------------------------------------------
static void runFetchCycle() {
    Storage::lock();
    AppSettings& s = Storage::settings();

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
        Serial.println("[fetch] Wi-Fi down, skipping cycle");
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

    static AircraftPoint tempPlanes[MAX_PLANES];
    int tempCount = 0;
    bool success = false;
    const char* usedName = "";

    for (int oi = 0; oi < orderCount && !success; oi++) {
        int idx = order[oi];
        if (skipCyclesRemaining[idx] > 0) { skipCyclesRemaining[idx]--; continue; }

        for (int i = 0; i < MAX_PLANES; i++) tempPlanes[i].valid = false;
        bool ok = providerFns[idx](tempPlanes, tempCount, homeLat, homeLon, maxRangeKm);

        if (ok) {
            success = true;
            usedName = providerNames[idx];
            failStreak[idx] = 0;
            Serial.printf("[fetch] %s OK, %d aircraft\n", providerNames[idx], tempCount);
        } else {
            Serial.printf("[fetch] %s FAILED (streak %d, skip %d)\n", providerNames[idx], failStreak[idx] + 1, skipCyclesRemaining[idx]);
            failStreak[idx]++;
            if (failStreak[idx] >= BACKOFF_SKIP_THRESHOLD) {
                skipCyclesRemaining[idx] = BACKOFF_SKIP_CYCLES;
                failStreak[idx] = 0;
            }
            // Small pause between providers so MbedTLS memory is cleanly released
            vTaskDelay(pdMS_TO_TICKS(800));
        }
    }

    xSemaphoreTake(dataMutex, portMAX_DELAY);
    status.fetchInProgress = false;
    status.lastAttemptMs = millis();
    status.lastFetchOk = success;
    if (success) {
        sharedCount = tempCount;
        for (int i = 0; i < tempCount; i++) sharedPlanes[i] = tempPlanes[i];
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
    // Stack: 16KB on Core 1 (Application core) so Core 0 network IDLE task is never starved
    xTaskCreatePinnedToCore(apiTask, "apiTask", 16384, nullptr, 1, nullptr, 1);
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
