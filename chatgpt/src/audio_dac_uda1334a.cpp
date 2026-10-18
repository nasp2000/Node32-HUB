#include "audio_dac_uda1334a.h"

#include "feature_flags.h"
#if ENABLE_UDA1334A == 1

#include <Arduino.h>
#include <driver/i2s.h>
#include <driver/gpio.h> // gpio_set_direction / gpio_set_level (optional MUTE pin)
#include "feature_flags.h"
#include "commands.h"    // sendVirtualMessage -> Serial + Web Monitor

UDA1334ADAC udaDac;

// ==================================================================================
// Implementation
// ==================================================================================

UDA1334ADAC::UDA1334ADAC()
    : _initialized(false),
      _format(44100, 16, 2),
      _pinCfg({})
{
}

bool UDA1334ADAC::begin(const AudioFormat& fmt) {
    if (_initialized) return true;

    _format = fmt;

    // I2S configuration
    i2s_config_t i2s_cfg = {
        .mode                   = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate            = (int)fmt.sampleRate,
        .bits_per_sample        = (i2s_bits_per_sample_t)fmt.bitDepth,
        .channel_format         = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format   = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags       = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count          = UDA1334A_I2S_DMA_BUF_COUNT,
        .dma_buf_len            = UDA1334A_I2S_DMA_BUF_LEN,
        .use_apll               = false,
        .tx_desc_auto_clear     = true,
        .fixed_mclk             = 0,
    };

    // I2S pin configuration (explicit assignments avoid designated-order issues across core versions)
    i2s_pin_config_t pin_cfg = {};
    pin_cfg.bck_io_num = UDA1334A_I2S_BCLK_PIN;
    pin_cfg.ws_io_num = UDA1334A_I2S_LRCK_PIN;
    pin_cfg.data_out_num = UDA1334A_I2S_DOUT_PIN;
    pin_cfg.data_in_num = I2S_PIN_NO_CHANGE;
    pin_cfg.mck_io_num = UDA1334A_I2S_MCLK_PIN;
    _pinCfg = pin_cfg;

    // Install and configure I2S driver
    esp_err_t err = i2s_driver_install(UDA1334A_I2S_PORT, &i2s_cfg, 0, nullptr);
    if (err != ESP_OK) {
        Serial.printf("[UDA1334A] Failed to install I2S driver: %s\n", esp_err_to_name(err));
        return false;
    }

    err = i2s_set_pin(UDA1334A_I2S_PORT, &pin_cfg);
    if (err != ESP_OK) {
        Serial.printf("[UDA1334A] Failed to set I2S pins: %s\n", esp_err_to_name(err));
        i2s_driver_uninstall(UDA1334A_I2S_PORT);
        return false;
    }

#if UDA1334A_MUTE_PIN >= 0
    // Optional MUTE control: drive the DAC mute pin LOW (audio active) as soon
    // as possible so a floating MUTE input can never leave the DAC muted or
    // noisy at boot.
    gpio_set_direction((gpio_num_t)UDA1334A_MUTE_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)UDA1334A_MUTE_PIN, 0);
    Serial.printf("[UDA1334A] MUTE pin = GPIO%d (software controlled)\n", UDA1334A_MUTE_PIN);
#else
    Serial.println("[UDA1334A] MUTE not wired (pin = -1)");
#endif

    _initialized = true;
    Serial.printf("[UDA1334A] Initialized: %u Hz, %d-bit, %d ch (I2S_NUM_%d)\n",
                  fmt.sampleRate, fmt.bitDepth, fmt.channels, UDA1334A_I2S_PORT);
    return true;
}

bool UDA1334ADAC::setAudioFormat(const AudioFormat& fmt) {
    if (!_initialized) return false;

    // Stop current stream
    i2s_stop(UDA1334A_I2S_PORT);

    // Reconfigure with new sample rate
    esp_err_t err = i2s_set_clk(UDA1334A_I2S_PORT, fmt.sampleRate, (i2s_bits_per_sample_t)fmt.bitDepth, I2S_CHANNEL_STEREO);
    if (err != ESP_OK) {
        Serial.printf("[UDA1334A] Failed to set clock: %s\n", esp_err_to_name(err));
        return false;
    }

    _format = fmt;

    // Resume streaming
    i2s_start(UDA1334A_I2S_PORT);

    Serial.printf("[UDA1334A] Format changed: %u Hz, %d-bit\n", fmt.sampleRate, fmt.bitDepth);
    return true;
}

size_t UDA1334ADAC::write(const uint8_t* buffer, size_t len) {
    if (!_initialized || buffer == nullptr || len == 0) return 0;

    size_t bytes_written = 0;
    esp_err_t err = i2s_write(UDA1334A_I2S_PORT, buffer, len, &bytes_written, portMAX_DELAY);
    if (err != ESP_OK) {
        Serial.printf("[UDA1334A] I2S write error: %s\n", esp_err_to_name(err));
        return 0;
    }

    return bytes_written;
}

void UDA1334ADAC::end() {
    if (!_initialized) return;

    i2s_stop(UDA1334A_I2S_PORT);
    i2s_driver_uninstall(UDA1334A_I2S_PORT);
    _initialized = false;

    Serial.println("[UDA1334A] Deinitialized");
}

void UDA1334ADAC::setMuted(bool mute) {
#if UDA1334A_MUTE_PIN >= 0
    gpio_set_level((gpio_num_t)UDA1334A_MUTE_PIN, mute ? 1 : 0);
#endif
}

void UDA1334ADAC::playTestSound() {
    // Do not bail out when not initialized: the radio pack owns the I2S port, so
    // a raw i2s_write() still reaches the same DAC pins.
    bool wasInitialized = _initialized;
    if (!wasInitialized && !begin(_format)) {
        Serial.println("[UDA1334A] Test sound: not initialized, using existing I2S driver");
    }
    const uint32_t rate = _format.sampleRate ? _format.sampleRate : 44100U;
    // A short ascending melody — C5, E5, G5, C6, then a low E3 sweep. Each note
    // plays ~220ms so left/right and both channels are audible on a 3.5mm jack.
    static const float kNotesHz[] = { 523.25f, 659.25f, 783.99f, 1046.50f, 164.81f };
    static const uint32_t kNoteMs[] = { 220, 220, 220, 260, 320 };
    const size_t kChunk = 512;                     // samples (stereo → 4 bytes each)
    int16_t buf[kChunk * 2];
    size_t totalWritten = 0;
    for (size_t n = 0; n < sizeof(kNotesHz) / sizeof(kNotesHz[0]); n++) {
        const float freq = kNotesHz[n];
        const uint32_t total = (uint32_t)(rate * kNoteMs[n] / 1000U);
        const float amp = (n < 4) ? 9000.0f : 12000.0f;  // louder bass so it is clearly audible
        uint32_t written = 0;
        while (written < total) {
            const size_t cur = (total - written < kChunk) ? (total - written) : kChunk;
            for (size_t i = 0; i < cur; i++) {
                const float t = (float)(written + i) / (float)rate;
                const int16_t s = (int16_t)(amp * sinf(2.0f * (float)M_PI * freq * t));
                buf[i * 2] = s;       // left
                buf[i * 2 + 1] = s;   // right
            }
            size_t bytes_written = 0;
            esp_err_t err = i2s_write(UDA1334A_I2S_PORT, (const uint8_t*)buf,
                                      cur * 2 * sizeof(int16_t), &bytes_written,
                                      pdMS_TO_TICKS(200));
            if (err != ESP_OK) {
                Serial.printf("[UDA1334A] Test sound: i2s_write error %s (%u bytes)\n",
                              esp_err_to_name(err), (unsigned)bytes_written);
                return;
            }
            totalWritten += bytes_written;
            written += cur;
        }
    }
    // Silence tail so the last note does not cut abruptly.
    memset(buf, 0, sizeof(buf));
    size_t bytes_written = 0;
    i2s_write(UDA1334A_I2S_PORT, (const uint8_t*)buf, sizeof(buf), &bytes_written, pdMS_TO_TICKS(200));
    totalWritten += bytes_written;
    Serial.printf("[UDA1334A] Test sound finished (%u bytes written)\n", (unsigned)totalWritten);

    // If we installed the driver for this test, release the port so the radio
    // output can claim it again. When the radio owned the port (begin() failed
    // above) or the DAC was already in use, keep it as-is.
    if (!wasInitialized && _initialized) {
        end();
        Serial.println("[UDA1334A] Test sound: released I2S driver");
    }
}

void UDA1334ADAC::playTimingTest(uint32_t seconds) {
    // Isolate the I2S clock: write exactly `seconds` of stereo 16-bit PCM at
    // the current sample rate through the exact i2s_write path the decoders
    // use and measure the wall-clock drain time. A correct clock drains it in
    // ~= `seconds`; a fast/slow clock (sample-rate mismatch) drains it faster/
    // slower and the ratio quantifies the error. i2s_write with a long timeout
    // blocks until the DMA has consumed the data, so elapsed wall time is the
    // real playback duration, independent of the decoder.
    if (seconds == 0) seconds = 60;
    bool wasInitialized = _initialized;
    if (!wasInitialized && !begin(_format)) {
        sendVirtualMessage(F("[TIMING] DAC not initialized, using existing I2S driver"), INFO, false, true);
    }
    const uint32_t rate = _format.sampleRate ? _format.sampleRate : 44100U;

    // Force the I2S to run at the target rate on the DAC pins. The radio pack
    // may have left the port stopped with the pins grounded (idle), so restart
    // it here to reproduce the exact clock the decoders use for playback.
    i2s_set_pin(UDA1334A_I2S_PORT, &_pinCfg);
    i2s_set_clk(UDA1334A_I2S_PORT, (int)rate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    i2s_start(UDA1334A_I2S_PORT);
    i2s_zero_dma_buffer(UDA1334A_I2S_PORT);

    const uint32_t framesTotal = seconds * rate;           // stereo frames to write
    const size_t kChunk = 1024;                            // frames per chunk
    int16_t* buf = (int16_t*)psramFound()
        ? (int16_t*)heap_caps_malloc(kChunk * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        : (int16_t*)malloc(kChunk * 2 * sizeof(int16_t));
    if (!buf) {
        sendVirtualMessage(F("[TIMING] out of memory"), WARN, false, true);
        return;
    }
    // 440 Hz sine at low amplitude (audible but not unpleasant for 60 s).
    for (size_t i = 0; i < kChunk; i++) {
        const float t = (float)i / (float)rate;
        const int16_t s = (int16_t)(9000.0f * sinf(2.0f * (float)M_PI * 440.0f * t));
        buf[i * 2] = s;
        buf[i * 2 + 1] = s;
    }

    // Zero the DMA so the clock starts fresh (no stale buffered samples).
    i2s_zero_dma_buffer(UDA1334A_I2S_PORT);
    const uint32_t t0 = millis();
    uint32_t writtenFrames = 0;
    bool failed = false;
    while (writtenFrames < framesTotal) {
        size_t cur = framesTotal - writtenFrames;
        if (cur > kChunk) cur = kChunk;
        size_t bytes = cur * 2 * sizeof(int16_t);
        size_t bytesWritten = 0;
        esp_err_t err = i2s_write(UDA1334A_I2S_PORT, (const uint8_t*)buf, bytes, &bytesWritten, pdMS_TO_TICKS(1000));
        if (err != ESP_OK) {
            failed = true;
            break;
        }
        writtenFrames += bytesWritten / (2 * sizeof(int16_t));
        if (bytesWritten == 0) {
            failed = true;
            break; // safety: DMA not consuming
        }
    }
    // Let the last chunk drain fully before reading the clock.
    vTaskDelay(pdMS_TO_TICKS(100));
    const uint32_t t1 = millis();
    const double actualSec = (double)(t1 - t0) / 1000.0;
    const double expectedSec = (double)framesTotal / (double)rate;
    const double ratio = (expectedSec > 0.0 && actualSec > 0.0)
                       ? expectedSec / actualSec : 1.0;   // >1 = clock fast (plays shorter)
    char dbg[160];
    snprintf(dbg, sizeof(dbg),
        "[TIMING] %u s expected, %.3f s actual, ratio %.4f (%.1f%% off) @ %u Hz",
        (unsigned)seconds, actualSec, ratio, (ratio - 1.0) * 100.0, (unsigned)rate);
    sendVirtualMessage(String(dbg), INFO, false, true);
    if (failed) sendVirtualMessage(F("[TIMING] i2s_write did not keep up"), WARN, false, true);

    heap_caps_free(buf);

    // Restore the silent idle state the radio pack uses when nothing plays
    // (stop clock, mute, ground the pins) so we never leave a hum behind.
    i2s_stop(UDA1334A_I2S_PORT);
    setMuted(true);
    const int pins[3] = { UDA1334A_I2S_BCLK_PIN, UDA1334A_I2S_LRCK_PIN, UDA1334A_I2S_DOUT_PIN };
    for (int i = 0; i < 3; i++) {
        if (pins[i] >= 0) {
            gpio_reset_pin((gpio_num_t)pins[i]);
            gpio_set_direction((gpio_num_t)pins[i], GPIO_MODE_OUTPUT);
            gpio_set_level((gpio_num_t)pins[i], 0);
        }
    }

    if (!wasInitialized && _initialized) {
        end();
        sendVirtualMessage(F("[TIMING] released I2S driver"), INFO, false, true);
    }
}

#endif // ENABLE_UDA1334A == 1

