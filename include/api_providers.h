#pragma once
#include <Arduino.h>
#include "config.h"

// Runs a background FreeRTOS task (pinned to core 0) that periodically
// fetches nearby aircraft from whichever provider is configured, trying
// providers in priority order and falling back on failure. Results are
// copied out through a mutex - never touch the internal buffer directly.
namespace ApiProviders {
    void begin();

    // Ask the background task to fetch as soon as possible (e.g. after a
    // long-press "refresh" or a location change). Non-blocking, safe to
    // call from the main core.
    void requestRefresh();

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
}
