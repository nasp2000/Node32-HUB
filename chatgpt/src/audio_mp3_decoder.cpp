#include "audio_mp3_decoder.h"

#include "feature_flags.h"

// Included unconditionally (outside the ENABLE_* guards) so the PlatformIO LDF
// conditional scanner sees the dependency and links the local minimp3 library
// (lib/minimp3) even when the pack macros are not defined during the LDF scan.
// Only the declarations are pulled here; the implementation is materialized
// below with MINIMP3_IMPLEMENTATION inside the pack guard.
#include "minimp3.h"

#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

// Phase 2 MP3 decoder: SD MP3 -> minimp3 -> native I2S -> UDA1334A.
//
// Uses the SAME native I2S driver (driver/i2s.h, legacy API) via
// audio_i2s_shared.h — NOT the Arduino ESP_I2S wrapper (linking both makes
// ESP-IDF 5.x abort at boot with a driver CONFLICT).
//
// minimp3 is frame-by-frame: mp3dec_decode_frame() tells us how many MP3 bytes
// it consumed (frame_bytes) and how many PCM samples it produced. The decoder
// re-syncs across ID3 headers and gaps, so we can feed it a rolling buffer.

#include <Arduino.h>
#include <driver/i2s.h>
#include "commands.h"
#include "psram_helpers.h"
#include "sdcard.h"
#include "audio_i2s_shared.h"

// Feeding buffer: >= 10 consecutive MP3 frames (~16 KB) for reliable sync.
static const uint32_t kMp3InBufBytes = 16 * 1024;
// PCM output for one frame: stereo 16-bit = 1152 samples/ch * 2 ch * 2 bytes.
static const uint32_t kMp3PcmFrames = MINIMP3_MAX_SAMPLES_PER_FRAME; // 2304 (stereo interleaved)

void audioMp3PlaySd(const char* path) {
    char buf[192];

    if (!sdcardIsMounted()) {
        sendVirtualMessage(F("[MP3] SD not mounted"), WARN, false, true);
        return;
    }
    if (!sdMutexLock(pdMS_TO_TICKS(8000))) {
        sendVirtualMessage(F("[MP3] SD busy"), WARN, false, true);
        return;
    }
    File f = SD_MMC.open(path, FILE_READ);
    sdMutexUnlock();
    if (!f) {
        sendVirtualMessage(String("[MP3] Cannot open ") + path, WARN, false, true);
        return;
    }

    uint8_t* inBuf = (uint8_t*)heap_caps_malloc(kMp3InBufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!inBuf) inBuf = (uint8_t*)heap_caps_malloc(kMp3InBufBytes, MALLOC_CAP_8BIT);
    if (!inBuf) {
        f.close();
        sendVirtualMessage(F("[MP3] OOM input buffer"), ERROR, false, true);
        return;
    }
    int16_t* pcm = (int16_t*)heap_caps_malloc(kMp3PcmFrames * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pcm) pcm = (int16_t*)heap_caps_malloc(kMp3PcmFrames * sizeof(int16_t), MALLOC_CAP_8BIT);
    if (!pcm) {
        heap_caps_free(inBuf);
        f.close();
        sendVirtualMessage(F("[MP3] OOM PCM buffer"), ERROR, false, true);
        return;
    }

    // Install the native I2S driver on the UDA1334A pins.
    audioI2sSetMuted(false);
    esp_err_t err = audioI2sInit(44100); // minimp3 may change rate per frame; reconfigure on change
    if (err != ESP_OK) {
        snprintf(buf, sizeof(buf), "[MP3] I2S init FAILED (%s)", esp_err_to_name(err));
        sendVirtualMessage(String(buf), ERROR, false, true);
        audioI2sSetMuted(true);
        heap_caps_free(inBuf);
        heap_caps_free(pcm);
        f.close();
        return;
    }

    mp3dec_t dec;
    mp3dec_init(&dec);

    uint32_t inLen = 0;          // bytes currently buffered in inBuf
    uint32_t inPos = 0;          // unconsumed offset within inBuf
    uint32_t totalIn = 0;        // total MP3 bytes read from file
    uint32_t totalPcm = 0;       // total PCM bytes written to I2S
    uint32_t framesOk = 0;
    int curRate = 44100;
    bool eof = false;
    bool firstRateLogged = false;

    sendVirtualMessage(String(F("[MP3] Decoding ")) + path, INFO, false, true);

    while (!eof) {
        // Top up the rolling buffer from the file.
        if (inLen < kMp3InBufBytes && !eof) {
            // Compact any unconsumed tail to the front.
            if (inPos > 0 && inLen > inPos) {
                memmove(inBuf, inBuf + inPos, inLen - inPos);
                inLen -= inPos;
                inPos = 0;
            } else if (inPos > 0) {
                inLen = 0;
                inPos = 0;
            }
            if (sdMutexLock(pdMS_TO_TICKS(8000))) {
                size_t got = f.read(inBuf + inLen, kMp3InBufBytes - inLen);
                sdMutexUnlock();
                if (got == 0) {
                    eof = true;
                } else {
                    inLen += (uint32_t)got;
                    totalIn += (uint32_t)got;
                }
            } else {
                sendVirtualMessage(F("[MP3] SD busy during read"), WARN, false, true);
                break;
            }
        }

        const uint8_t* avail = inBuf + inPos;
        uint32_t availLen = inLen - inPos;
        if (availLen == 0) {
            if (eof) break;
            continue;
        }

        mp3dec_frame_info_t info;
        memset(&info, 0, sizeof(info));
        int samples = mp3dec_decode_frame(&dec, avail, availLen, pcm, &info);

        if (info.frame_bytes > 0) {
            inPos += (uint32_t)info.frame_bytes;
            if (inPos >= inLen) { inLen = 0; inPos = 0; }

            if (samples > 0) {
                // Handle sample-rate / channel change (rare in one file).
                if (info.hz > 0 && info.hz != curRate) {
                    i2s_set_clk(UDA1334A_I2S_PORT, (uint32_t)info.hz,
                                I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
                    curRate = info.hz;
                    snprintf(buf, sizeof(buf), "[MP3] rate change -> %d Hz", info.hz);
                    sendVirtualMessage(String(buf), INFO, false, true);
                }
                if (!firstRateLogged) {
                    snprintf(buf, sizeof(buf), "[MP3] first frame: %d Hz, %d ch, %d samples",
                             info.hz, info.channels, samples);
                    sendVirtualMessage(String(buf), INFO, false, true);
                    firstRateLogged = true;
                }
                size_t pcmBytes = (size_t)samples * 2 * sizeof(int16_t);
                size_t written = 0;
                esp_err_t werr = i2s_write(UDA1334A_I2S_PORT, pcm, pcmBytes, &written, portMAX_DELAY);
                if (werr != ESP_OK || written != pcmBytes) {
                    snprintf(buf, sizeof(buf), "[MP3] I2S write err=%d (%u/%u bytes)",
                             (int)werr, (unsigned)written, (unsigned)pcmBytes);
                    sendVirtualMessage(String(buf), WARN, false, true);
                    break;
                }
                totalPcm += (uint32_t)written;
                framesOk++;
            }
            // 0 samples + frame_bytes>0 = skipped ID3/garbage; keep going.
            continue;
        }

        // frame_bytes == 0: need more data or end of file.
        if (eof) {
            break;
        }
        if (inPos > 0 && inLen == inPos) {
            inLen = 0;
            inPos = 0;
        }
        // Buffer full but decoder consumed nothing (bad sync region): drain a
        // byte and retry so we never spin on the same data.
        if (inLen == kMp3InBufBytes && availLen == kMp3InBufBytes) {
            inPos++;
        }
        if (inPos > 0 && inLen == kMp3InBufBytes) {
            memmove(inBuf, inBuf + inPos, inLen - inPos);
            inLen -= inPos;
            inPos = 0;
        }
        if (inLen == 0) break; // cannot progress
    }

    f.close();
    heap_caps_free(inBuf);
    heap_caps_free(pcm);

    snprintf(buf, sizeof(buf), "[MP3] done: %u frames, %u KB in, %u KB PCM out",
             (unsigned)framesOk, (unsigned)(totalIn / 1024), (unsigned)(totalPcm / 1024));
    sendVirtualMessage(String(buf), INFO, false, true);

    audioI2sRelease();
    sendVirtualMessage(F("[MP3] I2S port released, DAC idle (muted)"), INFO, false, true);
}

#endif // PACK_RADIO
#endif // ENABLE_UDA1334A == 1
