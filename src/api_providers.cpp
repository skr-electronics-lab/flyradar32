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

static void copyTrimmed(char* dest, size_t destSize, const char* src) {
    if (!dest || destSize == 0) return;
    dest[0] = '\0';
    if (!src) return;
    while (*src == ' ' || *src == '\t') src++;
    size_t len = strlen(src);
    while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t' || src[len - 1] == '\r' || src[len - 1] == '\n')) {
        len--;
    }
    size_t toCopy = len < (destSize - 1) ? len : (destSize - 1);
    if (toCopy > 0) memcpy(dest, src, toCopy);
    dest[toCopy] = '\0';
}

// Zero-heap OpenSky state-vector parser — works directly on a raw char buffer
// so no String copy of the (potentially 50-150 KB) response is needed.
static bool parseOpenSkyFallback(const char* str, int len, AircraftPoint temp[MAX_PLANES], int& tempCount,
                                 double homeLat, double homeLon, float maxRangeKm) {
    // find "states":
    const char* statesPtr = strstr(str, "\"states\":");
    if (!statesPtr) return false;
    int statesIdx = (int)(statesPtr - str);
    int p = statesIdx;
    while (p < len && str[p] != '[') p++;
    if (p >= len) return false;
    p++; // skip outer '['

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
            while (p < len && (str[p] == ' ' || str[p] == '\t')) p++;
            if (p < len && str[p] == ',') p++;
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
                    pt.operatorName[0] = '\0';
                    // OpenSky field[2] is origin_country, not operator
                    if (fields[2][0] != '\0') {
                        strncpy(pt.desc, fields[2], 31); pt.desc[31] = '\0';
                    } else {
                        pt.desc[0] = '\0';
                    }

                    tempCount++;
                }
            }
        }
    }
    return true;
}

static String cachedOpenSkyToken = "";
static unsigned long openSkyTokenFetchedMs = 0;
static const unsigned long OPENSKY_TOKEN_LIFETIME_MS = 25 * 60 * 1000UL;

static String getOpenSkyToken(const String& clientId, const String& clientSecret) {
    if (clientId.isEmpty() || clientSecret.isEmpty()) return "";

    if (cachedOpenSkyToken.length() > 0 && (millis() - openSkyTokenFetchedMs < OPENSKY_TOKEN_LIFETIME_MS)) {
        return cachedOpenSkyToken;
    }

    if (ESP.getMaxAllocHeap() < 22000) {
        Serial.printf("[fetch] Skipping OpenSky token: maxAllocHeap too low (%u < 22000)\n", ESP.getMaxAllocHeap());
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
    if (ESP.getMaxAllocHeap() < 24000) {  // need ~16KB for stream buf + TLS overhead
        Serial.printf("[fetch] Skipping OpenSky: maxAllocHeap too low (%u < 24000)\n", ESP.getMaxAllocHeap());
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(10);
    HTTPClient http;
    http.setTimeout(8000);
    http.setConnectTimeout(6000);
    http.setUserAgent("Mozilla/5.0 (ESP32 ADS-B Ground Station)");

    // Heap-allocate the read buffer: only lives during this fetch, freed before return.
    // 16KB fits reliably within maxAllocHeap even after TLS buffers are allocated.
    const int BUF_SIZE = 16384; // 16 KB — enough for OpenSky state vectors
    bool ok = false;
    if (http.begin(client, url)) {
        if (token.length() > 0) {
            http.addHeader("Authorization", "Bearer " + token);
        }
        int code = http.GET();
        Serial.printf("[fetch] OpenSky url=%s code=%d\n", url.c_str(), code);
        if (code == HTTP_CODE_OK) {
            static char openSkyBuf[16384];
            const int bufSize = sizeof(openSkyBuf);
            // Stream into static buffer — avoids heap allocation and large String copy.
            int bytesRead = 0;
            WiFiClient* stream = http.getStreamPtr();
            unsigned long deadline = millis() + 8000;
            while (http.connected() && bytesRead < bufSize - 1 && millis() < deadline) {
                int avail = stream->available();
                if (avail > 0) {
                    int toRead = min(avail, bufSize - 1 - bytesRead);
                    bytesRead += stream->readBytes(openSkyBuf + bytesRead, toRead);
                } else {
                    delay(5);
                }
            }
            openSkyBuf[bytesRead] = '\0';
            http.end();
            client.stop();

            ok = parseOpenSkyFallback(openSkyBuf, bytesRead, temp, tempCount, homeLat, homeLon, maxRangeKm);
            Serial.printf("[fetch] OpenSky parsed %d planes (ok=%d, bytes=%d)\n", tempCount, ok, bytesRead);
        } else {
            if (code == 401) {
                cachedOpenSkyToken = "";
            } else if (code == 429) {
                Serial.println("[fetch] OpenSky rate-limited (429), backing off");
                skipCyclesRemaining[PROVIDER_OPENSKY] = 6;
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
                                    double homeLat, double homeLon, float maxRangeKm, bool useHttps) {
    tempCount = 0;
    int radiusNm = (int)(maxRangeKm / 1.852) + 1;
    String url = String(useHttps ? "https://" : "http://") + host + "/v2/point/" + String(homeLat, 4) + "/" +
                 String(homeLon, 4) + "/" + String(radiusNm);

    Serial.printf("[fetch] %s heap: free=%u maxAlloc=%u\n", host, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    if (useHttps && ESP.getMaxAllocHeap() < 22000) {
        Serial.printf("[fetch] Skipping %s: maxAllocHeap too low (%u < 22000)\n", host, ESP.getMaxAllocHeap());
        return false;
    }

    HTTPClient http;
    http.setTimeout(8000);
    http.setConnectTimeout(6000);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36");

    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    bool beginOk = false;
    if (useHttps) {
        secureClient.setInsecure();
        secureClient.setHandshakeTimeout(10);
        beginOk = http.begin(secureClient, url);
    } else {
        beginOk = http.begin(plainClient, url);
    }

    if (!beginOk) {
        if (useHttps) secureClient.stop();
        else plainClient.stop();
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

        String payload = http.getString();
        http.end();
        if (useHttps) secureClient.stop();
        else plainClient.stop();

        DynamicJsonDocument doc(16384);
        DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));

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
                p.altitudeFt = 0;
                p.onGround = false;
                if (ac["alt_baro"].is<float>()) {
                    p.altitudeFt = (int)ac["alt_baro"].as<float>();
                } else if (ac["alt_baro"].is<int>()) {
                    p.altitudeFt = ac["alt_baro"].as<int>();
                } else if (strncmp(ac["alt_baro"] | "", "ground", 6) == 0) {
                    p.altitudeFt = 0;
                    p.onGround = true;
                }

                p.speedKt    = ac["gs"].as<float>();
                p.trackDeg   = ac["track"].as<float>();
                if (p.trackDeg < 0.0f) p.trackDeg += 360.0f;
                else if (p.trackDeg >= 360.0f) p.trackDeg = fmod(p.trackDeg, 360.0f);
                p.distanceKm = d;
                p.bearingDeg = bearingDeg(homeLat, homeLon, lat, lon);

                copyTrimmed(p.flight, sizeof(p.flight), ac["flight"]);
                copyTrimmed(p.icaoHex, sizeof(p.icaoHex), ac["hex"]);

                const char* model = ac["t"] | "";
                if (!model[0]) {
                    const char* rawType = ac["type"] | "";
                    if (rawType && strcmp(rawType, "adsb_icao") != 0 && strcmp(rawType, "mlat") != 0 && strcmp(rawType, "tisb") != 0) {
                        model = rawType;
                    }
                }
                copyTrimmed(p.aircraftType, sizeof(p.aircraftType), model);
                copyTrimmed(p.desc, sizeof(p.desc), ac["desc"]);
                copyTrimmed(p.squawk, sizeof(p.squawk), ac["squawk"]);
                copyTrimmed(p.registration, sizeof(p.registration), ac["r"]);
                copyTrimmed(p.operatorName, sizeof(p.operatorName), ac["ownOp"]);

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
        if (useHttps) secureClient.stop();
        else plainClient.stop();
    }
    return ok;
}

static bool fetchAirplanesLive(AircraftPoint temp[MAX_PLANES], int& tempCount, double lat, double lon, float rangeKm) {
    return fetchAdsbSchemaProvider(PROVIDER_AIRPLANES_LIVE, "api.airplanes.live", temp, tempCount, lat, lon, rangeKm, true);
}

// adsb.lol speaks the same v2 schema and supports plain HTTP — fast and zero TLS overhead!
static bool fetchAdsbLol(AircraftPoint temp[MAX_PLANES], int& tempCount, double lat, double lon, float rangeKm) {
    return fetchAdsbSchemaProvider(PROVIDER_ADSB_LOL, "api.adsb.lol", temp, tempCount, lat, lon, rangeKm, false);
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

    float maxRangeKm = 320.0f; // Fetch full 300+ km coverage so web scope & regional traffic are fully visible

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
static bool fetchWeather() {
    Storage::lock();
    double lat = Storage::settings().lat;
    double lon = Storage::settings().lon;
    Storage::unlock();

    // No location configured yet — don't fetch placeholder Gulf of Guinea data.
    if (lat == 0.0 && lon == 0.0) {
        Serial.println("[weather] Skipping: no location set (lat=0, lon=0)");
        return false;
    }

    String url = String("http://api.open-meteo.com/v1/forecast?latitude=") +
                 String(lat, 4) + "&longitude=" + String(lon, 4) +
                 "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
                 "precipitation,weather_code,cloud_cover,pressure_msl,wind_speed_10m,"
                 "wind_direction_10m,wind_gusts_10m&wind_speed_unit=kn";

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(8000);
    http.setConnectTimeout(6000);
    if (!http.begin(client, url)) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); client.stop(); return false; }
    String payload = http.getString();
    http.end();
    client.stop();

    StaticJsonDocument<1536> doc;
    if (deserializeJson(doc, payload)) return false;
    JsonObject cur = doc["current"];
    if (cur.isNull()) return false;

    ApiProviders::Weather w = {};
    w.valid = true;
    w.fetchedAt = time(nullptr);
    w.tempC = cur["temperature_2m"].as<float>();
    w.feelsC = cur["apparent_temperature"].as<float>();
    w.windKt = cur["wind_speed_10m"].as<float>();
    w.gustKt = cur["wind_gusts_10m"].as<float>();
    w.windDirDeg = (int)(cur["wind_direction_10m"].as<float>() + 0.5f);
    w.humidityPct = (int)(cur["relative_humidity_2m"].as<float>() + 0.5f);
    float pMsl = cur["pressure_msl"].as<float>();
    if (pMsl > 500.0f) w.pressureHpa = (int)(pMsl + 0.5f);
    else w.pressureHpa = 1013;
    w.cloudPct = (int)(cur["cloud_cover"].as<float>() + 0.5f);
    w.precipMm = cur["precipitation"].as<float>();
    w.wmoCode = cur["weather_code"].as<int>();

    xSemaphoreTake(dataMutex, portMAX_DELAY);
    sharedWeather = w;
    xSemaphoreGive(dataMutex);
    Serial.printf("[weather] %.1fC %dkt %ddeg %dhPa code=%d\n", w.tempC, (int)w.windKt, w.windDirDeg, w.pressureHpa, w.wmoCode);
    return true;
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

        // Weather: retry every 30s until first success, then every 10 minutes
        static unsigned long lastWeatherMs = 0;
        if (WiFi.status() == WL_CONNECTED) {
            unsigned long interval = weatherDone ? (10UL * 60UL * 1000UL) : (30UL * 1000UL);
            if (!weatherDone || millis() - lastWeatherMs > interval) {
                lastWeatherMs = millis();
                if (fetchWeather()) {
                    weatherDone = true;
                }
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
    // Stack: 12KB on Core 1 (Application core) — optimal balance for MbedTLS TLS
    xTaskCreatePinnedToCore(apiTask, "apiTask", 12288, nullptr, 1, nullptr, 1);
}

void ApiProviders::requestRefresh() {
    if (!dataMutex) return;
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    refreshRequested = true;
    for (int i = 0; i < PROVIDER_COUNT; i++) {
        skipCyclesRemaining[i] = 0;
        failStreak[i] = 0;
    }
    xSemaphoreGive(dataMutex);
}

void ApiProviders::setPrimaryProvider(const char* name) {
    if (!dataMutex) return;
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    if (name && strlen(name) > 0) {
        status.lastProviderUsed = name;
    }
    refreshRequested = true;
    for (int i = 0; i < PROVIDER_COUNT; i++) {
        skipCyclesRemaining[i] = 0;
        failStreak[i] = 0;
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
