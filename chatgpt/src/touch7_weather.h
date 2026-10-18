#pragma once

// touch7_weather.h — Open-Meteo forecast for the Previsão tab (+ optional
// OpenWeather AQI/UV when a key is configured). Cached + stale-flagged so the
// LCD never blocks on the network. JSON docs in PSRAM.

#include "feature_flags.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

#include <Arduino.h>

struct Touch7WeatherStatus {
    bool hasData;
    bool refreshing;
    bool stale;
    uint32_t ageSeconds;
    char lastError[64];
};

void touch7WeatherBegin();
void touch7WeatherTick();
void touch7WeatherInvalidate(); // location changed; fetch on the next loop tick
void touch7WeatherGetStatus(Touch7WeatherStatus& out);

// Last fetched values (for the Agora/Previsão tabs).
bool touch7WeatherHasData();
int touch7WeatherCode();      // Open-Meteo weathercode
bool touch7WeatherIsDay();
String touch7WeatherToday();  // pre-formatted line
void touch7WeatherDays(String out[5]);

// Geo: place search (Open-Meteo geocoding, keyless) + IP auto-locate
// (ip-api.com, keyless). Fetch raw payloads; parsing via touch7_geo.h
// (pure, unit-tested). False when offline or on HTTP error.
bool touch7GeoSearchFetch(const String& name, String& payload);
bool touch7GeoAutoFetch(String& payload);

#endif // SELECTED_PACK == PACK_S3_TOUCH7
