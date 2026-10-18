#include "touch7_display.h"
#include "languages/lang_active.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

// TouchMeteo-7 display engine: Waveshare ESP32-S3-Touch-LCD-7
// (ST7262 RGB 800x480 + GT911 touch + CH422G) driven by ESP32_Display_Panel,
// UI in LVGL v8.4. Single Arduino-loop task drives LVGL (no extra task/stack).
// Rule nº1: draw buffers, icons, tiles, JPEG slot all in PSRAM.

#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h> // reset/init for the quiesce window
#include <lvgl.h>
#include <Preferences.h> // boot-time NVS stats cache (see touch7SystemBegin)
#include "littlefs_manager.h" // boot-time LittleFS stats cache
#include <nvs_flash.h>        // nvs_get_stats
#include <esp_partition.h>    // esp_partition_find_first
#include <time.h>
#include <esp_cache.h>
#include <freertos/FreeRTOS.h> // T7-29: flash lock + VSYNC task-notify
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "psram_helpers.h"
#include "screen/display_profiles.h" // TOUCH7_WIDTH_PX/HEIGHT_PX
#include "touch7_config.h"
#include "touch7_audio.h"
#include "touch7_pir.h"
#include "touch7_alarms.h"
#include "touch7_log.h"
#include "touch7_weather.h"
#include "touch7_peers.h"
#include "touch7_peers_logic.h"
#include "sensors/sensor_config.h"
#if ENABLE_BH1750==1
#include "sensors/sensor_bh1750_config.h"
#endif
#if ENABLE_INA226==1
#include "sensors/sensor_ina226_config.h"
#endif
#if defined(BOARD_HAS_DS3231) && BOARD_HAS_DS3231==1
#include "rtc_ds3231.h"
#endif
#include "touch7_i2c.h"
#include "touch7_icons/touch7_icons.h" // MyAuraPT weather icon assets (v9)
#if ENABLE_AHT20_BMP280==1
#include "sensors/sensor_aht20_bmp280.h"
#endif

// Defined in diagnostics_impl.h; declared extern per TU (same pattern as
// sensor_config.cpp:44). Needed for the post-panel hardware retry.
#if ENABLE_AHT20_BMP280==1
extern SensorAHT20_BMP280 environmentalSensor;
#endif

using namespace esp_panel::drivers;
using namespace esp_panel::board;

// Runtime RGB pixel-clock tuning (C-linkage; must be at file scope — a
// linkage block is not allowed inside a function). Declared here to avoid
// pulling the esp_lcd rgb header; the handle type comes from the ops header.
extern "C" esp_err_t esp_lcd_rgb_panel_set_pclk(esp_lcd_panel_handle_t panel, uint32_t freq_hz);

Touch7Display touch7Display;

// NVS "all_configs" length cached at boot (see touch7SystemBegin).
size_t touch7NvsAllConfigsBytes = 0;
// Boot-cached flash/NVS stats for /api/status (see touch7SystemBegin).
bool touch7StatFlashMounted = false;
uint64_t touch7StatFlashTotal = 0, touch7StatFlashUsed = 0, touch7StatFlashFree = 0;
int touch7StatNvsTotalKb = -1, touch7StatNvsFreeKb = -1;

// Forward: UI click feedback (defined at the bottom of this file).
void touch7AudioClickSafe();
// Fullscreen fetch lives in touch7_peers.cpp (owns the JPEG state).
bool touch7PeerFetchFull(uint8_t idx, uint16_t* buf, uint16_t w, uint16_t h);

// ---------------------------------------------------------------------------
// LVGL glue (flush + touch)
// ---------------------------------------------------------------------------

// RGB VSYNC completion callback (IRAM-safe). With bounce==0 the panel's
// `on_vsync` handler runs this at each frame boundary; it wakes the loop
// task blocked in the flush until the frame-buffer switch has taken effect.
static volatile TaskHandle_t s_vsyncTask = nullptr;
// Anti-tearing state (declared early: used by touch7FlushCb below).
static bool s_doubleFb = false;
static const uint8_t* s_lastFb = nullptr;

// T7-29 cross-task flash lock. Flash writers on other tasks hold this while
// the cache is suspended; the flush try-locks through FB switch + VSYNC
// confirmation so the published front buffer cannot race a capture.
static SemaphoreHandle_t s_flashMutex = nullptr;
static void t7EnsureFlashMutex() {
    // Recursive: guards nest (central route dispatcher + per-handler guard +
    // PanelHold all take it on the same async task).
    if (!s_flashMutex) s_flashMutex = xSemaphoreCreateRecursiveMutex();
}

IRAM_ATTR static bool touch7VsyncCb(void* user_data) {
    TaskHandle_t t = user_data ? *(volatile TaskHandle_t*)user_data : nullptr;
    BaseType_t hp = pdFALSE;
    if (t) xTaskNotifyFromISR(t, ULONG_MAX, eNoAction, &hp);
    return (hp == pdTRUE);
}

static void touch7FlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    // Redraw-rate instrumentation (v9 dropped the public invalidation-area
    // list, so the per-area buckets from v8 are gone).
    static unsigned long s_flushes = 0, s_flushRepMs = 0;
    s_flushes++;
    unsigned long nowMs = millis();
    if (s_flushRepMs == 0) s_flushRepMs = nowMs;
    if (nowMs - s_flushRepMs >= 60000UL) {
        s_flushRepMs = nowMs;
        T7LOG("render flushes=%lu/60s idle=%lus dim=%d saver=%d",
              s_flushes, touch7PirIdleSec() / 1UL, touch7Display.isDimmed() ? 1 : 0,
              touch7Display.isSaverShown() ? 1 : 0);
        s_flushes = 0;
    }
    // Coherency: the CPU writes through the data cache, the RGB GDMA bypasses
    // it — write back each flushed area before it is scanned out.
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    esp_cache_msync(px_map, (size_t)w * (size_t)h * sizeof(lv_color_t),
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    // Anti-tearing (T7-29, double-FB only): present the freshly-rendered
    // buffer with an FB switch and wait for the VSYNC that confirms it took
    // effect. Keep the cross-task lock through that confirmation so captures
    // cannot copy the old front buffer after the panel has switched.
    // switchFrameBufferTo() runs flash-hosted GDMA code, so on contention we
    // skip presenting this frame (never block => no deadlock).
    LCD* lcd = (LCD*)lv_display_get_user_data(disp);
    if (s_doubleFb && lv_display_flush_is_last(disp) && lcd) {
        t7EnsureFlashMutex();
        if (!s_flashMutex || xSemaphoreTakeRecursive(s_flashMutex, 0) == pdTRUE) {
            TaskHandle_t notifyTask = xTaskGetCurrentTaskHandle();
            s_vsyncTask = notifyTask;
            // Clear a stale notification BEFORE switching: VSYNC may arrive
            // immediately after switchFrameBufferTo() and must not be erased.
            if (notifyTask) ulTaskNotifyValueClear(NULL, ULONG_MAX);
            lcd->switchFrameBufferTo(px_map);
            if (notifyTask) {
                // Bounded wait so a missed VSYNC can never wedge the loop.
                if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)) > 0) {
                    s_lastFb = (const uint8_t*)px_map;
                }
            } else {
                s_lastFb = (const uint8_t*)px_map;
            }
            if (s_flashMutex) xSemaphoreGiveRecursive(s_flashMutex);
        }
    }
    lv_display_flush_ready(disp);
}

static void touch7TouchCb(lv_indev_t* drv, lv_indev_data_t* data) {
    Touch* tp = (Touch*)lv_indev_get_user_data(drv);
    TouchPoint point;
    data->state = LV_INDEV_STATE_RELEASED;
    // T7-24 instrumentation: a GT911 read that hits the I2C timeout blocks
    // here ~1.1 s, so time every poll — slow polls (dt>200ms) measure the
    // true failure fraction (the boolean alone can't tell "no touch" apart
    // from "I2C error"). Reported every 60 s via T7LOG.
    static unsigned long s_tPolls = 0, s_tSlow = 0, s_tRepMs = 0;
    // T7-26b: phantom-touch hunt — count PRESS events + coordinate bbox.
    // A flaky GT911 alternating press/release with wandering coords would
    // invalidate nearly every pump (focus/style churn) AND reset the PIR
    // idle timer every poll (dim/saver never engage).
    static unsigned long s_tPress = 0;
    static int s_xMin = 9999, s_yMin = 9999, s_xMax = -1, s_yMax = -1;
    unsigned long t0 = millis();
    bool pressed = (tp->readPoints(&point, 1, 0) > 0);
    unsigned long dtms = millis() - t0;
    s_tPolls++;
    if (dtms > 200) s_tSlow++;
    if (pressed) {
        s_tPress++;
        if (point.x < s_xMin) s_xMin = point.x;
        if (point.y < s_yMin) s_yMin = point.y;
        if (point.x > s_xMax) s_xMax = point.x;
        if (point.y > s_yMax) s_yMax = point.y;
        data->point.x = point.x;
        data->point.y = point.y;
        data->state = LV_INDEV_STATE_PRESSED;
        touch7PirTouchActivity(); // any touch = presence, wakes screensaver
    }
    unsigned long nowMs = millis();
    if (s_tRepMs == 0) s_tRepMs = nowMs;
    if (nowMs - s_tRepMs >= 60000UL) {
        s_tRepMs = nowMs;
        T7LOG("touch polls=%lu slow=%lu press=%lu bbox=(%d,%d)-(%d,%d)", s_tPolls, s_tSlow,
              s_tPress, s_xMin, s_yMin, s_xMax, s_yMax);
    }
}

// Remote pointer state: HTTP updates coordinates/press state; the loop-owned
// LVGL indev samples it. A short critical section keeps the tuple coherent.
static portMUX_TYPE s_remoteTapMux = portMUX_INITIALIZER_UNLOCKED;
static int s_tapX = -1;
static int s_tapY = -1;
static bool s_remotePointerDown = false;
static bool s_remoteOneShot = false;
static bool s_remotePressPending = false;
static uint32_t s_remoteUpdatedMs = 0;

static void touch7RemoteTapCb(lv_indev_t* drv, lv_indev_data_t* data) {
    (void)drv;
    data->state = LV_INDEV_STATE_RELEASED;
    int x, y;
    bool valid, pressed, oneShot;
    uint32_t updated;
    uint32_t now = millis();
    portENTER_CRITICAL(&s_remoteTapMux);
    x = s_tapX;
    y = s_tapY;
    valid = (x >= 0 && y >= 0);
    oneShot = s_remoteOneShot;
    updated = s_remoteUpdatedMs;
    pressed = false;
    if (oneShot) {
        if (s_remotePressPending) {
            pressed = true;
            s_remotePressPending = false;
        } else {
            s_remoteOneShot = false;
        }
    } else if (s_remotePointerDown) {
        if (touch7RemotePointerExpired(now, updated)) {
            s_remotePointerDown = false; // fail-safe if the browser disappears
        } else {
            pressed = true;
        }
    }
    portEXIT_CRITICAL(&s_remoteTapMux);

    if (valid) {
        data->point.x = x;
        data->point.y = y;
    }
    if (pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        touch7PirTouchActivity();
    }
}

// ---------------------------------------------------------------------------
// Themes (Claro / Medio / Escuro) — all blue tones per plan.
// ---------------------------------------------------------------------------

#define TOUCH7_THEME_COUNT 6

struct Touch7Theme {
    uint32_t bg, bg2, panel, text, dim, accent, accentDk, panelText;
    uint8_t panelOpa;      // 0-255 (255 = solid; lower = frosted card)
    uint32_t borderColor;
};

// Theme 0 Claro / 1 Medio (RSK World indigo-purple) / 2 Escuro /
// 3 Sky (sky + dark frosted cards) / 4 Navy (Home-Assistant-style dark cards
// + bright blue accent) / 5 Bright (bright blue sky + yellow sun icons).
static const Touch7Theme TOUCH7_THEMES[TOUCH7_THEME_COUNT] = {
    {0x667eea, 0x764ba2, 0x1B2A6B, 0xFFFFFF, 0xCBD5E0, 0x667eea, 0x4C3575, 0xFFFFFF, 120, 0x5A6CC0}, // Claro
    {0x667eea, 0x764ba2, 0x1B2A6B, 0xFFFFFF, 0xCBD5E0, 0x764BA2, 0x4C3575, 0xFFFFFF, 120, 0x5A6CC0}, // Medio (RSK)
    {0x1A202C, 0x2D3748, 0x2D3748, 0xF7FAFC, 0xA0AEC0, 0x6A7BE0, 0x4A5ABF, 0xF7FAFC, 255, 0x8093F1}, // Escuro
    {0x2B5C8F, 0x6F9FCA, 0x1C3F66, 0xFFFFFF, 0xA9C4E0, 0x3E6EA8, 0x2B5C8F, 0xFFFFFF, 153, 0x4A7BAB}, // Sky
    {0x0E1B2A, 0x22344A, 0x1B2838, 0xF2F6FA, 0x9FB2C6, 0x2E7DBE, 0x1E5C93, 0xF2F6FA, 180, 0x2C4A6B}, // Navy
    {0x2F7FC1, 0x7FB8E0, 0x3A7FC0, 0xFFFFFF, 0xCFE4F5, 0x2E7DBE, 0x1E5C93, 0xFFFFFF, 178, 0x5A9BD4}, // Bright
};

// ---------------------------------------------------------------------------
// Procedural weather icons (original, drawn in code into PSRAM RGB565).
// 160x160, no alpha (bg = theme bg, repainted on theme change).
// ---------------------------------------------------------------------------

#define TOUCH7_ICON_PX 160

static void pxSet(uint16_t* buf, int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= TOUCH7_ICON_PX || y >= TOUCH7_ICON_PX) return;
    buf[y * TOUCH7_ICON_PX + x] = c;
}

static void pxDisc(uint16_t* buf, int cx, int cy, int r, uint16_t c) {
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++) {
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= r * r) pxSet(buf, x, y, c);
        }
}

static void pxLine(uint16_t* buf, int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pxSet(buf, x0, y0, c);
        pxSet(buf, x0 + 1, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void touch7PaintIcon(uint16_t* buf, Touch7CondGroup g, bool isDay, uint16_t bg) {
    const uint16_t YEL = 0xFFE0, YELD = 0xF5C0, WHT = 0xFFFF, GRY = 0xC618;
    const uint16_t BLU = 0x001F, LBLU = 0x3D7F, PALE = 0xEF7D;
    for (int i = 0; i < TOUCH7_ICON_PX * TOUCH7_ICON_PX; i++) buf[i] = bg;
    bool sun = (g == TOUCH7_COND_CLEAR) || (g == TOUCH7_COND_PARTLY);
    bool cloud = (g != TOUCH7_COND_CLEAR);
    if (sun) {
        if (isDay) {
            pxDisc(buf, 70, 60, 26, YEL);
            pxDisc(buf, 64, 54, 20, YELD);
            for (int k = 0; k < 8; k++) {
                float a = k * 3.14159f / 4.0f;
                pxLine(buf, 70 + (int)(34 * cos(a)), 60 + (int)(34 * sin(a)),
                       70 + (int)(48 * cos(a)), 60 + (int)(48 * sin(a)), YEL);
            }
        } else {
            pxDisc(buf, 70, 60, 24, PALE);
            pxDisc(buf, 78, 52, 20, bg); // crescent cut
        }
    }
    if (cloud) {
        int oy = (g == TOUCH7_COND_CLEAR) ? 0 : 0;
        int cx = sun ? 100 : 75, cy = sun ? 95 : 80;
        (void)oy;
        pxDisc(buf, cx - 28, cy + 6, 20, GRY);
        pxDisc(buf, cx, cy - 6, 26, WHT);
        pxDisc(buf, cx + 28, cy + 6, 20, WHT);
        for (int x = cx - 44; x <= cx + 44; x++)
            for (int y = cy + 6; y <= cy + 20; y++) pxSet(buf, x, y, WHT);
    }
    if (g == TOUCH7_COND_FOG) {
        for (int r = 0; r < 3; r++)
            for (int x = 40; x < 120; x++) {
                pxSet(buf, x, 70 + r * 16, GRY);
                pxSet(buf, x, 71 + r * 16, GRY);
            }
    }
    if (g == TOUCH7_COND_RAIN) {
        for (int k = 0; k < 3; k++)
            pxLine(buf, 55 + k * 22, 105, 48 + k * 22, 135, BLU);
    }
    if (g == TOUCH7_COND_SNOW) {
        pxDisc(buf, 60, 118, 3, WHT); pxDisc(buf, 82, 128, 3, WHT); pxDisc(buf, 102, 116, 3, WHT);
    }
    if (g == TOUCH7_COND_STORM) {
        pxLine(buf, 80, 95, 66, 118, YEL);
        pxLine(buf, 66, 118, 78, 118, YEL);
        pxLine(buf, 78, 118, 64, 140, YEL);
    }
}

// ---------------------------------------------------------------------------
// Implementation state
// ---------------------------------------------------------------------------

struct Touch7Impl {
    Board* board;
    LCD* lcd;
    Touch* touch;
    lv_display_t* disp;
    lv_color_t* dbuf1;
    lv_color_t* dbuf2;
    // Tabs
    lv_obj_t* tabAgora;
    lv_obj_t* tabPrev;
    lv_obj_t* tabPeers;
    lv_obj_t* tabCfg;
    lv_obj_t* tabBtns[4];
    lv_obj_t* statusWifi;
    lv_obj_t* statusClock;
    lv_obj_t* statusTemp;
    // Agora
    lv_obj_t* agoraTemp;
    lv_obj_t* agoraSub;
    lv_obj_t* agoraMeta;
    lv_obj_t* agoraSys;
    lv_obj_t* iconImg;
    lv_image_dsc_t iconDsc;
    uint16_t* iconBuf;
    Touch7CondGroup iconGroup;
    bool iconDay;
    lv_anim_t iconAnim;
    bool iconAnimOn;
    // Forecast
    lv_obj_t* prevToday;
    lv_obj_t* prevDays[5];
    // Peers
    lv_obj_t* peerImg[TOUCH7_PEERS_MAX];
    lv_obj_t* peerLbl[TOUCH7_PEERS_MAX];
    lv_obj_t* peerTile[TOUCH7_PEERS_MAX];
    lv_image_dsc_t peerDsc[TOUCH7_PEERS_MAX];
    uint16_t* peerBuf[TOUCH7_PEERS_MAX];
    lv_obj_t* fsBox;
    lv_obj_t* fsImg;
    lv_image_dsc_t fsDsc;
    uint16_t* fsBuf;
    // Cfg
    lv_obj_t* cfgThemeBtns[TOUCH7_THEME_COUNT];
    lv_obj_t* cfgClockBtns[5];
    lv_obj_t* cfgBright;
    lv_obj_t* cfgBrightValue;
    lv_obj_t* cfgDim;
    lv_obj_t* cfgSaver;
    lv_obj_t* cfgWifi;
    lv_obj_t* cfgSys;
    // Overlay
    lv_obj_t* toast;
    lv_obj_t* saver;
    lv_obj_t* saverClock;
    lv_obj_t* saverSub;
    lv_obj_t* saverArc;
    lv_obj_t* saverHour;
    lv_obj_t* saverMin;
    lv_point_precise_t saverHandH[2];
    lv_point_precise_t saverHandM[2];
    // Styles
    lv_style_t stPanel, stBtn, stBtnPr, stBar, stBarInd;
    uint8_t curTab;
};

static Touch7Impl* IM = 0;
static unsigned long s_lastTickMs = 0;
static unsigned long s_lastSecMs = 0;
static unsigned long s_lastAutoMs = 0;
static unsigned long s_lastPumpMs = 0;
static volatile bool s_renderActive = false;
static int s_lastBacklight = -1;
static bool s_brightManual = false;
static bool s_brightnessProgrammatic = false;
// Flash-write guard (see header): set from other tasks around LittleFS/NVS
// writes; the loop task then skips rendering while it is held.
static volatile bool s_flashHold = false;
// PanelHold nesting depth (T7-29): keeps nested holds from re-delling the
// panel or resurrecting it before the outermost release.
static int s_panelDepth = 0;
static volatile bool s_rebootPending = false;
// OTA: true while touch7OtaBegin() has stopped the panel and not yet rebooted.
static bool s_otaPanelStopped = false;
// LVGL v9 display/indev handles (created in begin()). The display object
// persists across PanelHold rebuilds; only its buffers + user_data are
// rewired in resume().
static lv_display_t* s_disp = nullptr;
static lv_indev_t* s_indev = nullptr;
static lv_indev_t* s_indevRemote = nullptr;
// Last requested brightness 0-255 (restored after panel rebuild).
static uint8_t s_lastBright = 220;

Touch7Display::Touch7Display()
    : _ready(false), _dimmed(false), _theme(1), _clockStyle(0), _impl(0) {}

// ---- small LVGL helpers ------------------------------------------------------

static lv_obj_t* t7CreateLabel(lv_obj_t* parent) {
    lv_obj_t* label = lv_label_create(parent);
    // LVGL's default label theme applies its own dark text style, which does
    // not inherit the screen/container colour. Set the foreground directly on
    // every label so it stays readable on all six Touch7 backgrounds.
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    return label;
}

static lv_obj_t* t7Label(lv_obj_t* parent, const lv_font_t* font, int x, int y, int w) {
    lv_obj_t* l = t7CreateLabel(parent);
    lv_obj_set_pos(l, x, y);
    if (w > 0) {
        lv_obj_set_width(l, w);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    }
    lv_obj_set_style_text_font(l, font, 0);
    return l;
}

static void t7StyleBtn(Touch7Impl* im, lv_obj_t* b) {
    lv_obj_add_style(b, &im->stBtn, 0);
    lv_obj_add_style(b, &im->stBtnPr, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 8, 0);
}

static void t7RefreshSelectionStyles(uint8_t theme, uint8_t clockStyle, uint8_t tab) {
    if (!IM) return;
    const Touch7Theme& th = TOUCH7_THEMES[theme < TOUCH7_THEME_COUNT ? theme : 1];
    for (int i = 0; i < TOUCH7_THEME_COUNT; i++) {
        lv_obj_t* button = IM->cfgThemeBtns[i];
        if (!button) continue;
        bool selected = (i == theme);
        lv_obj_set_style_border_width(button, selected ? 3 : 1, 0);
        lv_obj_set_style_border_color(button,
                                      selected ? lv_color_white() : lv_color_hex(th.borderColor), 0);
    }
    for (int i = 0; i < 5; i++) {
        lv_obj_t* button = IM->cfgClockBtns[i];
        if (!button) continue;
        bool selected = (i == clockStyle);
        lv_obj_set_style_border_width(button, selected ? 3 : 1, 0);
        lv_obj_set_style_border_color(button,
                                      selected ? lv_color_white() : lv_color_hex(th.borderColor), 0);
    }
    for (int i = 0; i < 4; i++) {
        lv_obj_t* button = IM->tabBtns[i];
        if (!button) continue;
        bool selected = (i == tab);
        lv_obj_set_style_border_width(button, selected ? 3 : 1, 0);
        lv_obj_set_style_border_color(button,
                                      selected ? lv_color_white() : lv_color_hex(th.borderColor), 0);
    }
}

static void t7IconPulseCb(void* var, int32_t v) {
    lv_obj_set_style_transform_scale_x((lv_obj_t*)var, v, 0);
    lv_obj_set_style_transform_scale_y((lv_obj_t*)var, v, 0);
}
static lv_anim_t s_iconPulse;

static void startIconAnim(Touch7Impl* im) {
    // MyAuraPT icon pulse: zoom 100% -> 121% and back (1.2 s each way,
    // infinite ping-pong). Tearing during the animation is accepted; the
    // T7-29 double buffer keeps everything else tear-free.
    if (!im || !im->iconImg) return;
    lv_anim_del(im->iconImg, t7IconPulseCb);
    lv_obj_set_style_transform_pivot_x(im->iconImg, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(im->iconImg, LV_PCT(50), 0);
    lv_anim_init(&s_iconPulse);
    lv_anim_set_var(&s_iconPulse, im->iconImg);
    lv_anim_set_exec_cb(&s_iconPulse, t7IconPulseCb);
    lv_anim_set_values(&s_iconPulse, 400, 480); // 1.56x -> ~1.88x (Aura pulse)
    lv_anim_set_time(&s_iconPulse, 2000);
    lv_anim_set_playback_time(&s_iconPulse, 2000);
    lv_anim_set_repeat_count(&s_iconPulse, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_iconPulse);
    im->iconAnimOn = true;
}

// Pulse only while the AGORA tab is visible and the screen is awake: in
// double-buffer FULL mode every animation frame redraws the whole 800x480, so
// running it on other tabs (or while dimmed/saver) needlessly burns FPS.
static void t7IconAnimSet(bool on) {
    if (!IM || !IM->iconImg) return;
    if (on) {
        if (!IM->iconAnimOn) startIconAnim(IM);
    } else if (IM->iconAnimOn) {
        lv_anim_del(IM->iconImg, t7IconPulseCb);
        IM->iconAnimOn = false;
        // Leave the icon at its resting scale.
        lv_obj_set_style_transform_scale_x(IM->iconImg, 400, 0);
        lv_obj_set_style_transform_scale_y(IM->iconImg, 400, 0);
    }
}

// ---- events -------------------------------------------------------------------

static void evtTab(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (!IM) return;
    for (int i = 0; i < 4; i++) {
        if (i == idx) lv_obj_set_hidden(i == 0 ? IM->tabAgora : i == 1 ? IM->tabPrev : i == 2 ? IM->tabPeers : IM->tabCfg, false);
        else lv_obj_set_hidden(i == 0 ? IM->tabAgora : i == 1 ? IM->tabPrev : i == 2 ? IM->tabPeers : IM->tabCfg, true);
    }
    IM->curTab = (uint8_t)idx;
    t7RefreshSelectionStyles(touch7Display.theme(), touch7Display.clockStyle(), (uint8_t)idx);
    t7IconAnimSet(idx == 0 && !touch7Display.isDimmed() && !touch7Display.isSaverShown());
    touch7AudioClickSafe();
}

static void evtTheme(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (!touch7ThemeValid(idx)) return;
    char js[32];
    snprintf(js, sizeof(js), "{\"theme\":%d}", idx);
    if (touch7ConfigManager.updateFromJsonDeferred(String(js))) {
        touch7Display.applyTheme((uint8_t)idx);
    }
}

static void evtClock(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (!touch7ClockValid(idx)) return;
    char js[40];
    snprintf(js, sizeof(js), "{\"clock_style\":%d}", idx);
    if (touch7ConfigManager.updateFromJsonDeferred(String(js))) {
        touch7Display.applyClockStyle((uint8_t)idx);
    }
}

static void evtBright(lv_event_t* e) {
    if (s_brightnessProgrammatic) return;
    lv_obj_t* bar = (lv_obj_t*)lv_event_get_target(e);
    int v = (int)lv_slider_get_value(bar);
    touch7Display.setBrightness((uint8_t)((v * 255) / 100));
    touch7Display.setBrightManual(true); // user override until AUTO re-arms
    char js[64];
    snprintf(js, sizeof(js), "{\"backlight_auto\":false,\"backlight_pct\":%d}", v);
    touch7ConfigManager.updateFromJsonDeferred(String(js));
    if (IM && IM->cfgBrightValue) {
        char value[24];
        snprintf(value, sizeof(value), "%d%%", v);
        lv_label_set_text(IM->cfgBrightValue, value);
    }
}

#if ENABLE_BH1750==1
static void evtBrightAuto(lv_event_t* e) {
    (void)e;
    if (touch7ConfigManager.updateFromJsonDeferred(String(F("{\"backlight_auto\":true}")))) {
        touch7Display.setBrightManual(false);
        if (IM && IM->cfgBrightValue) lv_label_set_text(IM->cfgBrightValue, "AUTO");
    }
}
#endif

// Dim/saver timeout steppers (presets incl. OFF). Mutate the config in RAM
// only; touch7SystemTick flushes it under PanelHold after LVGL returns.
static const int kT7DurSecs[] = {0, 60, 120, 300, 600, 1800};
static void t7FmtDur(int s, char* b, size_t n) {
    if (s <= 0) snprintf(b, n, "OFF");
    else if (s < 60) snprintf(b, n, "%ds", s);
    else snprintf(b, n, "%dm", s / 60);
}
static int t7NextDur(int cur) {
    const int N = (int)(sizeof(kT7DurSecs) / sizeof(kT7DurSecs[0]));
    for (int i = 0; i < N; i++) {
        if (cur < kT7DurSecs[i]) return kT7DurSecs[i];
    }
    return kT7DurSecs[0];
}
static void t7DimSaverLabel(lv_obj_t* btn, const char* title, int secs) {
    if (!btn) return;
    lv_obj_t* l = lv_obj_get_child(btn, 0);
    if (!l) return;
    char d[16], t[48];
    t7FmtDur(secs, d, sizeof(d));
    snprintf(t, sizeof(t), "%s: %s", title, d);
    lv_label_set_text(l, t);
}
static void evtDimStep(lv_event_t* e) {
    (void)e;
    if (!IM) return;
    int cur = touch7ConfigManager.getConfig().pir_dim_sec;
    int nxt = t7NextDur(cur);
    char js[48];
    snprintf(js, sizeof(js), "{\"pir_dim_sec\":%d}", nxt);
    if (touch7ConfigManager.updateFromJsonDeferred(String(js))) {
        t7DimSaverLabel(IM->cfgDim, "Escurecer", nxt);
    }
}
static void evtSaverStep(lv_event_t* e) {
    (void)e;
    if (!IM) return;
    int cur = touch7ConfigManager.getConfig().screensaver_sec;
    int nxt = t7NextDur(cur);
    char js[48];
    snprintf(js, sizeof(js), "{\"screensaver_sec\":%d}", nxt);
    if (touch7ConfigManager.updateFromJsonDeferred(String(js))) {
        t7DimSaverLabel(IM->cfgSaver, "Saver", nxt);
    }
}

static void evtFsClose(lv_event_t* e) {
    (void)e;
    touch7Display.closeFullscreen();
}

static void evtPeerOpen(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    touch7Display.openFullscreen((uint8_t)idx);
}

static void evtReboot(lv_event_t* e) {
    (void)e;
    // Run after lv_timer_handler returns so PanelHold never waits on its own
    // active render when pending screen settings need to be saved.
    s_rebootPending = true;
}

// ---- public methods --------------------------------------------------------------

bool Touch7Display::begin() {
    if (_ready) return true;
    // NOTE: log_i goes to UART0 (visible on the UART monitor even when USB
    // CDC is not enumerated yet). Steps pinpoint an early-boot I2C abort.
    T7LOG("step 1: begin entry");
    log_i("[TOUCH7] step 1: begin entry");
    Serial.println(F("[TOUCH7] init: Wire1 deferred (lazy, post-panel)"));
    T7LOG("step 2: Wire1 deferred");
    log_i("[TOUCH7] step 2: Wire1 deferred (lazy claim by sensors)");

    log_i("[TOUCH7] step 3: panel board init (legacy, port 0)");
    Serial.println(F("[TOUCH7] init: panel board (Waveshare 7\" supported conf)"));
    Touch7Impl* im = new Touch7Impl();
    memset(im, 0, sizeof(Touch7Impl));
    IM = im;
    _impl = im;
    im->board = new Board();
    T7LOG("step 4: board object");
    log_i("[TOUCH7] step 4: board object created");
    im->board->init();
    T7LOG("step 5: board init done");
    log_i("[TOUCH7] step 5: board init done");
    im->lcd = im->board->getLCD();
    // T7-34: match the vendor anti-tearing demo (avoid-tearing mode 1):
    // 2 framebuffers + FULL refresh + bounce buffer (board header, W*10) at
    // 16 MHz. The FB switch calls gdma_hal_append (flash) and the bounce ISR
    // reads PSRAM, so NO flash op may run while the panel streams: config
    // writes are deferred (ConfigManager loop) and flash-touching handlers
    // suspend the panel (Touch7PanelHold).
    if (im->lcd) im->lcd->configFrameBufferNumber(2);
    if (!im->board->begin()) {
        log_i("[TOUCH7] step 5 FAIL: board begin");
        Serial.println(F("[TOUCH7] board->begin() FAILED"));
        return false;
    }
    T7LOG("step 6: board begin OK");
    log_i("[TOUCH7] step 6: board begin OK");
    im->touch = im->board->getTouch();

    // LVGL core: prefer two full-screen draw buffers (the panel's two FBs).
    lv_init();
    void* fb0 = im->lcd->getFrameBufferByIndex(0);
    void* fb1 = im->lcd->getFrameBufferByIndex(1);
    if (!fb0) {
        Serial.println(F("[TOUCH7] no framebuffer, abort"));
        return false;
    }
    s_doubleFb = (fb1 != nullptr);
    s_lastFb = (const uint8_t*)fb0; // /ecra mirror snapshot source
    if (!s_doubleFb) {
        Serial.println(F("[TOUCH7] single framebuffer (FB1 alloc failed)"));
    }
    // LVGL v9 display: the RGB panel framebuffers are the draw buffers.
    // FULL render mode + two buffers = anti-tearing swap (T7-29); PARTIAL with
    // one buffer degrades to the old tearing behaviour.
    s_disp = lv_display_create(TOUCH7_WIDTH_PX, TOUCH7_HEIGHT_PX);
    lv_display_set_flush_cb(s_disp, touch7FlushCb);
    lv_display_set_user_data(s_disp, im->lcd);
    lv_display_set_buffers(s_disp, fb0, s_doubleFb ? fb1 : nullptr,
                           TOUCH7_WIDTH_PX * TOUCH7_HEIGHT_PX * sizeof(lv_color_t),
                           s_doubleFb ? LV_DISPLAY_RENDER_MODE_FULL
                                      : LV_DISPLAY_RENDER_MODE_PARTIAL);
    im->disp = s_disp;
    if (s_doubleFb) {
        // VSYNC-completion notify target = this (loop) task; the panel
        // callback lets the flush block until the FB switch lands on a
        // frame boundary.
        s_vsyncTask = xTaskGetCurrentTaskHandle();
        im->lcd->attachRefreshFinishCallback(touch7VsyncCb, (void*)&s_vsyncTask);
    }

    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, touch7TouchCb);
    lv_indev_set_user_data(s_indev, im->touch);

    // Virtual pointer for remote taps (HTTP /api/touch7-tap).
    s_indevRemote = lv_indev_create();
    lv_indev_set_type(s_indevRemote, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indevRemote, touch7RemoteTapCb);

    buildUi();
    // Apply the persisted theme (was hardcoded Medio, so the saved theme was
    // ignored at boot).
    _theme = (uint8_t)touch7ClampInt(
        touch7ConfigManager.isInitialized() ? touch7ConfigManager.getConfig().theme : 1, 0, TOUCH7_THEME_COUNT - 1);
    _ready = true;
    applyTheme(_theme);
    const int bootBrightness = touch7ConfigManager.isInitialized()
        ? touch7ClampInt(touch7ConfigManager.getConfig().backlight_pct, 5, 100) : 85;
    setBrightness((uint8_t)((bootBrightness * 255) / 100));
    s_lastTickMs = millis();
    T7LOG("step 7: ready");
    log_i("[TOUCH7] step 7: ready 800x480 LVGL9");
    Serial.println(F("[TOUCH7] ready 800x480 LVGL9"));
    return true;
}

void Touch7Display::tick() {
    if (!_ready || !IM) return;
    unsigned long now = millis();
    unsigned long dt = now - s_lastTickMs;
    if (dt > 0) {
        if (dt > 100) dt = 100;
        lv_tick_inc(dt);
        s_lastTickMs = now;
    }
    // Pump LVGL at most ~25 Hz, and never while a flash write is held:
    // rendering touches PSRAM, which panics if SPI1 flash is mid-op. A lower
    // rate also leaves more PSRAM bandwidth for the RGB GDMA (less jitter).
    if (!s_flashHold && now - s_lastPumpMs >= 40) {
        s_lastPumpMs = now;
        s_renderActive = true;
        static bool _t7firstRender = false;
        if (!_t7firstRender) {
            _t7firstRender = true;
            T7LOG("loop: first LVGL render");
        }
        lv_timer_handler();
        s_renderActive = false;
    }
    // 1 s jobs: clock, agora refresh, auto-brightness.
    if (now - s_lastSecMs >= 1000) {
        s_lastSecMs = now;
        refreshClock();
        refreshAgora();
    }
    if (now - s_lastAutoMs >= 5000) {
        s_lastAutoMs = now;
        autoBrightness();
    }
}

void Touch7Display::setBrightness(uint8_t brightness) {
    if (!_ready || !IM || !IM->board) return;
    s_lastBright = brightness;
    int pct = (brightness * 100) / 255;
    if (_dimmed && pct > 15) pct = 15;
    if (pct == s_lastBacklight) return;
    s_lastBacklight = pct;
    IM->board->getBacklight()->setBrightness(pct);
}

void Touch7Display::setBrightManual(bool manual) {
    s_brightManual = manual;
}

void Touch7Display::setDimmed(bool dimmed) {
    _dimmed = dimmed;
    if (!_ready || !IM) return;
    if (dimmed) {
        t7IconAnimSet(false);
        s_lastBacklight = -1;
        setBrightness(38); // ~15%
    } else {
        s_lastBacklight = -1;
        const Touch7Config& cfg = touch7ConfigManager.getConfig();
        int pct = touch7ClampInt(cfg.backlight_pct, 5, 100);
        setBrightness((uint8_t)((pct * 255) / 100));
        t7IconAnimSet(IM->curTab == 0 && !isSaverShown());
    }
}

bool Touch7Display::isSaverShown() const {
    if (!_ready || !IM || !IM->saver) return false;
    return !lv_obj_is_hidden(IM->saver);
}

void Touch7Display::showStatus(const String& line1, const String& line2, const String& line3) {
    if (!_ready || !IM) return;
    // Status bar is 32 px: keep the network line compact (no wrap).
    String short1 = line1;
    if (short1.length() > 64) short1 = short1.substring(0, 61) + "...";
    lv_label_set_text(IM->statusWifi, short1.c_str());
    lv_label_set_text(IM->cfgSys, line2.c_str());
    lv_label_set_text(IM->agoraSys, line3.c_str());
}

void Touch7Display::showToast(const String& l1, const String& l2, const String& l3) {
    if (!_ready || !IM) return;
    String t = l1 + "\n" + l2;
    if (l3.length()) t += "\n" + l3;
    lv_label_set_text(IM->toast, t.c_str());
    lv_obj_set_hidden(IM->toast, false);
}

void Touch7Display::hideToast() {
    if (!_ready || !IM) return;
    lv_obj_set_hidden(IM->toast, true);
}

void Touch7Display::setAgora(float tempC, float humPct, float pressHpa, float lux,
                             int condCode, bool isDay) {
    if (!_ready || !IM) return;
    char b[64];
    // T7-25: single-FB streaming tears on every redraw — only touch labels
    // whose text actually changed (1 s jobs call this unconditionally).
    static char s_temp[64] = "", s_meta[96] = "";
    snprintf(b, sizeof(b), "%.1f C", tempC);
    if (strcmp(b, s_temp) != 0) {
        strncpy(s_temp, b, sizeof(s_temp) - 1);
        s_temp[sizeof(s_temp) - 1] = '\0';
        lv_label_set_text(IM->agoraTemp, b);
        lv_label_set_text(IM->statusTemp, b);
    }
    snprintf(b, sizeof(b), "Hum %.0f%%   Press %.0fhPa   Lux %.0f", humPct, pressHpa, lux);
    if (strcmp(b, s_meta) != 0) {
        strncpy(s_meta, b, sizeof(s_meta) - 1);
        s_meta[sizeof(s_meta) - 1] = '\0';
        lv_label_set_text(IM->agoraMeta, b);
    }
    Touch7CondGroup g = touch7CondGroup(condCode);
    if (g != IM->iconGroup || isDay != IM->iconDay) {
        IM->iconGroup = g;
        IM->iconDay = isDay;
        // Aura (MyAuraPT) asset for the WMO code; retain the procedural icon
        // only as a fallback.
        const lv_image_dsc_t* dsc = t7ChooseImage(condCode, isDay);
        if (dsc) {
            lv_image_set_src(IM->iconImg, dsc);
        } else {
            const Touch7Theme& th = TOUCH7_THEMES[_theme >= TOUCH7_THEME_COUNT ? 1 : _theme];
            touch7PaintIcon(IM->iconBuf, g, isDay, lv_color_to_u16(lv_color_hex(th.bg)));
            lv_image_set_src(IM->iconImg, &IM->iconDsc);
        }
        lv_obj_invalidate(IM->iconImg);
        t7IconAnimSet(IM->curTab == 0 && !_dimmed && !isSaverShown());
    }
}

void Touch7Display::setWeatherNow(float tempC, float humPct, float pressureHpa,
                                  int condCode, bool isDay, const String& condition,
                                  const String& place, const String& sunset) {
    if (!_ready || !IM) return;
    setAgora(tempC, humPct, pressureHpa, 0.0f, condCode, isDay);
    lv_label_set_text(IM->agoraSub, condition.c_str());

    char details[96];
    snprintf(details, sizeof(details), "Hum %.0f%%  Press %.0f hPa", humPct, pressureHpa);
    lv_label_set_text(IM->agoraMeta, details);
    snprintf(details, sizeof(details), L(activeLang->touch7LocalSunset).c_str(),
             place.c_str(), sunset.c_str());
    lv_label_set_text(IM->agoraSys, details);
}

void Touch7Display::setForecast(const String& todayLine, const String dayLines[5]) {
    if (!_ready || !IM) return;
    lv_label_set_text(IM->prevToday, todayLine.c_str());
    for (int i = 0; i < 5; i++) lv_label_set_text(IM->prevDays[i], dayLines[i].c_str());
}

uint16_t* Touch7Display::peerTileBuffer(uint8_t idx) {
    if (!IM || idx >= TOUCH7_PEERS_MAX) return 0;
    return IM->peerBuf[idx];
}

void Touch7Display::invalidatePeerTile(uint8_t idx) {
    if (!_ready || !IM || idx >= TOUCH7_PEERS_MAX) return;
    lv_obj_invalidate(IM->peerImg[idx]);
}

void Touch7Display::setPeerStatus(uint8_t idx, const String& name, const String& status, bool live) {
    if (!_ready || !IM || idx >= TOUCH7_PEERS_MAX) return;
    String t = name + "\n" + status + (live ? "" : "\nOFFLINE");
    lv_label_set_text(IM->peerLbl[idx], t.c_str());
}

void Touch7Display::setWifiInfo(const String& info) {
    if (!_ready || !IM) return;
    lv_label_set_text(IM->cfgWifi, info.c_str());
}

void Touch7Display::setClockNow(const String& hhmm, const String& date) {
    if (!_ready || !IM) return;
    lv_label_set_text(IM->statusClock, hhmm.c_str());
    String s = hhmm + "\n" + date;
    lv_label_set_text(IM->saverClock, s.c_str());
}

void Touch7Display::applyTheme(uint8_t theme) {
    if (theme >= TOUCH7_THEME_COUNT) return;
    _theme = theme;
    if (!_ready || !IM) return;
    const Touch7Theme& th = TOUCH7_THEMES[theme];
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(th.bg), 0);
    lv_obj_set_style_bg_grad_color(lv_screen_active(), lv_color_hex(th.bg2), 0);
    lv_obj_set_style_bg_grad_dir(lv_screen_active(), LV_GRAD_DIR_VER, 0);
    // Root colour is a fallback for non-label objects. Every LVGL label also
    // gets an explicit foreground in t7CreateLabel because the default theme
    // assigns label text directly instead of inheriting this property.
    lv_obj_set_style_text_color(lv_screen_active(), lv_color_hex(th.text), 0);
    // On-gradient labels use the main text colour (white on the vivid bg).
    lv_obj_t* onGrad[] = {
        IM->agoraTemp, IM->agoraSub, IM->agoraMeta, IM->agoraSys,
        IM->prevToday, IM->cfgWifi, IM->cfgSys,
    };
    for (size_t i = 0; i < sizeof(onGrad) / sizeof(onGrad[0]); i++) {
        if (onGrad[i]) lv_obj_set_style_text_color(onGrad[i], lv_color_hex(th.text), 0);
    }
    // Tab containers: set the colour on the container too, so EVERY label
    // inside (e.g. the 'Tema'/'Relogio'/'Brilho' section titles) inherits it
    // instead of falling back to the LVGL default (black).
    lv_obj_t* onGradTabs[] = { IM->tabAgora, IM->tabPrev, IM->tabPeers, IM->tabCfg };
    for (size_t i = 0; i < sizeof(onGradTabs) / sizeof(onGradTabs[0]); i++) {
        if (onGradTabs[i]) lv_obj_set_style_text_color(onGradTabs[i], lv_color_hex(th.text), 0);
    }
    for (int i = 0; i < 5; i++) {
        if (IM->prevDays[i]) lv_obj_set_style_text_color(IM->prevDays[i], lv_color_hex(th.text), 0);
    }
    // On-panel labels (top bar, peer tiles) use the card foreground colour.
    lv_obj_t* onPanel[] = { IM->statusWifi, IM->statusClock, IM->statusTemp };
    for (size_t i = 0; i < sizeof(onPanel) / sizeof(onPanel[0]); i++) {
        if (onPanel[i]) lv_obj_set_style_text_color(onPanel[i], lv_color_hex(th.panelText), 0);
    }
    for (int i = 0; i < TOUCH7_PEERS_MAX; i++) {
        if (IM->peerLbl[i]) lv_obj_set_style_text_color(IM->peerLbl[i], lv_color_hex(th.panelText), 0);
    }
    lv_style_set_bg_color(&IM->stPanel, lv_color_hex(th.panel));
    lv_style_set_text_color(&IM->stPanel, lv_color_hex(th.panelText));
    // Frosted-card themes use a translucent panel over the screen gradient;
    // solid themes set panelOpa = 255.
    lv_style_set_bg_opa(&IM->stPanel, (lv_opa_t)th.panelOpa);
    lv_style_set_border_color(&IM->stPanel, lv_color_hex(th.borderColor));
    lv_style_set_border_opa(&IM->stPanel, LV_OPA_COVER);
    lv_style_set_bg_color(&IM->stBtn, lv_color_hex(th.accent));
    lv_style_set_bg_grad_color(&IM->stBtn, lv_color_hex(th.accentDk));
    lv_style_set_text_color(&IM->stBtn, lv_color_white());
    lv_style_set_bg_color(&IM->stBtnPr, lv_color_hex(th.accentDk));
    lv_style_set_text_color(&IM->stBtnPr, lv_color_white());
    lv_style_set_bg_color(&IM->stBarInd, lv_color_hex(th.accent));
    if (IM->cfgBright) {
        lv_obj_set_style_border_color(IM->cfgBright, lv_color_hex(th.accent), LV_PART_KNOB);
    }
    t7RefreshSelectionStyles(theme, _clockStyle, IM->curTab);
    // Icon assets are RGB565A8 (transparent bg), so they composite over the
    // new gradient automatically — no repaint needed.
    lv_obj_invalidate(IM->iconImg);
}

void Touch7Display::applyClockStyle(uint8_t style) {
    if (style > 4) return;
    _clockStyle = style;
    if (_ready && IM) t7RefreshSelectionStyles(_theme, _clockStyle, IM->curTab);
    // Screensaver re-renders on next tick via refreshClock().
}

void Touch7Display::showScreensaver(bool show) {
    if (!_ready || !IM) return;
    if (show) {
        refreshClock();
        bool analog = (_clockStyle >= 3);
        if (analog) {
            lv_obj_set_hidden(IM->saverClock, true);
            lv_obj_set_hidden(IM->saverArc, false);
            lv_obj_set_hidden(IM->saverHour, false);
            lv_obj_set_hidden(IM->saverMin, false);
        } else {
            lv_obj_set_hidden(IM->saverClock, false);
            lv_obj_set_hidden(IM->saverArc, true);
            lv_obj_set_hidden(IM->saverHour, true);
            lv_obj_set_hidden(IM->saverMin, true);
        }
        lv_obj_set_hidden(IM->saver, false);
    } else {
        lv_obj_set_hidden(IM->saver, true);
    }
    t7IconAnimSet(!show && IM->curTab == 0 && !_dimmed);
}

void Touch7Display::openFullscreen(uint8_t idx) {
    if (!_ready || !IM || idx >= TOUCH7_PEERS_MAX) return;
    if (!IM->fsBuf) {
        IM->fsBuf = (uint16_t*)ps_malloc(640UL * 480UL * 2UL);
        if (!IM->fsBuf) return;
        IM->fsDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        IM->fsDsc.header.cf = LV_COLOR_FORMAT_RGB565;
        IM->fsDsc.header.w = 640;
        IM->fsDsc.header.h = 480;
        IM->fsDsc.header.stride = 640 * 2;
        IM->fsDsc.data_size = 640UL * 480UL * 2UL;
        IM->fsDsc.data = (const uint8_t*)IM->fsBuf;
        lv_image_set_src(IM->fsImg, &IM->fsDsc);
    }
    if (touch7PeerFetchFull(idx, IM->fsBuf, 640, 480)) {
        lv_obj_invalidate(IM->fsImg);
    }
    lv_obj_set_hidden(IM->fsBox, false);
}

void Touch7Display::closeFullscreen() {
    if (!IM) return;
    lv_obj_set_hidden(IM->fsBox, true);
    if (IM->fsBuf) {
        heap_caps_free(IM->fsBuf);
        IM->fsBuf = 0;
    }
}

bool Touch7Display::renderActive() const {
    return s_renderActive;
}

// Hidden config flush window: the screen is already off (dimmed or saver), so
// the panel del/rebuild from Touch7PanelHold is not visible to the user.
bool touch7ConfigFlushAllowed() {
    return touch7Display.isDimmed() || touch7Display.isSaverShown();
}

void Touch7Display::setPclkHz(uint32_t hz) {
    if (!_ready || !IM || !IM->lcd) return;
    esp_lcd_rgb_panel_set_pclk((esp_lcd_panel_handle_t)IM->lcd->getRefreshPanelHandle(), hz);
    T7LOG("pclk -> %lu Hz", (unsigned long)hz);
}

size_t Touch7Display::frameBytes() const {
    return (size_t)TOUCH7_WIDTH_PX * (size_t)TOUCH7_HEIGHT_PX * 2UL;
}

bool Touch7Display::captureFrame(uint8_t* dst, size_t dstLen) {
    if (!_ready || !dst || !s_lastFb) return false;
    const size_t len = frameBytes();
    if (dstLen < len) return false;
    // Serialize the front-buffer pointer with framebuffer flips. LVGL renders
    // into the back buffer while this lock keeps the currently presented one
    // stable for the complete 768 KB snapshot.
    Touch7FlashLock frameLock;
    if (!_ready) return false;
    const uint8_t* front = s_lastFb;
    if (!front) return false;
    // Invalidate the CPU cache for the FB region so we read what the render
    // path wrote (it may have been written on the other core).
    esp_cache_msync((void*)front, len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    memcpy(dst, front, len);
    return true;
}

void Touch7Display::remoteTap(uint16_t x, uint16_t y) {
    if (!_ready) return;
    if (x >= TOUCH7_WIDTH_PX || y >= TOUCH7_HEIGHT_PX) return;
    uint32_t now = millis();
    portENTER_CRITICAL(&s_remoteTapMux);
    s_tapX = (int)x;
    s_tapY = (int)y;
    s_remotePointerDown = false;
    s_remoteOneShot = true;
    s_remotePressPending = true;
    s_remoteUpdatedMs = now;
    portEXIT_CRITICAL(&s_remoteTapMux);
}

void Touch7Display::remotePointer(uint16_t x, uint16_t y, bool pressed) {
    if (!_ready || x >= TOUCH7_WIDTH_PX || y >= TOUCH7_HEIGHT_PX) return;
    uint32_t now = millis();
    portENTER_CRITICAL(&s_remoteTapMux);
    s_tapX = (int)x;
    s_tapY = (int)y;
    s_remotePointerDown = pressed;
    s_remoteOneShot = false;
    s_remotePressPending = false;
    s_remoteUpdatedMs = now;
    portEXIT_CRITICAL(&s_remoteTapMux);
}

// Forward: engine quiesce helper (defined below).
static void touch7Quiesce(bool stop);

// Panel-up probe for sensor managers (declared in touch7_i2c.h): true once
// the RGB panel is initialized and streaming. Flash reads must not happen
// after this point (cache panic, T7-23).
bool touch7RgbStreaming() { return touch7Display.isInitialized(); }

void Touch7Display::setFlashHold(bool hold) {
    if (hold == (bool)s_flashHold) return;
    if (hold) {
        // Hold first (stops new renders), then stop the engine: fully safe
        // window for the flash op that follows on either task.
        s_flashHold = true;
        touch7Quiesce(true);
    } else {
        // Resume engine first, then rendering.
        touch7Quiesce(false);
        s_flashHold = false;
    }
}

// Quiesce helper: pauses task-side PSRAM traffic around flash writes
// (rendering + audio streaming). NOTE: this does NOT stop the RGB VSYNC
// ISR (nothing short of panel del does that — proven: reset/init churn
// doesn't stop VSYNC and the per-VSYNC restart path runs flash-hosted GDMA
// code on every frame). ISR safety comes from NEVER writing flash
// automatically (see loop persist skips); user POST saves are atomic
// (.tmp+rename) so even a race-reboot can't corrupt config.
static int s_quiesceDepth = 0;

static void touch7Quiesce(bool stop) {
    if (stop) {
        if (s_quiesceDepth++ > 0) return;
        // Stop audio first (its task streams PSRAM continuously), then wait
        // for an in-flight render to finish. Render stays paused via the
        // s_flashHold flag until resume.
        touch7AudioStop();
        for (int i = 0; i < 100 && touch7Display.renderActive(); i++) {
            delay(5);
        }
    } else {
        if (s_quiesceDepth == 0) return;
        if (--s_quiesceDepth > 0) return;
    }
}

void Touch7Display::shutdown() {
    // OTA: stop the LVGL pump and take the flash lock (held until reboot or
    // touch7OtaAbort), then wait out any in-flight render before del — the
    // loop task must not touch the panel while we free it.
    touch7FlashHold(true);
    touch7FlashLock();
    unsigned long t0 = millis();
    while (s_renderActive && millis() - t0 < 1000) delay(5);
    s_vsyncTask = nullptr;
    if (IM && IM->board) {
        log_i("[TOUCH7] shutdown: panel del (OTA)");
        IM->board->del();
    }
    _ready = false;
}

// ---- Touch7 TF slot: external CS via the Board's CH422G (EXIO4, active LOW)
// The Waveshare slot hangs CS off the expander, so the Arduino SD backend
// (which toggles a GPIO) cannot drive it. The card is kept selected by
// holding EXIO4 LOW for the whole SPI session; SD.begin() runs with a dummy
// CS pin (SDCARD_SPI_CS_PIN=-1). Never instantiate a second CH422G here —
// the Board owns I2C0/expander and re-creating it races the LCD/touch.
bool touch7SdAssertCs() {
    if (!IM || !IM->board) return false;
    auto exp = IM->board->getIO_Expander();
    if (!exp || !exp->getBase()) return false;
#ifndef TOUCH7_SD_CS_EXIO
#define TOUCH7_SD_CS_EXIO 4
#endif
    exp->getBase()->digitalWrite(TOUCH7_SD_CS_EXIO, 0);
    return true;
}

// ---- T7-28 PanelHold: del/rebuild the RGB panel around flash writes ------
// Runs on the async HTTP task (POST handlers) or loop task (steppers):
// LVGL is single-task by construction here because suspend() first stops
// new pumps (flash hold) and waits out any in-flight render before del,
// while tick() early-returns on !_ready for the whole window.
void Touch7Display::suspendPanelForWrite() {
    // Idempotent: nested holds do not re-del the panel (depth-guarded).
    if (s_panelDepth++ > 0) {
        touch7FlashHold(true);
        return;
    }
    if (!IM) {
        touch7FlashHold(true);
        return;
    }
    touch7FlashLock(); // exclude the flush FB switch for the whole window
    T7LOG("panelhold: suspend");
    log_i("[TOUCH7] panelhold: suspend");
    touch7FlashHold(true);
    unsigned long t0 = millis();
    while (s_renderActive && millis() - t0 < 500) delay(5);
    Touch7Impl* im = IM;
    if (im->board) {
        im->board->del();
        delete im->board;
        im->board = nullptr;
    }
    im->lcd = nullptr;
    im->touch = nullptr;
    im->disp = nullptr;
    s_vsyncTask = nullptr;
    _ready = false;
}

bool Touch7Display::resumePanelAfterWrite() {
    if (s_panelDepth == 0) return true;       // no matching suspend
    if (--s_panelDepth > 0) return true;      // still nested: keep panel down
    do {
        if (!IM) break;
        Touch7Impl* im = IM;
        T7LOG("panelhold: rebuild");
        log_i("[TOUCH7] panelhold: rebuild");
        im->board = new Board();
        if (!im->board) break;
        im->board->init();
        im->lcd = im->board->getLCD();
        // T7-34: 2 framebuffers again (anti-tearing), same as begin().
        if (im->lcd) im->lcd->configFrameBufferNumber(2);
        if (!im->board->begin()) {
            log_i("[TOUCH7] panelhold: board begin FAILED");
            break;
        }
        im->touch = im->board->getTouch();
        // The expander is re-created with the Board: re-assert TF CS (EXIO4
        // LOW) so an already-mounted SPI card survives panel rebuilds.
        touch7SdAssertCs();
        void* fb0 = im->lcd ? im->lcd->getFrameBufferByIndex(0) : nullptr;
        void* fb1 = im->lcd ? im->lcd->getFrameBufferByIndex(1) : nullptr;
        if (!fb0) {
            log_i("[TOUCH7] panelhold: no framebuffer");
            break;
        }
        // Rewire the persistent LVGL display/indev to the fresh panel FBs.
        s_doubleFb = (fb1 != nullptr);
        s_lastFb = (const uint8_t*)fb0;
        if (!s_disp) {
            log_i("[TOUCH7] panelhold: no display handle");
            break;
        }
        lv_display_set_user_data(s_disp, im->lcd);
        lv_display_set_buffers(s_disp, fb0, s_doubleFb ? fb1 : nullptr,
                               TOUCH7_WIDTH_PX * TOUCH7_HEIGHT_PX * sizeof(lv_color_t),
                               s_doubleFb ? LV_DISPLAY_RENDER_MODE_FULL
                                          : LV_DISPLAY_RENDER_MODE_PARTIAL);
        if (s_indev) lv_indev_set_user_data(s_indev, im->touch);
        s_vsyncTask = s_doubleFb ? xTaskGetCurrentTaskHandle() : nullptr;
        if (s_doubleFb) {
            im->lcd->attachRefreshFinishCallback(touch7VsyncCb, (void*)&s_vsyncTask);
        }
        // Back online first (tick pumps stay parked on s_flashHold), then
        // restore brightness (dim cap re-applies via _dimmed) and repaint
        // everything into the fresh framebuffer.
        _ready = true;
        setBrightness(s_lastBright);
        lv_obj_invalidate(lv_screen_active());
        s_lastTickMs = millis();
        touch7FlashHold(false);
        touch7FlashUnlock();
        T7LOG("panelhold: ready");
        log_i("[TOUCH7] panelhold: ready");
        return true;
    } while (0);
    // Rebuild failed: keep the system ALIVE (web/network keep working) with
    // the display off rather than rebooting. A later save may retry.
    log_i("[TOUCH7] panelhold: rebuild FAILED — display off (no reboot)");
    touch7FlashHold(false);
    touch7FlashUnlock();
    _ready = false;
    return false;
}

void touch7PanelSuspend() {
    touch7Display.suspendPanelForWrite();
}

bool touch7PanelResume() {
    return touch7Display.resumePanelAfterWrite();
}

// Called around LittleFS/NVS writes from other tasks (see header).
void touch7FlashHold(bool hold) {
    Touch7Display* d = &touch7Display;
    d->setFlashHold(hold);
}

bool touch7FlashHeld() {
    return s_flashHold;
}

// T7-29 cross-task flash lock (see header). Held by flash writers on other
// tasks for the write duration; try-locked by the flush around the FB switch.
void touch7FlashLock() {
    t7EnsureFlashMutex();
    if (s_flashMutex) xSemaphoreTakeRecursive(s_flashMutex, portMAX_DELAY);
}

void touch7FlashUnlock() {
    if (s_flashMutex) xSemaphoreGiveRecursive(s_flashMutex);
}

void touch7OtaBegin() {
    s_otaPanelStopped = true;
    touch7Display.shutdown();
}

// OTA failed (bad signature/abort): bring the display back and release the
// OTA flash lock. The success path reboots instead, so this is only called
// on the error branch of the upload handler.
void touch7OtaAbort() {
    if (!s_otaPanelStopped) return;
    s_otaPanelStopped = false;
    s_panelDepth = 1; // force the shared rebuild path (shutdown didn't hold)
    touch7Display.resumePanelAfterWrite();
}

void Touch7Display::buildUi() {
    Touch7Impl* im = IM;
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(TOUCH7_THEMES[1].bg), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(TOUCH7_THEMES[1].bg2), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(TOUCH7_THEMES[1].text), 0);

    // Panel + button 3D styles (rebordo elevado; pressed afundado).
    lv_style_init(&im->stPanel);
    lv_style_set_bg_color(&im->stPanel, lv_color_hex(TOUCH7_THEMES[1].panel));
    lv_style_set_text_color(&im->stPanel, lv_color_hex(TOUCH7_THEMES[1].panelText));
    lv_style_set_radius(&im->stPanel, 10);
    lv_style_set_border_width(&im->stPanel, 2);
    lv_style_set_border_color(&im->stPanel, lv_color_hex(0x3B82F6));
    lv_style_init(&im->stBtn);
    lv_style_set_bg_color(&im->stBtn, lv_color_hex(TOUCH7_THEMES[1].accent));
    lv_style_set_bg_grad_color(&im->stBtn, lv_color_hex(TOUCH7_THEMES[1].accentDk));
    lv_style_set_bg_grad_dir(&im->stBtn, LV_GRAD_DIR_VER);
    lv_style_set_text_color(&im->stBtn, lv_color_white());
    lv_style_set_radius(&im->stBtn, 8);
    lv_style_set_border_width(&im->stBtn, 2);
    lv_style_set_border_color(&im->stBtn, lv_color_hex(0x0B2447));
    lv_style_set_shadow_width(&im->stBtn, 6);
    lv_style_set_shadow_color(&im->stBtn, lv_color_black());
    lv_style_set_shadow_offset_y(&im->stBtn, 3);
    lv_style_init(&im->stBtnPr);
    lv_style_set_bg_color(&im->stBtnPr, lv_color_hex(TOUCH7_THEMES[1].accentDk));
    lv_style_set_text_color(&im->stBtnPr, lv_color_white());
    lv_style_set_radius(&im->stBtnPr, 8);
    lv_style_set_border_width(&im->stBtnPr, 2);
    lv_style_set_shadow_width(&im->stBtnPr, 0);
    lv_style_init(&im->stBar);
    lv_style_set_bg_color(&im->stBar, lv_color_hex(0x1B2F55));
    lv_style_set_radius(&im->stBar, 6);
    lv_style_init(&im->stBarInd);
    lv_style_set_bg_color(&im->stBarInd, lv_color_hex(TOUCH7_THEMES[1].accent));
    lv_style_set_radius(&im->stBarInd, 6);

    // Status bar (800x32).
    lv_obj_t* bar = lv_obj_create(scr);
    lv_obj_set_size(bar, 800, 32);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_add_style(bar, &im->stPanel, 0);
    im->statusWifi = t7Label(bar, &lv_font_montserrat_12, 8, 6, 420);
    im->statusClock = t7Label(bar, &lv_font_montserrat_16, 440, 4, 160);
    im->statusTemp = t7Label(bar, &lv_font_montserrat_16, 620, 4, 170);

    // Tabs (800x384 @y=32).
    im->tabAgora = lv_obj_create(scr);
    im->tabPrev = lv_obj_create(scr);
    im->tabPeers = lv_obj_create(scr);
    im->tabCfg = lv_obj_create(scr);
    lv_obj_t* tabs[4] = {im->tabAgora, im->tabPrev, im->tabPeers, im->tabCfg};
    for (int i = 0; i < 4; i++) {
        lv_obj_set_size(tabs[i], 800, 384);
        lv_obj_set_pos(tabs[i], 0, 32);
        lv_obj_set_style_bg_opa(tabs[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tabs[i], 0, 0);
        if (i) lv_obj_set_hidden(tabs[i], true);
    }

    // AGORA tab.
    im->iconBuf = (uint16_t*)ps_malloc(TOUCH7_ICON_PX * TOUCH7_ICON_PX * 2UL);
    im->iconDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    im->iconDsc.header.cf = LV_COLOR_FORMAT_RGB565;
    im->iconDsc.header.w = TOUCH7_ICON_PX;
    im->iconDsc.header.h = TOUCH7_ICON_PX;
    im->iconDsc.header.stride = TOUCH7_ICON_PX * 2;
    im->iconDsc.data_size = TOUCH7_ICON_PX * TOUCH7_ICON_PX * 2UL;
    im->iconDsc.data = (const uint8_t*)im->iconBuf;
    im->iconGroup = TOUCH7_COND_PARTLY;
    im->iconDay = true;
    touch7PaintIcon(im->iconBuf, im->iconGroup, true, 0x0E2A);
    im->iconImg = lv_image_create(im->tabAgora);
    lv_image_set_src(im->iconImg, t7ChooseImage(2, true)); // Aura asset
    lv_image_set_scale(im->iconImg, 400);                  // ~1.56x the 100px asset
    lv_obj_set_pos(im->iconImg, 30, 80);
    im->agoraTemp = t7Label(im->tabAgora, &lv_font_montserrat_48, 260, 60, 300);
    lv_label_set_text(im->agoraTemp, "--.- C");
    im->agoraSub = t7Label(im->tabAgora, &lv_font_montserrat_20, 260, 130, 300);
    lv_label_set_text(im->agoraSub, "A iniciar...");
    im->agoraMeta = t7Label(im->tabAgora, &lv_font_montserrat_16, 260, 165, 300);
    im->agoraSys = t7Label(im->tabAgora, &lv_font_montserrat_12, 40, 300, 720);

    // PREVISÃO tab.
    im->prevToday = t7Label(im->tabPrev, &lv_font_montserrat_20, 20, 12, 760);
    for (int i = 0; i < 5; i++) {
        im->prevDays[i] = t7Label(im->tabPrev, &lv_font_montserrat_16, 20 + (i % 2) * 380, 70 + (i / 2) * 60, 360);
        lv_label_set_text(im->prevDays[i], "---");
    }

    // PEERS tab (2x2 tiles).
    for (int i = 0; i < TOUCH7_PEERS_MAX; i++) {
        int col = i % 2, row = i / 2;
        im->peerTile[i] = lv_obj_create(im->tabPeers);
        lv_obj_set_size(im->peerTile[i], 384, 182);
        lv_obj_set_pos(im->peerTile[i], 8 + col * 392, 4 + row * 188);
        lv_obj_add_style(im->peerTile[i], &im->stPanel, 0);
        im->peerBuf[i] = (uint16_t*)ps_malloc(TOUCH7_TILE_W * TOUCH7_TILE_H * 2UL);
        if (im->peerBuf[i]) memset(im->peerBuf[i], 0, TOUCH7_TILE_W * TOUCH7_TILE_H * 2UL);
        im->peerDsc[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        im->peerDsc[i].header.cf = LV_COLOR_FORMAT_RGB565;
        im->peerDsc[i].header.w = TOUCH7_TILE_W;
        im->peerDsc[i].header.h = TOUCH7_TILE_H;
        im->peerDsc[i].header.stride = TOUCH7_TILE_W * 2;
        im->peerDsc[i].data_size = TOUCH7_TILE_W * TOUCH7_TILE_H * 2UL;
        im->peerDsc[i].data = (const uint8_t*)im->peerBuf[i];
        im->peerImg[i] = lv_image_create(im->peerTile[i]);
        lv_image_set_src(im->peerImg[i], &im->peerDsc[i]);
        lv_image_set_scale(im->peerImg[i], 224);
        lv_obj_set_pos(im->peerImg[i], 6, 4);
        im->peerLbl[i] = t7Label(im->peerTile[i], &lv_font_montserrat_12, 290, 10, 90);
        lv_label_set_text(im->peerLbl[i], "---");
        lv_obj_set_clickable(im->peerTile[i], true);
        lv_obj_add_event_cb(im->peerTile[i], evtPeerOpen, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    // Fullscreen peer box (hidden).
    im->fsBox = lv_obj_create(scr);
    lv_obj_set_size(im->fsBox, 800, 480);
    lv_obj_set_pos(im->fsBox, 0, 0);
    lv_obj_set_style_bg_color(im->fsBox, lv_color_black(), 0);
    im->fsImg = lv_image_create(im->fsBox);
    lv_obj_set_pos(im->fsImg, 80, 0);
    im->fsBuf = 0;
    lv_obj_t* fsClose = lv_button_create(im->fsBox);
    t7StyleBtn(im, fsClose);
    lv_obj_set_size(fsClose, 120, 48);
    lv_obj_set_pos(fsClose, 660, 420);
    lv_obj_t* fsCloseL = t7CreateLabel(fsClose);
    lv_label_set_text(fsCloseL, "Fechar");
    lv_obj_center(fsCloseL);
    lv_obj_add_event_cb(fsClose, evtFsClose, LV_EVENT_CLICKED, NULL);
    lv_obj_set_hidden(im->fsBox, true);

    // CFG tab.
    lv_obj_t* thL = t7Label(im->tabCfg, &lv_font_montserrat_16, 20, 8, 340);
    lv_label_set_text(thL, "Tema");
    const char* thN[TOUCH7_THEME_COUNT] = {"Claro", "Medio", "Escuro", "Sky", "Navy", "Bright"};
    for (int i = 0; i < TOUCH7_THEME_COUNT; i++) {
        im->cfgThemeBtns[i] = lv_button_create(im->tabCfg);
        t7StyleBtn(im, im->cfgThemeBtns[i]);
        lv_obj_set_size(im->cfgThemeBtns[i], 108, 44);
        lv_obj_set_pos(im->cfgThemeBtns[i], 20 + (i % 3) * 116, 34 + (i / 3) * 50);
        lv_obj_t* l = t7CreateLabel(im->cfgThemeBtns[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_label_set_text(l, thN[i]);
        lv_obj_center(l);
        lv_obj_add_event_cb(im->cfgThemeBtns[i], evtTheme, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    lv_obj_t* clL = t7Label(im->tabCfg, &lv_font_montserrat_16, 20, 140, 340);
    lv_label_set_text(clL, L(activeLang->touch7MenuClock).c_str());
    const char* clN[5] = {"Digital 1", "Digital 2", "Digital 3", activeLang->touch7MenuAnalog, activeLang->touch7MenuArc};
    for (int i = 0; i < 5; i++) {
        im->cfgClockBtns[i] = lv_button_create(im->tabCfg);
        t7StyleBtn(im, im->cfgClockBtns[i]);
        lv_obj_set_size(im->cfgClockBtns[i], 108, 44);
        lv_obj_set_pos(im->cfgClockBtns[i], 20 + (i % 3) * 116, 166 + (i / 3) * 50);
        lv_obj_t* l = t7CreateLabel(im->cfgClockBtns[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_label_set_text(l, clN[i]);
        lv_obj_center(l);
        lv_obj_add_event_cb(im->cfgClockBtns[i], evtClock, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    lv_obj_t* brL = t7Label(im->tabCfg, &lv_font_montserrat_16, 20, 270, 220);
    lv_label_set_text(brL, "Brilho");
    im->cfgBright = lv_slider_create(im->tabCfg);
#if ENABLE_BH1750==1
    lv_obj_set_size(im->cfgBright, 232, 28);
#else
    // This pack has no light sensor; give the manual control the full column
    // rather than showing an AUTO button that cannot work.
    lv_obj_set_size(im->cfgBright, 352, 28);
#endif
    lv_obj_set_pos(im->cfgBright, 20, 300);
    lv_obj_add_style(im->cfgBright, &im->stBar, 0);
    lv_obj_add_style(im->cfgBright, &im->stBarInd, LV_PART_INDICATOR);
    lv_slider_set_range(im->cfgBright, 5, 100);
    int initialBright = touch7ConfigManager.isInitialized()
        ? touch7ClampInt(touch7ConfigManager.getConfig().backlight_pct, 5, 100) : 85;
    lv_slider_set_value(im->cfgBright, initialBright, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(im->cfgBright, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_border_color(im->cfgBright, lv_color_hex(TOUCH7_THEMES[1].accent), LV_PART_KNOB);
    lv_obj_set_style_border_width(im->cfgBright, 2, LV_PART_KNOB);
    lv_obj_add_event_cb(im->cfgBright, evtBright, LV_EVENT_VALUE_CHANGED, NULL);
#if ENABLE_BH1750==1
    im->cfgBrightValue = t7Label(im->tabCfg, &lv_font_montserrat_12, 20, 330, 232);
#else
    im->cfgBrightValue = t7Label(im->tabCfg, &lv_font_montserrat_12, 20, 330, 352);
#endif
    char initialBrightText[16];
    snprintf(initialBrightText, sizeof(initialBrightText), "%d%%", initialBright);
    lv_label_set_text(im->cfgBrightValue, initialBrightText);
#if ENABLE_BH1750==1
    lv_obj_t* brAuto = lv_button_create(im->tabCfg);
    t7StyleBtn(im, brAuto);
    lv_obj_set_size(brAuto, 112, 44);
    lv_obj_set_pos(brAuto, 268, 292);
    lv_obj_t* brAutoL = t7CreateLabel(brAuto);
    lv_label_set_text(brAutoL, "AUTO");
    lv_obj_center(brAutoL);
    lv_obj_add_event_cb(brAuto, evtBrightAuto, LV_EVENT_CLICKED, NULL);
#endif
    lv_obj_t* statusTitle = t7Label(im->tabCfg, &lv_font_montserrat_16, 400, 8, 372);
    lv_label_set_text(statusTitle, "Rede e sistema");
    im->cfgWifi = t7Label(im->tabCfg, &lv_font_montserrat_16, 400, 36, 372);
    im->cfgSys = t7Label(im->tabCfg, &lv_font_montserrat_12, 400, 78, 372);
    lv_obj_t* idleTitle = t7Label(im->tabCfg, &lv_font_montserrat_16, 400, 150, 372);
    lv_label_set_text(idleTitle, L(activeLang->touch7MenuScreen).c_str());

    // Dim / screensaver timeout steppers (persisted, apply live).
    im->cfgDim = lv_button_create(im->tabCfg);
    t7StyleBtn(im, im->cfgDim);
    lv_obj_set_size(im->cfgDim, 372, 46);
    lv_obj_set_pos(im->cfgDim, 400, 178);
    lv_obj_t* dimL = t7CreateLabel(im->cfgDim);
    lv_obj_center(dimL);
    lv_obj_add_event_cb(im->cfgDim, evtDimStep, LV_EVENT_CLICKED, NULL);
    im->cfgSaver = lv_button_create(im->tabCfg);
    t7StyleBtn(im, im->cfgSaver);
    lv_obj_set_size(im->cfgSaver, 372, 46);
    lv_obj_set_pos(im->cfgSaver, 400, 232);
    lv_obj_t* savL = t7CreateLabel(im->cfgSaver);
    lv_obj_center(savL);
    lv_obj_add_event_cb(im->cfgSaver, evtSaverStep, LV_EVENT_CLICKED, NULL);
    t7DimSaverLabel(im->cfgDim, "Escurecer",
                    touch7ConfigManager.isInitialized()
                        ? touch7ConfigManager.getConfig().pir_dim_sec : 300);
    t7DimSaverLabel(im->cfgSaver, "Saver",
                    touch7ConfigManager.isInitialized()
                        ? touch7ConfigManager.getConfig().screensaver_sec : 600);
    lv_obj_t* rb = lv_button_create(im->tabCfg);
    t7StyleBtn(im, rb);
    lv_obj_set_size(rb, 180, 46);
    lv_obj_set_pos(rb, 400, 294);
    lv_obj_t* rbl = t7CreateLabel(rb);
    lv_label_set_text(rbl, "Reiniciar");
    lv_obj_center(rbl);
    lv_obj_add_event_cb(rb, evtReboot, LV_EVENT_CLICKED, NULL);

    // Tab bar (800x64 @y=416).
    const char* tabN[4] = {"AGORA", "PREVISÃO", "PEERS", "CFG"};
    for (int i = 0; i < 4; i++) {
        im->tabBtns[i] = lv_button_create(scr);
        t7StyleBtn(im, im->tabBtns[i]);
        lv_obj_set_size(im->tabBtns[i], 188, 56);
        lv_obj_set_pos(im->tabBtns[i], 8 + i * 196, 418);
        lv_obj_t* l = t7CreateLabel(im->tabBtns[i]);
        lv_label_set_text(l, tabN[i]);
        lv_obj_center(l);
        lv_obj_add_event_cb(im->tabBtns[i], evtTab, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    im->curTab = 0;
    t7RefreshSelectionStyles(_theme, _clockStyle, im->curTab);

    // Toast + screensaver (hidden).
    im->toast = t7Label(scr, &lv_font_montserrat_20, 150, 180, 500);
    lv_obj_add_style(im->toast, &im->stPanel, 0);
    lv_obj_set_hidden(im->toast, true);
    im->saver = lv_obj_create(scr);
    lv_obj_set_size(im->saver, 800, 480);
    lv_obj_set_pos(im->saver, 0, 0);
    lv_obj_set_style_bg_color(im->saver, lv_color_black(), 0);
    im->saverClock = t7Label(im->saver, &lv_font_montserrat_48, 0, 150, 800);
    lv_obj_set_style_text_align(im->saverClock, LV_TEXT_ALIGN_CENTER, 0);
    im->saverSub = t7Label(im->saver, &lv_font_montserrat_20, 0, 280, 800);
    lv_obj_set_style_text_align(im->saverSub, LV_TEXT_ALIGN_CENTER, 0);
    // Analog face (styles 3-4): arc dial + hour/min hands, hidden by default.
    im->saverArc = lv_arc_create(im->saver);
    lv_obj_set_size(im->saverArc, 300, 300);
    lv_obj_set_pos(im->saverArc, 250, 60);
    lv_arc_set_bg_angles(im->saverArc, 0, 360);
    lv_arc_set_angles(im->saverArc, 0, 360);
    lv_obj_set_clickable(im->saverArc, false);
    static lv_point_precise_t handH0[2] = {{0, 0}, {0, -70}};
    static lv_point_precise_t handM0[2] = {{0, 0}, {0, -110}};
    im->saverHandH[0] = handH0[0]; im->saverHandH[1] = handH0[1];
    im->saverHandM[0] = handM0[0]; im->saverHandM[1] = handM0[1];
    im->saverHour = lv_line_create(im->saver);
    lv_line_set_points(im->saverHour, im->saverHandH, 2);
    lv_obj_set_pos(im->saverHour, 400, 210);
    lv_obj_set_style_line_width(im->saverHour, 6, 0);
    lv_obj_set_style_line_color(im->saverHour, lv_color_white(), 0);
    im->saverMin = lv_line_create(im->saver);
    lv_line_set_points(im->saverMin, im->saverHandM, 2);
    lv_obj_set_pos(im->saverMin, 400, 210);
    lv_obj_set_style_line_width(im->saverMin, 4, 0);
    lv_obj_set_style_line_color(im->saverMin, lv_color_white(), 0);
    lv_obj_set_hidden(im->saver, true);

    startIconAnim(im);
}

void Touch7Display::refreshClock() {
    if (!IM) return;
    time_t now = time(0);
    struct tm ti;
    localtime_r(&now, &ti);
    char hhmm[16], date[32];
    if (now < 1000000000L) {
        snprintf(hhmm, sizeof(hhmm), "--:--");
        snprintf(date, sizeof(date), "sem hora");
    } else {
        snprintf(hhmm, sizeof(hhmm), "%02d:%02d", ti.tm_hour, ti.tm_min);
        snprintf(date, sizeof(date), "%02d-%02d-%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    }
    // T7-25: single-FB streaming tears on every redraw — this 1 s job must
    // not touch labels/hands unless the minute (or date) actually changed.
    static char s_hhmm[16] = "", s_date[32] = "";
    if (!strcmp(hhmm, s_hhmm) && !strcmp(date, s_date)) return;
    strncpy(s_hhmm, hhmm, sizeof(s_hhmm) - 1);
    s_hhmm[sizeof(s_hhmm) - 1] = '\0';
    strncpy(s_date, date, sizeof(s_date) - 1);
    s_date[sizeof(s_date) - 1] = '\0';
    setClockNow(String(hhmm), String(date));
    // Digital saver variants: style 1 adds the weekday line.
    if (_clockStyle == 1 && now >= 1000000000L) {
        static const char* wd[] = {"DOM","SEG","TER","QUA","QUI","SEX","SAB"};
        String s = String(wd[ti.tm_wday % 7]) + " " + String(date);
        lv_label_set_text(IM->saverSub, s.c_str());
    }
    // Analog saver face (styles 3-4): rotate hands; style 4 adds minute arc.
    if (_clockStyle >= 3 && now >= 1000000000L) {
        float mh = (ti.tm_hour % 12) * 30.0f + ti.tm_min * 0.5f;
        float mm = ti.tm_min * 6.0f;
        float rh = mh * 3.14159f / 180.0f, rm = mm * 3.14159f / 180.0f;
        IM->saverHandH[1].x = (lv_coord_t)(70 * sin(rh));
        IM->saverHandH[1].y = (lv_coord_t)(-70 * cos(rh));
        IM->saverHandM[1].x = (lv_coord_t)(110 * sin(rm));
        IM->saverHandM[1].y = (lv_coord_t)(-110 * cos(rm));
        lv_line_set_points(IM->saverHour, IM->saverHandH, 2);
        lv_line_set_points(IM->saverMin, IM->saverHandM, 2);
        if (_clockStyle == 4) lv_arc_set_value(IM->saverArc, ti.tm_min);
        else lv_arc_set_value(IM->saverArc, 60);
    }
}

void Touch7Display::refreshAgora() {
    if (!IM) return;
#if ENABLE_AHT20_BMP280==1
    const SensorConfig& cfg = sensorConfigManager.getConfig();
    if (cfg.last_reading_time > 0) {
        float lux = 0;
#if ENABLE_BH1750==1
        lux = bh1750ConfigManager.getConfig().last_lux;
#endif
        int code = touch7WeatherHasData() ? touch7WeatherCode() : 1;
        bool isDay = touch7WeatherHasData() ? touch7WeatherIsDay() : true;
        setAgora(cfg.last_temp_avg, cfg.last_humidity, cfg.last_pressure_hpa, lux, code, isDay);
        // T7-25: only touch labels whose text actually changed (1 s job).
        static char s_agoraSub[96] = "", s_saverSub[96] = "";
        char sub[96];
        snprintf(sub, sizeof(sub), "Min %.1f  Max %.1f", cfg.last_temp_avg - 1.0f, cfg.last_temp_avg + 1.0f);
        if (strcmp(sub, s_agoraSub) != 0) {
            strncpy(s_agoraSub, sub, sizeof(s_agoraSub) - 1);
            s_agoraSub[sizeof(s_agoraSub) - 1] = '\0';
            lv_label_set_text(IM->agoraSub, sub);
        }
        if (_clockStyle != 1) {
            snprintf(sub, sizeof(sub), "%.1fC  %.0f%%", cfg.last_temp_avg, cfg.last_humidity);
            if (strcmp(sub, s_saverSub) != 0) {
                strncpy(s_saverSub, sub, sizeof(s_saverSub) - 1);
                s_saverSub[sizeof(s_saverSub) - 1] = '\0';
                lv_label_set_text(IM->saverSub, sub);
            }
        }
    }
#endif
}

void Touch7Display::autoBrightness() {
    if (!IM) return;
    if (s_brightManual) return; // user dragged the bar; AUTO re-arms it
#if ENABLE_BH1750==1
    const Touch7Config& tcfg = touch7ConfigManager.getConfig();
    if (!tcfg.backlight_auto) return;
    float lux = bh1750ConfigManager.getConfig().last_lux;
    if (lux <= 0 && bh1750ConfigManager.getConfig().last_reading_time == 0) return;
    int pct = touch7LuxToBacklight(lux);
    s_lastBacklight = -1;
    setBrightness((uint8_t)((pct * 255) / 100));
    s_brightnessProgrammatic = true;
    lv_slider_set_value(IM->cfgBright, pct, LV_ANIM_OFF);
    s_brightnessProgrammatic = false;
    if (IM->cfgBrightValue) {
        char value[24];
        snprintf(value, sizeof(value), "AUTO %d%%", pct);
        lv_label_set_text(IM->cfgBrightValue, value);
    }
#endif
}

void touch7AudioClickSafe() {
    // Called from LVGL event context (loop task): play click if idle.
    if (!touch7AudioIsPlaying()) touch7AudioPlay("/falas/click.wav");
}

void touch7SystemBegin() {
    touch7ConfigManager.begin();
    // Cache the NVS "all_configs" size now (pre-panel: flash reads are safe
    // here) so /api/status never touches NVS while the display streams.
    {
        Preferences p;
        if (p.begin("all_configs", true)) {
            touch7NvsAllConfigsBytes =
                (size_t)(p.isKey("data") ? p.getString("data", "").length() : 0);
            p.end();
        }
    }
    // Cache flash/NVS stats now (pre-panel: flash reads are safe here) so
    // /api/status never touches flash while the display streams.
    touch7StatFlashMounted = (littlefsBegin() && littlefsMounted());
    if (touch7StatFlashMounted) {
        touch7StatFlashTotal = littlefsTotalBytes();
        touch7StatFlashUsed  = littlefsUsedBytes();
        touch7StatFlashFree  = littlefsFreeBytes();
    }
    {
        nvs_stats_t st;
        if (nvs_get_stats(NULL, &st) == ESP_OK && st.total_entries > 0) {
            const esp_partition_t* np = esp_partition_find_first(
                ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
            if (np) {
                touch7StatNvsTotalKb = (int)(np->size / 1024UL);
                uint32_t freeEntries = st.total_entries - st.used_entries;
                double ratio = (double)freeEntries / (double)st.total_entries;
                touch7StatNvsFreeKb = (int)((uint64_t)(ratio * (double)np->size) / 1024ULL);
            }
        }
    }
    touch7PirBegin();
    touch7AudioBegin();
    touch7WeatherBegin();
    touch7PeersBegin();
    touch7AlarmsBegin();
    // Apply persisted theme/clock once the display is up.
    const Touch7Config& cfg = touch7ConfigManager.getConfig();
    touch7Display.applyTheme((uint8_t)touch7ClampInt(cfg.theme, 0, TOUCH7_THEME_COUNT - 1));
    touch7Display.applyClockStyle((uint8_t)touch7ClampInt(cfg.clock_style, 0, 4));
}

// Forward (defined below).
static void touch7SensorsRetryTick();

void touch7SystemTick() {
    touch7Display.tick();
    touch7PirTick();
    touch7AudioTick();
    touch7AlarmsTick();
    touch7WeatherTick();
    touch7PeersTick();
    touch7SensorsRetryTick();

    if (s_rebootPending) {
        s_rebootPending = false;
        if (touch7ConfigManager.hasDeferredSave()) {
            Touch7PanelHold hold;
            touch7ConfigManager.flushDeferredSave();
        }
        ESP.restart();
    }
    if (touch7ConfigManager.deferredSaveDue(touch7ConfigFlushAllowed())) {
        Touch7PanelHold hold;
        touch7ConfigManager.flushDeferredSave();
    }
}

// Post-panel sensor retry: retired by T7-24c. Local I2C sensors are
// unsupported on TOUCH7 (shared-bus deadlock — see sensor_aht20_bmp280.cpp),
// all begins report absent by design, so there is nothing to retry. Kept as
// a no-op so touch7SystemTick() needs no changes.
static void touch7SensorsRetryTick() {
    (void)touch7Display.isInitialized();
    return;
}

#endif // SELECTED_PACK == PACK_S3_TOUCH7
