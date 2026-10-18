#include "touch7_audio.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

#include "touch7_config.h"
#include "touch7_display.h"
#include "touch7_peers_logic.h"
#include "psram_helpers.h"
#include <LittleFS.h>

// 8-bit PWM @ ~62.5 kHz carrier (LEDC 80 MHz / 1280), audio tick 16 kHz.
#define TOUCH7_AUDIO_CHUNK 4096
// Preloaded phrases: WAVs are read ONCE at boot (pre-panel, flash is safe
// then) into PSRAM; playback never touches LittleFS at runtime, so it can
// never race the RGB DMA. Total cap 1 MB.
#define TOUCH7_WAV_PRELOAD_MAX (1024UL * 1024UL)

struct Touch7WavSlot {
    char name[40];
    uint8_t* data;   // PCM payload (header stripped), PSRAM
    uint32_t len;
    uint32_t rate;
    uint8_t bits;
    uint8_t channels;
};

static Touch7WavSlot s_wavs[12];
static uint8_t s_wavCount = 0;

static TaskHandle_t s_task = 0;
static volatile bool s_stopReq = false;
static volatile bool s_playing = false;
static String s_file;
static uint8_t* s_chunk = 0;

static void audioTask(void* arg) {
    (void)arg;
    // Play from the preloaded PSRAM slot (never touch flash at runtime:
    // LittleFS reads race the RGB DMA on shared SPI1).
    String base = s_file;
    int slash = base.lastIndexOf('/');
    if (slash >= 0) base = base.substring(slash + 1);
    const Touch7WavSlot* slot = 0;
    for (uint8_t i = 0; i < s_wavCount; i++) {
        if (base.equalsIgnoreCase(s_wavs[i].name)) {
            slot = &s_wavs[i];
            break;
        }
    }
    if (!slot) {
        s_playing = false;
        s_task = 0;
        vTaskDelete(0);
        return;
    }
    const Touch7Config& cfg = touch7ConfigManager.getConfig();
    float vol = (float)touch7ClampInt(cfg.volume, 0, 10) / 10.0f;
    uint32_t rate = slot->rate ? slot->rate : 16000;
    uint32_t stepUs = 1000000UL / rate;
    unsigned long nextUs = micros();
    const uint8_t* pcm = slot->data;
    uint32_t dataLeft = slot->len;
    uint8_t bits = slot->bits;
    uint8_t channels = slot->channels;
    while (!s_stopReq && dataLeft > 0) {
        // Pause while a flash write is held elsewhere (PSRAM must stay
        // untouched); resume keeps timing by re-anchoring nextUs.
        if (touch7FlashHeld()) {
            delay(10);
            nextUs = micros();
            continue;
        }
        size_t want = TOUCH7_AUDIO_CHUNK;
        if (want > dataLeft) want = dataLeft;
        memcpy(s_chunk, pcm, want);
        pcm += want;
        int r = (int)want;
        dataLeft -= (uint32_t)r;
        if (bits == 16) {
            int16_t* s16 = (int16_t*)s_chunk;
            int n = r / 2;
            for (int i = 0; i < n && !s_stopReq; i++) {
                int16_t v = (channels > 1) ? (int16_t)(((int32_t)s16[2*i] + s16[2*i+1]) / 2) : s16[i];
                uint8_t pwm = (uint8_t)(((int32_t)v + 32768) * vol / 256);
                ledcWrite(TOUCH7_AUDIO_PWM_PIN, pwm);
                nextUs += stepUs;
                long wait = (long)(nextUs - micros());
                if (wait > 0 && wait < 100000L) delayMicroseconds((unsigned)wait);
                else nextUs = micros();
            }
        } else {
            for (int i = 0; i < r && !s_stopReq; i++) {
                uint8_t v8 = s_chunk[(channels > 1) ? (size_t)(i & ~1) : (size_t)i];
                uint8_t pwm = (uint8_t)(((int32_t)v8 - 128) * vol + 128);
                ledcWrite(TOUCH7_AUDIO_PWM_PIN, pwm);
                nextUs += stepUs;
                long wait = (long)(nextUs - micros());
                if (wait > 0 && wait < 100000L) delayMicroseconds((unsigned)wait);
                else nextUs = micros();
            }
        }
    }
    ledcWrite(TOUCH7_AUDIO_PWM_PIN, 128); // idle mid-rail (no thump)
    s_playing = false;
    s_task = 0;
    vTaskDelete(0);
}

void touch7AudioBegin() {
    if (!psramFound()) return;
    if (!s_chunk) s_chunk = (uint8_t*)ps_malloc(TOUCH7_AUDIO_CHUNK);
    ledcAttach(TOUCH7_AUDIO_PWM_PIN, 62500, 8);
    ledcWrite(TOUCH7_AUDIO_PWM_PIN, 128);
    // Preload /falas/*.wav into PSRAM (called pre-panel: flash reads safe).
    uint32_t budget = TOUCH7_WAV_PRELOAD_MAX;
    File root = LittleFS.open("/falas");
    if (root && root.isDirectory()) {
        File f = root.openNextFile();
        while (f && s_wavCount < 12 && budget > 0) {
            String name = String(f.name());
            if ((name.endsWith(".wav") || name.endsWith(".WAV")) && f.size() > 44 &&
                (uint32_t)f.size() <= budget) {
                uint8_t hdr[44];
                if (f.read(hdr, 44) == 44) {
                    Touch7WavInfo wi = touch7ParseWavHeader(hdr, 44);
                    if (wi.ok && wi.dataBytes <= budget) {
                        uint8_t* buf = (uint8_t*)ps_malloc(wi.dataBytes);
                        if (buf && f.read(buf, wi.dataBytes) == wi.dataBytes) {
                            Touch7WavSlot& s = s_wavs[s_wavCount++];
                            strncpy(s.name, name.c_str(), sizeof(s.name) - 1);
                            s.name[sizeof(s.name) - 1] = 0;
                            s.data = buf;
                            s.len = wi.dataBytes;
                            s.rate = wi.sampleRate ? wi.sampleRate : 16000;
                            s.bits = wi.bits;
                            s.channels = wi.channels;
                            budget -= wi.dataBytes;
                        } else if (buf) {
                            heap_caps_free(buf);
                        }
                    }
                }
            }
            f = root.openNextFile();
        }
    }
    Serial.printf("[TOUCH7] audio: %u phrases preloaded\n", s_wavCount);
}

void touch7AudioTick() {
    // Watchdog placeholder: playback is task-driven; volume applies per file.
}

bool touch7AudioPlay(const String& file) {
    if (s_playing || !s_chunk) return false;
    if (file.length() == 0 || file.length() > 128) return false;
    if (file.indexOf("..") >= 0) return false;
    s_file = file;
    s_stopReq = false;
    s_playing = true;
    if (xTaskCreatePinnedToCore(audioTask, "t7audio", 4096, 0, 2, &s_task, 1) != pdPASS) {
        s_playing = false;
        s_task = 0;
        return false;
    }
    return true;
}

bool touch7AudioIsPlaying() { return s_playing; }

void touch7AudioStop() {
    s_stopReq = true;
}

void touch7AudioClick() {
    // Synthesized in-loop click would block; play the bundled click file if present.
    if (!s_playing) touch7AudioPlay("/falas/click.wav");
}

#endif // SELECTED_PACK == PACK_S3_TOUCH7
