#include "api_providers.h"
#include "storage.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

// ---------------------------------------------------------------
// Shared state, protected by dataMutex. NEVER touch these fields
// from outside this file without holding the mutex.
// ---------------------------------------------------------------
static SemaphoreHandle_t dataMutex = nullptr;
static AircraftPoint sharedPlanes[MAX_PLANES];
static int sharedCount = 0;
static ApiProviders::Weather sharedWeather = {false, 0};
static bool everSucceeded = false;

static bool refreshRequested = true;
static ApiProviders::Status status = {false, false, "", 0, 0};

// Per-provider consecutive-failure counters for simple backoff.
static int failStreak[PROVIDER_COUNT] = {0, 0, 0};
#define BACKOFF_SKIP_THRESHOLD 3   // after this many fails in a row, skip provider for a while
#define BACKOFF_SKIP_CYCLES    5

static int skipCyclesRemaining[PROVIDER_COUNT] = {0, 0, 0};

// ---------------------------------------------------------------
// Shared zoom table (km) — indexed by settings zoomLevel 0..2
// ---------------------------------------------------------------
const float ApiProviders::ZOOM_KM[3] = {50.0f, 100.0f, 150.0f};

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

static String cachedOpenSkyToken = "";
static unsigned long openSkyTokenFetchedMs = 0;
static const unsigned long OPENSKY_TOKEN_LIFETIME_MS = 25 * 60 * 1000UL;

static String getOpenSkyToken(const String& clientId, const String& clientSecret) {
    if (clientId.isEmpty() || clientSecret.isEmpty()) return "";

    if (cachedOpenSkyToken.length() > 0 && (millis() - openSkyTokenFetchedMs < OPENSKY_TOKEN_LIFETIME_MS)) {
        return cachedOpenSkyToken;
    }

    if (ESP.getMaxAllocHeap() < 35000) {
        Serial.printf("[fetch] Skipping OpenSky token: maxAllocHeap too low (%u < 35000)\n", ESP.getMaxAllocHeap());
        return "";
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(10);

    HTTPClient http;
    http.setTimeout(10000);
    http.setConnectTimeout(6000);

    String tokenUrl = "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token";
    if (http.begin(client, tokenUrl)) {
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        String body = "grant_type=client_credentials&client_id=" + clientId + "&client_secret=" + clientSecret;
        int code = http.POST(body);
        Serial.printf("[fetch] OpenSky OAuth token response code: %d\n", code);
        if (code == HTTP_CODE_OK) {
            String payload = http.getString();
            http.end();
            client.stop();

            DynamicJsonDocument doc(2048);
            DeserializationError err = deserializeJson(doc, payload);
            if (!err && doc.containsKey("access_token")) {
                cachedOpenSkyToken = doc["access_token"].as<String>();
                openSkyTokenFetchedMs = millis();
                Serial.printf("[fetch] OpenSky OAuth token obtained! (len=%d)\n", cachedOpenSkyToken.length());
                return cachedOpenSkyToken;
            } else {
                Serial.println("[fetch] Failed to parse OpenSky token JSON");
            }
        } else {
            http.end();
            client.stop();
        }
    } else {
        client.stop();
    }

    return "";
}

// ---------------------------------------------------------------
// Provider: OpenSky Network (https://opensky-network.org)
// High-reliability open REST API returning state vectors in bounding box
// ---------------------------------------------------------------
static bool fetchOpenSky(AircraftPoint temp[MAX_PLANES], int& tempCount,
                         double homeLat, double homeLon, float maxRangeKm) {
    tempCount = 0;

    Storage::lock();
    String clientId = Storage::settings().openSkyClientId;
    String clientSecret = Storage::settings().openSkyClientSecret;
    Storage::unlock();

    String token = getOpenSkyToken(clientId, clientSecret);

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
    if (ESP.getMaxAllocHeap() < 35000) {
        Serial.printf("[fetch] Skipping OpenSky: maxAllocHeap too low (%u < 35000)\n", ESP.getMaxAllocHeap());
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(10);
    HTTPClient http;
    http.setTimeout(8000);
    http.setConnectTimeout(6000);
    http.setUserAgent("Mozilla/5.0 (ESP32 ADS-B Ground Station)");

    bool ok = false;
    if (http.begin(client, url)) {
        if (token.length() > 0) {
            http.addHeader("Authorization", "Bearer " + token);
        }
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
            if (code == 401) {
                cachedOpenSkyToken = "";
            }
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
// providerIdx is needed so a 429 back-off hits the provider that was
// actually rate-limited, not a hardcoded one.
// ---------------------------------------------------------------
static bool fetchAdsbSchemaProvider(int providerIdx, const char* host, AircraftPoint temp[MAX_PLANES], int& tempCount,
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
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36");
    if (!http.begin(client, url)) {
        client.stop();
        return false;
    }

    int code = http.GET();
    Serial.printf("[fetch] %s url=%s code=%d\n", host, url.c_str(), code);
    bool ok = false;
    if (code == HTTP_CODE_OK) {
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

        DynamicJsonDocument doc(6144);
        DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        http.end();
        client.stop();

        Serial.printf("[fetch] %s parse err=%s docSize=%u\n", host, err.c_str(), (unsigned)doc.size());
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
        if (code == 429) {
            Serial.printf("[fetch] %s rate-limited (429), backing off\n", host);
            skipCyclesRemaining[providerIdx] = 6;
        }
        http.end();
        client.stop();
    }
    return ok;
}

static bool fetchAirplanesLive(AircraftPoint temp[MAX_PLANES], int& tempCount, double lat, double lon, float rangeKm) {
    return fetchAdsbSchemaProvider(PROVIDER_AIRPLANES_LIVE, "api.airplanes.live", temp, tempCount, lat, lon, rangeKm);
}

// adsb.lol speaks the same v2 schema and supports HTTPS â€” one shared
// implementation, ~120 duplicated lines deleted.
static bool fetchAdsbLol(AircraftPoint temp[MAX_PLANES], int& tempCount, double lat, double lon, float rangeKm) {
    return fetchAdsbSchemaProvider(PROVIDER_ADSB_LOL, "api.adsb.lol", temp, tempCount, lat, lon, rangeKm);
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
static unsigned long lastCycleDoneMs = 0;
static bool cycleStartedOnce = false; // false => first cycle after boot runs immediately

static void runFetchCycle() {
    if (cycleStartedOnce && millis() - lastCycleDoneMs < 5000) {
        xSemaphoreTake(dataMutex, portMAX_DELAY);
        status.fetchInProgress = false;
        xSemaphoreGive(dataMutex);
        return;
    }
    cycleStartedOnce = true;

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
            vTaskDelay(pdMS_TO_TICKS(1200));
        }
    }

    lastCycleDoneMs = millis();

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

// ---------------------------------------------------------------
// Weather (Open-Meteo, free, no API key). Runs in apiTask every 10 min
// and on first boot; keeps last good data on failure.
// ---------------------------------------------------------------
static void fetchWeather() {
    Storage::lock();
    double lat = Storage::settings().lat;
    double lon = Storage::settings().lon;
    Storage::unlock();

    String url = String("https://api.open-meteo.com/v1/forecast?latitude=") +
                 String(lat, 4) + "&longitude=" + String(lon, 4) +
                 "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
                 "precipitation,weather_code,cloud_cover,pressure_msl,wind_speed_10m,"
                 "wind_direction_10m,wind_gusts_10m&wind_speed_unit=kn";

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(10);
    HTTPClient http;
    http.setTimeout(8000);
    http.setConnectTimeout(6000);
    if (!http.begin(client, url)) return;

    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); client.stop(); return; }
    String payload = http.getString();
    http.end();
    client.stop();

    StaticJsonDocument<1536> doc;
    if (deserializeJson(doc, payload)) return;
    JsonObject cur = doc["current"];
    if (cur.isNull()) return;

    ApiProviders::Weather w = {};
    w.valid = true;
    w.fetchedAt = time(nullptr);
    w.tempC = cur["temperature_2m"] | 0.0f;
    w.feelsC = cur["apparent_temperature"] | 0.0f;
    w.windKt = cur["wind_speed_10m"] | 0.0f;
    w.gustKt = cur["wind_gusts_10m"] | 0.0f;
    w.windDirDeg = cur["wind_direction_10m"] | 0;
    w.humidityPct = cur["relative_humidity_2m"] | 0;
    w.pressureHpa = cur["pressure_msl"] | 0;
    w.cloudPct = cur["cloud_cover"] | 0;
    w.precipMm = cur["precipitation"] | 0.0f;
    w.wmoCode = cur["weather_code"] | 0;

    xSemaphoreTake(dataMutex, portMAX_DELAY);
    sharedWeather = w;
    xSemaphoreGive(dataMutex);
    Serial.printf("[weather] %.1fC %dkt %ddeg code=%d\n", w.tempC, (int)w.windKt, w.windDirDeg, w.wmoCode);
}

static void apiTask(void* param) {
    bool weatherDone = false;
    for (;;) {
        bool run = false;
        xSemaphoreTake(dataMutex, portMAX_DELAY);
        if (refreshRequested && !status.fetchInProgress) {
            refreshRequested = false;
            status.fetchInProgress = true;
            run = true;
        }
        xSemaphoreGive(dataMutex);

        if (run) runFetchCycle();

        // Weather: first time once Wi-Fi is up, then every 10 minutes
        static unsigned long lastWeatherMs = 0;
        if (WiFi.status() == WL_CONNECTED) {
            if (!weatherDone || millis() - lastWeatherMs > 10UL * 60UL * 1000UL) {
                fetchWeather();
                weatherDone = true;
                lastWeatherMs = millis();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------
void ApiProviders::begin() {
    dataMutex = xSemaphoreCreateMutex();
    sharedCount = 0;
    // Stack: 8KB on Core 1 (Application core)
    xTaskCreatePinnedToCore(apiTask, "apiTask", 8192, nullptr, 1, nullptr, 1);
}

void ApiProviders::requestRefresh() {
    if (!dataMutex) return;
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    if (!status.fetchInProgress) {
        refreshRequested = true;
    }
    xSemaphoreGive(dataMutex);
}

bool ApiProviders::getLatest(AircraftPoint out[MAX_PLANES], int& count) {
    if (!dataMutex) { count = 0; return false; }
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        count = 0;
        return false;
    }
    count = sharedCount;
    for (int i = 0; i < sharedCount; i++) out[i] = sharedPlanes[i];
    bool ret = everSucceeded;
    xSemaphoreGive(dataMutex);
    return ret;
}

ApiProviders::Status ApiProviders::getStatus() {
    if (!dataMutex) return status;
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return status;
    }
    Status copy = status;
    xSemaphoreGive(dataMutex);
    return copy;
}

ApiProviders::Weather ApiProviders::getWeather() {
    if (!dataMutex) return sharedWeather;
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) != pdTRUE) return sharedWeather;
    Weather copy = sharedWeather;
    xSemaphoreGive(dataMutex);
    return copy;
}
