#include "touch7_alarms.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

#include <LittleFS.h>
#include "touch7_config.h"
#include "touch7_display.h"
#include "touch7_audio.h"
#include "touch7_peers_logic.h"
#include "sensors/sensor_config.h"

// One phrase per alarm bit (upload via /files if missing).
static const char* TOUCH7_ALARM_WAV[3] = {
    "/falas/quente.wav",   // THOT
    "/falas/frio.wav",     // TCOLD
    "/falas/humidade.wav", // THUM
};
static const char* TOUCH7_ALARM_PT[3] = {
    "Temperatura alta", "Temperatura baixa", "Humidade alta",
};

#define TOUCH7_ALARM_COOLDOWN_MS (15UL * 60UL * 1000UL)
#define TOUCH7_ALARM_TICK_MS 30000UL

static unsigned long s_lastTick = 0;
static unsigned long s_lastFire[3] = {0, 0, 0};
static uint8_t s_prevMask = 0;

void touch7AlarmsBegin() {
    s_lastTick = 0;
    s_prevMask = 0;
    s_lastFire[0] = s_lastFire[1] = s_lastFire[2] = 0;
}

void touch7AlarmsTick() {
    if (!touch7ConfigManager.isInitialized()) return;
    unsigned long now = millis();
    if (now - s_lastTick < TOUCH7_ALARM_TICK_MS) return;
    s_lastTick = now;
#if ENABLE_AHT20_BMP280==1
    const SensorConfig& scfg = sensorConfigManager.getConfig();
    if (scfg.last_reading_time == 0) return;
    const Touch7Config& cfg = touch7ConfigManager.getConfig();
    uint8_t mask = touch7AlarmCheck(scfg.last_temp_avg, scfg.last_humidity,
                                    cfg.t_max, cfg.t_min, cfg.h_max);
    for (int b = 0; b < 3; b++) {
        uint8_t bit = (uint8_t)(1u << (unsigned)b);
        bool rising = (mask & bit) && !(s_prevMask & bit);
        if (rising && now - s_lastFire[b] >= TOUCH7_ALARM_COOLDOWN_MS) {
            s_lastFire[b] = now;
            touch7Display.showToast(TOUCH7_ALARM_PT[b], "ver meteorologia", "");
            if (!touch7AudioIsPlaying() && LittleFS.exists(TOUCH7_ALARM_WAV[b])) {
                touch7AudioPlay(TOUCH7_ALARM_WAV[b]);
            }
        }
    }
    s_prevMask = mask;
#endif
}

#endif // SELECTED_PACK == PACK_S3_TOUCH7
