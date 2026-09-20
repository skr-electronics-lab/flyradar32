#pragma once
#include <Arduino.h>
#include "config.h"

// Runs a background FreeRTOS task (pinned to core 1, the Arduino app
// core — keeps TLS off the async_tcp core 0) that periodically
// fetches nearby aircraft from whichever provider is configured, trying
// providers in priority order and falling back on failure. Results are
// copied out through a mutex - never touch the internal buffer directly.
namespace ApiProviders {
    void begin();

    static const int ZOOM_LEVEL_COUNT = 4;
    extern const float ZOOM_KM[ZOOM_LEVEL_COUNT];

    // Ask the background task to fetch as soon as possible (e.g. after a
    // long-press "refresh" or a location change). Non-blocking, safe to
    // call from the main core.
    void requestRefresh();
    void setPrimaryProvider(const char* name);

    // Thread-safe snapshot copy. Returns true if data was ever
    // successfully fetched at least once.
    bool getLatest(AircraftPoint out[MAX_PLANES], int& count);

    // For the status bar / settings screen.
    struct Status {
        bool     fetchInProgress;
        bool     lastFetchOk;
        String   lastProviderUsed;
        unsigned long lastSuccessMs;   // millis() timestamp, 0 = never
        unsigned long lastAttemptMs;
    };
    Status getStatus();

    // Station weather from Open-Meteo (cached ~10 min, no API key).
    struct Weather {
        bool     valid;        // false until first successful fetch
        time_t   fetchedAt;    // epoch, 0 = never
        float    tempC;
        float    feelsC;
        float    windKt;
        float    gustKt;
        int      windDirDeg;
        int      humidityPct;
        int      pressureHpa;
        int      cloudPct;
        float    precipMm;
        int      wmoCode;      // raw weather code (0=clear, 61=light rain...)
    };
    Weather getWeather();
}
