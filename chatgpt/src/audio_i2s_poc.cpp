#include "audio_i2s_poc.h"

#include "feature_flags.h"
#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

// Phase 1 PoC: known PCM -> I2S (native ESP-IDF driver) -> UDA1334A.
//
// Uses the SAME native I2S driver (driver/i2s.h, legacy API) that the radio
// already uses in audio_dac_uda1334a.cpp — the plan (§9.5) defines "ESP_I2S"
// as this native ESP-IDF driver, NOT the Arduino ESP_I2S wrapper library.
//
// IMPORTANT: the Arduino ESP_I2S library (I2SClass) must NOT be linked in the
// same firmware as driver/i2s.h. ESP-IDF 5.x detects both drivers and aborts
// at boot with "CONFLICT! The new i2s driver can't work along with the legacy
// i2s driver". Hence this TU only uses driver/i2s.h (via audio_i2s_shared.h).
//
// Generates a known stereo 16-bit 44.1 kHz sine into PSRAM, writes it through
// the I2S driver, verifies rate/bytes, and reports on Serial + Web monitor via
// sendVirtualMessage. Releases the port on return.

#include <Arduino.h>
#include <driver/i2s.h>
#include "commands.h"
#include "psram_helpers.h"
#include "audio_i2s_shared.h"

// Keep the test short but long enough to hear it: 1 s of 44.1 kHz stereo.
static const uint32_t kPocSampleRate = 44100;
static const uint32_t kPocFrames     = 44100; // 1 s
static const float    kPocFreqL      = 440.0f; // A4 on the left channel
static const float    kPocFreqR      = 880.0f; // A5 on the right channel
static const float    kPocAmp        = 0.5f;

void audioI2sPocRun() {
    char buf[192];

    // ---- 1) Generate known PCM in PSRAM (fallback to DRAM) ----
    const uint32_t bytesTotal = i2sPocBytesForFrames(kPocFrames, 2, 16);
    int16_t* pcm = nullptr;
    if (psramFound()) {
        pcm = (int16_t*)heap_caps_malloc(bytesTotal, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!pcm) {
        pcm = (int16_t*)heap_caps_malloc(bytesTotal, MALLOC_CAP_8BIT);
    }
    if (!pcm) {
        snprintf(buf, sizeof(buf), "[I2S-POC] OOM: cannot allocate %lu bytes PCM", (unsigned long)bytesTotal);
        sendVirtualMessage(String(buf), ERROR, false, true);
        return;
    }

    I2sPocTone tone;
    tone.sampleRate = kPocSampleRate;
    tone.channels   = 2;
    tone.bitDepth   = 16;
    tone.freqL      = kPocFreqL;
    tone.freqR      = kPocFreqR;
    tone.amplitude  = kPocAmp;
    uint32_t bytesFilled = i2sPocFillTone(pcm, kPocFrames, tone);
    if (bytesFilled != bytesTotal) {
        snprintf(buf, sizeof(buf), "[I2S-POC] PCM fill mismatch: %lu != %lu", (unsigned long)bytesFilled, (unsigned long)bytesTotal);
        sendVirtualMessage(String(buf), ERROR, false, true);
        heap_caps_free(pcm);
        return;
    }

    // ---- 2) Configure the native I2S driver on the UDA1334A pins ----
    audioI2sSetMuted(false); // DAC must be unmuted (MUTE pin LOW) before writing
    esp_err_t err = audioI2sInit(kPocSampleRate);
    if (err != ESP_OK) {
        snprintf(buf, sizeof(buf), "[I2S-POC] i2s init FAILED (%s)", esp_err_to_name(err));
        sendVirtualMessage(String(buf), ERROR, false, true);
        audioI2sSetMuted(true);
        heap_caps_free(pcm);
        return;
    }

    // ---- 3) Verify the configured parameters are what we asked for ----
    // The legacy driver exposes the applied sample rate via i2s_get_clk().
    // Bit depth and channel layout are fixed by the config we passed in; they
    // are reported for completeness (there is no getter for them).
    const float rateF = i2s_get_clk(UDA1334A_I2S_PORT);
    const uint32_t rate = (rateF > 0) ? (uint32_t)(rateF + 0.5f) : 0;
    const bool rateOk = (rate == kPocSampleRate);

    snprintf(buf, sizeof(buf),
             "[I2S-POC] init OK: rate=%lu(%s) bits=16(%s) ch=stereo(%s) pins B=%d/L=%d/D=%d/M=%d",
             (unsigned long)rate, rateOk ? "ok" : "MISMATCH",
             "ok",
             "ok",
             (int)UDA1334A_I2S_BCLK_PIN, (int)UDA1334A_I2S_LRCK_PIN,
             (int)UDA1334A_I2S_DOUT_PIN, (int)UDA1334A_I2S_MCLK_PIN);
    sendVirtualMessage(String(buf), INFO, false, true);

    // ---- 4) Write the known PCM and check for short writes / errors ----
    size_t written = 0;
    err = i2s_write(UDA1334A_I2S_PORT, pcm, bytesTotal, &written, portMAX_DELAY);
    const bool writeOk = (err == ESP_OK) && (written == bytesTotal);

    snprintf(buf, sizeof(buf), "[I2S-POC] write %u/%lu bytes err=%d (%s)",
             (unsigned)written, (unsigned long)bytesTotal, (int)err,
             writeOk ? "OK, no underrun" : "UNDERRUN/WRITE ERROR");
    sendVirtualMessage(String(buf), writeOk ? INFO : WARN, false, true);

    if (rateOk && writeOk) {
        snprintf(buf, sizeof(buf), "[I2S-POC] PASS: %lu Hz stereo 16-bit -> UDA1334A",
                 (unsigned long)kPocSampleRate);
        sendVirtualMessage(String(buf), INFO, false, true);
    } else {
        snprintf(buf, sizeof(buf), "[I2S-POC] FAIL: see checks above");
        sendVirtualMessage(String(buf), WARN, false, true);
    }

    // ---- 5) Release the I2S port so the radio runtime can claim it ----
    heap_caps_free(pcm);
    audioI2sRelease();
    sendVirtualMessage(F("[I2S-POC] I2S port released, DAC idle (muted)"), INFO, false, true);
}

#endif // PACK_RADIO
#endif // ENABLE_UDA1334A == 1
