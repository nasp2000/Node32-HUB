#pragma once

// touch7_peers_logic.h — pure helpers for TouchMeteo-7 UI/config scheduling.
// No Arduino dependencies (stdint only) so native doctests compile it on host.
// Rule nº1: callers keep all buffers in PSRAM; these functions allocate nothing.

#include <stdint.h>

// Clamp helpers ---------------------------------------------------------------
static inline int touch7ClampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Peer polling ----------------------------------------------------------------
// 2 peers (default): 10 s cadence. 3-4 peers: round-robin 15-20 s so one slow
// node never stalls the loop and the CAM 2-client limit is respected.
static inline int touch7PeerPollIntervalSec(int enabledPeers) {
    if (enabledPeers <= 2) return 10;
    if (enabledPeers == 3) return 15;
    return 20;
}

// Backoff after consecutive failures (10s,20s,40s,80s, then cap 120s) + slot jitter.
static inline int touch7PeerBackoffSec(int consecFails, int slotIdx) {
    int b;
    if (consecFails >= 5) b = 120;
    else b = 10 << touch7ClampInt(consecFails - 1, 0, 3); // 10..80
    return b + (slotIdx & 1 ? 3 : 0);
}

// Condition groups (Open-Meteo weathercode -> animated icon set) --------------
enum Touch7CondGroup : uint8_t {
    TOUCH7_COND_CLEAR = 0,
    TOUCH7_COND_PARTLY = 1,
    TOUCH7_COND_CLOUDY = 2,
    TOUCH7_COND_FOG = 3,
    TOUCH7_COND_RAIN = 4,
    TOUCH7_COND_SNOW = 5,
    TOUCH7_COND_STORM = 6
};

static inline Touch7CondGroup touch7CondGroup(int code) {
    if (code == 0) return TOUCH7_COND_CLEAR;
    if (code == 1 || code == 2) return TOUCH7_COND_PARTLY;
    if (code == 3) return TOUCH7_COND_CLOUDY;
    if (code == 45 || code == 48) return TOUCH7_COND_FOG;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return TOUCH7_COND_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return TOUCH7_COND_SNOW;
    if (code >= 95 && code <= 99) return TOUCH7_COND_STORM;
    return TOUCH7_COND_CLOUDY;
}

// Theme / clock style ids ------------------------------------------------------
static inline bool touch7ThemeValid(int t) { return t >= 0 && t <= 5; } // 0..5 (see TOUCH7_THEMES)
static inline bool touch7ClockValid(int s) { return s >= 0 && s <= 4; }

// Defer settings writes until the display is dim/saver, with a bounded
// foreground fallback and retry interval. Unsigned subtraction is wrap-safe.
static inline bool touch7DeferredSaveDue(uint32_t nowMs, uint32_t dirtySinceMs,
                                         uint32_t lastAttemptMs, bool displayHidden) {
    if (lastAttemptMs != 0 && (uint32_t)(nowMs - lastAttemptMs) < 30000UL) return false;
    uint32_t age = (uint32_t)(nowMs - dirtySinceMs);
    return age >= 60000UL || (displayHidden && age >= 3000UL);
}

static inline bool touch7RemotePointerExpired(uint32_t nowMs, uint32_t updatedMs) {
    return (uint32_t)(nowMs - updatedMs) > 500UL;
}

// Lux -> backlight % (BH1750 auto-brightness curve) -----------------------------
static inline int touch7LuxToBacklight(float lux) {
    if (lux < 0) lux = 0;
    if (lux < 5) return 20;      // night
    if (lux < 50) return 35;
    if (lux < 200) return 55;
    if (lux < 600) return 75;
    return 100;                  // daylight
}

// I2C address guard: Touch-LCD-7 shared bus map --------------------------------
// 0x38 AHT20, 0x76/0x77 BMP280, 0x23 BH1750, 0x68 DS3231, 0x40 INA226,
// 0x5D touch GT911, 0x20 CH422G. Anything else on 8/9 is unexpected.
static inline bool touch7I2CAddrExpected(uint8_t addr) {
    return addr == 0x38 || addr == 0x76 || addr == 0x77 || addr == 0x23 ||
           addr == 0x68 || addr == 0x40 || addr == 0x5D || addr == 0x20;
}

// Threshold alarms (T/H phrases) --------------------------------------------------
// Bitmask out. High-temp enabled if tMax>0; low-temp if tMin<tMax; humidity if
// hMax>0. Degenerate config (tMax<=tMin) disables the low-temp alarm.
enum Touch7Alarm : uint8_t {
    TOUCH7_ALARM_NONE  = 0,
    TOUCH7_ALARM_THOT  = 1,
    TOUCH7_ALARM_TCOLD = 2,
    TOUCH7_ALARM_THUM  = 4
};

static inline uint8_t touch7AlarmCheck(float tempC, float humPct,
                                       float tMax, float tMin, float hMax) {
    uint8_t m = TOUCH7_ALARM_NONE;
    if (tMax > 0 && tempC > tMax) m |= TOUCH7_ALARM_THOT;
    if (tMax > tMin && tempC < tMin) m |= TOUCH7_ALARM_TCOLD;
    if (hMax > 0 && humPct > hMax) m |= TOUCH7_ALARM_THUM;
    return m;
}
struct Touch7WavInfo {
    bool ok;
    uint16_t channels;
    uint32_t sampleRate;
    uint16_t bits;
    uint32_t dataBytes;
};

static inline Touch7WavInfo touch7ParseWavHeader(const uint8_t* h, uint32_t len) {
    Touch7WavInfo r;
    r.ok = false; r.channels = 0; r.sampleRate = 0; r.bits = 0; r.dataBytes = 0;
    if (h == 0 || len < 44) return r;
    if (!(h[0]=='R'&&h[1]=='I'&&h[2]=='F'&&h[3]=='F')) return r;
    if (!(h[8]=='W'&&h[9]=='A'&&h[10]=='V'&&h[11]=='E')) return r;
    uint16_t audioFmt = (uint16_t)(h[20] | (h[21]<<8));
    r.channels   = (uint16_t)(h[22] | (h[23]<<8));
    r.sampleRate = (uint32_t)(h[24] | (h[25]<<8) | (h[26]<<16) | (h[27]<<24));
    r.bits       = (uint16_t)(h[34] | (h[35]<<8));
    r.dataBytes  = (uint32_t)(h[40] | (h[41]<<8) | (h[42]<<16) | (h[43]<<24));
    r.ok = (audioFmt == 1) && (r.channels >= 1) && (r.sampleRate > 0) &&
           (r.bits == 8 || r.bits == 16) && (r.dataBytes > 0);
    return r;
}

// Remote tap clamp (HTTP -> LVGL 800x480) ------------------------------------------
// Out-of-range taps are rejected (false); callers re-check bounds anyway.
static inline bool touch7ClampTap(int x, int y, int* ox, int* oy) {
    if (x < 0 || y < 0 || x >= 800 || y >= 480) return false;
    if (ox) *ox = touch7ClampInt(x, 0, 799);
    if (oy) *oy = touch7ClampInt(y, 0, 479);
    return true;
}
