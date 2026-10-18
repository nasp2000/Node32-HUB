/**
 * lv_conf.h — LVGL v9.6 configuration for the TouchMeteo-7 pack.
 *
 * Found via -Isrc + -DLV_CONF_INCLUDE_SIMPLE (both set in [env:s3_touch7]).
 * Only this env compiles LVGL, so this file never affects other builds.
 * Everything not defined here falls back to LVGL's internal defaults (OS,
 * stdlib, theme, draw engine are all sensible by default).
 * Rule nº1: draw buffers live in PSRAM (allocated in touch7_display.cpp),
 * LVGL object heap kept small here.
 */
#if 1

#ifndef LV_CONF_H
#define LV_CONF_H

/* RGB565 panel (ST7262). v9.6: LV_COLOR_DEPTH is deprecated; set the default
 * color format directly (LV_COLOR_FORMAT_RGB565), which also avoids the
 * deprecation warning. */
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565

/* Object heap: internal RAM (v9 value is in BYTES). Draw buffers are separate,
 * in PSRAM. Raise if widgets ever fail to allocate on HW. */
#define LV_MEM_SIZE (48U * 1024U)

/* Default display refresh period (ms) — we pump lv_tick_inc from the loop. */
#define LV_DEF_REFR_PERIOD 30

/* Software draw engine + buffer alignment */
#define LV_USE_DRAW_SW 1
#define LV_DRAW_BUF_ALIGN 4

/* Widgets actually used by the TouchMeteo-7 UI */
#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1
#define LV_USE_BAR 1
#define LV_USE_SLIDER 1
#define LV_USE_IMAGE 1
#define LV_USE_ARC 1
#define LV_USE_LINE 1
#define LV_USE_KEYBOARD 0

/* Log / asserts */
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 0

/* Fonts (builtin only) */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

/* File system + decoders off (JPEG handled by the JPEGDecoder lib) */
#define LV_USE_FS_STDIO 0
#define LV_USE_PNG 0
#define LV_USE_BMP 0
#define LV_USE_SJPG 0

#endif /*LV_CONF_H*/
#endif
