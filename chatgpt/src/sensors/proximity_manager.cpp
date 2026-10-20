// proximity_manager.cpp
// Industrial proximity sensor manager.
//   Phase 1 (PROXIMITY_USE_PCNT==0): software ISR with debounce — simple, works for any sensor.
//   Phase 2 (PROXIMITY_USE_PCNT==1): ESP32 PCNT hardware counters — no ISR CPU load,
//     hardware glitch filter, supports up to ~50 kHz. Limits: max 4 channels (4 PCNT units).

#include "proximity_manager.h"

#include "feature_flags.h"
#if ENABLE_PROXIMITY==1

#include <Arduino.h>
#include "esp_system.h" // esp_reset_reason() for the CSV boot row
#include "feature_flags.h"
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
#include "../tca9554_do.h"
#endif
#include "../commands.h"
#include "../arduinojson_fix.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "../littlefs_manager.h"
#include "../sdcard.h"        // sdcardShouldUseSD() + SD_MMC (ENABLE_SDCARD guard)
#include "../sd_layout.h"     // standard flat day-file paths (all packs)
#include "../flash_fallback_store.h" // no-SD LittleFS writers (stub when disabled)
#include "../ConfigManager.h" // ConfigManager type needed for getSdcardName()
#include "../sd_rotation_manager.h" // stringToInterval() / intervalToMs() for CSV flush interval
#include "../email_message_builder.h" // buildEmailSignature() for shift-end email body
#include "../psram_helpers.h"         // psramFound() / heap_caps_malloc for PSRAM buffers
#include "../languages/lang_active.h" // activeLang for the localized email time-bar legend

extern unsigned long lastDataReceivedForInactivityAlert;
extern bool inactivityAlertSent;
extern bool inactivityAlertSentSlot[];
extern ConfigManager configManager;

// Default GPIO pins. Override PROXIMITY_GPIO_CHn in user_config.h if needed.
#ifndef PROXIMITY_GPIO_CH0
  #define PROXIMITY_GPIO_CH0  4
#endif
#ifndef PROXIMITY_GPIO_CH1
  #define PROXIMITY_GPIO_CH1  5
#endif
#ifndef PROXIMITY_GPIO_CH2
  #define PROXIMITY_GPIO_CH2  6
#endif
#ifndef PROXIMITY_GPIO_CH3
  #define PROXIMITY_GPIO_CH3  7
#endif
#ifndef PROXIMITY_GPIO_CH4
  #define PROXIMITY_GPIO_CH4  15
#endif
#ifndef PROXIMITY_GPIO_CH5
  #define PROXIMITY_GPIO_CH5  16
#endif
#ifndef PROXIMITY_GPIO_CH6
  #define PROXIMITY_GPIO_CH6  17
#endif
#ifndef PROXIMITY_GPIO_CH7
  #define PROXIMITY_GPIO_CH7  18
#endif

// How often (ms) to recompute rate_ppm from the count delta.
#define PROX_RATE_UPDATE_MS  5000UL

// Amount of time with no pulses before rate is zeroed (ms).
#define PROX_RATE_IDLE_MS    60000UL

// ISR burst threshold: if raw count delta exceeds this in a single tick, disable ISR.
#define PROX_ISR_BURST_THRESHOLD  1000UL

// Inactivity-reset validity guards (sensor-only boards). Measured idle-machine
// chatter arrives as multi-edge bursts inside a single drain tick (~20 edges),
// while genuine part flow drains as 1-2 edges per tick — so more than
// PROX_INACTIVITY_MAX_TICK_DELTA edges in one tick is treated as chatter.
// (PROX_INACTIVITY_CONFIRM_MS was a 10s confirmation window that required
// pulses to arrive within 10s of the last edge; REMOVED — see fix in proximityTick
// — real machines can have cycle times > 10s during normal production.)
#define PROX_INACTIVITY_MAX_TICK_DELTA  4UL
#define PROX_INACTIVITY_CONFIRM_MS      10000UL  // retained for compatibility; no longer used in reset decision
// Minimum time (ms) between the last two accepted edges for the inactivity
// timer to consider them as real production. Noise on idle DI inputs often
// produces edges at very short, regular intervals (e.g. 5-50 ms), whereas
// genuine part flow has much longer cycle times. This guards against sustained
// low-rate chatter that passes the MAX_TICK_DELTA filter but still keeps
// seconds_since_last_data at ~0.
// NOTE: must stay well below the FASTEST real production cycle on the target
// machine. A 600ppm line (10 parts/s) has a 100ms inter-pulse interval; the old
// 200ms threshold masked it as noise, so the inactivity timer never reset while
// the line was running at speed and alerts fired falsely. 60ms separates real
// part flow (>=100ms on a 600ppm line) from electrical chatter (<=50ms).
#define PROX_INACTIVITY_MIN_CYCLE_MS    60UL

// Ring buffer for recent events (per channel).
struct ProxRecentEvent {
    uint32_t ms;
    uint32_t count_in;
    float    rate_ppm;
};
static ProxRecentEvent _recentEvents[PROXIMITY_MAX_CHANNELS][PROX_RECENT_EVENTS];
static uint8_t _recentEventIdx[PROXIMITY_MAX_CHANNELS];

// Anomaly detection: moving-average rate history buffer.
static float _rateHistory[PROXIMITY_MAX_CHANNELS][PROX_ANOMALY_WINDOW];
static uint8_t _rateHistoryIdx[PROXIMITY_MAX_CHANNELS];

// NVS namespace and key for persistent channel configuration.
#define PROX_CFG_PATH  "/config/sensor_proximity.json"

// Maximum DO pulse duration (10 s) to prevent relay overheating.
#define PROX_DO_MAX_PULSE_MS 10000UL
// Maximum count multiplier (1 pulse = N parts). Raised from 20 to 9999 so a
// manual value (e.g. 200x, 4000x) can be entered on the web page.
#define PROX_MULT_MAX 9999
// Maximum count divider (N pulses = 1 part, e.g. 2-3 moves per part).
// Same range as the multiplier; remainder carries across drain ticks.
#define PROX_DIV_MAX 9999
#define PROX_CNT_PATH  "/config/sensor_prox_counters.json"
#define PROX_DAYBASE_PATH "/config/sensor_prox_daybase.json"
// Shift baselines persisted so a mid-shift reboot keeps the shift window
// (shift-end totals/OEE previously restarted at boot: live counters restore
// from flash but the RAM-only baselines were re-captured, silently dropping
// all pre-reboot production from the shift).
#define PROX_SHIFTBASE_PATH "/config/sensor_prox_shiftbase.json"
// Re-save the day baselines at most this often (LittleFS wear): the day-boundary
// capture always saves; otherwise throttled.
#define PROX_DAYBASE_SAVE_MS (30UL * 60UL * 1000UL)
static uint32_t _lastDaySaveMs = 0;

// Per-channel count baselines for the OEE/shift window (shared scope: used by
// BOTH the PCNT and software-ISR HAL variants plus the shift engine below).
// Captured at each shift start; ALSO restarted by proximityResetChannel() /
// proximityResetOee() so the OEE window always shares the count window's
// origin (otherwise the baseline-relative OEE inputs clamp at 0 until the
// zeroed counters catch up with stale baselines and A/P/Q read 0 long after
// a manual reset).
static uint32_t _shiftCountInBase[PROXIMITY_MAX_CHANNELS];
static uint32_t _shiftCountOutBase[PROXIMITY_MAX_CHANNELS];
static uint32_t _shiftRunSecBase[PROXIMITY_MAX_CHANNELS];
static uint32_t _shiftDowntSecBase[PROXIMITY_MAX_CHANNELS];
static uint32_t _shiftGoodPartsBase[PROXIMITY_MAX_CHANNELS];
static uint32_t _shiftSetupBase[PROXIMITY_MAX_CHANNELS];
// Day baselines captured at the shift-aligned day rollover for the day-so-far
// OEE. Same shape as the shift baselines; captured by _proxDayTick() and zeroed /
// re-based together with them on resets and OEE resets.
static uint32_t _dayCountInBase[PROXIMITY_MAX_CHANNELS];
static uint32_t _dayRunSecBase[PROXIMITY_MAX_CHANNELS];
// Day-so-far production carried across counter resets (shift auto-reset,
// manual reset): pre-reset day deltas accumulate here while the live bases
// re-anchor at 0, so Day OEE/donut always span [day boundary, now].
// Reset only at the day rollover, on OEE reset, or on daybase restore failure.
// Persisted in the daybase file alongside the live bases.
static uint32_t _dayAccumIn[PROXIMITY_MAX_CHANNELS];
static uint32_t _dayAccumRun[PROXIMITY_MAX_CHANNELS];
static uint32_t _dayAccumDownt[PROXIMITY_MAX_CHANNELS];
static uint32_t _dayAccumGood[PROXIMITY_MAX_CHANNELS];
static uint32_t _dayDowntSecBase[PROXIMITY_MAX_CHANNELS];
static uint32_t _dayGoodPartsBase[PROXIMITY_MAX_CHANNELS];
static int _lastDayKey = -1;          // tm_year*1000 + tm_yday, -1 = never captured
static uint32_t _lastDayCheckMs = 0;

// Forward declarations: defined later in Phase 4 block, called from proximityTick().
static void _proxShiftTick(uint32_t nowMs);
static void _proxDayTick(uint32_t nowMs);
static void _proxInjectTick(uint32_t nowMs);
static void _proxWriteCSVRow(const char* type, int8_t shiftNum,
                              const uint32_t* chIn, const uint32_t* chOut, const float* chPpm,
                              const uint32_t* chRun, const uint32_t* chDown, const uint32_t* chGood,
                              time_t nowOverride = 0);
static bool _proxReadHourlyCounts(const String& csvPath, int startHour, int durHours,
                                  long hourVal[24], uint32_t* chartTotalOut);
static void _proxWriteWeeklyOee(int8_t endedShift, int startHour, int durHours,
                                uint32_t shiftPiecesDI1, float shiftAvailDI1,
                                float shiftPerfDI1, float shiftQualDI1, float shiftOeeDI1,
                                uint32_t shiftScrapDI1, uint32_t shiftSetupDI1,
                                uint32_t shiftGoodDI1, uint32_t shiftRunSec,
                                uint32_t shiftPlannedSec, uint32_t shiftElapsedSec,
                                float shiftTargetPpm, uint32_t* csvTotalOut,
                                time_t ctxNow = 0);

// Forward declarations for counter persistence (defined later in this file).
void saveProximityCounters();
void loadProximityCounters();

// Counter recovery layers: RTC RAM mirror (no-wear, survives soft resets) first,
// SD CSV tail (power-cut) second, sparse LittleFS event snapshots last. This
// S3/P4 RTC_SLOW_MEM area is SRAM — unlimited writes, never wears the flash.
static void _proxRtcSave(uint32_t nowMs);
static bool _proxRtcRestore();
static bool _proxRestoreFromSdCsvTail();

// RTC_NOINIT_ATTR survives esp_restart/OTA/WDT/deep sleep with zero flash wear;
// a real power cut clears it (the magic guard then falls back to SD/LittleFS).
RTC_NOINIT_ATTR static ProxRtcSnapshot s_proxRtc;
// True when counters came from the RTC mirror instead of LittleFS at boot.
static bool s_proxFromRtc = false;

// Default channel configuration — all 8 entries defined; only PROXIMITY_CHANNELS are used.
// gpio_out=255 means no DO pin. gpio_in2=255 means no count_out sensor. debounce=50ms. mode=COUNTER.
static ProximityChannel _ch[PROXIMITY_MAX_CHANNELS] = {
    {"DI1", PROXIMITY_GPIO_CH0, 255, 255, PROX_MODE_COUNTER,
#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
     true,          // 8DI8DO: passive by default (Y-wiring) — only reads, never fires output
#else
     false,
#endif
     1, 50, 0,0,0,0,0,0,0,0,0,false,0,0,false,0, 0,0, 0,0,0,0,0,0,0,},
    {"DI2", PROXIMITY_GPIO_CH1, 255, 255, PROX_MODE_COUNTER,
#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
     true,          // 8DI8DO: passive by default (Y-wiring) — only reads, never fires output
#else
     false,
#endif
     1, 50, 0,0,0,0,0,0,0,0,0,false,0,0,false,0, 0,0, 0,0,0,0,0,0,0,},
    {"DI3", PROXIMITY_GPIO_CH2, 255, 255, PROX_MODE_COUNTER,
#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
     true,          // 8DI8DO: passive by default (Y-wiring) — only reads, never fires output
#else
     false,
#endif
     1, 50, 0,0,0,0,0,0,0,0,0,false,0,0,false,0, 0,0, 0,0,0,0,0,0,0,},
    {"DI4", PROXIMITY_GPIO_CH3, 255, 255, PROX_MODE_COUNTER,
#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
     true,          // 8DI8DO: passive by default (Y-wiring) — only reads, never fires output
#else
     false,
#endif
     1, 50, 0,0,0,0,0,0,0,0,0,false,0,0,false,0, 0,0, 0,0,0,0,0,0,0,},
    {"DI5", PROXIMITY_GPIO_CH4, 255, 255, PROX_MODE_COUNTER,
#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
     true,          // 8DI8DO: passive by default (Y-wiring) — only reads, never fires output
#else
     false,
#endif
     1, 50, 0,0,0,0,0,0,0,0,0,false,0,0,false,0, 0,0, 0,0,0,0,0,0,0,},
    {"DI6", PROXIMITY_GPIO_CH5, 255, 255, PROX_MODE_COUNTER,
#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
     true,          // 8DI8DO: passive by default (Y-wiring) — only reads, never fires output
#else
     false,
#endif
     1, 50, 0,0,0,0,0,0,0,0,0,false,0,0,false,0, 0,0, 0,0,0,0,0,0,0,},
    {"DI7", PROXIMITY_GPIO_CH6, 255, 255, PROX_MODE_COUNTER,
#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
     true,          // 8DI8DO: passive by default (Y-wiring) — only reads, never fires output
#else
     false,
#endif
     1, 50, 0,0,0,0,0,0,0,0,0,false,0,0,false,0, 0,0, 0,0,0,0,0,0,0,},
    {"DI8", PROXIMITY_GPIO_CH7, 255, 255, PROX_MODE_COUNTER,
#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
     true,          // 8DI8DO: passive by default (Y-wiring) — only reads, never fires output
#else
     false,
#endif
     1, 50, 0,0,0,0,0,0,0,0,0,false,0,0,false,0, 0,0, 0,0,0,0,0,0,0,},
};

// TCA9554 DO channel mapping: index N maps proximity channel N to a relay output.
// 255 = disabled (no relay linked). Only compiled for boards with the expander.
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
static uint8_t _tca9554_ch[PROXIMITY_MAX_CHANNELS] = {
    0, 1, 2, 3, 4, 5, 6, 7  // Default: CH0→DO1 ... CH7→DO8
};
#endif

// Rate estimation state: count and timestamp of the last rate window start.
static uint32_t _rateBaseCount[PROXIMITY_MAX_CHANNELS];
static uint32_t _rateBaseMs[PROXIMITY_MAX_CHANNELS];

// count_out via gpio_in2 — always software ISR regardless of PCNT mode.
// Only RISING edges on gpio_in2 increment count_out; no debounce (signal expected clean).
static volatile uint32_t _rawCountOut[PROXIMITY_MAX_CHANNELS];
static uint32_t          _drainedCountOut[PROXIMITY_MAX_CHANNELS];
// Divider remainder carry per channel (IN/OUT): fractional pulses toward the
// next whole part. Zeroed on reset/config-divider-change/boot so a stale
// remainder can never materialize phantom parts.
static uint32_t          _divRemIn[PROXIMITY_MAX_CHANNELS];
static uint32_t          _divRemOut[PROXIMITY_MAX_CHANNELS];

static volatile uint32_t _lastEdgeOutMs[PROXIMITY_MAX_CHANNELS];

// OEE tick tracking: timestamp of last OEE accumulator update per channel.
static uint32_t _lastOeeTickMs[PROXIMITY_MAX_CHANNELS];
static uint32_t _oeeMsAccum[PROXIMITY_MAX_CHANNELS]; // sub-second accumulator for OEE seconds

IRAM_ATTR static void onEdgeOut(uint8_t ch) {
    uint32_t now = millis();
    if (now - _lastEdgeOutMs[ch] >= _ch[ch].debounce_ms) {
        _rawCountOut[ch] += 1;  // volatile-safe increment (avoids C++20 -Wvolatile on ++ on volatile)
        _lastEdgeOutMs[ch] = now;
    }
}
IRAM_ATTR static void isrOut0() { onEdgeOut(0); }
IRAM_ATTR static void isrOut1() { onEdgeOut(1); }
IRAM_ATTR static void isrOut2() { onEdgeOut(2); }
IRAM_ATTR static void isrOut3() { onEdgeOut(3); }
IRAM_ATTR static void isrOut4() { onEdgeOut(4); }
IRAM_ATTR static void isrOut5() { onEdgeOut(5); }
IRAM_ATTR static void isrOut6() { onEdgeOut(6); }
IRAM_ATTR static void isrOut7() { onEdgeOut(7); }
static void (*const _isrOutTable[PROXIMITY_MAX_CHANNELS])() = {
    isrOut0, isrOut1, isrOut2, isrOut3,
    isrOut4, isrOut5, isrOut6, isrOut7,
};

// One-shot log per channel: fires on the first accepted pulse after boot/reset.
static bool _firstPulseLogged[PROXIMITY_MAX_CHANNELS];

// Boot diagnostic: log the effective channel configuration (what is armed on which GPIO).
// Runs in setup() during the boot window, so it always reaches Serial + Web Monitor.
static void _proxLogBootSummary() {
    for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
        if (_ch[i].gpio_in == 255) {
            sendVirtualMessage(String(F("Prox DI")) + String((int)i + 1) + F(": disabled (gpio=255)"), INFO);
            continue;
        }
        const char* modeStr = _ch[i].mode == PROX_MODE_PRESENCE ? "PRESENCE"
                            : (_ch[i].mode == PROX_MODE_SPEED    ? "SPEED" : "COUNTER");
        sendVirtualMessage(String(F("Prox DI")) + String((int)i + 1) +
            F(": armed GPIO=") + String((int)_ch[i].gpio_in) +
            F(" mode=") + String(modeStr) +
            F(" db=") + String(_ch[i].debounce_ms) +
            F("ms out=") + String((int)_ch[i].gpio_out) +
            F(" in2=") + String((int)_ch[i].gpio_in2), INFO);
    }
}

// ============================================================================
// PHASE 2 — PCNT hardware counters (PROXIMITY_USE_PCNT==1)
// ============================================================================
#if PROXIMITY_USE_PCNT==1

// ESP32-S3 has 4 PCNT units; each unit has one 16-bit counter shared by its two
// input channels. Therefore only 4 independent pulse streams are supported.
// If you need more than 4 channels, set PROXIMITY_USE_PCNT=0 (software ISR).
#if PROXIMITY_CHANNELS > 4
  #error "PROXIMITY_USE_PCNT: ESP32-S3 has 4 PCNT units — set PROXIMITY_CHANNELS <= 4 or use PROXIMITY_USE_PCNT=0"
#endif

#include "driver/pcnt.h"

// Overflow count per unit. Each time the PCNT counter hits h_lim, this
// increments by PCNT_H_LIM so the 16-bit hardware counter can be extended
// to a full 32-bit range. ISR-accessed — must stay in DRAM.
#define PCNT_H_LIM  30000
static volatile int32_t _pcntAccum[4] = {};

// Last edge timestamp per channel, updated in overflow ISR.
static volatile uint32_t _lastEdgeMs[PROXIMITY_MAX_CHANNELS];

// Per-unit ISR: increments accumulator and records edge time.
// pcnt_unit_t maps directly to channel index (unit 0 = channel 0, etc.).
IRAM_ATTR static void pcntIsrHandler(void* arg) {
    uint8_t ch = (uint8_t)(uintptr_t)arg;
    uint32_t status = 0;
    pcnt_get_event_status((pcnt_unit_t)ch, &status);
    if (status & PCNT_EVT_H_LIM) {
        _pcntAccum[ch] += PCNT_H_LIM;
        _lastEdgeMs[ch]  = (uint32_t)millis();
    }
}

// Returns cumulative count (accumulator + live counter), atomic read.
static uint32_t pcntReadTotal(uint8_t ch) {
    int16_t cnt = 0;
    noInterrupts();
    pcnt_get_counter_value((pcnt_unit_t)ch, &cnt);
    int32_t accum = _pcntAccum[ch];
    interrupts();
    return (uint32_t)(accum + (int32_t)cnt);
}

void proximityBegin() {
    loadProximityConfig();      // Apply any NVS-persisted channel config.
    loadProximityShiftConfig(); // Apply any NVS-persisted shift config.
    { ProximityShiftConfig _sc = proximityGetShiftConfig();
      char _dbg[200]; snprintf(_dbg, sizeof(_dbg),
        "[prox:diag] begin(PCNT) ch0: n=%s m=%u db=%lu gl=%lu mo=%d | shift: en=%d ns=%u h0=%u:%u",
        _ch[0].name, (unsigned)_ch[0].mode, (unsigned long)_ch[0].debounce_ms,
        (unsigned long)_ch[0].goal, (int)_ch[0].monitor_only,
        (int)_sc.enabled, (unsigned)_sc.nSlots,
        (unsigned)_sc.slots[0].hour, (unsigned)_sc.slots[0].minute);
      sendVirtualMessage(String(_dbg), INFO, false, true); }
    memset((void*)_lastEdgeMs, 0, sizeof(_lastEdgeMs));
    memset((void*)_lastEdgeOutMs, 0, sizeof(_lastEdgeOutMs));
    memset((void*)_lastOeeTickMs, 0, sizeof(_lastOeeTickMs));
    memset((void*)_oeeMsAccum, 0, sizeof(_oeeMsAccum));

    // Install the shared PCNT ISR dispatch service (flag 0 = default priority).
    pcnt_isr_service_install(0);

    for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
        _rateBaseMs[i]    = millis();
        _rateBaseCount[i] = 0;
        _pcntAccum[i]     = 0;
        _countStartMs[i]  = millis();

        uint8_t gpio = _ch[i].gpio_in;
        if (gpio == 255) continue;

        // Configure PCNT unit i, channel 0: count RISING edges on gpio.
        pcnt_config_t cfg = {};
        cfg.pulse_gpio_num = (int)gpio;
        cfg.ctrl_gpio_num  = PCNT_PIN_NOT_USED;
        cfg.channel        = PCNT_CHANNEL_0;
        cfg.unit           = (pcnt_unit_t)i;
        cfg.pos_mode       = PCNT_CHANNEL_EDGE_ACTION_INCREASE;
        cfg.neg_mode       = PCNT_CHANNEL_EDGE_ACTION_HOLD;
        cfg.lctrl_mode     = PCNT_CHANNEL_LEVEL_ACTION_KEEP;
        cfg.hctrl_mode     = PCNT_CHANNEL_LEVEL_ACTION_KEEP;
        cfg.counter_h_lim  = PCNT_H_LIM;
        cfg.counter_l_lim  = 0;
        pcnt_unit_config(&cfg);

        // Hardware glitch filter: ~100 APB cycles = ~1.25 µs at 80 MHz.
        // Rejects electrical spikes shorter than this window.
        // NOTE: This does not replace mechanical debounce — for that, use PROXIMITY_USE_PCNT=0.
        pcnt_filter_value_set((pcnt_unit_t)i, 100);
        pcnt_filter_enable((pcnt_unit_t)i);

        // Register overflow callback and start counter.
        pcnt_event_enable((pcnt_unit_t)i, PCNT_EVT_H_LIM);
        pcnt_isr_handler_add((pcnt_unit_t)i, pcntIsrHandler, (void*)(uintptr_t)i);

        pcnt_counter_pause((pcnt_unit_t)i);
        pcnt_counter_clear((pcnt_unit_t)i);
        pcnt_counter_resume((pcnt_unit_t)i);

        if (_ch[i].gpio_out != 255) {
            pinMode(_ch[i].gpio_out, OUTPUT);
            digitalWrite(_ch[i].gpio_out, LOW);
        }
    }

    // Setup gpio_in2 ISRs (always software, independent of PCNT mode for gpio_in).
    for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
        _rawCountOut[i]     = 0;
        _drainedCountOut[i] = 0;
        if (_ch[i].gpio_in2 == 255) continue;
        pinMode(_ch[i].gpio_in2, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(_ch[i].gpio_in2), _isrOutTable[i], RISING);
    }

    // Boot diagnostic: effective channel config (always reaches Serial + Web Monitor).
    _proxLogBootSummary();
    memset((void*)_firstPulseLogged, 0, sizeof(_firstPulseLogged));
    // Counter recovery: RTC RAM mirror first (no flash write; survives soft
    // resets/OTA/WDT/deep sleep) then the sparse LittleFS snapshot. A real
    // power cut falls through to the SD CSV tail at the first NTP-synced tick.
    if (!_proxRtcRestore()) {
        loadProximityCounters(); // Restore count_in/count_out saved before last reboot.
    }
    loadProximityDayBaselines(); // Restore day baselines (day OEE/donut continue since the day boundary).
}

void proximityResetChannel(uint8_t ch) {
    if (ch >= PROXIMITY_CHANNELS) return;

    // Carry day-so-far production across the reset so Day OEE/donut always
    // span [day boundary, now]: fold the live tails into the accumulators
    // BEFORE the counters re-anchor at 0 below (manual resets keep the day
    // too — "reset" zeroes the live counts, not what happened today).
    _dayAccumIn[ch]   += (_ch[ch].count_in >= _dayCountInBase[ch])
        ? _ch[ch].count_in - _dayCountInBase[ch] : 0U;
    _dayAccumRun[ch]  += (_ch[ch].running_seconds >= _dayRunSecBase[ch])
        ? _ch[ch].running_seconds - _dayRunSecBase[ch] : 0U;
    _dayAccumDownt[ch] += (_ch[ch].downtime_seconds >= _dayDowntSecBase[ch])
        ? _ch[ch].downtime_seconds - _dayDowntSecBase[ch] : 0U;
    _dayAccumGood[ch] += (_ch[ch].good_parts >= _dayGoodPartsBase[ch])
        ? _ch[ch].good_parts - _dayGoodPartsBase[ch] : 0U;

    noInterrupts();
    pcnt_counter_pause((pcnt_unit_t)ch);
    pcnt_counter_clear((pcnt_unit_t)ch);
    _pcntAccum[ch]      = 0;
    _rawCountOut[ch]    = 0;
    _drainedCountOut[ch]= 0;
    _lastEdgeOutMs[ch]  = 0;
    interrupts();
    pcnt_counter_resume((pcnt_unit_t)ch);

    _ch[ch].count_in       = 0;
    _ch[ch].count_out      = 0;
    _divRemIn[ch]          = 0;
    _divRemOut[ch]         = 0;
    _ch[ch].last_pulse_ms  = 0;
    _ch[ch].cycle_time_ms  = 0;
    _ch[ch].rate_ppm       = 0.0f;
    _ch[ch].running_seconds   = 0;
    _ch[ch].downtime_seconds  = 0;
    _ch[ch].good_parts        = 0;
    _ch[ch].setup_count       = 0;
    // Restart the OEE/shift baselines together with the counters (they mirror
    // the zeroed values, so the baseline-relative OEE inputs start at 0 and
    // grow instead of clamping against stale pre-reset values).
    noInterrupts();
    _shiftCountInBase[ch]   = 0;
    _shiftCountOutBase[ch]  = 0;
    _shiftRunSecBase[ch]    = 0;
    _shiftDowntSecBase[ch]  = 0;
    _shiftGoodPartsBase[ch] = 0;
    _shiftSetupBase[ch]     = 0;
    _dayCountInBase[ch]     = 0;
    _dayRunSecBase[ch]      = 0;
    _dayDowntSecBase[ch]    = 0;
    _dayGoodPartsBase[ch]   = 0;
    interrupts();
    _rateBaseCount[ch]        = 0;
    _rateBaseMs[ch]           = millis();
    _lastOeeTickMs[ch]        = millis();
    _oeeMsAccum[ch]           = 0;
    _countStartMs[ch]         = millis();
    _firstPulseLogged[ch]     = false;
    // Single-channel reset re-based this channel: persist so a reboot pairs
    // the zeroed bases with fresh counters (the stale-guard would reject them).
    saveProximityShiftBaselines();
}

// Helper used in proximityTick() to get the new raw count and last edge time.
static inline uint32_t _getRaw(uint8_t i)  { return pcntReadTotal(i); }
static inline uint32_t _getEdgeMs(uint8_t i) { return _lastEdgeMs[i]; }
static uint32_t _drainedCount[PROXIMITY_MAX_CHANNELS]; // last-drained snapshot
// millis() at which each channel's count window started (elapsed shown on the
// web page). Reset to now on every proximityResetChannel() / boot — NOT persisted.
static uint32_t _countStartMs[PROXIMITY_MAX_CHANNELS];

// ============================================================================
// PHASE 1 — Software ISR (PROXIMITY_USE_PCNT==0)
// ============================================================================
#else  // PROXIMITY_USE_PCNT==0

// Volatile ISR-safe state: incremented / written only in IRAM_ATTR ISRs.
static volatile uint32_t _rawCount[PROXIMITY_MAX_CHANNELS];
static volatile uint32_t _lastEdgeMs[PROXIMITY_MAX_CHANNELS];
static uint32_t _drainedCount[PROXIMITY_MAX_CHANNELS];
// millis() at which each channel's count window started (elapsed shown on the
// web page). Reset to now on every proximityResetChannel() / boot — NOT persisted.
static uint32_t _countStartMs[PROXIMITY_MAX_CHANNELS];

// onEdge is called inside an ISR — must remain in IRAM.
// Software debounce: ignores edges arriving within debounce_ms of the last accepted one.
IRAM_ATTR static void onEdge(uint8_t ch) {
    uint32_t now = millis();
    if (now - _lastEdgeMs[ch] >= _ch[ch].debounce_ms) {
        _rawCount[ch] += 1;  // volatile-safe increment (avoids C++20 -Wvolatile on ++ on volatile)
        _lastEdgeMs[ch] = now;
    }
}

// Individual ISR stubs — lambdas cannot be IRAM_ATTR, so one stub per channel is required.
IRAM_ATTR static void isrCh0() { onEdge(0); }
IRAM_ATTR static void isrCh1() { onEdge(1); }
IRAM_ATTR static void isrCh2() { onEdge(2); }
IRAM_ATTR static void isrCh3() { onEdge(3); }
IRAM_ATTR static void isrCh4() { onEdge(4); }
IRAM_ATTR static void isrCh5() { onEdge(5); }
IRAM_ATTR static void isrCh6() { onEdge(6); }
IRAM_ATTR static void isrCh7() { onEdge(7); }

static void (*const _isrTable[PROXIMITY_MAX_CHANNELS])() = {
    isrCh0, isrCh1, isrCh2, isrCh3,
    isrCh4, isrCh5, isrCh6, isrCh7,
};

void proximityBegin() {
    loadProximityConfig();      // Apply any NVS-persisted channel config.
    loadProximityShiftConfig(); // Apply any NVS-persisted shift config.
    { ProximityShiftConfig _sc = proximityGetShiftConfig();
      char _dbg[200]; snprintf(_dbg, sizeof(_dbg),
        "[prox:diag] begin(SW) ch0: n=%s m=%u db=%lu gl=%lu mo=%d | shift: en=%d ns=%u h0=%u:%u",
        _ch[0].name, (unsigned)_ch[0].mode, (unsigned long)_ch[0].debounce_ms,
        (unsigned long)_ch[0].goal, (int)_ch[0].monitor_only,
        (int)_sc.enabled, (unsigned)_sc.nSlots,
        (unsigned)_sc.slots[0].hour, (unsigned)_sc.slots[0].minute);
      sendVirtualMessage(String(_dbg), INFO, false, true); }
    for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
        _rawCount[i]      = 0;
        _lastEdgeMs[i]    = 0;
        _lastEdgeOutMs[i] = 0;
        _lastOeeTickMs[i] = 0;
        _oeeMsAccum[i]    = 0;
        _countStartMs[i]  = millis();
        _drainedCount[i]  = 0;
        _divRemIn[i]      = 0;
        _divRemOut[i]     = 0;
        _rateBaseCount[i] = 0;
        _rateBaseMs[i]    = millis();

        uint8_t gpio = _ch[i].gpio_in;
        if (gpio == 255) continue;

        pinMode(gpio, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(gpio), _isrTable[i], RISING);

        if (_ch[i].gpio_out != 255) {
            pinMode(_ch[i].gpio_out, OUTPUT);
            digitalWrite(_ch[i].gpio_out, LOW);
        }
    }

    // Setup gpio_in2 ISRs (always software, independent of PCNT mode for gpio_in).
    for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
        _rawCountOut[i]     = 0;
        _drainedCountOut[i] = 0;
        if (_ch[i].gpio_in2 == 255) continue;
        pinMode(_ch[i].gpio_in2, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(_ch[i].gpio_in2), _isrOutTable[i], RISING);
    }

    // Boot diagnostic: effective channel config (always reaches Serial + Web Monitor).
    _proxLogBootSummary();
    memset((void*)_firstPulseLogged, 0, sizeof(_firstPulseLogged));
    // Counter recovery: RTC RAM mirror first (no flash write; survives soft
    // resets/OTA/WDT/deep sleep) then the sparse LittleFS snapshot. A real
    // power cut falls through to the SD CSV tail at the first NTP-synced tick.
    if (!_proxRtcRestore()) {
        loadProximityCounters(); // Restore count_in/count_out saved before last reboot.
    }
    loadProximityDayBaselines(); // Restore day baselines (day OEE/donut continue since the day boundary).
}

void proximityResetChannel(uint8_t ch) {
    if (ch >= PROXIMITY_CHANNELS) return;

    // Carry day-so-far production across the reset (same rule as the PCNT
    // variant above): fold live tails into the accumulators BEFORE zeroing.
    _dayAccumIn[ch]   += (_ch[ch].count_in >= _dayCountInBase[ch])
        ? _ch[ch].count_in - _dayCountInBase[ch] : 0U;
    _dayAccumRun[ch]  += (_ch[ch].running_seconds >= _dayRunSecBase[ch])
        ? _ch[ch].running_seconds - _dayRunSecBase[ch] : 0U;
    _dayAccumDownt[ch] += (_ch[ch].downtime_seconds >= _dayDowntSecBase[ch])
        ? _ch[ch].downtime_seconds - _dayDowntSecBase[ch] : 0U;
    _dayAccumGood[ch] += (_ch[ch].good_parts >= _dayGoodPartsBase[ch])
        ? _ch[ch].good_parts - _dayGoodPartsBase[ch] : 0U;

    // Zero all ISR counters atomically.
    noInterrupts();
    _rawCount[ch]       = 0;
    _drainedCount[ch]   = 0;
    _rawCountOut[ch]    = 0;
    _drainedCountOut[ch]= 0;
    _lastEdgeOutMs[ch]  = 0;
    interrupts();

    _ch[ch].count_in       = 0;
    _ch[ch].count_out      = 0;
    _divRemIn[ch]          = 0;
    _divRemOut[ch]         = 0;
    _ch[ch].last_pulse_ms  = 0;
    _ch[ch].cycle_time_ms  = 0;
    _ch[ch].rate_ppm          = 0.0f;
    _ch[ch].running_seconds   = 0;
    _ch[ch].downtime_seconds  = 0;
    _ch[ch].good_parts        = 0;
    _ch[ch].setup_count       = 0;
    // Restart the OEE/shift baselines together with the counters (mirror the
    // zeroed values — see PCNT variant above for rationale).
    noInterrupts();
    _shiftCountInBase[ch]   = 0;
    _shiftCountOutBase[ch]  = 0;
    _shiftRunSecBase[ch]    = 0;
    _shiftDowntSecBase[ch]  = 0;
    _shiftGoodPartsBase[ch] = 0;
    _shiftSetupBase[ch]     = 0;
    _dayCountInBase[ch]     = 0;
    _dayRunSecBase[ch]      = 0;
    _dayDowntSecBase[ch]    = 0;
    _dayGoodPartsBase[ch]   = 0;
    interrupts();
    _rateBaseCount[ch]        = 0;
    _rateBaseMs[ch]           = millis();
    _lastOeeTickMs[ch]        = millis();
    _oeeMsAccum[ch]           = 0;
    _countStartMs[ch]         = millis();
    _firstPulseLogged[ch]     = false;
    // Single-channel reset re-based this channel (mirror PCNT variant above).
    saveProximityShiftBaselines();
}

static inline uint32_t _getRaw(uint8_t i)    { return _rawCount[i]; }
static inline uint32_t _getEdgeMs(uint8_t i) { return _lastEdgeMs[i]; }

#endif // PROXIMITY_USE_PCNT

// Returns true if channel `ch` is referenced as scrap_ch by another channel.
// A scrap channel's gate-OFF pulses are discarded (not routed to setup_count).
static bool isScrapChannel(uint8_t ch) {
    for (uint8_t j = 0; j < PROXIMITY_CHANNELS; j++) {
        if (j != ch && _ch[j].scrap_ch < PROXIMITY_CHANNELS && _ch[j].scrap_ch == ch) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Shared tick, reset-all, get, count — identical for both counting modes.
// ============================================================================

void proximityTick() {
    uint32_t now = millis();
    _proxShiftTick(now);   // Phase 4: shift boundary check.
    _proxDayTick(now);     // Day baseline check (shift-day rollover, day-so-far OEE).
    _proxInjectTick(now);  // Periodic injection into the RX buffer rules engine.

    for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
        // OEE running/downtime tracking (every tick from loop).
        // Count edges received since the last tick to decide running vs downtime.
        // Using the real edge count (delta>0) instead of rate_ppm avoids the
        // 1-minute smoothing lag: a tick with zero edges is genuine downtime
        // even if rate_ppm has not yet decayed to 0, so micro-stops now lower A/P.
        uint32_t _rawNow    = _getRaw(i);
        uint32_t _deltaNow  = (_rawNow > _drainedCount[i]) ? (_rawNow - _drainedCount[i]) : 0;
        // Accumulate edges into the 1-second OEE window (loop runs faster than 1s,
        // so a single tick's delta is not representative — sum across the whole second).
        _ch[i].oee_count_window += _deltaNow;
        if (_lastOeeTickMs[i] > 0 && _ch[i].mode != PROX_MODE_PRESENCE) {
            uint32_t elapsed = now - _lastOeeTickMs[i];
            // Cap elapsed to avoid absurd downtime accumulation after a long
            // loop stall / missed ticks (e.g. OTA, deep sleep, or a tick gap).
            // Without this, a single large gap can inflate downtime_seconds by
            // days and drive availability to ~0%.
            if (elapsed > 2000) elapsed = 2000;
            bool gateOpen = true;
            if (_ch[i].gate_ch < PROXIMITY_CHANNELS && _ch[i].gate_ch != i) {
                gateOpen = _ch[_ch[i].gate_ch].state; // PRESENCE channel GPIO state
            }
            _oeeMsAccum[i] += elapsed;
            // FIX (issues #3/#7 — OEE): consumption + window-clear moved into
            // proximityOeeWindowConsume(). Issue #3: the window count is captured
            // ONCE before the loop, so a loop stall (elapsed >= 2000ms) doesn't
            // clear the window mid-second and count the 2nd second as downtime.
            // Issue #7 (live OEE 0.13% with the machine producing): the window
            // used to be zeroed on EVERY tick, wiping the sub-second accumulation
            // on a fast loop, so at the 1000ms boundary the window held only the
            // last tick's delta (usually 0) → running_seconds≈0, A/OEE≈0.
            // Now the window is cleared only when a second was consumed.
            // Slow-machine grace (standard APT): ideal EDGE cycle from target
            // cadence AND multiplier (1 pulse = N parts); a second without
            // edges still counts as running while the gap since the last edge
            // is within 2x that cycle (capped). Fast lines (edges every
            // second) never touch this path — zero behaviour change for them.
            uint32_t graceMs = proximityOeeGraceMs(_ch[i].target_ppm, _ch[i].multiplier, _ch[i].divider);
            proximityOeeWindowConsume(_oeeMsAccum[i], _ch[i].oee_count_window,
                                      gateOpen, _ch[i].running_seconds,
                                      _ch[i].downtime_seconds,
                                      _ch[i].last_pulse_ms, now, graceMs);
            // gateOpen == false → neither running nor downtime advances (presence not active)
        }
        _lastOeeTickMs[i] = now;
        // Drain count_out from gpio_in2 (always software ISR, independent of PCNT mode).
        uint32_t rawOut   = _rawCountOut[i];
        uint32_t deltaOut = rawOut - _drainedCountOut[i];
        if (deltaOut > 0) {
            _drainedCountOut[i] = rawOut;
            uint32_t dParts = proxApplyDivider(deltaOut, _ch[i].multiplier, _ch[i].divider, &_divRemOut[i]);
            _ch[i].count_out += dParts;
            _ch[i].total_out += dParts;
            // Direction detection: compare IN vs OUT edge timing within 500ms window.
            if (_ch[i].dir_edge_ms_in > 0 && now - _ch[i].dir_edge_ms_in <= 500) {
                _ch[i].direction = 1; // IN first → entering
            } else {
                _ch[i].direction = 2; // OUT first or no recent IN → exiting
            }
        }

        // Drain new counts since the last tick.
        uint32_t raw   = _getRaw(i);
        uint32_t delta = raw - _drainedCount[i];

        if (delta > 0) {
            _drainedCount[i] = raw;

            uint32_t edgeMs  = _getEdgeMs(i);

            // Real-time inactivity timer reset: each VALID production pulse resets
            // the inactivity alert timer immediately, instead of waiting for the
            // 60-second _proxInjectTick cycle.
            // On boards without RS232/LPT (e.g. Waveshare 8DI8DO), proximity is
            // the only data source — but raw DI chatter on an idle machine must not
            // reset it, otherwise seconds_since_last_data stays at ~0 forever and
            // the alert never fires. A pulse counts as production only when all of
            // these hold:
            //   - the channel is NOT a scrap channel (scrap edges on DI2 must not
            //     hold the timer alive — only production pulses on DI1 reset it);
            //   - the channel actually counts parts (not PRESENCE state reporting);
            //   - its gate channel (if wired) reports the machine active (same rule
            //     as the OEE running/downtime decision above);
            //   - few edges arrived in this drain tick (a multi-edge burst inside a
            //     single tick is electrically impossible for real part flow — it is
            //     sensor chatter from a noisy/oscillating input);
            //   - the inter-pulse interval is above the minimum (sustained low-rate
            //     chatter on idle DI inputs produces edges at short, regular intervals
            //     that pass the burst filter but still keep the timer at ~0);
            //   - another accepted edge happened recently (ongoing activity, so an
            //     isolated single glitch does not reset the timer).
#if ENABLE_RS232==0 && ENABLE_DB25==0
            // On boards without RS232/LPT (e.g. Waveshare 8DI8DO), proximity is
            // the ONLY production data source. Reset the timer on every accepted
            // edge from production channels — without this, the timer would never
            // start and alerts would never fire. Scrap channels are excluded so
            // their edges cannot hold the timer alive on an idle production line.
            // The delta <= PROX_INACTIVITY_MAX_TICK_DELTA guard prevents
            // chatter/glitches from resetting the timer.
            // FIX (issues #1 — Inactivity Alert): Any valid
            // production pulse (DI1 non-scrap) must restart the inactivity timer.
            // The previous condition required that the pulse arrive within
            // PROX_INACTIVITY_CONFIRM_MS (10s) of the last edge — but real
            // machines may run > 10s cycles between parts during normal production,
            // which meant the timer would NOT restart and false alerts would be
            // generated. Removed the confirmatory window; now every production pulse
            // resets the timer, provided it passes the chatter guard.
            const bool _resetInactivity =\
                (delta <= PROX_INACTIVITY_MAX_TICK_DELTA) &&\
                (_ch[i].last_pulse_ms != 0) &&\
                ((edgeMs - _ch[i].last_pulse_ms) >= PROX_INACTIVITY_MIN_CYCLE_MS) &&\
                !isScrapChannel(i);  // scrap channels must not hold the timer alive
#else
            const bool _resetInactivity = configManager.getInactivityAlertSensorsResetTimer();
#endif
            if (_resetInactivity) {
                lastDataReceivedForInactivityAlert = millis();
                inactivityAlertSent = false;
                for (int _i = 0; _i < 6; ++_i) inactivityAlertSentSlot[_i] = false;
            }

            // Burst detection: >1000 edges in a single tick → likely sensor fault.
            if (delta > PROX_ISR_BURST_THRESHOLD && _ch[i].gpio_in != 255) {
                sendVirtualMessage(String(F("Proximity burst detected on ch")) + String(i) + F(" — disabling ISR"), ERROR);
                detachInterrupt(digitalPinToInterrupt(_ch[i].gpio_in));
                _ch[i].gpio_in = 255;
            }

            if (_ch[i].mode != PROX_MODE_PRESENCE) {
                // Parts for this drain: pulses x multiplier, divided into whole
                // parts (N pulses = 1 part) with remainder carried forward.
                uint32_t dParts = proxApplyDivider(delta, _ch[i].multiplier, _ch[i].divider, &_divRemIn[i]);
                // Setup routing (Nuno spec): when this channel has a gate_ch and the
                // gate PRESENCE channel is OFF, pulses count as setup, NOT production.
                bool _gateOff = false;
                if (_ch[i].gate_ch < PROXIMITY_CHANNELS && _ch[i].gate_ch != i) {
                    _gateOff = !_ch[_ch[i].gate_ch].state; // gate channel PRESENCE state
                }
                if (_gateOff && _ch[i].gate_ch < PROXIMITY_CHANNELS && _ch[i].gate_ch != i) {
                    // gate OFF: non-scrap channels route to setup; scrap channels
                    // are discarded (they only count real scrap while producing).
                    if (!isScrapChannel(i)) {
                        _ch[i].setup_count += dParts;
                    }
                } else {
                    _ch[i].count_in += dParts;
                }
                _ch[i].total_in += dParts;
            }
            // Diagnostic: log the first accepted pulse on this channel (signal arrived).
            if (!_firstPulseLogged[i] && _ch[i].count_in > 0) {
                _firstPulseLogged[i] = true;
                sendVirtualMessage(String(F("Prox CH")) + String((int)i) +
                    F(": first pulse (count_in=") + String(_ch[i].count_in) + F(")"), INFO);
            }
            // Direction detection: record IN edge timestamp.
            _ch[i].dir_edge_ms_in = edgeMs;
            // Push to recent events ring buffer.
            uint8_t ri = _recentEventIdx[i] % PROX_RECENT_EVENTS;
            _recentEvents[i][ri].ms       = edgeMs;
            _recentEvents[i][ri].count_in = _ch[i].count_in;
            _recentEvents[i][ri].rate_ppm = _ch[i].rate_ppm;
            _recentEventIdx[i] = ri + 1;
            // Recompute good parts (IN - OUT) on every edge event.
            uint32_t inVal  = _ch[i].count_in;
            uint32_t outVal = _ch[i].count_out;
            _ch[i].good_parts = (inVal > outVal) ? (inVal - outVal) : 0;

            if (_ch[i].last_pulse_ms > 0) {
                _ch[i].cycle_time_ms = edgeMs - _ch[i].last_pulse_ms;
            }
            _ch[i].last_pulse_ms = edgeMs;

            // Schedule DO pulse if any output (GPIO or TCA9554) is configured, not yet active, and has a nonzero duration.
            bool goalReached = (_ch[i].goal > 0 && _ch[i].count_in > _ch[i].goal);
            bool hasGpioOut  = (_ch[i].gpio_out != 255);
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
            bool hasTcaOut   = (_tca9554_ch[i] != 255);
#else
            bool hasTcaOut   = false;
#endif
            if (!_ch[i].monitor_only && !goalReached && (hasGpioOut || hasTcaOut) && !_ch[i].do_active && _ch[i].pulse_dur_ms > 0) {
                _ch[i].do_start_ms = now + _ch[i].pulse_delay_ms;
                _ch[i].do_active   = true;
            }
        }

        // PRESENCE mode: read live GPIO level from main context (safe, no ISR).
        // Optocoupler inverts the signal: GPIO LOW = sensor active (ON), HIGH = inactive (OFF).
        if (_ch[i].mode == PROX_MODE_PRESENCE && _ch[i].gpio_in != 255) {
            _ch[i].state = (digitalRead(_ch[i].gpio_in) == LOW);
        }

        // Update rate_ppm every PROX_RATE_UPDATE_MS.
        uint32_t elapsed = now - _rateBaseMs[i];
        if (elapsed >= PROX_RATE_UPDATE_MS) {
            uint32_t newCounts = _ch[i].count_in - _rateBaseCount[i];

            if (newCounts > 0 && elapsed > 0) {
                _ch[i].rate_ppm = (float)newCounts * 60000.0f / (float)elapsed;
            } else if (elapsed >= PROX_RATE_IDLE_MS) {
                // No pulses for a full idle window — rate is zero.
                _ch[i].rate_ppm = 0.0f;
            }

            _rateBaseCount[i] = _ch[i].count_in;
            _rateBaseMs[i]    = now;

            // Anomaly detection: moving average vs current rate.
            if (_ch[i].rate_ppm > 0) {
                _rateHistory[i][_rateHistoryIdx[i] % PROX_ANOMALY_WINDOW] = _ch[i].rate_ppm;
                _rateHistoryIdx[i]++;
                if (_rateHistoryIdx[i] >= PROX_ANOMALY_WINDOW) {
                    float sum = 0;
                    for (uint8_t k = 0; k < PROX_ANOMALY_WINDOW; k++) sum += _rateHistory[i][k];
                    float avg = sum / (float)PROX_ANOMALY_WINDOW;
                    if (avg > 0 && _ch[i].rate_ppm < avg * 0.5f) {
                        sendVirtualMessage(String(F("Anomaly: ch")) + String(i) + F(" PPM dropped 50%"), WARN);
                    }
                }
            }
        }

        // Drive DO output pulse (skipped when monitor_only is set).
        // On boards with TCA9554, also drive the mapped relay output.
        if (!_ch[i].monitor_only && _ch[i].do_active) {
            bool hasGpio    = (_ch[i].gpio_out != 255);
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
            bool hasTca     = (_tca9554_ch[i] != 255);
#else
            bool hasTca     = false;
#endif
            if (hasGpio || hasTca) {
                uint32_t onAt  = _ch[i].do_start_ms;
                uint32_t offAt = onAt + _ch[i].pulse_dur_ms;
                if (now >= onAt && now < offAt) {
                    if (hasGpio) digitalWrite(_ch[i].gpio_out, HIGH);
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
                    if (hasTca)  tca9554SetOutput(_tca9554_ch[i], true);
#endif
                } else if (now >= offAt) {
                    if (hasGpio) digitalWrite(_ch[i].gpio_out, LOW);
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
                    if (hasTca)  tca9554SetOutput(_tca9554_ch[i], false);
#endif
                    _ch[i].do_active = false;
                }
            } else {
                // No output configured — deactivate immediately once time passes.
                if (now >= _ch[i].do_start_ms + _ch[i].pulse_dur_ms)
                    _ch[i].do_active = false;
            }
        }
    }
}

void proximityResetAll() {
    for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
        proximityResetChannel(i);
    }
    saveProximityCounters();
    // Persist the carried day production immediately: a reboot before the next
    // throttled daybase save must not lose the shift that just ended.
    saveProximityDayBaselines();
    // Same for the zeroed shift bases (fresh shift window starts here).
    saveProximityShiftBaselines();
}

void proximityResetTotal(uint8_t ch) {
    if (ch >= PROXIMITY_CHANNELS) return;
    _ch[ch].total_in  = 0;
    _ch[ch].total_out = 0;
    saveProximityCounters();
}

void proximityResetOee(uint8_t ch) {
    if (ch >= PROXIMITY_CHANNELS) return;
    // Restart the OEE window WITHOUT zeroing counts: capture the live counters
    // as the new baselines (deltas restart at 0 and grow) and restart the
    // elapsed clock, so A/P/Q resume coherently. Zeroing the absolutes while
    // keeping stale baselines would clamp every OEE input at 0 until the
    // counters caught up with the pre-reset totals.
    noInterrupts();
    _shiftCountInBase[ch]   = _ch[ch].count_in;
    _shiftCountOutBase[ch]  = _ch[ch].count_out;
    _shiftRunSecBase[ch]    = _ch[ch].running_seconds;
    _shiftDowntSecBase[ch]  = _ch[ch].downtime_seconds;
    _shiftGoodPartsBase[ch] = _ch[ch].good_parts;
    _shiftSetupBase[ch]     = _ch[ch].setup_count;
    _dayCountInBase[ch]     = _ch[ch].count_in;
    _dayRunSecBase[ch]      = _ch[ch].running_seconds;
    _dayDowntSecBase[ch]    = _ch[ch].downtime_seconds;
    _dayGoodPartsBase[ch]   = _ch[ch].good_parts;
    interrupts();
    // OEE reset restarts the day window too: drop carried production so the
    // re-anchored bases above read 0 and grow (same intent as the shift bases).
    _dayAccumIn[ch]    = 0;
    _dayAccumRun[ch]   = 0;
    _dayAccumDownt[ch] = 0;
    _dayAccumGood[ch]  = 0;
    _countStartMs[ch]        = millis();
    _lastOeeTickMs[ch]       = millis();
    _oeeMsAccum[ch]          = 0;
    // OEE reset re-based the shift bases: persist for the same reason.
    saveProximityShiftBaselines();
}

ProximityChannel proximityGetChannel(uint8_t ch) {
    if (ch >= PROXIMITY_CHANNELS) {
        ProximityChannel empty = {};
        return empty;
    }
    // Brief interrupt lock to get a consistent snapshot of the volatile fields.
    noInterrupts();
    ProximityChannel snap = _ch[ch];
    interrupts();
    return snap;
}

uint32_t proximityCountElapsedSec(uint8_t ch) {
    if (ch >= PROXIMITY_CHANNELS) return 0;
    // millis() wraps every ~49 days; uint32 subtraction stays correct across
    // a single wrap, which is all we need for a monotonic elapsed display.
    uint32_t nowMs = millis();
    uint32_t start = _countStartMs[ch];
    return (nowMs >= start) ? (nowMs - start) / 1000UL : 0;
}

// Planned-stop overlap for one channel's current elapsed window, for the OEE
// callers: reset wall-clock derives from elapsed so no new state is needed.
// Unsynced clock or empty window → 0 (legacy elapsed-based A).
uint32_t proximityChannelPlannedStopSec(uint8_t ch) {
    uint32_t elapsed = proximityCountElapsedSec(ch);
    if (elapsed == 0) return 0;
    time_t nowW = time(nullptr);
    if (nowW < 1000000000L) return 0;
    return proximityPlannedStopSec((time_t)(nowW - (time_t)elapsed), nowW);
}

uint8_t proximityChannelCount() {
    return PROXIMITY_CHANNELS;
}

// Board default GPIO for a channel (DI1=GPIO4 ... DI8=GPIO11 on 8DI8DO).
// Used by the web config so the GPIO field is pre-filled and needs no manual entry.
uint8_t proximityDefaultGpioIn(uint8_t ch) {
    static const uint8_t defs[PROXIMITY_MAX_CHANNELS] = {
        PROXIMITY_GPIO_CH0, PROXIMITY_GPIO_CH1, PROXIMITY_GPIO_CH2, PROXIMITY_GPIO_CH3,
        PROXIMITY_GPIO_CH4, PROXIMITY_GPIO_CH5, PROXIMITY_GPIO_CH6, PROXIMITY_GPIO_CH7
    };
    if (ch >= PROXIMITY_MAX_CHANNELS) return 255;
    return defs[ch];
}

// ============================================================================
// Phase 4 — Shift management and RX buffer injection.
// ============================================================================

// Interval between periodic RX-buffer injections of live channel data (ms).
#define PROX_INJECT_INTERVAL_MS  60000UL

// Routes formatted CSV data through the rules engine, monitor, and SD pipeline.
extern void injectDataToRXBuffer(const String& data, const String& source, bool resetInactivity = true);

// Per-channel count baselines (defined above, next to the reset functions that
// restart them; captured at each shift start; main-loop only, not ISR).

// Current active shift index (-1 = undetected since boot).
static int8_t _currentShiftIdx = -1;

// Called by proximity_shift_config.cpp when a new shift config is applied.
void proximityResetShiftDetection() {
    _currentShiftIdx = -1;
    // Slot meanings may have changed: drop the persisted shift baselines so
    // the next detection captures fresh (a same-count slot edit would
    // otherwise restore bases under shifted hour meanings).
    if (littlefsBegin()) LittleFS.remove(PROX_SHIFTBASE_PATH);
}

// Rate-limiter timestamps for shift check, periodic injection, and CSV write.
static uint32_t _lastShiftCheckMs = 0;
static uint32_t _lastInjectMs     = 0;
static uint32_t _lastCSVWriteMs   = 0; // Tracks last CSV write; respects getSdcardFlushInterval().
static uint32_t _lastCntSaveMs    = 0; // Tracks last LittleFS counter save (6 h sparse edge snapshot).



// --- Counter persistence (survive reboot) ---
// Three layers, least-to-most frequent flash wear:
//   1. RTC RAM mirror (_proxRtcSave / _proxRtcRestore) — every inject tick,
//      zero flash writes; the recovery path for soft reboots/OTA/WDT.
//   2. SD CSV tail (_proxRestoreFromSdCsvTail) — absolute tick rows at the
//      configured flush interval; the recovery path for a real power cut.
//   3. LittleFS (saveProximityCounters/loadProximityCounters) — event-driven
//      (shift-end, resets, day rollover, API) + one 6 h sparse edge snapshot.

void saveProximityCounters() {
    if (!littlefsBegin()) return;
    // 8 channels x 8 fields overflow a 1024 pool (proven on-device: the 8th
    // channel serialized as {}). 2048 fits with margin (PSRAM-backed).
    SpiRamJsonDocument doc(2048);
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < PROXIMITY_MAX_CHANNELS; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["in"]   = _ch[i].count_in;
        obj["out"]  = _ch[i].count_out;
        obj["ti"]   = _ch[i].total_in;
        obj["to"]   = _ch[i].total_out;
        obj["run"]  = _ch[i].running_seconds;
        obj["down"] = _ch[i].downtime_seconds;
        obj["good"] = _ch[i].good_parts;
        obj["setup"] = _ch[i].setup_count;
    }
    if (doc.overflowed()) {
        sendVirtualMessage(F("Prox counters save FAILED (JSON pool overflow, file kept)"), ERROR, false, true);
        return;
    }
    String cntTmp = String(PROX_CNT_PATH) + ".tmp";
    File f = LittleFS.open(cntTmp, "w");
    if (!f) {
        sendVirtualMessage(F("Prox counters save FAILED (could not open tmp file)"), ERROR);
        return;
    }
    size_t bytes = serializeJsonPretty(doc, f);
    f.close();
    storNoteLfsWrite(bytes);
    if (bytes > 0) {
        LittleFS.remove(PROX_CNT_PATH);
        LittleFS.rename(cntTmp, PROX_CNT_PATH);
        sendVirtualMessage(F("Prox counters saved"), INFO, false, true);
    } else {
        LittleFS.remove(cntTmp);
        sendVirtualMessage(F("Prox counters save FAILED (wrote 0 bytes)"), ERROR);
    }
}

void loadProximityCounters() {
    if (!littlefsBegin()) return;
    File f = LittleFS.open(PROX_CNT_PATH, "r");
    if (!f) return;
    SpiRamJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err != DeserializationError::Ok) {
        // Loud (not silent): a corrupt counters file used to discard every
        // channel with no trace, restarting all counts at boot.
        sendVirtualMessage(F("Prox counters restore FAILED (corrupt file, starting fresh)"), ERROR, false, true);
        return;
    }

    JsonArray arr = doc.as<JsonArray>();
    uint8_t i = 0;
    for (JsonVariant v : arr) {
        if (i >= PROXIMITY_MAX_CHANNELS) break;
        JsonObject obj = v.as<JsonObject>();
        _ch[i].count_in  = obj["in"]  | 0U;
        _ch[i].count_out = obj["out"] | 0U;
        _ch[i].total_in  = obj["ti"]  | 0U;
        _ch[i].total_out = obj["to"] | 0U;
        // Running-state fields (added later): missing keys keep 0, so files
        // written by older firmware still load.
        _ch[i].running_seconds  = obj["run"]   | 0U;
        _ch[i].downtime_seconds = obj["down"]  | 0U;
        _ch[i].good_parts       = obj["good"]  | 0U;
        _ch[i].setup_count      = obj["setup"] | 0U;
        // Sync rate baseline so ppm starts fresh from the restored count.
        _rateBaseCount[i] = _ch[i].count_in;
        i++;
    }
    if (i > 0) {
        sendVirtualMessage(String(F("Prox counters: restored ")) + String((int)i) + F(" channel(s)"), INFO);
    }
}

// Mirror the live counters into RTC RAM (RTC_SLOW_MEM — SRAM, zero flash wear).
// Refresh cadence: every inject tick (60 s). Writes magic LAST so a crash in
// the middle of a mirror keeps the previous consistent snapshot readable.
static void _proxRtcSave(uint32_t nowMs) {
    time_t raw = time(nullptr);
    uint32_t dayKey = (uint32_t)-1;
    if (raw > 1000) {
        struct tm t, at;
        localtime_r(&raw, &t);
        at = t;
        proximityShiftDayAlignedDate(raw, &at);
        dayKey = (uint32_t)proximityDayKey(at.tm_year, at.tm_yday);
    }
    noInterrupts();
    for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
        s_proxRtc.ch[i].in     = _ch[i].count_in;
        s_proxRtc.ch[i].out    = _ch[i].count_out;
        s_proxRtc.ch[i].total_in  = _ch[i].total_in;
        s_proxRtc.ch[i].total_out = _ch[i].total_out;
        s_proxRtc.ch[i].run    = _ch[i].running_seconds;
        s_proxRtc.ch[i].down   = _ch[i].downtime_seconds;
        s_proxRtc.ch[i].good   = _ch[i].good_parts;
        s_proxRtc.ch[i].setup  = _ch[i].setup_count;
    }
    interrupts();
    s_proxRtc.magic     = PROX_RTC_MAGIC;
    s_proxRtc.version   = PROX_RTC_VERSION;
    s_proxRtc.channels  = PROXIMITY_CHANNELS;
    s_proxRtc.reserved  = 0;
    s_proxRtc.dayKey    = dayKey;
    s_proxRtc.updBootMs = nowMs;
}

// Restore counters from the RTC mirror (survives esp_restart/OTA/WDT/deep
// sleep). Rejects garbage (power-on), version/channel mismatch, and a stale
// mirror from a previous shift-aligned day. Returns true when the live counters
// were (re)seeded from RAM.
static bool _proxRtcRestore() {
    if (s_proxRtc.magic != PROX_RTC_MAGIC) return false;
    if (s_proxRtc.version != PROX_RTC_VERSION) return false;
    if (s_proxRtc.channels != PROXIMITY_CHANNELS) return false;
    time_t raw = time(nullptr);
    if (raw > 1000) {
        struct tm t, at;
        localtime_r(&raw, &t);
        at = t;
        proximityShiftDayAlignedDate(raw, &at);
        int todayKey = proximityDayKey(at.tm_year, at.tm_yday);
        if (s_proxRtc.dayKey != (uint32_t)-1 && (int)s_proxRtc.dayKey != todayKey) return false;
    }
    noInterrupts();
    for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
        _ch[i].count_in        = s_proxRtc.ch[i].in;
        _ch[i].count_out       = s_proxRtc.ch[i].out;
        _ch[i].total_in        = s_proxRtc.ch[i].total_in;
        _ch[i].total_out       = s_proxRtc.ch[i].total_out;
        _ch[i].running_seconds = s_proxRtc.ch[i].run;
        _ch[i].downtime_seconds= s_proxRtc.ch[i].down;
        _ch[i].good_parts      = s_proxRtc.ch[i].good;
        _ch[i].setup_count     = s_proxRtc.ch[i].setup;
        _rateBaseCount[i]      = s_proxRtc.ch[i].in;
    }
    interrupts();
    s_proxFromRtc = true;
    sendVirtualMessage(F("Prox counters: restored from RTC RAM (pre-reboot mirror)"), INFO);
    return true;
}

// Power-cut recovery point: the LAST absolute (tick/shift_boundary) row of
// today's aligned-day PROXIMITY CSV, written at the configured flush interval
// (30m/20m/...). total_in/out and setup are not stored in the CSV — they keep
// the values the sparse LittleFS snapshot restored at boot. Runs once, after
// NTP sync, only when the RTC mirror did not restore (real power cut).
static bool _proxRestoreFromSdCsvTail() {
#if ENABLE_SDCARD==1
    if (!sdcardShouldUseSD()) return false;
    time_t now = time(nullptr);
    if (now < 1000000000L) return false; // NTP not synced — cannot name the daily file.

    struct tm ti;
    localtime_r(&now, &ti);
    struct tm fdate = ti;
    proximityShiftDayAlignedDate(now, &fdate);

    extern ConfigManager configManager;
    String sdName = configManager.getSdcardName();
    if (sdName.isEmpty()) sdName = "SD_LOG";
    char dateSuf[16];
    snprintf(dateSuf, sizeof(dateSuf), "%02d-%02d-%04d",
             fdate.tm_mday, fdate.tm_mon + 1, fdate.tm_year + 1900);
    String filePathS = sd_layout::dayFile(sdName, "PROXIMITY", String(dateSuf), "csv");

    if (!sdMutexLock(pdMS_TO_TICKS(1500))) return false;
    if (!SD_MMC.exists(filePathS.c_str())) { sdMutexUnlock(); return false; }
    File f = SD_MMC.open(filePathS.c_str(), FILE_READ);
    if (!f) { sdMutexUnlock(); return false; }

    uint32_t in[PROXIMITY_MAX_CHANNELS], outV[PROXIMITY_MAX_CHANNELS],
             run[PROXIMITY_MAX_CHANNELS], down[PROXIMITY_MAX_CHANNELS],
             good[PROXIMITY_MAX_CHANNELS];
    bool have = false;
    char lineBuf[700];
    while (f.available()) {
        int len = 0;
        while (f.available() && len < (int)sizeof(lineBuf) - 1) {
            int c = f.read();
            if (c == '\n') break;
            if (c != '\r') lineBuf[len++] = (char)c;
        }
        lineBuf[len] = '\0';
        if (len == 0) continue;
        // Header rows and boot/shift_end rows fail proxCsvParseAbsRow; only the
        // last absolute tick/shift_boundary snapshot lands in the live counters.
        if (proxCsvParseAbsRow(lineBuf, PROXIMITY_CHANNELS, in, outV, run, down, good)) {
            have = true;
        }
    }
    f.close();
    sdMutexUnlock();
    if (!have) return false;

    noInterrupts();
    for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
        _ch[i].count_in        = in[i];
        _ch[i].count_out       = outV[i];
        _ch[i].running_seconds = run[i];
        _ch[i].downtime_seconds= down[i];
        _ch[i].good_parts      = good[i];
        _rateBaseCount[i]      = in[i];
    }
    interrupts();
    sendVirtualMessage(F("Prox counters: restored from SD CSV tail (power-cut point)"), INFO);
    return true;
#else
    return false;
#endif
}

// Persist the day baselines (+ day key) so the day-so-far OEE and donut survive
// a reboot like the charts do (which backfill from SD). Atomic write (.tmp).
// Called on every shift-day-boundary capture and throttled every PROX_DAYBASE_SAVE_MS.
void saveProximityDayBaselines() {
    if (!littlefsBegin()) return;
    SpiRamJsonDocument doc(2048);
    doc["dayKey"] = _lastDayKey;
    JsonArray arr = doc.createNestedArray("ch");
    for (uint8_t i = 0; i < PROXIMITY_MAX_CHANNELS; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["in"]   = _dayCountInBase[i];
        obj["run"]  = _dayRunSecBase[i];
        obj["down"] = _dayDowntSecBase[i];
        obj["good"] = _dayGoodPartsBase[i];
        obj["accIn"]   = _dayAccumIn[i];
        obj["accRun"]  = _dayAccumRun[i];
        obj["accDown"] = _dayAccumDownt[i];
        obj["accGood"] = _dayAccumGood[i];
    }
    if (doc.overflowed()) {
        sendVirtualMessage(F("Prox daybase save FAILED (JSON pool overflow, file kept)"), ERROR, false, true);
        return;
    }
    String tmpPath = String(PROX_DAYBASE_PATH) + ".tmp";
    File f = LittleFS.open(tmpPath, "w");
    if (!f) return;
    size_t bytes = serializeJson(doc, f);
    f.close();
    storNoteLfsWrite(bytes);
    if (bytes > 0) {
        LittleFS.remove(PROX_DAYBASE_PATH);
        LittleFS.rename(tmpPath, PROX_DAYBASE_PATH);
        _lastDaySaveMs = millis();
    } else {
        LittleFS.remove(tmpPath);
    }
}

// Restore day baselines saved before last reboot. Only applies when the saved
// day key matches today (RTC synced); otherwise the first _proxDayTick after
// sync captures fresh baselines. Missing keys keep 0 (oldForward-compat).
void loadProximityDayBaselines() {
    _lastDaySaveMs = millis();
    if (!littlefsBegin()) return;
    File f = LittleFS.open(PROX_DAYBASE_PATH, "r");
    if (!f) return;
    SpiRamJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err != DeserializationError::Ok) {
        sendVirtualMessage(F("Prox daybase restore FAILED (corrupt file, starting fresh)"), ERROR, false, true);
        return;
    }
    time_t raw = time(nullptr);
    if (raw <= 1000) return; // RTC not synced yet — capture fresh on first sync.
    struct tm t;
    localtime_r(&raw, &t);
    struct tm at = t;
    proximityShiftDayAlignedDate(raw, &at);
    int todayKey = proximityDayKey(at.tm_year, at.tm_yday);
    int savedKey = doc["dayKey"] | -1;
    if (savedKey != todayKey) return; // Yesterday's baselines — start fresh.
    JsonArray arr = doc["ch"].as<JsonArray>();
    if (arr.isNull()) return; // Old file without channels — start fresh (forward-compat).
    uint8_t i = 0;
    for (JsonVariant v : arr) {
        if (i >= PROXIMITY_MAX_CHANNELS) break;
        JsonObject obj = v.as<JsonObject>();
        // Missing keys keep 0, so files from older firmware still load.
        _dayCountInBase[i]   = obj["in"]   | 0U;
        _dayRunSecBase[i]    = obj["run"]  | 0U;
        _dayDowntSecBase[i]  = obj["down"] | 0U;
        _dayGoodPartsBase[i] = obj["good"] | 0U;
        _dayAccumIn[i]       = obj["accIn"]   | 0U;
        _dayAccumRun[i]      = obj["accRun"]  | 0U;
        _dayAccumDownt[i]    = obj["accDown"] | 0U;
        _dayAccumGood[i]     = obj["accGood"] | 0U;
        i++;
    }
    if (i > 0) {
        _lastDayKey = todayKey;
        sendVirtualMessage(F("Prox: day baselines restored (day OEE/donut continue since the day boundary)"), INFO);
    }
}

// Persist the shift baselines (+ shift index, day key, slot count) so a
// mid-shift reboot keeps the shift window instead of restarting it at boot.
// Atomic write (.tmp). Called on every shift-baseline capture (shift change),
// on resets/OEE-reset (bases zeroed/re-based) — baselines are write-once per
// shift, so no throttling is needed.
void saveProximityShiftBaselines() {
    if (!littlefsBegin()) return;
    time_t raw = time(nullptr);
    int dayKey = -1;
    if (raw > 1000) {
        struct tm t;
        localtime_r(&raw, &t);
        struct tm at = t;
        proximityShiftDayAlignedDate(raw, &at);
        dayKey = proximityDayKey(at.tm_year, at.tm_yday);
    }
    ProximityShiftConfig sc = proximityGetShiftConfig();
    SpiRamJsonDocument doc(2048);
    doc["shift"]  = (int)_currentShiftIdx;
    doc["dayKey"] = dayKey;
    doc["nSlots"] = (int)sc.nSlots;
    JsonArray arr = doc.createNestedArray("ch");
    for (uint8_t i = 0; i < PROXIMITY_MAX_CHANNELS; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["in"]    = _shiftCountInBase[i];
        obj["out"]   = _shiftCountOutBase[i];
        obj["run"]   = _shiftRunSecBase[i];
        obj["down"]  = _shiftDowntSecBase[i];
        obj["good"]  = _shiftGoodPartsBase[i];
        obj["setup"] = _shiftSetupBase[i];
    }
    if (doc.overflowed()) {
        sendVirtualMessage(F("Prox shiftbase save FAILED (JSON pool overflow, file kept)"), ERROR, false, true);
        return;
    }
    String tmpPath = String(PROX_SHIFTBASE_PATH) + ".tmp";
    File f = LittleFS.open(tmpPath, "w");
    if (!f) return;
    size_t bytes = serializeJson(doc, f);
    f.close();
    storNoteLfsWrite(bytes);
    if (bytes > 0) {
        LittleFS.remove(PROX_SHIFTBASE_PATH);
        LittleFS.rename(tmpPath, PROX_SHIFTBASE_PATH);
    } else {
        LittleFS.remove(tmpPath);
    }
}

// Restore shift baselines saved before last reboot. Applies ONLY when the
// saved shift is still the active one (same slot index, same aligned day,
// same slot count) and the restored live counters are at/above the saved
// bases (guards a stale counters file). Returns true when applied — the
// caller then skips the first-detection fresh capture. Missing keys keep 0
// (forward-compat: a file without "ch" simply does not apply).
static bool _proxRestoreShiftBaselines(int8_t detectedShift) {
    if (!littlefsBegin()) return false;
    File f = LittleFS.open(PROX_SHIFTBASE_PATH, "r");
    if (!f) return false;
    SpiRamJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err != DeserializationError::Ok) {
        sendVirtualMessage(F("Prox shiftbase restore FAILED (corrupt file, starting fresh)"), ERROR, false, true);
        return false;
    }
    time_t raw = time(nullptr);
    if (raw <= 1000) return false; // RTC not synced yet — capture fresh.
    struct tm t;
    localtime_r(&raw, &t);
    struct tm at = t;
    proximityShiftDayAlignedDate(raw, &at);
    int todayKey = proximityDayKey(at.tm_year, at.tm_yday);
    int savedShift = doc["shift"] | -1;
    int savedKey   = doc["dayKey"] | -1;
    ProximityShiftConfig sc = proximityGetShiftConfig();
    int savedNSlots = doc["nSlots"] | -1;
    if (savedShift < 0 || savedShift >= (int)sc.nSlots) return false;
    if (savedNSlots != (int)sc.nSlots) return false; // slots edited — restart window
    if (savedKey != todayKey) return false;          // another day — restart window
    if (savedShift != (int)detectedShift) return false; // shift already changed — restart
    JsonArray arr = doc["ch"].as<JsonArray>();
    if (arr.isNull()) return false;
    // Stage into locals first: only commit when every channel passes the
    // stale-counters guard below.
    uint32_t bIn[PROXIMITY_MAX_CHANNELS], bOut[PROXIMITY_MAX_CHANNELS];
    uint32_t bRun[PROXIMITY_MAX_CHANNELS], bDown[PROXIMITY_MAX_CHANNELS];
    uint32_t bGood[PROXIMITY_MAX_CHANNELS], bSetup[PROXIMITY_MAX_CHANNELS];
    uint8_t i = 0;
    for (JsonVariant v : arr) {
        if (i >= PROXIMITY_MAX_CHANNELS) break;
        JsonObject obj = v.as<JsonObject>();
        bIn[i]    = obj["in"]    | 0U;
        bOut[i]   = obj["out"]   | 0U;
        bRun[i]   = obj["run"]   | 0U;
        bDown[i]  = obj["down"]  | 0U;
        bGood[i]  = obj["good"]  | 0U;
        bSetup[i] = obj["setup"] | 0U;
        i++;
    }
    if (i == 0) return false;
    noInterrupts();
    for (uint8_t k = 0; k < PROXIMITY_MAX_CHANNELS; k++) {
        // Live counters restored from flash must be at/above their own shift
        // bases; below means a stale counters file (saved before the bases
        // were captured, e.g. failed post-reset save) — do not apply.
        if (_ch[k].count_in < bIn[k] || _ch[k].running_seconds < bRun[k]) {
            interrupts();
            return false;
        }
    }
    for (uint8_t k = 0; k < PROXIMITY_MAX_CHANNELS; k++) {
        _shiftCountInBase[k]    = bIn[k];
        _shiftCountOutBase[k]   = bOut[k];
        _shiftRunSecBase[k]     = bRun[k];
        _shiftDowntSecBase[k]   = bDown[k];
        _shiftGoodPartsBase[k]  = bGood[k];
        _shiftSetupBase[k]      = bSetup[k];
    }
    _currentShiftIdx = (int8_t)savedShift;
    interrupts();
    sendVirtualMessage(F("Prox: shift baselines restored (shift totals/OEE continue across reboot)"), INFO);
    return true;
}

// --- Shift detection ---

// Returns the 0-based index of the shift that is currently active.
// Logic: latest slot whose start <= now. Before the first daily slot → last slot (night shift).
static int8_t _detectCurrentShift(const struct tm* t) {
    ProximityShiftConfig sc = proximityGetShiftConfig();
    if (sc.nSlots == 0) return -1;
    int cur  = t->tm_hour * 60 + t->tm_min;
    int best = -1, bestMin = -1;
    for (int i = 0; i < sc.nSlots; i++) {
        int sm = sc.slots[i].hour * 60 + sc.slots[i].minute;
        if (sm <= cur && sm > bestMin) { bestMin = sm; best = i; }
    }
    if (best == -1) best = sc.nSlots - 1; // Before first slot → night shift.
    return (int8_t)best;
}

// --- RX buffer injection (rules engine, monitor, SD pipeline) ---

// Appends a CSV row to /{sdcardName}/PROXIMITY_DD-MM-YYYY/PROXIMITY_DD-MM-YYYY.csv on the SD card.
// type:     "tick" for periodic absolute snapshots; "shift_end" for per-shift deltas;
//           "shift_boundary" for the absolute snapshot written at the exact shift transition.
// shiftNum: 1-based shift number (shift_end rows only); -1 leaves the column empty.
// chIn/chOut: per-channel IN/OUT counts. chPpm: per-channel rate in pulses/min.
// Daily CSV row layout (append-only, append-to-the-right compatible):
//   timestamp,type,shift[,ch<i>_in,ch<i>_out,ch<i>_ppm,ch<i>_run,ch<i>_down,ch<i>_good]*
// Ticks and shift_boundary hold ABSOLUTE snapshots; shift_end holds per-shift
// DELTAS (same window as its in/out neighbors); boot holds the live snapshot
// at boot with the ESP reset-reason code in the shift slot (0 UNKNOWN,
// 1 POWERON, 2 EXT, 3 SW, 4 PANIC, 5 INT_WDT, 6 TASK_WDT, 7 WDT,
// 8 DEEPSLEEP, 9 BROWNOUT, 10 SDIO — same codes as log.cpp). Readers parse fixed
// columns from the LEFT, so old files (3 cols/channel) and boot rows keep
// working; the hourly chart only counts tick/shift_boundary rows.
static void _proxWriteCSVRow(const char* type, int8_t shiftNum,
                               const uint32_t* chIn, const uint32_t* chOut, const float* chPpm,
                               const uint32_t* chRun, const uint32_t* chDown, const uint32_t* chGood,
                               time_t nowOverride) {
#if ENABLE_SDCARD==1
    // Dual sink: SD card when mounted, else LittleFS /oee mirror (same bytes,
    // same layout — see flash_fallback_store.h). Readers and EMAILDAY/SENSORDAY
    // resolve both transparently.
    const bool useFlash = !sdcardShouldUseSD();
    // Shift-boundary rows (shift_end/shift_boundary) pass the pre-boundary
    // context time so they land in the ENDED shift's day folder; periodic
    // ticks pass 0 and keep wall-clock time.
    time_t now = (nowOverride > 1000000000L) ? nowOverride : time(nullptr);
    if (now < 1000000000L) return; // NTP not synced — cannot name the daily file.

    struct tm ti;
    localtime_r(&now, &ti);

    // Folder/file date uses the shift-aligned day boundary (Waveshare pack);
    // the per-row timestamp below still uses the real wall-clock time.
    struct tm fdate = ti;
    proximityShiftDayAlignedDate(now, &fdate);

    extern ConfigManager configManager;
    String sdName = configManager.getSdcardName();
    if (sdName.isEmpty()) sdName = "SD_LOG";

    char dateSuf[16];
    snprintf(dateSuf, sizeof(dateSuf), "%02d-%02d-%04d",
             fdate.tm_mday, fdate.tm_mon + 1, fdate.tm_year + 1900);

    // Build the data row once (shared by both sinks).
    char row[512];
    {
        char *p = row;
        int  rem = (int)sizeof(row);
        int  n   = snprintf(p, rem, "%04d-%02d-%02d %02d:%02d:%02d,%s,",
                            ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                            ti.tm_hour, ti.tm_min, ti.tm_sec, type);
        p += n; rem -= n;
        if (shiftNum >= 0 && rem > 0) {
            n = snprintf(p, rem, "%d", (int)shiftNum);
            p += n; rem -= n;
        }
        for (uint8_t i = 0; i < PROXIMITY_CHANNELS && rem > 2; i++) {
            n = snprintf(p, rem, ",%lu,%lu,%.1f,%lu,%lu,%lu",
                         (unsigned long)chIn[i], (unsigned long)chOut[i], (double)chPpm[i],
                         (unsigned long)chRun[i], (unsigned long)chDown[i], (unsigned long)chGood[i]);
            p += n; rem -= n;
        }
    }

    if (useFlash) {
        // Same header columns as the SD path below (keep in sync).
        char hdr[768];
        {
            char *p = hdr;
            int  rem = (int)sizeof(hdr);
            int  n   = snprintf(p, rem, "timestamp,type,shift");
            p += n; rem -= n;
            for (uint8_t i = 0; i < PROXIMITY_CHANNELS && rem > 64; i++) {
                n = snprintf(p, rem, ",ch%u_in,ch%u_out,ch%u_ppm,ch%u_run,ch%u_down,ch%u_good",
                             (unsigned)i, (unsigned)i, (unsigned)i, (unsigned)i, (unsigned)i, (unsigned)i);
                p += n; rem -= n;
            }
        }
        String fbName = String("PROXIMITY_") + dateSuf + ".csv";
        flashOeeAppend(fbName, hdr, row);
        return;
    }

    String rootDir   = String("/") + sdName;
    String filePathS = sd_layout::dayFile(sdName, "PROXIMITY", String(dateSuf), "csv");

    if (!SD_MMC.exists(rootDir.c_str()))   SD_MMC.mkdir(rootDir.c_str());

    // FTP holds the SD exclusively for its whole upload cycle (HISTORIC #57):
    // a short lock timeout then skip this row rather than interleave with the
    // SDMMC scan/stream (that concurrency is what crashed the board). Once the
    // cycle ends the next interval resumes the appends. A long cycle leaves a
    // hole in the hourly history (the chart spreads the surviving delta), so
    // report it instead of dropping it silently — throttled to one per hour.
    if (!sdMutexLock(pdMS_TO_TICKS(1500))) {
        static uint32_t s_lastSkipWarnMs = 0;
        const uint32_t nowSkipMs = millis();
        if (s_lastSkipWarnMs == 0 || (nowSkipMs - s_lastSkipWarnMs) >= 3600000UL) {
            s_lastSkipWarnMs = nowSkipMs;
            sendVirtualMessage(String(F("Prox: CSV row skipped (SD busy, FTP cycle) — hourly history gap: ")) +
                               filePathS, WARN, false, true);
        }
        return;
    }

    File f = SD_MMC.open(filePathS.c_str(), FILE_APPEND);
    if (!f) { sdMutexUnlock(); return; }

    // Write CSV header on the first write to this file.
    if (f.size() == 0) {
        f.print(F("timestamp,type,shift"));
        for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
            char col[48];
            snprintf(col, sizeof(col), ",ch%u_in,ch%u_out,ch%u_ppm,ch%u_run,ch%u_down,ch%u_good",
                     (unsigned)i, (unsigned)i, (unsigned)i, (unsigned)i, (unsigned)i, (unsigned)i);
            f.print(col);
        }
        f.println();
    }

    // Write the pre-built data row (shared builder above — same bytes).
    size_t rowStart = f.size();
    f.print(row);
    f.println();
    size_t rowEnd = f.size();
    f.close();
    storNoteSdWrite(rowEnd > rowStart ? rowEnd - rowStart : 0);
    sdMutexUnlock();
#endif
}

// Injects a shift-end summary line when a shift boundary is detected.
// Format: prox_shift_end,<N>,prox_ch<i>_in,<delta>,prox_ch<i>_out,<delta>,prox_ch<i>_ppm,<rate>,...
static void _proxInjectShiftMarker(int8_t endedShift, time_t ctxNow) {
    uint32_t chIn[PROXIMITY_CHANNELS], chOut[PROXIMITY_CHANNELS];
    float    chPpm[PROXIMITY_CHANNELS];
    uint32_t chRun[PROXIMITY_CHANNELS], chDown[PROXIMITY_CHANNELS], chGood[PROXIMITY_CHANNELS];
    ProximityShiftConfig scf = proximityGetShiftConfig();

    for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
        noInterrupts();
        ProximityChannel ch = _ch[i];
        interrupts();
        chIn[i]  = (ch.count_in  >= _shiftCountInBase[i])  ? ch.count_in  - _shiftCountInBase[i]  : 0;
        chOut[i] = (ch.count_out >= _shiftCountOutBase[i]) ? ch.count_out - _shiftCountOutBase[i] : 0;
        chPpm[i] = ch.rate_ppm;
        chRun[i]  = (ch.running_seconds >= _shiftRunSecBase[i])
            ? ch.running_seconds - _shiftRunSecBase[i] : 0;
        chDown[i] = (ch.downtime_seconds >= _shiftDowntSecBase[i])
            ? ch.downtime_seconds - _shiftDowntSecBase[i] : 0;
        chGood[i] = (ch.good_parts >= _shiftGoodPartsBase[i])
            ? ch.good_parts - _shiftGoodPartsBase[i] : 0;
    }
    // Write shift-end CSV row and inject into RX buffer for rules engine alerts.
    // The turn number is rotated so the turn starting at the day boundary is
    // Turno 1 (proximityShiftTurnNumber), matching the frontend shift label.
    int turnNum = proximityShiftTurnNumber(scf, endedShift);
    _proxWriteCSVRow("shift_end", turnNum, chIn, chOut, chPpm, chRun, chDown, chGood, ctxNow);
    String rxLine = F("prox_shift_end,");
    rxLine += turnNum;
    for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
        noInterrupts();
        uint32_t runSec = (_ch[i].running_seconds >= _shiftRunSecBase[i])
            ? _ch[i].running_seconds - _shiftRunSecBase[i] : 0;
        uint32_t downtSec = (_ch[i].downtime_seconds >= _shiftDowntSecBase[i])
            ? _ch[i].downtime_seconds - _shiftDowntSecBase[i] : 0;
        uint32_t goodP = (_ch[i].good_parts >= _shiftGoodPartsBase[i])
            ? _ch[i].good_parts - _shiftGoodPartsBase[i] : 0;
        float tgtPpm = _ch[i].target_ppm;
        interrupts();
        rxLine += F(",prox_ch"); rxLine += (unsigned)i;
        rxLine += F("_delta_in,");  rxLine += (unsigned long)chIn[i];
        rxLine += F(",prox_ch"); rxLine += (unsigned)i;
        rxLine += F("_delta_out,"); rxLine += (unsigned long)chOut[i];
        rxLine += F(",prox_ch"); rxLine += (unsigned)i;
        rxLine += F("_run_sec,"); rxLine += (unsigned long)runSec;
        rxLine += F(",prox_ch"); rxLine += (unsigned)i;
        rxLine += F("_downt_sec,"); rxLine += (unsigned long)downtSec;
        rxLine += F(",prox_ch"); rxLine += (unsigned)i;
        rxLine += F("_good,"); rxLine += (unsigned long)goodP;
        rxLine += F(",prox_ch"); rxLine += (unsigned)i;
        rxLine += F("_target_ppm,"); rxLine += String(tgtPpm, 1);
    }
    injectDataToRXBuffer(rxLine, "PROX_SHIFT", /*resetInactivity=*/false);

    // Inject a second formatted line with computed OEE% for email-ready summary.
    // Format: prox_oee,DI1:OEE=85.3%A=85.8%P=100.0%Q=99.4%,DI2:...
    String oeeLine = F("prox_oee,");
    for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
        noInterrupts();
        uint32_t runSec = (_ch[i].running_seconds >= _shiftRunSecBase[i])
            ? _ch[i].running_seconds - _shiftRunSecBase[i] : 0;
        uint32_t goodP = (_ch[i].good_parts >= _shiftGoodPartsBase[i])
            ? _ch[i].good_parts - _shiftGoodPartsBase[i] : 0;
        uint32_t cIn = chIn[i];
        float tgtPpm = _ch[i].target_ppm;
        uint8_t sch = _ch[i].scrap_ch;
        uint32_t scrapIn = 0;
        if (sch < PROXIMITY_CHANNELS && sch != i) {
            scrapIn = (_ch[sch].count_in >= _shiftCountInBase[sch])
                ? _ch[sch].count_in - _shiftCountInBase[sch] : 0;
        }
        interrupts();
        uint32_t effectiveGood = (scrapIn > 0 && cIn > scrapIn) ? (cIn - scrapIn) : goodP;
        // Wall-clock shift window first (reboot-immune); millis fallback when
        // the clock is unsynced, shifts are off, or the shift just started.
        uint32_t shElapsed = proximityShiftElapsedSec();
        uint32_t shPlanned;
        if (shElapsed == 0) {
            shElapsed = proximityCountElapsedSec(i);
            shPlanned = proximityChannelPlannedStopSec(i);
        } else {
            shPlanned = proximityShiftPlannedStopSec();
        }
        ProximityOeeResult r = proximityComputeOee(
            runSec, shElapsed,
            cIn, effectiveGood, tgtPpm, shPlanned);
        if (i > 0) oeeLine += ',';
        oeeLine += F("DI"); oeeLine += (unsigned)(i + 1);
        oeeLine += F(":OEE="); oeeLine += String(r.oee, 1); oeeLine += '%';
        oeeLine += F(" A="); oeeLine += String(r.availability, 1); oeeLine += '%';
        oeeLine += F(" P="); oeeLine += String(r.performance, 1); oeeLine += '%';
        oeeLine += F(" Q="); oeeLine += String(r.quality, 1); oeeLine += '%';
    }
    injectDataToRXBuffer(oeeLine, "PROX_OEE", /*resetInactivity=*/false);
}

// Injects live channel counters into the RX buffer every PROX_INJECT_INTERVAL_MS.
// CSV writes happen at the configured microSD flush interval (getSdcardFlushInterval()),
// not necessarily every inject cycle.
// Format: prox_ch<i>_in,<total>,prox_ch<i>_out,<total>,prox_ch<i>_ppm,<rate>,...
static void _proxInjectTick(uint32_t nowMs) {
    if (nowMs - _lastInjectMs < PROX_INJECT_INTERVAL_MS) return;
    _lastInjectMs = nowMs;

    // Power-cut recovery point: after NTP sync, when the RTC mirror did NOT
    // restore at boot, pull the last absolute CSV snapshot from the microSD.
    // Runs once, BEFORE the counters are captured below, so the boot row and
    // every subsequent output already reflect the restored values. Pre-sync
    // ticks retry silently until the clock is valid.
    if (!s_proxFromRtc) {
        if (time(nullptr) > 1000000000L) {
            static bool s_sdRestoreTried = false;
            if (!s_sdRestoreTried) {
                s_sdRestoreTried = true;
                _proxRestoreFromSdCsvTail();
            }
        }
    }

    uint32_t chIn[PROXIMITY_CHANNELS], chOut[PROXIMITY_CHANNELS];
    float    chPpm[PROXIMITY_CHANNELS];
    uint32_t chRun[PROXIMITY_CHANNELS], chDown[PROXIMITY_CHANNELS], chGood[PROXIMITY_CHANNELS];

    String line;
    line.reserve((uint8_t)PROXIMITY_CHANNELS * 48);
    for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
        noInterrupts();
        ProximityChannel ch = _ch[i];
        interrupts();
        chIn[i]  = ch.count_in;
        chOut[i] = ch.count_out;
        chPpm[i] = ch.rate_ppm;
        chRun[i]  = ch.running_seconds;
        chDown[i] = ch.downtime_seconds;
        chGood[i] = ch.good_parts;
        if (i > 0) line += ',';
        line += F("prox_ch"); line += (unsigned)i; line += F("_in,");  line += (unsigned long)chIn[i];
        line += F(",prox_ch"); line += (unsigned)i; line += F("_out,"); line += (unsigned long)chOut[i];
        line += F(",prox_ch"); line += (unsigned)i; line += F("_ppm,"); line += String(chPpm[i], 1);
    }
    // One-shot boot marker row (type "boot", ESP reset-reason code in the shift
    // slot, live snapshot in the channels): anchors post-boot archaeology even
    // when the machine is idle and no tick rows exist yet. Emitted on the first
    // call with a synced clock; wall time reconstructed from boot millis
    // (within ~1 min, the inject rate-limit skew). Skipped by the hourly chart
    // parser and the shift-history list like any non-tick row.
    {
        static bool s_bootRowDone = false;
        static uint32_t s_bootMs = 0;
        if (s_bootMs == 0) s_bootMs = nowMs;
        time_t nowSec = time(nullptr);
        if (!s_bootRowDone && nowSec > 1000000000L) {
            uint32_t gapSec = (millis() >= s_bootMs) ? (millis() - s_bootMs) / 1000UL : 0;
            time_t bootWall = nowSec;
            if ((time_t)gapSec < nowSec) bootWall = nowSec - (time_t)gapSec;
            _proxWriteCSVRow("boot", (int8_t)esp_reset_reason(),
                             chIn, chOut, chPpm, chRun, chDown, chGood, bootWall);
            s_bootRowDone = true;
        }
    }
    // Check activity before injecting into the RX buffer.
    // Skip injection entirely when all counters are zero — avoids polluting the buffer
    // with irrelevant all-zero rows when no sensor is physically connected.
    bool hasActivity = false;
    for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
        if (chIn[i] > 0 || chOut[i] > 0) { hasActivity = true; break; }
    }
    if (hasActivity) {
        // FIX (issues #7 — Inactivity Alerts): periodic status injection must NOT
        // reset the inactivity timer. It was called with the default
        // resetInactivity=true, so as long as any counter was >0 (always true
        // after the first production pulse) the timer was reset every
        // PROX_INJECT_INTERVAL_MS (60s) and the alert never fired — the machine
        // could sit idle for hours with no alert. Only a real production pulse
        // (handled in proximityTick's drain block) may reset the timer.
        injectDataToRXBuffer(line, "PROXIMITY", /*resetInactivity=*/false);
    }

    if (hasActivity) {
        // CSV write is rate-limited to the configured microSD flush interval.
        // Default is 30m; can be changed on the MicroSD config page.
        // Early writes happen on shift-end (command-driven) — never on every inject cycle.
        extern ConfigManager configManager;
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_WAVESHARE)
        // Waveshare: align CSV writes to fixed clock boundaries (00:00, 00:30,
        // 01:00, ...) so rows land on predictable timestamps for easy analysis.
        // If the device reboots at 10:35, the next write is at 11:00, not 11:05.
        if (configManager.getSdcardFlushInterval() == "30m") {
            time_t nowSec = time(nullptr);
            if (nowSec > 1000000000L) {  // NTP synced
                static time_t s_nextCsvAligned = 0;
                if (s_nextCsvAligned == 0 || nowSec >= s_nextCsvAligned) {
                    _proxWriteCSVRow("tick", -1, chIn, chOut, chPpm, chRun, chDown, chGood);
                    s_nextCsvAligned = ((nowSec / 1800L) + 1) * 1800L;
                }
            }
        } else {
            unsigned long csvIntervalMs = intervalToMs(stringToInterval(configManager.getSdcardFlushInterval()));
            if ((nowMs - _lastCSVWriteMs) >= csvIntervalMs) {
                _proxWriteCSVRow("tick", -1, chIn, chOut, chPpm, chRun, chDown, chGood);
                _lastCSVWriteMs = nowMs;
            }
        }
#else
        unsigned long csvIntervalMs = intervalToMs(stringToInterval(configManager.getSdcardFlushInterval()));
        if ((nowMs - _lastCSVWriteMs) >= csvIntervalMs) {
            _proxWriteCSVRow("tick", -1, chIn, chOut, chPpm, chRun, chDown, chGood);
            _lastCSVWriteMs = nowMs;
        }
#endif
    }

    // LittleFS counter snapshot is now SPARSE (edge only): 6 h guard plus the
    // event-driven saves (shift-end, resets, day rollover, API + reset-all).
    // Soft-reboot recovery comes from the RTC RAM mirror, power-cut from the SD
    // CSV tail — periodic flash writes add nothing but wear.
    const uint32_t CNT_SAVE_INTERVAL_MS = 6UL * 3600UL * 1000UL; // 6 hours
    if ((nowMs - _lastCntSaveMs) >= CNT_SAVE_INTERVAL_MS) {
        saveProximityCounters();
        _lastCntSaveMs = nowMs;
    }

    // RTC RAM mirror (RTC_SLOW_MEM, zero flash wear) — refreshed every inject
    // tick so a soft reboot/OTA/WDT/deep sleep loses nothing.
    _proxRtcSave(nowMs);

#if ENABLE_PROXIMITY_CHARTS==1
    // Only feed the chart accumulator when there is real activity — same guard as the CSV.
    // This prevents the .chart file from being created and filled with all-zero records
    // before any sensor is physically connected.
    if (hasActivity) {
        extern void proximityChartAddTick(uint8_t count, const uint32_t* chIn, const uint32_t* chOut, const float* chPpm);
        proximityChartAddTick(PROXIMITY_CHANNELS, chIn, chOut, chPpm);
    }
#endif
}

// --- Shift tick: called every loop iteration from proximityTick() ---

// True when hour h is one of the chart window columns [startHour, startHour+durHours).
static bool _proxHourInWindow(int h, int startHour, int durHours) {
    if (h < 0 || h > 23 || durHours <= 0) return false;
    for (int k = 0; k < durHours && k < 24; k++) {
        if ((startHour + k) % 24 == h) return true;
    }
    return false;
}

// Reads a PROXIMITY daily CSV and attributes each ch0_in delta to the hour of the
// PREVIOUS sample, filling hourVal[24] (-1 = no sample for that hour) and the
// chart total (sum of hourVal over the shift window). Returns false on open/read
// failure (no usable data). Shared by the ASCII chart and the weekly OEE CSV.
static bool _proxReadHourlyCounts(const String& csvPath, int startHour, int durHours,
                                  long hourVal[24], uint32_t* chartTotalOut) {
#if ENABLE_SDCARD==1
    // Source 1: SD card. FTP holds the SD exclusively during its cycle
    // (HISTORIC #57): short lock timeout, then fail the read (the caller
    // omits the chart, never wedging the SDMMC host).
    char* buf = nullptr;
    size_t fileSize = 0;
    if (!sdMutexLock(pdMS_TO_TICKS(1500))) return false;
    {
        File f;
        if (SD_MMC.exists(csvPath.c_str())) f = SD_MMC.open(csvPath.c_str(), FILE_READ);
        if (f) {
            // Read the whole CSV into a single PSRAM buffer (one bulk SD read,
            // then parse in memory). Falls back to internal RAM when PSRAM is
            // unavailable.
            size_t sz = (size_t)f.size();
            if (sz > 0 && sz <= 200000UL) {
                char* b = (char*)heap_caps_malloc(sz + 1,
                    psramFound() ? MALLOC_CAP_SPIRAM : (MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
                if (b) {
                    size_t rd = f.read((uint8_t*)b, sz);
                    f.close();
                    sdMutexUnlock();
                    if (rd == sz) { b[sz] = '\0'; buf = b; fileSize = sz; }
                    else heap_caps_free(b);
                } else { f.close(); sdMutexUnlock(); }
            } else { f.close(); sdMutexUnlock(); }
        } else { sdMutexUnlock(); }
    }
    // Source 2: LittleFS /oee mirror (same layout) when SD is not the storage.
    if (!buf && !sdcardShouldUseSD()) {
        String flashPath;
        if (flashFbFindFile(csvPath, flashPath)) {
            File ff = LittleFS.open(flashPath.c_str(), FILE_READ);
            if (ff) {
                size_t sz = (size_t)ff.size();
                if (sz > 0 && sz <= 200000UL) {
                    char* b = (char*)heap_caps_malloc(sz + 1,
                        psramFound() ? MALLOC_CAP_SPIRAM : (MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
                    if (b) {
                        size_t rd = ff.read((uint8_t*)b, sz);
                        ff.close();
                        if (rd == sz) { b[sz] = '\0'; buf = b; fileSize = sz; }
                        else heap_caps_free(b);
                    } else { ff.close(); }
                } else { ff.close(); }
            }
        }
    }
    if (!buf) return false;

    long prevIn = -1;
    int  prevHour = -1;
    int  prevMin = -1;      // minute of the previous sample (gap-aware attribution)
    long prevPrevIn = -1;   // ch0 of the tick BEFORE the last one (for closing the last hour)
    int  prevPrevHour = -1; // hour of the tick BEFORE the last one
    long lastCh0 = -1;   // ch0 of the most recent tick (for closing the last hour)
    int  lastHour = -1;  // hour of the most recent tick
    long boundaryDelta = -1;  // delta that crosses into lastShiftHour (for double-tick fix)
    int  boundaryFromHour = -1; // hour the boundary delta was attributed to
    bool lastPairSpread = false; // last pair spanned >1 bucket (delta already distributed)
    int lastShiftHour = (startHour + durHours - 1) % 24;
    char* line = buf;
    while (line && *line) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        // Trim leading whitespace (none expected, but safe).
        char* p = line;
        while (*p == ' ' || *p == '\r') p++;
        if (*p) {
            // timestamp,type,shift,ch0_in,...
            char* c1 = strchr(p, ',');
            if (c1 && c1 - p >= 14) {
                char* c2 = strchr(c1 + 1, ',');
                if (c2) {
                    char* c3 = strchr(c2 + 1, ',');
                    if (c3) {
                        char* c4 = strchr(c3 + 1, ',');
                        if (c4) {
                            size_t typeLen = (size_t)(c2 - (c1 + 1));
                            bool isTick = (typeLen == 4) && strncmp(c1 + 1, "tick", 4) == 0;
                            bool isBoundary = (typeLen == 14) && strncmp(c1 + 1, "shift_boundary", 14) == 0;
                            if (isTick || isBoundary) {
                                long ch0 = strtol(c3 + 1, nullptr, 10);
                                int hour = (p[11] - '0') * 10 + (p[12] - '0');
                                int minute = (c1 - p >= 16) ? ((p[14] - '0') * 10 + (p[15] - '0')) : 0;
                                if (minute < 0 || minute > 59) minute = 0;
                                lastPairSpread = false;
                                if (hour >= 0 && hour <= 23 && prevIn >= 0 && prevHour >= 0) {
                                    long delta = ch0 - prevIn;
                                    int attrHour = prevHour;
                                    if (delta < 0) {
                                        // Counter reset since the previous row (shift
                                        // auto-reset at the boundary, manual reset, or
                                        // reboot without counter restore): ch0 is the
                                        // production since the reset. Attribute it to
                                        // the previous row's hour when charted, else
                                        // to the current row's hour, so the first
                                        // partial hour after a reset is counted
                                        // instead of silently dropped. A reset flush
                                        // is already in its final hour: it is NOT
                                        // recorded for the close-last-hour move below
                                        // (hourVal[prevHour] never held it).
                                        delta = ch0;
                                        attrHour = _proxHourInWindow(prevHour, startHour, durHours)
                                            ? prevHour : hour;
                                        if (hourVal[attrHour] < 0) hourVal[attrHour] = 0;
                                        hourVal[attrHour] += delta;
                                    } else {
                                        // Gap-aware attribution: credit the delta to
                                        // every bucket it actually covers, in
                                        // proportion to the minutes spent there.
                                        // A lost tick (reboot, SD busy/FTP lock,
                                        // clock jump, OTA) used to dump HOURS of
                                        // production into one column — a single huge
                                        // bar plus empty hours. Normal 30 min ticks
                                        // stay single-bucket (unchanged output).
                                        int prevMinOfDay = prevHour * 60 + ((prevMin < 0) ? 0 : prevMin);
                                        int curMinOfDay  = hour * 60 + minute;
                                        int spanMin = curMinOfDay - prevMinOfDay;
                                        if (spanMin < 0) spanMin += 1440;   // crossed midnight
                                        if (spanMin == 0) spanMin = 1;      // sub-minute sample pair
                                        int firstBucketMin = 60 - ((prevMin < 0) ? 0 : prevMin);
                                        if (firstBucketMin <= 0 || firstBucketMin > 60) firstBucketMin = 60;
                                        int consumed = 0;
                                        long credited = 0;
                                        int buckets = 0;
                                        for (int k = 0; k < 24 && consumed < spanMin; k++) {
                                            int hh = (prevHour + k) % 24;
                                            int bucketMin = (k == 0) ? firstBucketMin : 60;
                                            int take = spanMin - consumed;
                                            if (take > bucketMin) take = bucketMin;
                                            long share = (long)((double)delta * (double)take / (double)spanMin + 0.5);
                                            if (share > delta - credited) share = delta - credited;
                                            if (share < 0) share = 0;
                                            if (_proxHourInWindow(hh, startHour, durHours)) {
                                                if (hourVal[hh] < 0) hourVal[hh] = 0;
                                                hourVal[hh] += share;
                                            }
                                            credited += share;
                                            consumed += take;
                                            buckets++;
                                        }
                                        // Rounding remainder goes to the sample's own hour.
                                        if (credited < delta && _proxHourInWindow(hour, startHour, durHours)) {
                                            if (hourVal[hour] < 0) hourVal[hour] = 0;
                                            hourVal[hour] += (delta - credited);
                                        }
                                        if (buckets > 1) {
                                            lastPairSpread = true;
                                        } else if (hour == lastShiftHour && prevHour != lastShiftHour && prevHour >= 0) {
                                            // Single-bucket delta crossing into the last
                                            // shift hour keeps the double-tick fix below.
                                            boundaryDelta = delta;
                                            boundaryFromHour = prevHour;
                                        }
                                    }
                                }
                                prevPrevIn = prevIn;
                                prevPrevHour = prevHour;
                                prevIn = ch0;
                                prevHour = (hour >= 0 && hour <= 23) ? hour : prevHour;
                                prevMin = minute;
                                lastCh0 = ch0;
                                lastHour = (hour >= 0 && hour <= 23) ? hour : lastHour;
                            }
                        }
                    }
                }
            }
        }
        line = nl ? (nl + 1) : nullptr;
    }
    // Close the last hour correctly (shift-end flush re-credited to the last
    // shift hour so it is never reported as 0). Skipped when the final pair
    // spanned several buckets: the gap-aware distribution above already
    // credited every hour it covered, so moving it again would double-count.
    if (!lastPairSpread && lastHour >= 0 && prevPrevHour >= 0) {
        if (boundaryDelta >= 0 && boundaryFromHour >= 0
                && boundaryFromHour != lastShiftHour) {
            int hourDiff = (lastShiftHour - boundaryFromHour + 24) % 24;
            if (hourDiff == 1) {
                if (hourVal[boundaryFromHour] >= boundaryDelta) hourVal[boundaryFromHour] -= boundaryDelta;
                else hourVal[boundaryFromHour] = 0;
                if (hourVal[lastShiftHour] < 0) hourVal[lastShiftHour] = 0;
                hourVal[lastShiftHour] += boundaryDelta;
            }
        }
        if (prevPrevIn >= 0 && lastCh0 >= 0) {
            long delta = lastCh0 - prevPrevIn;
            if (delta < 0) delta = 0;
            if (prevPrevHour != lastShiftHour && delta > 0) {
                bool alreadyMoved = (boundaryDelta >= 0
                                     && prevPrevHour == boundaryFromHour
                                     && delta == boundaryDelta);
                if (!alreadyMoved) {
                    if (hourVal[prevPrevHour] >= delta) hourVal[prevPrevHour] -= delta;
                    else hourVal[prevPrevHour] = 0;
                    if (hourVal[lastShiftHour] < 0) hourVal[lastShiftHour] = 0;
                    hourVal[lastShiftHour] += delta;
                }
            }
        }
    }
    heap_caps_free(buf);

    uint32_t chartTotal = 0;
    for (int k = 0; k < durHours; k++) {
        int h = (startHour + k) % 24;
        if (hourVal[h] > 0) chartTotal += (uint32_t)hourVal[h];
    }
    if (chartTotalOut) *chartTotalOut = chartTotal;
    return true;
#else
    (void)csvPath; (void)startHour; (void)durHours; (void)hourVal; (void)chartTotalOut;
    return false;
#endif
}

// Writes/creates the weekly OEE CSV: one file per ISO week at the SD root
// /{sdcardName}/wk<NN>-oee.csv (NOT inside PROXIMITY_<date>). At each shift-end it
// appends one row per hour of the ended shift using the daily-CSV hourly breakdown,
// with day+hour+shift+count-per-hour+total+scrap+setup+oee. Additive — never touches
// the per-day PROXIMITY_*.csv files. total accumulates across the shift hours and the
// final row's total is pinned to the real shift-end total (shiftPiecesDI1). The
// summary columns (good, run_sec, plan_sec, unplanned_sec, elapsed_sec, ppm,
// weekday) repeat the shift-level values on every hour row so each line is
// self-contained for Excel analysis.
// The hour column is the END hour (the hour the count is taken), same as the shift
// email chart and the web dashboard: a shift 6H-14H writes hours 7..14.
static void _proxWriteWeeklyOee(int8_t endedShift, int startHour, int durHours,
                                uint32_t shiftPiecesDI1, float shiftAvailDI1,
                                float shiftPerfDI1, float shiftQualDI1, float shiftOeeDI1,
                                uint32_t shiftScrapDI1, uint32_t shiftSetupDI1,
                                uint32_t shiftGoodDI1, uint32_t shiftRunSec,
                                uint32_t shiftPlannedSec, uint32_t shiftElapsedSec,
                                float shiftTargetPpm, uint32_t* csvTotalOut,
                                time_t ctxNow) {
#if ENABLE_SDCARD==1
    // Dual sink: SD card when mounted, else LittleFS /oee mirror (same rows).
    // The hourly breakdown read below is remap-aware (SD daily, else Flash
    // /oee daily mirror), so rows stay correct in both modes.
    const bool useFlash = !sdcardShouldUseSD();
    // Hourly breakdown must come from the ENDED shift's day folder: at a
    // day-boundary shift end the caller passes the pre-boundary context time,
    // otherwise the read hits the new (empty) folder and writes zero rows.
    time_t now = (ctxNow > 1000000000L) ? ctxNow : time(nullptr);
    if (now < 1000000000L) return; // NTP not synced — cannot name the weekly file.

    struct tm ti;
    localtime_r(&now, &ti);
    int isoYear = 0;
    int week = proximityIsoWeek(ti, isoYear);

    extern ConfigManager configManager;
    String sdName = configManager.getSdcardName();
    if (sdName.isEmpty()) sdName = "SD_LOG";
    String rootDir = String("/") + sdName;
    char wkName[24];
    proximityWeeklyOeeFileName(week, wkName, sizeof(wkName));
    String filePathS = rootDir + "/" + wkName;

    // Weekly header (shared by both sinks — keep identical).
    static const char kWkHeader[] = "date,shift,hour,count_hour,total,scrap,setup,a,p,q,oee,good,run_sec,plan_sec,unplanned_sec,elapsed_sec,ppm,weekday";

    File f;
    size_t proxWkStart = 0;
    if (!useFlash) {
        // FTP holds the SD exclusively during its cycle (HISTORIC #57): short lock
        // timeout then skip this weekly write rather than interleave with the scan.
        if (!sdMutexLock(pdMS_TO_TICKS(1500))) return;

        if (!SD_MMC.exists(rootDir.c_str())) SD_MMC.mkdir(rootDir.c_str());
        f = SD_MMC.open(filePathS.c_str(), FILE_APPEND);
        if (!f) { sdMutexUnlock(); return; }
        proxWkStart = f.size();
        if (f.size() == 0) {
            f.println(kWkHeader);
        }
    }

    // Hourly breakdown from the shift-aligned day CSV (same source the email chart uses).
    struct tm fdate = ti;
    proximityShiftDayAlignedDate(now, &fdate);
    char dateSuf[16];
    snprintf(dateSuf, sizeof(dateSuf), "%02d-%02d-%04d",
             fdate.tm_mday, fdate.tm_mon + 1, fdate.tm_year + 1900);
    String csvPath = sd_layout::dayFile(sdName, "PROXIMITY", String(dateSuf), "csv");

    long hourVal[24];
    for (int h = 0; h < 24; h++) hourVal[h] = -1;
    _proxReadHourlyCounts(csvPath, startHour, durHours, hourVal, nullptr);

    // Turn number rotated so the day-boundary turn is Turno 1 (same as the
    // shift-end CSV marker and the frontend label).
    int turnNum = proximityShiftTurnNumber(proximityGetShiftConfig(), endedShift);

    uint32_t runningTotal = 0;
    uint32_t unplannedSec = (shiftElapsedSec > shiftPlannedSec + shiftRunSec)
        ? (shiftElapsedSec - shiftPlannedSec - shiftRunSec) : 0;
    for (int k = 0; k < durHours; k++) {
        int dataHour = (startHour + k) % 24;
        int hour = (dataHour + 1) % 24; // label = END hour (the count is taken at hour+1)
        uint32_t countHour = (hourVal[dataHour] > 0) ? (uint32_t)hourVal[dataHour] : 0;
        runningTotal += countHour;
        uint32_t total = (k == durHours - 1) ? shiftPiecesDI1 : runningTotal;
        // Calendar date for THIS hour: 'back' hours before the shift-end boundary.
        int back = durHours - 1 - k;
        struct tm rt;
        time_t t = now - (time_t)back * 3600;
        localtime_r(&t, &rt);
        rt.tm_hour = hour; // pin the reported hour (independent of wall-clock alignment)
        char row[200];
        proximityFormatOeeRow(row, sizeof(row), rt, turnNum, hour,
                              countHour, total, shiftScrapDI1, shiftSetupDI1,
                              shiftAvailDI1, shiftPerfDI1, shiftQualDI1, shiftOeeDI1,
                              shiftGoodDI1, shiftRunSec, shiftPlannedSec,
                              unplannedSec, shiftElapsedSec, shiftTargetPpm);
        if (!useFlash) {
            f.println(row);
        } else {
            flashOeeAppend(String(wkName), kWkHeader, row);
        }
    }
    if (!useFlash) {
        size_t proxWkEnd = f.size();
        f.close();
        storNoteSdWrite(proxWkEnd > proxWkStart ? proxWkEnd - proxWkStart : 0);
        sdMutexUnlock();
    }
    // CSV-derived shift total (unpinned hourly sum, same source as the email
    // chart): the caller cross-checks it against the live counter delta to
    // detect a mid-shift reboot with lost baselines.
    if (csvTotalOut) *csvTotalOut = runningTotal;
#else
    (void)endedShift; (void)startHour; (void)durHours;
    (void)shiftPiecesDI1; (void)shiftAvailDI1; (void)shiftPerfDI1; (void)shiftQualDI1;
    (void)shiftOeeDI1; (void)shiftScrapDI1; (void)shiftSetupDI1;
    (void)shiftGoodDI1; (void)shiftRunSec; (void)shiftPlannedSec;
    (void)shiftElapsedSec; (void)shiftTargetPpm; (void)csvTotalOut;
#endif
}

// Builds an ASCII bar chart of pieces-per-hour for the ended shift.
// Reads the day's CSV tick rows, attributes each delta of ch0_in to the hour
// of the later sample, and renders a column per shift hour from startHour for
// durHours, each labelled by its END hour (the hour the count is taken): a shift
// 06H-14H renders buckets 07H..14H, where the 07H bucket holds the production of
// 06:00-07:00. Matches the web dashboard and the weekly OEE CSV labelling.
// Each column also shows planned stops (U+2593 MEDIUM SHADE) for the fraction
// of that hour covered by configured stops — absolute wall-clock windows
// derived from refWall, so a mid-shift reboot can never hide them (the live
// millis-based snapshot window restarts at boot, these windows don't).
// refWall<=0 (or unsynced clock) disables the overlay. Returns an empty
// String when there is no usable data (chart omitted).
String proximityBuildHourlyChart(const String& csvPath, int startHour, int durHours, uint32_t totalPieces, uint32_t* chartTotalOut, time_t refWall) {
    String chart;
#if ENABLE_SDCARD==1
    // No bare SD_MMC.exists here: _proxReadHourlyCounts (below) performs the
    // SD access under the exclusivity mutex (FTP cycle, HISTORIC #57) and
    // returns false when the file is missing, producing the same empty chart.
    long hourVal[24];
    for (int h = 0; h < 24; h++) hourVal[h] = -1; // -1 = no sample for that hour
    uint32_t chartTotal = 0;
    if (!_proxReadHourlyCounts(csvPath, startHour, durHours, hourVal, &chartTotal)) return chart;
    if (chartTotalOut) *chartTotalOut = chartTotal;

    int nCols = durHours > 24 ? 24 : durHours;
    // 10-cell bar (owner pick 2026-10-15): one chart row per shift hour, so the
    // shift-end body stays phone-width. Same visual, half the columns.
    int barLen = 10;
    // Planned-stop seconds per column: last column takes refWall's date,
    // earlier columns walk back one day on each hour wrap (23->0).
    uint32_t planSec[24] = {0};
    time_t ref = (refWall >= 1000000000L) ? refWall : time(nullptr);
    if (ref >= 1000000000L) {
        struct tm rt;
        localtime_r(&ref, &rt);
        int dateOff = 0;
        for (int k = nCols - 1; k >= 0; k--) {
            int h = (startHour + k) % 24;
            if (k < nCols - 1) {
                int hn = (startHour + k + 1) % 24;
                if (h > hn) dateOff -= 1;
            }
            struct tm ct = rt;
            ct.tm_mday += dateOff;
            ct.tm_hour = h; ct.tm_min = 0; ct.tm_sec = 0; ct.tm_isdst = -1;
            time_t ws = mktime(&ct);
            if (ws > 0) planSec[k] = proximityPlannedStopSec(ws, ws + 3600);
        }
    }

    long maxV = 0;
    bool any = false;
    for (int k = 0; k < nCols; k++) {
        int h = (startHour + k) % 24;
        if (hourVal[h] > 0) { any = true; if (hourVal[h] > maxV) maxV = hourVal[h]; }
        if (planSec[k] > 0) any = true;
    }
    if (!any) return chart;
    // Fixed width for the numeric field: digits of maxV + 1 for padding.
    char mbuf[16]; snprintf(mbuf, sizeof(mbuf), "%ld", maxV);
    int numW = (int)strlen(mbuf);
    if (numW < 3) numW = 3;

    size_t renderCap = (size_t)nCols * 100 + 64;
    char* render = (char*)heap_caps_malloc(renderCap,
        psramFound() ? MALLOC_CAP_SPIRAM : (MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    if (!render) return chart;
    char* o = render;
    size_t room = renderCap;
    int n = 0;

    // Same production-bar visual as the shift email's stacked time bar:
    // U+2593 DARK SHADE for production, U+2592 MEDIUM SHADE for the
    // planned-stop share of the hour, U+2591 LIGHT SHADE for the empty
    // track (lighter than FULL BLOCK so the black is softer in email).
    const char* FILL  = "\xE2\x96\x93";
    const char* PLAN  = "\xE2\x96\x92";
    const char* EMPTY = "\xE2\x96\x91";

    for (int k = 0; k < nCols; k++) {
        int h = (startHour + k) % 24;
        long v = (hourVal[h] < 0) ? 0 : hourVal[h];
        // Planned share first (absolute time fraction), production fills what
        // is left — identical output to before when no stops are configured.
        int plan = (int)(((long)barLen * (long)planSec[k] + 1800L) / 3600L);
        if (plan > barLen) plan = barLen;
        int avail = barLen - plan;
        int fill = (maxV > 0 && avail > 0) ? (int)((long)avail * v / maxV) : 0;
        if (v > 0 && fill < 1 && avail > 0) fill = 1;
        // Columns are labelled by their END hour (the hour the count is taken),
        // so a shift 06H-14H shows buckets 07H..14H: "07H 14980 pcs [▓▓▓▓▓...]".
        int lbl = (h + 1) % 24;
        // "21H   446 pcs  [▓░░░░░░░░░]" / "17H 17340 pcs  [▓▓▓▓▒▒▒▒▒░]"
        // The " pcs" suffix is constant-width and the count is right-aligned
        // to numW, so every row's bar starts at the same column.
        n = snprintf(o, room, "%02dH %*d pcs" , lbl, numW, (int)v);
        o += n; room -= (size_t)n;
        n = snprintf(o, room, "  [");
        o += n; room -= (size_t)n;
        for (int b = 0; b < barLen && room > 3; b++) {
            const char* seg = (b < fill) ? FILL : ((b < fill + plan) ? PLAN : EMPTY);
            *o++ = seg[0]; *o++ = seg[1]; *o++ = seg[2];
            room -= 3;
        }
        n = snprintf(o, room, "]");
        o += n; room -= (size_t)n;
        if (room > 1) { *o++ = '\n'; room--; }
    }
    *o = '\0';

    chart = render;
    heap_caps_free(render);
#endif
    return chart;
}

// Returns the current shift slot index (0-based) from the RTC time, or -1 when
// shifts are disabled, no slots are configured, or the clock is not synced.
int8_t proximityGetCurrentShiftIndex() {
    ProximityShiftConfig sc = proximityGetShiftConfig();
    if (!sc.enabled || sc.nSlots == 0) return -1;
    time_t raw = time(nullptr);
    if (raw <= 1000) return -1;
    struct tm t;
    localtime_r(&raw, &t);
    return _detectCurrentShift(&t);
}

// Computes the hour window of a shift slot for the hourly chart.
// Returns false when shifts are disabled, the index is invalid, or no slots are set.
// Columns are the ACTUAL shift hours [start, end): e.g. shift 14h->22h shows
// 14h 15h ... 20h 21h (8 columns). The shift-end flush (last tick -> boundary
// snapshot) lands in the last column naturally, and no column exists outside
// the shift — so another shift's production can never leak into the chart
// (previously columns were [start+1, end], dropping the first hour and
// crediting the flush to a fictional end-hour column that also collected the
// previous day's tail for that hour). When capToNow is true the duration is
// truncated to the elapsed time since the shift started (used by the manual
// test email, which shows "up to now").
bool proximityGetShiftWindow(int8_t shiftIdx, int& startHour, int& durHours, bool capToNow) {
    ProximityShiftConfig sc = proximityGetShiftConfig();
    if (!sc.enabled || sc.nSlots == 0) return false;
    if (shiftIdx < 0 || shiftIdx >= (int)sc.nSlots) return false;
    startHour = sc.slots[shiftIdx].hour;
    int startMin = sc.slots[shiftIdx].hour * 60 + sc.slots[shiftIdx].minute;
    int nextIdx = (shiftIdx + 1) % (int)sc.nSlots;
    int endMin = sc.slots[nextIdx].hour * 60 + sc.slots[nextIdx].minute;
    if (nextIdx == 0 && endMin <= startMin) endMin += 1440;
    durHours = (endMin - startMin) / 60;
    if (durHours < 1) durHours = 1;
    if (capToNow) {
        time_t raw = time(nullptr);
        if (raw > 1000) {
            struct tm t;
            localtime_r(&raw, &t);
            int nowMin = t.tm_hour * 60 + t.tm_min;
            int elapsed = nowMin - startMin;
            if (elapsed < 0) elapsed = 0;
            int elapsedHours = (elapsed + 59) / 60;
            if (elapsedHours < 1) elapsedHours = 1;
            if (elapsedHours < durHours) durHours = elapsedHours;
        }
    }
    return true;
}

// Computes the running shift summary for channel 0 (pieces since shift start and
// OEE), from the live channel state. Used by the manual test email button.
void proximityGetRunningShiftSummary(uint32_t& pieces, float& oee, float& avail, float& perf, float& qual) {
    noInterrupts();
    ProximityChannel snap = _ch[0];
    uint8_t sch = _ch[0].scrap_ch;
    uint32_t scrapIn = (sch < PROXIMITY_CHANNELS && sch != 0) ? _ch[sch].count_in : 0;
    interrupts();
    uint32_t cIn = (snap.count_in >= _shiftCountInBase[0])
        ? snap.count_in - _shiftCountInBase[0] : 0;
    uint32_t runSec   = (snap.running_seconds >= _shiftRunSecBase[0])
        ? snap.running_seconds - _shiftRunSecBase[0] : 0;
    uint32_t goodP    = (snap.good_parts >= _shiftGoodPartsBase[0])
        ? snap.good_parts - _shiftGoodPartsBase[0] : 0;
    float    tgtPpm   = snap.target_ppm;
    uint32_t effectiveGood = (scrapIn > 0 && cIn > scrapIn) ? (cIn - scrapIn) : goodP;
    // Wall-clock shift window first (reboot-immune); millis fallback when
    // the clock is unsynced, shifts are off, or the shift just started.
    uint32_t shElapsed = proximityShiftElapsedSec();
    uint32_t shPlanned;
    if (shElapsed == 0) {
        shElapsed = proximityCountElapsedSec(0);
        shPlanned = proximityChannelPlannedStopSec(0);
    } else {
        shPlanned = proximityShiftPlannedStopSec();
    }
    ProximityOeeResult r = proximityComputeOee(runSec, shElapsed, cIn, effectiveGood, tgtPpm, shPlanned);
    avail = r.availability;
    perf  = r.performance;
    qual  = r.quality;
    oee   = r.oee;
    pieces = cIn;
}

// Computes the running day summary for channel 0 (pieces since the day boundary
// and OEE), from the live channel state. Mirrors proximityGetRunningShiftSummary
// over the day baselines / [day boundary, now] window. Used by the OEE alert
// engine and the OEE emails.
void proximityGetRunningDaySummary(uint32_t& pieces, float& oee, float& avail, float& perf, float& qual) {
    noInterrupts();
    ProximityChannel snap = _ch[0];
    uint8_t sch = _ch[0].scrap_ch;
    uint32_t scrapSnapIn = (sch < PROXIMITY_CHANNELS && sch != 0) ? _ch[sch].count_in : 0;
    interrupts();
    uint32_t cIn = 0, runSec = 0, dDownt = 0, goodP = 0;
    proximityGetDayDeltas(0, cIn, runSec, dDownt, goodP);
    (void)dDownt;
    uint32_t scrapDayIn = 0;
    if (sch < PROXIMITY_CHANNELS && sch != 0) {
        uint32_t sIn = 0, sRun = 0, sDown = 0, sGood = 0;
        proximityGetDayDeltas(sch, sIn, sRun, sDown, sGood);
        scrapDayIn = sIn;
    }
    float    tgtPpm   = snap.target_ppm;
    uint32_t effectiveGood = (scrapDayIn > 0 && cIn > scrapDayIn) ? (cIn - scrapDayIn) : goodP;
    ProximityOeeResult r = proximityComputeOee(runSec, proximityDayElapsedSec(), cIn, effectiveGood, tgtPpm, proximityDayPlannedStopSec());
    avail = r.availability;
    perf  = r.performance;
    qual  = r.quality;
    oee   = r.oee;
    pieces = cIn;
}

// OEE alert engine state (per slot, channel 0 / DI1).
static uint32_t _oeeAlertBelowMs[ConfigManager::OEE_ALERT_SLOT_COUNT];
static bool     _oeeAlertFired[ConfigManager::OEE_ALERT_SLOT_COUNT];
static uint8_t  _oeeAlertSentToday[ConfigManager::OEE_ALERT_SLOT_COUNT];
static int      _oeeAlertDay = -1;
static uint32_t _oeeAlertLastMs = 0;

// OEE alert engine: evaluates the configured OEE alert slots (shift OEE, day OEE,
// availability on DI1) and emails when a value stays below threshold. Called from
// the main loop next to the inactivity check; throttled internally to 30 s.
// Guards: counters must be running (shift) and RTC synced (day); email must be
// configured; per-slot max/day; re-arm only after the value recovers.
void proximityOeeAlertTick() {
    bool anyEnabled = false;
    for (int i = 0; i < ConfigManager::OEE_ALERT_SLOT_COUNT; ++i) {
        if (configManager.getOeeAlertEnabled(i)) { anyEnabled = true; break; }
    }
    if (!anyEnabled) return;
    uint32_t now = millis();
    if (now - _oeeAlertLastMs < 30000UL) return;
    _oeeAlertLastMs = now;

    // Day rollover: reset per-slot daily counters and episode state. Keyed by
    // the shift-aligned production day (same R2 model as day OEE/donut/charts),
    // NOT the calendar date — otherwise the max/day budget would reset at
    // midnight mid-production-day and could be spent twice (pre/post midnight).
    time_t rawT = time(nullptr);
    if (rawT > 1000) {
        struct tm at;
        proximityShiftDayAlignedDate(rawT, &at);
        int key = proximityDayKey(at.tm_year, at.tm_yday);
        if (key != _oeeAlertDay) {
            _oeeAlertDay = key;
            for (int i = 0; i < ConfigManager::OEE_ALERT_SLOT_COUNT; ++i) {
                _oeeAlertSentToday[i] = 0;
                _oeeAlertFired[i] = false;
                _oeeAlertBelowMs[i] = 0;
            }
        }
    }

    // Live values for channel 0 (DI1 — same convention as the shift-end email).
    uint32_t sPieces = 0, dPieces = 0;
    float sOee = 0, sAvail = 0, sPerf = 0, sQual = 0;
    float dOee = 0, dAvail = 0, dPerf = 0, dQual = 0;
    proximityGetRunningShiftSummary(sPieces, sOee, sAvail, sPerf, sQual);
    proximityGetRunningDaySummary(dPieces, dOee, dAvail, dPerf, dQual);
    (void)sPerf; (void)sQual; (void)dAvail; (void)dPerf; (void)dQual;
    // Shift window exists when the wall-clock shift elapsed is known; before
    // NTP sync the millis window still counts (same fallback as the summary).
    bool shiftValid = (proximityShiftElapsedSec() > 0) || (proximityCountElapsedSec(0) > 0);
    bool dayValid = proximityDayElapsedSec() > 0;

    for (int i = 0; i < ConfigManager::OEE_ALERT_SLOT_COUNT; ++i) {
        if (!configManager.getOeeAlertEnabled(i)) { _oeeAlertBelowMs[i] = 0; _oeeAlertFired[i] = false; continue; }
        int metric = configManager.getOeeAlertMetric(i);
        String metricName;
        if (metric == 0)      metricName = L(activeLang->emailOeeMetricShift);
        else if (metric == 1) metricName = L(activeLang->emailOeeMetricDay);
        else                  metricName = L(activeLang->emailOeeMetricAvail);
        float value = 0; bool valid = false;
        if (metric == 0)      { value = sOee;   valid = shiftValid; }
        else if (metric == 1) { value = dOee;   valid = dayValid; }
        else                  { value = sAvail; valid = shiftValid; }
        if (!valid) { _oeeAlertBelowMs[i] = 0; _oeeAlertFired[i] = false; continue; }
        float threshold = (float)configManager.getOeeAlertThreshold(i);
        if (!(value < threshold)) { _oeeAlertBelowMs[i] = 0; _oeeAlertFired[i] = false; continue; }
        if (_oeeAlertBelowMs[i] == 0) _oeeAlertBelowMs[i] = now;
        if (_oeeAlertFired[i]) continue;
        uint32_t persistMs = (uint32_t)configManager.getOeeAlertPersistMin(i) * 60000UL;
        if (!oeeAlertShouldFire(value, threshold, now - _oeeAlertBelowMs[i], persistMs)) continue;
        int maxDay = configManager.getOeeAlertMaxPerDay(i);
        if (_oeeAlertSentToday[i] >= maxDay) continue;
        String emails = configManager.getOeeAlertEmails(i);
        emails.trim();
        if (emails.length() == 0 || emails.indexOf('@') < 0) continue;
        if (!isEmailConfiguredAndWifiConnected()) continue;
        String sysName = configManager.getSystemName();
        if (sysName.length() == 0) sysName = String(F("Node32-HUB"));
        char vb[16]; snprintf(vb, sizeof(vb), "%.1f", (double)value);
        String subject = sysName + String(F(" - ")) + L(activeLang->emailOeeTag) + String(F(" - ")) + metricName +
                         String(F(" ")) + vb + String(F("% < ")) + String((int)threshold) + String(F("%"));
        String body = L(activeLang->emailOeePrefix) + metricName + String(F(" = ")) + vb +
                      String(F("% (")) + L(activeLang->emailOeeThresholdLbl) + String(F(" ")) + String((int)threshold) + String(F("%)\n"));
        ProximityChannel ch0 = proximityGetChannel(0);
        body += String(F("Channel: ")) + (ch0.name[0] ? String(ch0.name) : String(F("DI1"))) + String(F(" (DI1)\n"));
        body += (metric == 1)
            ? (L(activeLang->emailOeeDayPieces) + String(dPieces) + String(F("\n")))
            : (L(activeLang->emailOeeShiftPieces) + String(sPieces) + String(F("\n")));
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
        // Same device footer as the shift-end emails (buildEmailSignature).
        body += buildEmailSignature();
#endif
        extern bool startInactivityAlertAsync(const String&, const String&, const String&);
        if (startInactivityAlertAsync(emails, subject, body)) {
            _oeeAlertSentToday[i]++;
            _oeeAlertFired[i] = true;
            sendVirtualMessage(String(F("Prox: OEE alert sent (slot ")) + String(i) + String(F(")")), WARN);
        }
    }
}

bool proximityGetShiftBaselines(uint8_t ch, uint32_t& countInBase, uint32_t& runSecBase,
                                uint32_t& downtSecBase, uint32_t& goodPartsBase) {
    if (ch >= PROXIMITY_MAX_CHANNELS) return false;
    countInBase  = _shiftCountInBase[ch];
    runSecBase   = _shiftRunSecBase[ch];
    downtSecBase = _shiftDowntSecBase[ch];
    goodPartsBase = _shiftGoodPartsBase[ch];
    return true;
}

// Day-so-far deltas for the day OEE (/api/proximity/oee, day summary, alerts).
// Carried pre-reset production plus the live tail, so the values always span
// [day boundary, now] even across shift auto-resets. Returns false if the
// channel index is out of range.
bool proximityGetDayDeltas(uint8_t ch, uint32_t& dIn, uint32_t& dRunSec,
                           uint32_t& dDowntSec, uint32_t& dGood) {
    if (ch >= PROXIMITY_MAX_CHANNELS) return false;
    noInterrupts();
    uint32_t cntIn = _ch[ch].count_in;
    uint32_t runS  = _ch[ch].running_seconds;
    uint32_t downS = _ch[ch].downtime_seconds;
    uint32_t goodP = _ch[ch].good_parts;
    interrupts();
    dIn      = proxDayDelta(_dayAccumIn[ch],   cntIn, _dayCountInBase[ch]);
    dRunSec  = proxDayDelta(_dayAccumRun[ch],  runS,  _dayRunSecBase[ch]);
    dDowntSec = proxDayDelta(_dayAccumDownt[ch], downS, _dayDowntSecBase[ch]);
    dGood    = proxDayDelta(_dayAccumGood[ch], goodP, _dayGoodPartsBase[ch]);
    return true;
}

// Seconds since the current day's START — the last shift day-boundary rollover
// (e.g. 22:00 for 6/14/22 shifts), matching the CSV folder labels written by
// proxShiftAlignedDate(). Falls back to local midnight when no boundary is
// configured (legacy behavior). 0 when the RTC is not synced yet.
uint32_t proximityDayElapsedSec() {
    time_t raw = time(nullptr);
    if (raw <= 1000) return 0;
    struct tm t;
    localtime_r(&raw, &t);
    uint8_t b = proximityShiftDayBoundaryHour();
    if (b == 0) {
        return (uint32_t)(t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec);
    }
    struct tm bt = t;
    bt.tm_hour = b; bt.tm_min = 0; bt.tm_sec = 0; bt.tm_isdst = -1;
    time_t boundary = mktime(&bt);
    if (boundary > raw) {
        // Before today's boundary → the day started at yesterday's boundary.
        bt.tm_mday -= 1;
        boundary = mktime(&bt);
    }
    if (boundary <= 0 || boundary > raw) return 0;
    return (uint32_t)(raw - boundary);
}

// Planned-stop overlap for the day-so-far window [day boundary, now], for the day
// OEE caller. Unsynced clock or empty window → 0.
uint32_t proximityDayPlannedStopSec() {
    uint32_t elapsed = proximityDayElapsedSec();
    if (elapsed == 0) return 0;
    time_t nowW = time(nullptr);
    if (nowW < 1000000000L) return 0;
    return proximityPlannedStopSec(nowW - (time_t)elapsed, nowW);
}

// Wall-clock start of the CURRENT shift (slot start, most recent in wall
// time). 0 when the clock is unsynced or shifts are disabled/empty.
static time_t proximityShiftStartWall() {
    time_t raw = time(nullptr);
    if (raw < 1000000000L) return 0;
    ProximityShiftConfig sc = proximityGetShiftConfig();
    if (!sc.enabled) return 0;
    return proxShiftStartWallFor(sc.nSlots, sc.slots, raw);
}

// Seconds since the current shift START (wall clock, reboot-immune). 0 when
// unavailable — live OEE callers fall back to proximityCountElapsedSec.
uint32_t proximityShiftElapsedSec() {
    time_t raw = time(nullptr);
    time_t start = proximityShiftStartWall();
    if (start <= 0 || raw <= start) return 0;
    return (uint32_t)(raw - start);
}

// Planned-stop overlap for the running-shift window [shift start, now].
// 0 when unavailable — live OEE callers fall back to
// proximityChannelPlannedStopSec.
uint32_t proximityShiftPlannedStopSec() {
    time_t raw = time(nullptr);
    time_t start = proximityShiftStartWall();
    if (start <= 0 || raw <= start) return 0;
    return proximityPlannedStopSec(start, raw);
}

// Builds the full shift email body: "Registos:", pieces + OEE, hourly ASCII chart
// and the device footer. Shared by the auto shift-end email and the test button.
String proximityBuildShiftEmailBody(const String& csvPath, int startHour, int durHours,
                                    uint32_t pieces, float oee, float avail, float perf, float qual,
                                    uint32_t scrapCount, uint32_t setupCount,
                                    uint32_t runSec, uint32_t plannedSec, uint32_t elapsedSec,
                                    time_t refWall) {
    String body;
    body.reserve(512 + (size_t)(durHours > 24 ? 24 : durHours) * 60);

    // Shift turno label from the chart window. Columns are the actual shift
    // hours [start, end), so turno start = first column and turno end = one
    // past the last column.
    int turnoStart = startHour;
    int turnoEnd   = (startHour + durHours) % 24;

    // Build the chart first so we can use the CSV-based total that matches
    // the hourly breakdown exactly (fixes count mismatch between live counter
    // snapshot and CSV delta sums).
    // FIX (issues #2 — Piece count): Use shiftPiecesDI1 (the live DI1 counter
    // delta since shift start) as the authoritative total, NOT chartTotal
    // (sum of CSV hourly deltas) which can drift due to missed tick writes.
    // The chart is still shown for the per-hour breakdown.
    uint32_t chartTotal = 0;
    String chart = proximityBuildHourlyChart(csvPath, startHour, durHours, pieces, &chartTotal, refWall);
    // pieces already holds shiftPiecesDI1 from the caller — do NOT overwrite
    // with chartTotal which can differ by missed CSV ticks (issues #2).

    body += L(activeLang->emailShiftRecords) + String(F("  (")) + L(activeLang->emailShiftName) + String(F(" "));
    if (turnoStart < 10) body += '0';
    body += String(turnoStart) + F("H-");
    if (turnoEnd < 10) body += '0';
    body += String(turnoEnd) + F("H)\n\n");

    body += String(pieces) + F(" pcs");
    if (scrapCount > 0) {
        body += F("  (");
        body += String(scrapCount);
        body += String(F(" ")) + L(activeLang->emailShiftScrap) + String(F(")"));
    }
    if (setupCount > 0) {
        body += F(" (");
        body += String(setupCount);
        body += F(" setup)");
    }
    body += F("\n\n");
    if (pieces > 0 || oee > 0.0f) {
        if (oee > 0.0f) {
            body += String(F("OEE ")) + String((int)(oee + 0.5f)) + F("%  A=") + String((int)(avail + 0.5f))
                  + F("%  P=") + String((int)(perf + 0.5f)) + F("%  Q=") + String((int)(qual + 0.5f)) + F("%\n\n");
        } else {
            body += L(activeLang->emailShiftNoTarget) + String(F("\n\n"));
        }
    } else {
        body += F("OEE N/A\n\n");
    }
    // Stacked time bar (production / planned / unplanned) between the OEE line
    // and the hourly pieces, separated by one blank line on each side.
    // Legend follows the active language (lang PT/EN).
    if (elapsedSec > 0) {
        body += proximityBuildTimeBarAscii(runSec, plannedSec, elapsedSec,
            L(activeLang->emailTimeProd), L(activeLang->emailTimePlan), L(activeLang->emailTimeUnplan));
        body += F("\n\n");
    }
    if (chart.length() > 0) {
        body += L(activeLang->emailShiftPcsHour) + String(F("\n\n"));
        body += chart;
        body += "\n";
    }
    #if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    body += buildEmailSignature();
    #endif
    return body;
}

static void _proxCaptureDayBaselines() {
    for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
        noInterrupts();
        _dayCountInBase[i]   = _ch[i].count_in;
        _dayRunSecBase[i]    = _ch[i].running_seconds;
        _dayDowntSecBase[i]  = _ch[i].downtime_seconds;
        _dayGoodPartsBase[i] = _ch[i].good_parts;
        interrupts();
        // New production day: yesterday's carried production is over.
        _dayAccumIn[i]    = 0;
        _dayAccumRun[i]   = 0;
        _dayAccumDownt[i] = 0;
        _dayAccumGood[i]  = 0;
    }
}

// Capture day baselines at the shift-aligned day rollover for the day-so-far
// OEE. Runs alongside _proxShiftTick() (same 30 s rate limit). First detection
// after boot captures the live counters so the day window starts at boot, not 0.
// The aligned day key (proxShiftAlignedDate) rolls over at the day boundary
// (e.g. 22:00 for 6/14/22 shifts), never splitting the overnight first turn.
static void _proxDayTick(uint32_t nowMs) {
    if (nowMs - _lastDayCheckMs < 30000UL) return;
    _lastDayCheckMs = nowMs;
    time_t raw = time(nullptr);
    if (raw <= 1000) return; // RTC not synced yet.
    struct tm t;
    localtime_r(&raw, &t);
    struct tm at = t;
    proximityShiftDayAlignedDate(raw, &at);
    int key = proximityDayKey(at.tm_year, at.tm_yday);
    if (key == _lastDayKey) {
        // Same day: re-save throttled so a reboot keeps most of the day.
        if (nowMs - _lastDaySaveMs >= PROX_DAYBASE_SAVE_MS) saveProximityDayBaselines();
        return;
    }
    _lastDayKey = key;
    _proxCaptureDayBaselines();
    saveProximityDayBaselines();
    // Co-save counters with the fresh bases: a reboot in the minutes after the
    // rollover must not pair new bases with stale counters (day would clamp at 0
    // until counters catch up). One extra write per day.
    saveProximityCounters();
}

// T8 — turno_data.txt: persist the exact shift-end email body (SD when
// mounted, else the same path on LittleFS).
// DECISIONS (brief T8 + T8b data-hora, documented here):
//   - Written EVERY time a shift closes, even when the email is skipped (no
//     recipients / no network / no storage) or fails to start: the .txt is
//     the local record, the email is best-effort delivery.
//   - TWO files per close: dated "/{sys}/turno{N}_{YYYY-MM-DD}_{HH-MM}.txt"
//     (per-shift history, shift END wall time) + latest "/{sys}/turno_data.txt"
//     (overwritten, always the last shift).
//   - Never attached to the email: dated files use ISO dates, which never
//     match the shift-end email's DD-MM-YYYY folder filter; the send path
//     below is untouched.
//   - SD guarded by the short SD mutex (FTP holds it exclusively); on lock
//     failure the write is skipped with a log, never blocking the turno.
static void _proxWriteShiftDataTxtAt(const String& body, const String& sys, const String& path) {
    if (body.isEmpty() || path.isEmpty()) return;
#if ENABLE_SDCARD==1
    if (proximityShiftDataTxtUseSD(currentStorageMode == STORAGE_MODE_SD, sdcardIsMounted())) {
        if (!sdMutexLock(pdMS_TO_TICKS(1500))) {
            sendVirtualMessage(F("Prox: turno_data.txt skipped, SD busy"), WARN);
            return;
        }
        String rootDir = String("/") + (sys.isEmpty() ? String("SD_LOG") : sys);
        if (!SD_MMC.exists(rootDir.c_str())) SD_MMC.mkdir(rootDir.c_str());
        File f = SD_MMC.open(path.c_str(), FILE_WRITE);
        bool ok = false;
        size_t w = 0;
        if (f) { w = f.print(body); f.close(); ok = (w == body.length()); }
        sdMutexUnlock();
        if (ok) {
            storNoteSdWrite(w);
            sendVirtualMessage(String(F("Prox: turno_data.txt saved -> ")) + path, INFO);
        } else {
            sendVirtualMessage(String(F("Prox: turno_data.txt SD write FAILED: ")) + path, WARN, false, true);
        }
        return;
    }
#endif
#if ENABLE_FILESYSTEM==1
    if (!littlefsBegin()) {
        sendVirtualMessage(F("Prox: turno_data.txt skipped, LittleFS unavailable"), WARN);
        return;
    }
    String dir = path.substring(0, path.lastIndexOf('/'));
    if (dir.length() > 1 && !LittleFS.exists(dir.c_str())) LittleFS.mkdir(dir.c_str());
    File lf = LittleFS.open(path.c_str(), "w");
    bool lok = false;
    size_t lw = 0;
    if (lf) { lw = lf.print(body); lf.close(); lok = (lw == body.length()); }
    if (lok) {
        storNoteLfsWrite(lw);
        sendVirtualMessage(String(F("Prox: turno_data.txt saved -> LittleFS:")) + path, INFO);
    } else {
        sendVirtualMessage(String(F("Prox: turno_data.txt LittleFS write FAILED: ")) + path, WARN, false, true);
    }
#else
    sendVirtualMessage(F("Prox: turno_data.txt skipped, no storage backend"), WARN);
#endif
}

static void _proxShiftTick(uint32_t nowMs) {    ProximityShiftConfig sc = proximityGetShiftConfig();
    if (!sc.enabled || sc.nSlots == 0) return;

    // Rate limit: re-check at most every 30 s to avoid redundant time() calls.
    if (nowMs - _lastShiftCheckMs < 30000UL) return;
    _lastShiftCheckMs = nowMs;

    time_t rawTime = time(nullptr);
    if (rawTime <= 1000) return; // RTC not synced yet.

    struct tm t;
    localtime_r(&rawTime, &t);
    int8_t newShift = _detectCurrentShift(&t);

    if (_currentShiftIdx == -1) {
        // First detection after boot: prefer the persisted shift baselines so
        // a mid-shift reboot keeps the shift window (totals/OEE continue);
        // only capture fresh when there is nothing usable saved (first boot,
        // config change, another day/shift, or stale counters).
        if (_proxRestoreShiftBaselines(newShift)) return;
        _currentShiftIdx = newShift;
        for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
            noInterrupts();
            _shiftCountInBase[i]    = _ch[i].count_in;
            _shiftCountOutBase[i]   = _ch[i].count_out;
            _shiftRunSecBase[i]     = _ch[i].running_seconds;
            _shiftDowntSecBase[i]   = _ch[i].downtime_seconds;
            _shiftGoodPartsBase[i]  = _ch[i].good_parts;
            _shiftSetupBase[i]      = _ch[i].setup_count;
            interrupts();
        }
        saveProximityShiftBaselines();
        return;
    }

    if (newShift != _currentShiftIdx) {
        // Shift boundary crossed: write shift-end summary to CSV and rules engine.
        sendVirtualMessage(String(F("Prox: shift change -> shift ")) + String(proximityShiftTurnNumber(sc, newShift)), INFO);
        int8_t endedShift = _currentShiftIdx;
        // Shift-day context for everything below (marker rows, weekly hourly
        // read, email folder/subject): just BEFORE this boundary, so a
        // day-boundary shift end (22h/23h/00h/18h, …) still resolves to the
        // folder holding the ended shift's ticks instead of the new day's
        // (nearly empty) folder. Works for any shift layout — no hour is
        // hardcoded. Falls back to rawTime-120s if the slot is out of range.
        time_t shiftDayCtx = (rawTime > 120) ? rawTime - 120 : rawTime;
        if (newShift >= 0 && newShift < (int)sc.nSlots) {
            shiftDayCtx = proxShiftBoundaryCtxTime(rawTime,
                sc.slots[newShift].hour, sc.slots[newShift].minute);
        }
        _proxInjectShiftMarker(endedShift, shiftDayCtx);

        // Write an immediate "shift_boundary" snapshot at the exact shift boundary.
        // Labeled differently from periodic "tick" rows so the CSV clearly shows the
        // shift transition: shift_end (deltas) → shift_boundary (absolute at boundary).
        // The chart parser reads shift_boundary like tick to close the final hour.
        // Must happen BEFORE proximityResetAll() zeroes the counters below.
        {
            uint32_t finIn[PROXIMITY_CHANNELS], finOut[PROXIMITY_CHANNELS];
            float    finPpm[PROXIMITY_CHANNELS];
            uint32_t finRun[PROXIMITY_CHANNELS], finDown[PROXIMITY_CHANNELS], finGood[PROXIMITY_CHANNELS];
            for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
                noInterrupts();
                ProximityChannel ch = _ch[i];
                interrupts();
                finIn[i]  = ch.count_in;
                finOut[i] = ch.count_out;
                finPpm[i] = ch.rate_ppm;
                finRun[i]  = ch.running_seconds;
                finDown[i] = ch.downtime_seconds;
                finGood[i] = ch.good_parts;
            }
            _proxWriteCSVRow("shift_boundary", -1, finIn, finOut, finPpm, finRun, finDown, finGood, shiftDayCtx);
            _lastCSVWriteMs = nowMs; // satisfy the 30-min rate limiter so it doesn't immediately re-write
        }

        // Snapshot DI1 shift-end data for the email body BEFORE auto-reset zeroes _ch[].
        uint32_t shiftPiecesDI1 = 0;
        float    shiftOeeDI1    = 0.0f;
        float    shiftAvailDI1  = 0.0f;
        float    shiftPerfDI1   = 0.0f;
        float    shiftQualDI1   = 0.0f;
        uint32_t shiftScrapDI1  = 0;
        uint32_t shiftSetupDI1  = 0;
        uint32_t shiftRunSec    = 0;
        uint32_t shiftPlannedSec = 0;
        uint32_t shiftElapsedSec = 0;
        uint32_t shiftGoodDI1   = 0;
        float    shiftTargetPpm = 0.0f;
        {
            noInterrupts();
            ProximityChannel snap = _ch[0];
            uint8_t sch = _ch[0].scrap_ch;
            uint32_t scrapRaw = (sch < PROXIMITY_CHANNELS && sch != 0) ? _ch[sch].count_in : 0;
            uint32_t scrapBase = 0;
            if (sch < PROXIMITY_CHANNELS && sch != 0) scrapBase = _shiftCountInBase[sch];
            interrupts();
            uint32_t scrapIn = (scrapRaw >= scrapBase) ? (scrapRaw - scrapBase) : 0;
            uint32_t cIn = (snap.count_in >= _shiftCountInBase[0])
                ? snap.count_in - _shiftCountInBase[0] : 0;
            uint32_t setupIn = (snap.setup_count >= _shiftSetupBase[0])
                ? snap.setup_count - _shiftSetupBase[0] : 0;
            uint32_t runSec   = (snap.running_seconds >= _shiftRunSecBase[0])
                ? snap.running_seconds - _shiftRunSecBase[0] : 0;
            uint32_t goodP    = (snap.good_parts >= _shiftGoodPartsBase[0])
                ? snap.good_parts - _shiftGoodPartsBase[0] : 0;
            float    tgtPpm   = snap.target_ppm;
            uint32_t effectiveGood = (scrapIn > 0 && cIn > scrapIn) ? (cIn - scrapIn) : goodP;
            // Absolute shift window [start,end): reboot-immune planned share
            // for the email time bar — the millis count-start window restarts
            // at boot and hides stops between shift start and boot. The OEE
            // line is recomputed from the same inputs so line and bar agree.
            time_t shStartW = 0, shEndW = 0;
            uint32_t shLenSec = proxShiftWindowSec(sc, endedShift, shiftDayCtx, shStartW, shEndW);
            uint32_t absPlannedSec = 0, absElapsedSec = proximityCountElapsedSec(0);
            if (shLenSec > 0) {
                absPlannedSec = proximityPlannedStopSec(shStartW, shEndW);
                absElapsedSec = shLenSec;
            } else {
                absPlannedSec = proximityChannelPlannedStopSec(0);
            }
            ProximityOeeResult r = proximityComputeOee(
                runSec, absElapsedSec,
                cIn, effectiveGood, tgtPpm, absPlannedSec);
            shiftAvailDI1 = r.availability;
            shiftPerfDI1  = r.performance;
            shiftQualDI1  = r.quality;
            shiftOeeDI1   = r.oee;
            shiftPiecesDI1 = cIn;
            shiftScrapDI1  = scrapIn;
            shiftSetupDI1  = setupIn;
            shiftRunSec     = runSec;
            shiftPlannedSec = absPlannedSec;
            shiftElapsedSec = absElapsedSec;
            shiftGoodDI1    = effectiveGood;
            shiftTargetPpm  = tgtPpm;
        }

        // Weekly OEE CSV: append one row per hour of the ended shift to the
        // single per-week file /{sdcardName}/wk<NN>-oee.csv (additive).
        int wkStartHour = 0, wkDurHours = 0;
        proximityGetShiftWindow(endedShift, wkStartHour, wkDurHours, false);
        uint32_t csvShiftTotal = 0;
        _proxWriteWeeklyOee(endedShift, wkStartHour, wkDurHours,
                            shiftPiecesDI1, shiftAvailDI1, shiftPerfDI1, shiftQualDI1,
                            shiftOeeDI1, shiftScrapDI1, shiftSetupDI1,
                            shiftGoodDI1, shiftRunSec, shiftPlannedSec, shiftElapsedSec,
                            shiftTargetPpm, &csvShiftTotal, shiftDayCtx);
        // Cross-check: the CSV-derived shift total (hourly breakdown, reboot
        // immune) vs the live counter delta. When the CSV sees substantially
        // MORE, the live baselines were lost mid-shift (reboot before the
        // shiftbase restore existed, corrupt files, missed saves) and the
        // shift totals/OEE above cover only part of the shift — say so loudly
        // instead of sending silently truncated numbers.
        bool shiftTruncated = (csvShiftTotal > shiftPiecesDI1 + shiftPiecesDI1 / 10 + 500);
        if (shiftTruncated) {
            sendVirtualMessage(String(F("Prox: shift-end total truncated? live=")) + String((unsigned long)shiftPiecesDI1)
                + F(" csv=") + String((unsigned long)csvShiftTotal)
                + F(" (mid-shift reboot with lost baselines?)"), WARN, false, true);
        }

        // If auto-reset is enabled, zero all counters at the shift boundary.
        // The new baselines will be zero after reset, which is correct.
        if (sc.auto_reset_on_shift) {
            proximityResetAll();
            saveProximityCounters();
        }

        // T8 — turno_data.txt: build the shift-end body ONCE here (same inputs
        // the email uses below) and persist it locally. Runs on EVERY shift
        // close, even when the email guards below skip the send.
        String shiftEndBody;
        String shiftEndSys;
        {
            extern ConfigManager configManager;
            struct tm tnT8;
            proximityShiftDayAlignedDate(shiftDayCtx, &tnT8);
            char dateSufT8[16];
            snprintf(dateSufT8, sizeof(dateSufT8), "%02d-%02d-%04d",
                     tnT8.tm_mday, tnT8.tm_mon + 1, tnT8.tm_year + 1900);
            shiftEndSys = configManager.getSdcardName();
            if (shiftEndSys.isEmpty()) shiftEndSys = "SD_LOG";
            String csvPathT8 = sd_layout::dayFile(shiftEndSys, "PROXIMITY", String(dateSufT8), "csv");
            int chartStartHourT8 = 0, durHoursT8 = 0;
            proximityGetShiftWindow(endedShift, chartStartHourT8, durHoursT8, false);
            shiftEndBody = proximityBuildShiftEmailBody(csvPathT8, chartStartHourT8, durHoursT8,
                                                       shiftPiecesDI1, shiftOeeDI1,
                                                       shiftAvailDI1, shiftPerfDI1, shiftQualDI1,
                                                       shiftScrapDI1, shiftSetupDI1,
                                                       shiftRunSec, shiftPlannedSec, shiftElapsedSec,
                                                       shiftDayCtx);
            if (shiftTruncated) {
                shiftEndBody += F("\nNOTE: shift totals above cover only part of the shift ");
                shiftEndBody += F("(live ");
                shiftEndBody += String((unsigned long)shiftPiecesDI1);
                shiftEndBody += F(" pcs vs ");
                shiftEndBody += String((unsigned long)csvShiftTotal);
                shiftEndBody += F(" pcs in the hourly breakdown — likely a mid-shift reboot).\n");
            }
            // Dated T8b (data+hora) + latest pointer (brief: turno_data.txt = mais recente).
            {
                int turnNumT8 = proximityShiftTurnNumber(proximityGetShiftConfig(), endedShift);
                struct tm teT8;
                localtime_r(&shiftDayCtx, &teT8);
                String datedPath = proximityShiftDataTxtPathDated(
                    shiftEndSys, turnNumT8,
                    tnT8.tm_year + 1900, tnT8.tm_mon + 1, tnT8.tm_mday,
                    teT8.tm_hour, teT8.tm_min);
                _proxWriteShiftDataTxtAt(shiftEndBody, shiftEndSys, datedPath);
                _proxWriteShiftDataTxtAt(shiftEndBody, shiftEndSys,
                                         proximityShiftDataTxtPath(shiftEndSys));
            }
        }

        // Auto-email: send today's proximity CSV folder when shiftday_emails are configured.
        // Distinguish the guard failures so a missed 6h email is traceable in the
        // Serial + Web monitor logs (each skip path logs with bypassDedup=true).
        if (sc.shiftday_emails[0] != '\0' && isEmailConfiguredAndWifiConnected() && storageIsAvailable()) {
            time_t rawNow = time(nullptr);
            if (rawNow > 1000000000L) {
                // Folder + subject use the pre-boundary context: at a
                // day-boundary shift end (22h/23h/00h/18h, …) rawNow already
                // belongs to the new day folder while the ended shift's ticks
                // live in the previous one.
                struct tm tn;
                proximityShiftDayAlignedDate(shiftDayCtx, &tn);
                char dateSuf[16];
                snprintf(dateSuf, sizeof(dateSuf), "%02d-%02d-%04d",
                         tn.tm_mday, tn.tm_mon + 1, tn.tm_year + 1900);
                extern ConfigManager configManager;
                String sdName = configManager.getSdcardName();
                if (sdName.isEmpty()) sdName = "SD_LOG";
                String csvPath = sd_layout::dayFile(sdName, "PROXIMITY", String(dateSuf), "csv");

#if ENABLE_SDCARD==1
                bool usingSD = (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted());
                // FTP holds the SD exclusively during its cycle (HISTORIC #57):
                // short lock timeout then treat it as "not found" (skip email).
                bool csvMissing = false;
                if (usingSD) {
                    if (!sdMutexLock(pdMS_TO_TICKS(1500))) {
                        csvMissing = true;
                    } else {
                        csvMissing = !SD_MMC.exists(csvPath.c_str());
                        sdMutexUnlock();
                    }
                }
                if (csvMissing) {
                    sendVirtualMessage(String(F("Prox: shift email skipped, CSV not found: ")) + csvPath, WARN);
                } else {
#endif
                    extern bool startEmailDayAsync(const String&, const String&, const String&);
                    extern String emailDayCmdExtra;
                    // Build subject: "{system_name} - ShiftEnd - DD-MM-YYYY"
                    String sysName = configManager.getSystemName();
                    if (sysName.length() == 0) sysName = String(F("Node32-HUB"));
                    String subject = sysName + String(F(" - ")) + L(activeLang->emailShiftEndTag) + String(F(" - ")) + dateSuf;
                    // Build body: "Registos:", pieces + OEE, hourly ASCII chart, footer
                    // T8: reuse the prebuilt shiftEndBody (identical inputs) — the
                    // email send itself is unchanged.
                    String body = shiftEndBody;
                    emailDayCmdExtra = "";
                    if (startEmailDayAsync(csvPath, subject, body)) {
                        sendVirtualMessage(String(F("Prox: shift-end email started -> ")) + String(dateSuf), INFO);
                    } else {
                        sendVirtualMessage(F("Prox: shift-end email failed to start"), WARN);
                    }
#if ENABLE_SDCARD==1
                }
#endif
            }
        } else {
            if (sc.shiftday_emails[0] == '\0') {
                sendVirtualMessage(F("Prox: shift-end email skipped, no shiftday emails configured"), INFO, false, true);
            } else if (!isEmailConfiguredAndWifiConnected()) {
                sendVirtualMessage(F("Prox: shift-end email skipped, email not configured or no network"), WARN, false, true);
            } else {
                sendVirtualMessage(F("Prox: shift-end email skipped, storage unavailable"), WARN, false, true);
            }
        }

        // Capture new shift baselines.
        _currentShiftIdx = newShift;
        for (uint8_t i = 0; i < PROXIMITY_CHANNELS; i++) {
            noInterrupts();
            _shiftCountInBase[i]    = _ch[i].count_in;
            _shiftCountOutBase[i]   = _ch[i].count_out;
            _shiftRunSecBase[i]     = _ch[i].running_seconds;
            _shiftDowntSecBase[i]   = _ch[i].downtime_seconds;
            _shiftGoodPartsBase[i]  = _ch[i].good_parts;
            _shiftSetupBase[i]      = _ch[i].setup_count;
            interrupts();
        }
        // Persist the fresh bases (+ co-save counters): a reboot before the
        // next counter save must pair these bases with counters saved at or
        // after the capture, never with a stale pre-capture save.
        saveProximityShiftBaselines();
        saveProximityCounters();
    }
}


void loadProximityConfig() {
    if (!littlefsBegin()) return;
    File f = LittleFS.open(PROX_CFG_PATH, "r");
    if (!f) {
        sendVirtualMessage(F("Prox config: no saved file, using defaults"), INFO);
        return;
    }
    SpiRamJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err != DeserializationError::Ok) {
        sendVirtualMessage(F("Prox config: JSON parse failed, keeping defaults"), WARN);
        return;
    }

    // Unified format: {"channels":[...], "shift":{...}, ...}. Channels may be
    // read from doc["channels"], or from the top-level array (legacy format).
    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull()) arr = doc["channels"].as<JsonArray>();

    uint8_t i = 0;
    for (JsonVariant v : arr) {
        if (i >= PROXIMITY_MAX_CHANNELS) break;
        JsonObject obj = v.as<JsonObject>();
        const char* n = obj["n"] | "";
        String nm = n;
        // Migrate old auto-generated default names ("Channel 0".."Channel 7") to DI1..DI8.
        // User-customized names are left untouched.
        if (nm.startsWith("Channel ")) {
            int oldIdx = nm.substring(8).toInt();
            if (oldIdx >= 0 && oldIdx <= 7) nm = String("DI") + String(oldIdx + 1);
        }
        strncpy(_ch[i].name, nm.c_str(), sizeof(_ch[i].name) - 1);
        _ch[i].name[sizeof(_ch[i].name) - 1] = '\0';
        _ch[i].mode           = obj["m"]  | _ch[i].mode;
#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
        _ch[i].gpio_in        = proximityDefaultGpioIn(i);   // fixed DI pins — ignore saved gi (even 255)
#else
        _ch[i].gpio_in        = obj["gi"] | _ch[i].gpio_in;
#endif
        _ch[i].gpio_out       = obj["go"] | _ch[i].gpio_out;
        _ch[i].gpio_in2       = (uint8_t)(obj["gi2"] | (int)_ch[i].gpio_in2);
        _ch[i].debounce_ms    = obj["db"] | _ch[i].debounce_ms;
        _ch[i].goal           = obj["gl"] | _ch[i].goal;
        _ch[i].pulse_delay_ms = obj["pd"] | _ch[i].pulse_delay_ms;
        _ch[i].pulse_dur_ms   = obj["pu"] | _ch[i].pulse_dur_ms;
        if (_ch[i].pulse_dur_ms > PROX_DO_MAX_PULSE_MS) _ch[i].pulse_dur_ms = PROX_DO_MAX_PULSE_MS;
        _ch[i].monitor_only   = obj["mo"] |
#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
                                 true;   // 8DI8DO: passive default (Y-wiring)
#else
                                 false;
#endif
        _ch[i].multiplier     = (int32_t)(obj["ml"] | 1);
        if (_ch[i].multiplier < 1) _ch[i].multiplier = 1;      // legacy negatives/0 -> default
        if (_ch[i].multiplier > PROX_MULT_MAX) _ch[i].multiplier = PROX_MULT_MAX;
        _ch[i].divider        = (int32_t)(obj["dv"] | 1);
        if (_ch[i].divider < 1) _ch[i].divider = 1;            // legacy negatives/0 -> default
        if (_ch[i].divider > PROX_DIV_MAX) _ch[i].divider = PROX_DIV_MAX;
        _ch[i].target_ppm     = obj["tp"] | 0.0f;
        _ch[i].oee_manual     = obj["om"] | 0.0f;
        _ch[i].gate_ch        = (uint8_t)(obj["gc"] | 255);
        _ch[i].scrap_ch       = (uint8_t)(obj["sc"] | 255);
        if (_ch[i].gate_ch >= PROXIMITY_MAX_CHANNELS && _ch[i].gate_ch != 255) _ch[i].gate_ch = 255;
        if (_ch[i].scrap_ch >= PROXIMITY_MAX_CHANNELS && _ch[i].scrap_ch != 255) _ch[i].scrap_ch = 255;
        if (_ch[i].gate_ch == i) _ch[i].gate_ch = 255;   // prevent self-reference
        if (_ch[i].scrap_ch == i) _ch[i].scrap_ch = 255;
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
        _tca9554_ch[i] = (uint8_t)(obj["tc"] | (int)_tca9554_ch[i]);
#endif
        i++;
    }
    // Load the shift section from the same unified file (shift + shiftday emails).
    proximityShiftFromJson(doc);
    if (i > 0) {
        sendVirtualMessage(String(F("Prox config: loaded ")) + String((int)i) + F(" channel(s)"), INFO);
        { char _dbg[160]; snprintf(_dbg, sizeof(_dbg),
            "[prox:diag] load OK: ch0=%s m=%u db=%lu gl=%lu mo=%d ml=%d",
            _ch[0].name, (unsigned)_ch[0].mode, (unsigned long)_ch[0].debounce_ms,
            (unsigned long)_ch[0].goal, (int)_ch[0].monitor_only, (int)_ch[0].multiplier);
          sendVirtualMessage(String(_dbg), INFO, false, true); }
    }
}

bool saveProximityConfig() {
    if (!littlefsBegin()) return false;
    // Ensure /config directory exists before opening the file.
    if (!LittleFS.exists("/config")) LittleFS.mkdir("/config");
    SpiRamJsonDocument doc(8192);
    JsonObject root = doc.to<JsonObject>();
    JsonArray arr = root.createNestedArray("channels");
    for (uint8_t i = 0; i < PROXIMITY_MAX_CHANNELS; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["n"]  = _ch[i].name;
        obj["m"]  = (uint8_t)_ch[i].mode;
        obj["gi"] = _ch[i].gpio_in;
        obj["go"] = _ch[i].gpio_out;
        obj["gi2"]= _ch[i].gpio_in2;
        obj["db"] = _ch[i].debounce_ms;
        obj["gl"] = _ch[i].goal;
        obj["pd"] = _ch[i].pulse_delay_ms;
        obj["pu"] = _ch[i].pulse_dur_ms;
        obj["mo"] = _ch[i].monitor_only;
        obj["ml"] = _ch[i].multiplier;
        obj["dv"] = _ch[i].divider;
        obj["tp"] = _ch[i].target_ppm;
        obj["om"] = _ch[i].oee_manual;
        obj["gc"] = _ch[i].gate_ch;
        obj["sc"] = _ch[i].scrap_ch;
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
        obj["tc"] = _tca9554_ch[i];
#endif
    }
    // Shift + shiftday emails live in the SAME file (section "shift").
    // Planned stops live there too (section "stops").
    proximityShiftToJson(root);
    proximityStopsToJson(root);

    String cfgTmp = String(PROX_CFG_PATH) + ".tmp";
    File f = LittleFS.open(cfgTmp, "w");
    if (!f) {
        sendVirtualMessage(F("Prox config save FAILED (could not open tmp file)"), ERROR);
        return false;
    }
    size_t bytes = serializeJsonPretty(doc, f);
    f.close();
    storNoteLfsWrite(bytes);
    if (bytes > 0) {
        LittleFS.remove(PROX_CFG_PATH);
        LittleFS.rename(cfgTmp, PROX_CFG_PATH);
        sendVirtualMessage(String(F("Prox config saved (")) + String((int)bytes) + F(" bytes, unified)"), INFO, false, true);
    } else {
        LittleFS.remove(cfgTmp);
        sendVirtualMessage(F("Prox config save FAILED (wrote 0 bytes)"), ERROR);
        return false;
    }
    return true;
}

void proximityConfigureChannel(uint8_t ch, const char* name, uint8_t mode,
                                uint8_t gpio_in, uint8_t gpio_out, uint8_t gpio_in2,
                                uint32_t debounce_ms, uint32_t goal,
                                uint32_t pulse_delay_ms, uint32_t pulse_dur_ms,
                                bool monitor_only, int32_t multiplier,
                                float target_ppm, float oee_manual,
                                uint8_t gate_ch, uint8_t scrap_ch,
                                int32_t divider) {
    if (ch >= PROXIMITY_MAX_CHANNELS) return;
    // Clamp DO pulse duration to prevent relay overheating.
    if (pulse_dur_ms > PROX_DO_MAX_PULSE_MS) pulse_dur_ms = PROX_DO_MAX_PULSE_MS;
    // Brief interrupt lock: debounce_ms is read inside the ISR (PROXIMITY_USE_PCNT==0).
    noInterrupts();
    strncpy(_ch[ch].name, name ? name : "", sizeof(_ch[ch].name) - 1);
    _ch[ch].name[sizeof(_ch[ch].name) - 1] = '\0';
    _ch[ch].mode           = mode;
#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
    _ch[ch].gpio_in        = proximityDefaultGpioIn(ch);   // fixed DI pins — always the board pin
#else
    _ch[ch].gpio_in        = gpio_in;
#endif
    _ch[ch].gpio_out       = gpio_out;
    _ch[ch].gpio_in2       = gpio_in2;
    _ch[ch].debounce_ms    = debounce_ms;
    _ch[ch].goal           = goal;
    _ch[ch].pulse_delay_ms = pulse_delay_ms;
    _ch[ch].pulse_dur_ms   = pulse_dur_ms;
    _ch[ch].monitor_only   = monitor_only;
    _ch[ch].multiplier     = (multiplier < 1) ? 1 : (multiplier > PROX_MULT_MAX) ? PROX_MULT_MAX : multiplier;
    int32_t newDiv         = (divider < 1) ? 1 : (divider > PROX_DIV_MAX) ? PROX_DIV_MAX : divider;
    if (newDiv != _ch[ch].divider) {
        // Divider change restarts fractional accumulation: a remainder from the
        // old divisor would otherwise materialize phantom parts under the new one.
        _divRemIn[ch]  = 0;
        _divRemOut[ch] = 0;
        _ch[ch].divider = newDiv;
    }
    _ch[ch].target_ppm     = target_ppm;
    _ch[ch].oee_manual     = oee_manual;
    _ch[ch].gate_ch        = (gate_ch < PROXIMITY_MAX_CHANNELS && gate_ch != ch) ? gate_ch : 255;
    _ch[ch].scrap_ch       = (scrap_ch < PROXIMITY_MAX_CHANNELS && scrap_ch != ch) ? scrap_ch : 255;
    interrupts();
}

#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
uint8_t proximityGetTCA9554Ch(uint8_t ch) {
    if (ch >= PROXIMITY_MAX_CHANNELS) return 255;
    return _tca9554_ch[ch];
}
void proximitySetTCA9554Ch(uint8_t ch, uint8_t tca_ch) {
    if (ch >= PROXIMITY_MAX_CHANNELS) return;
    _tca9554_ch[ch] = tca_ch;
}
#endif

// ---- Modbus holding register update --------------------------------
// Maps live channel data to HR 100..175 (0x0064..0x00AF).
// Called from loop_impl.h at slow rate (~1 Hz is enough).

#if ENABLE_MODBUS==1 && ENABLE_MODBUS_SLAVE==1
#include "../modbus_slave.h"

void proximityModbusTick() {
    const uint8_t n = proximityChannelCount();
    for (uint8_t i = 0; i < n; ++i) {
        ProximityChannel ch;
        noInterrupts();
        ch = _ch[i];
        interrupts();

        const uint16_t base = (uint16_t)(100u + i * 16u);
        modbusSlaveSetHreg(base + 0, (uint16_t)ch.count_in);
        modbusSlaveSetHreg(base + 1, (uint16_t)ch.count_out);
        // net stock as INT16 (signed difference, clamped to [-32768, 32767])
        int32_t net = (int32_t)ch.count_in - (int32_t)ch.count_out;
        if (net >  32767) net =  32767;
        if (net < -32768) net = -32768;
        modbusSlaveSetHreg(base + 2, (uint16_t)(int16_t)net);
        modbusSlaveSetHreg(base + 3, (uint16_t)ch.goal);
        modbusSlaveSetHreg(base + 4, (uint16_t)(ch.rate_ppm * 10.0f));
        modbusSlaveSetHreg(base + 5, ch.state ? 1u : 0u);
        modbusSlaveSetHreg(base + 6, (uint16_t)ch.debounce_ms);
        modbusSlaveSetHreg(base + 7, (uint16_t)(ch.total_in & 0xFFFFU));
        modbusSlaveSetHreg(base + 8, (uint16_t)((ch.total_in >> 16) & 0xFFFFU));
        modbusSlaveSetHreg(base + 9, (uint16_t)(ch.total_out & 0xFFFFU));
        // OEE as uint16: 0-1000 represents 0.0-100.0%
        uint32_t cIn = ch.count_in;
        uint32_t runSec = (ch.running_seconds >= _shiftRunSecBase[i])
            ? ch.running_seconds - _shiftRunSecBase[i] : 0;
        uint32_t goodP = (ch.good_parts >= _shiftGoodPartsBase[i])
            ? ch.good_parts - _shiftGoodPartsBase[i] : 0;
        uint32_t scrapIn = 0;
        if (ch.scrap_ch < PROXIMITY_CHANNELS && ch.scrap_ch != i) {
            ProximityChannel scrapCh = _ch[ch.scrap_ch];
            scrapIn = (scrapCh.count_in >= _shiftCountInBase[ch.scrap_ch])
                ? scrapCh.count_in - _shiftCountInBase[ch.scrap_ch] : 0;
        }
        uint32_t effectiveGood = (scrapIn > 0 && cIn > scrapIn)
            ? (cIn - scrapIn) : goodP;
        ProximityOeeResult r = proximityComputeOee(runSec, proximityCountElapsedSec(i), cIn, effectiveGood, ch.target_ppm, proximityChannelPlannedStopSec(i));
        modbusSlaveSetHreg(base + 10, (uint16_t)(r.oee * 10.0f));
    }
}

#else

void proximityModbusTick() {}  // stub when Modbus slave disabled

#endif // ENABLE_MODBUS==1 && ENABLE_MODBUS_SLAVE==1

#endif // ENABLE_PROXIMITY==1
