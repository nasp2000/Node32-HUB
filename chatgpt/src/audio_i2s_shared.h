#pragma once

#include "feature_flags.h"

// ============================================================================
// Shared native I2S (driver/i2s.h legacy) init/teardown for the UDA1334A DAC.
//
// Used by the Phase 1 PoC (audio_i2s_poc.cpp) and the Phase 2 MP3 decoder
// (audio_mp3_decoder.cpp). Both MUST use the legacy driver API — linking the
// Arduino ESP_I2S wrapper (I2SClass, driver/i2s_std.h) alongside it makes
// ESP-IDF 5.x abort at boot ("CONFLICT! ... legacy i2s driver").
// ============================================================================

#include <Arduino.h>
#include <driver/i2s.h>
#include <driver/gpio.h>

#ifndef UDA1334A_I2S_BCLK_PIN
  #define UDA1334A_I2S_BCLK_PIN   26
#endif
#ifndef UDA1334A_I2S_LRCK_PIN
  #define UDA1334A_I2S_LRCK_PIN   25
#endif
#ifndef UDA1334A_I2S_DOUT_PIN
  #define UDA1334A_I2S_DOUT_PIN   27
#endif
#ifndef UDA1334A_I2S_MCLK_PIN
  #define UDA1334A_I2S_MCLK_PIN   -1
#endif
#ifndef UDA1334A_I2S_PORT
  #define UDA1334A_I2S_PORT       I2S_NUM_0
#endif
#ifndef UDA1334A_MUTE_PIN
  #define UDA1334A_MUTE_PIN       -1
#endif

// Drive the UDA1334A soft-mute pin (active-high = silence). Must be LOW before
// any audio is written, otherwise a floating (high-Z) MUTE input can leave the
// DAC muted.
static inline void audioI2sSetMuted(bool muted) {
#if UDA1334A_MUTE_PIN >= 0
    gpio_set_direction((gpio_num_t)UDA1334A_MUTE_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)UDA1334A_MUTE_PIN, muted ? 1 : 0);
#endif
}

// Ground BCLK/LRCK/DOUT after releasing the port so a passive DAC sees solid
// lows (not floating lines) and stays silent until the radio plays again.
static inline void audioI2sGroundIdlePins() {
    const int idlePins[3] = { UDA1334A_I2S_BCLK_PIN, UDA1334A_I2S_LRCK_PIN, UDA1334A_I2S_DOUT_PIN };
    for (int i = 0; i < 3; i++) {
        if (idlePins[i] >= 0) {
            gpio_reset_pin((gpio_num_t)idlePins[i]);
            gpio_set_direction((gpio_num_t)idlePins[i], GPIO_MODE_OUTPUT);
            gpio_set_level((gpio_num_t)idlePins[i], 0);
        }
    }
}

// Install the native I2S driver and route the UDA1334A pins. Returns ESP_OK on
// success (or the esp_err_t that failed). Caller must have unmuted the DAC.
static inline esp_err_t audioI2sInit(uint32_t sampleRate) {
    i2s_config_t i2s_cfg = {
        .mode                   = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate            = (int)sampleRate,
        .bits_per_sample        = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format         = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format   = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags       = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count          = 4,
        .dma_buf_len            = 256,
        .use_apll               = false,
        .tx_desc_auto_clear     = true,
        .fixed_mclk             = 0,
    };
    i2s_pin_config_t pin_cfg = {};
    pin_cfg.bck_io_num   = UDA1334A_I2S_BCLK_PIN;
    pin_cfg.ws_io_num    = UDA1334A_I2S_LRCK_PIN;
    pin_cfg.data_out_num = UDA1334A_I2S_DOUT_PIN;
    pin_cfg.data_in_num  = I2S_PIN_NO_CHANGE;
    pin_cfg.mck_io_num   = UDA1334A_I2S_MCLK_PIN;

    esp_err_t err = i2s_driver_install(UDA1334A_I2S_PORT, &i2s_cfg, 0, nullptr);
    if (err != ESP_OK) return err;
    err = i2s_set_pin(UDA1334A_I2S_PORT, &pin_cfg);
    if (err != ESP_OK) {
        i2s_driver_uninstall(UDA1334A_I2S_PORT);
        return err;
    }
    return ESP_OK;
}

// Stop the clock, release the port, re-mute the DAC and ground the pins so the
// passive DAC stays silent until the radio runtime claims the port again.
static inline void audioI2sRelease() {
    i2s_stop(UDA1334A_I2S_PORT);
    i2s_driver_uninstall(UDA1334A_I2S_PORT);
    audioI2sSetMuted(true);
    audioI2sGroundIdlePins();
}
