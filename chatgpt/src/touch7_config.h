#ifndef TOUCH7_CONFIG_H
#define TOUCH7_CONFIG_H

#include "feature_flags.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

#include <Arduino.h>
#include "sensors/sensor_config_base.h"
#include "touch7_display.h" // TOUCH7_PEERS_MAX, theme/clock ids

// ====================================================================================
// TouchMeteo-7 persistent config: /config/touch7.json (atomic write via base).
// Peers (2 default / 4 max), thresholds, theme, volume, clock style, PIR, OWM key.
// ====================================================================================

struct Touch7Peer {
    char name[32];
    char host[64]; // IP or hostname (no scheme); empty = slot unused
    char user[32];
    char pass[64];
    bool enabled;

    Touch7Peer() : enabled(false) {
        name[0]=0; host[0]=0; user[0]=0; pass[0]=0;
    }
};

struct Touch7Config {
    bool  sensor_enabled;    // gates peer polling (SensorConfigBase contract)
    int   read_interval_sec; // base peer poll cadence (auto-scaled by peer count)
    int   theme;             // 0 Claro / 1 Medio / 2 Escuro / 3 Sky / 4 Navy / 5 Bright
    int   clock_style;       // 0..4 (digital x3 + analog x2)
    int   volume;            // 0..10 speaker phrases
    float t_max;             // alarm thresholds (0 = disabled for max pair use)
    float t_min;
    float h_max;
    int   pir_dim_sec;       // dim after N s without presence (0 = off)
    int   screensaver_sec;   // giant clock after N s (0 = off)
    bool  backlight_auto;    // BH1750 auto-brightness
    int   backlight_pct;     // persisted manual brightness (5..100)
    char  owm_key[64];       // OpenWeather key (AQI/UV); empty = OpenMeteo only
    float wx_lat;            // weather position (Open-Meteo): default Lisboa
    float wx_lon;
    char  wx_place[64];      // display name ("Lisboa", "Arraiolos", ...)
    Touch7Peer peers[TOUCH7_PEERS_MAX];

    Touch7Config() :
        sensor_enabled(true),
        read_interval_sec(10),
        theme(1),
        clock_style(0),
        volume(6),
        t_max(27.5f),
        t_min(18.0f),
        h_max(70.0f),
        pir_dim_sec(300),
        screensaver_sec(600),
        backlight_auto(true),
        backlight_pct(85),
        wx_lat(38.72f),
        wx_lon(-9.14f)
    {
        owm_key[0] = 0;
        strncpy(wx_place, "Lisboa", sizeof(wx_place) - 1);
        wx_place[sizeof(wx_place) - 1] = 0;
    }
};

class Touch7ConfigManager : public SensorConfigBase<Touch7ConfigManager, Touch7Config> {
public:
    bool begin(const String& filePath = "/config/touch7.json");

    // Required by SensorConfigBase
    String toJson() const;
    bool   updateFromJson(const String& json);
    // UI events only mutate RAM; touch7SystemTick flushes later under
    // Touch7PanelHold when the screen is dim/saver (or after 60 seconds).
    bool updateFromJsonDeferred(const String& json);
    bool deferredSaveDue(bool displayHidden) const;
    bool flushDeferredSave();
    bool hasDeferredSave() const { return _deferredDirty; }

    int enabledPeerCount() const;

private:
    bool applyJson(const String& json);
    bool _deferredDirty = false;
    uint32_t _deferredSinceMs = 0;
    uint32_t _deferredLastAttemptMs = 0;
};

extern Touch7ConfigManager touch7ConfigManager;

#endif // SELECTED_PACK == PACK_S3_TOUCH7
#endif // TOUCH7_CONFIG_H
