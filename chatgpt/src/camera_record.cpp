// camera_record.cpp — AVI/MJPEG recording to SD and JPEG timelapse capture.

#include "camera_record.h"
#include "camera_record_naming.h"   // pre-NTP folder/name decisions (native-tested)
#include "camera_p4_h264.h"         // Fase 1: codec/bitrate/NAL helpers (pure header)
#include "camera_p4_h264_enc.h"     // Fase 1: HW encoder wrapper (stub off-P4)
// Daily storage stats (defined in sd_tasks_impl.h)
void storNoteSdWrite(uint64_t bytes);

#include "feature_flags.h"
#if ENABLE_CAMERA==1

#include <Arduino.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "sdcard.h"           // sdcardShouldUseSD(), SD alias
#include "camera_psram_ring.h"   // PSRAM shock-absorber (pure header)
#include "feature_flags.h"
#include "camera_manager.h"   // cameraGetConfig(), cameraCaptureFrame()
#include "camera_config.h"
#include "camera_motion.h"    // cameraMotionSamplingActive() — skip captures mid-switch
#include "direct_log.h"

// ---------------------------------------------------------------------------
// AVI layout constants (see header for full diagram)
// ---------------------------------------------------------------------------
#define AVI_HDR_BYTES     212   // bytes before movi LIST (RIFF + hdrl + avih + strl + strh + strf)
#define MOVI_HDR_BYTES     12   // LIST[4] + size[4] + movi[4]
#define FRAME_HDR_BYTES     8   // '00dc'[4] + size[4]
#define HEADER_TOTAL      (AVI_HDR_BYTES + MOVI_HDR_BYTES)  // 224
// SD write buffer: aligned chunk writes reduce SD command overhead.
#define CAM_AVI_IOBUF_SIZE 16384
#define CAM_WRITE_Q_DEPTH  4
#define CAM_WRITE_Q_SEND_TIMEOUT_MS 5

// ---------------------------------------------------------------------------
// Little-endian write helpers
// ---------------------------------------------------------------------------
static inline void wLE32(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
static inline void wLE16(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
}
static inline void wFCC(uint8_t* p, const char* tag) {
    memcpy(p, tag, 4);
}

// ---------------------------------------------------------------------------
// Map framesize enum value to a short label string (used in filename).
// Only trustworthy on S3: on P4 the enum is the legacy boot mode and says
// nothing about the live geometry — filenames there use
// cameraRecordResLabel(w, h) instead (see camera_record_naming.h).
// ---------------------------------------------------------------------------
static const char* frameSizeLabel(uint8_t fs) {
    switch (fs) {
        case 6:  return "QVGA";
        case 9:  return "HVGA";
        case 10: return "VGA";
        case 11: return "SVGA";
        case 12: return "XGA";
        case 13: return "HD";
        case 14: return "SXGA";
        case 15: return "UXGA";
        case 16: return "FHD";
        case 19: return "QXGA";
        case 24: return "5MP";
        default: return "VID";
    }
}

// ---------------------------------------------------------------------------
// Build the 224-byte AVI header block (AVI_HDR_BYTES + MOVI_HDR_BYTES).
//
// moviContentSize = 4("movi") + sum_of_all_frame_chunk_bytes.
// Pass moviContentSize=4 and totalFrames=0 for the initial placeholder.
// Call again with real values on finalise, then seek(0) and write.
// ---------------------------------------------------------------------------
static void buildAviHeader(uint8_t* hdr, uint32_t fps, uint32_t totalFrames,
                            uint16_t w, uint16_t h, uint32_t moviContentSize) {
    memset(hdr, 0, HEADER_TOTAL);

    uint32_t usPerFrame = (fps > 0) ? (1000000UL / fps) : 66666UL;
    uint32_t sugBuf     = (uint32_t)w * h * 2;
    // RIFF size = total file bytes − 8.
    // total = 12(RIFF) + 200(LIST hdrl) + 12(LIST movi header) + moviContentSize
    //       = 224 + moviContentSize
    uint32_t riffSize   = 216UL + moviContentSize;

    // [0] RIFF AVI header
    wFCC(hdr + 0,  "RIFF");
    wLE32(hdr + 4,  riffSize);
    wFCC(hdr + 8,  "AVI ");

    // [12] LIST hdrl  (content = 4 + 64 + 124 = 192)
    wFCC(hdr + 12, "LIST");
    wLE32(hdr + 16, 192);
    wFCC(hdr + 20, "hdrl");

    // [24] avih (AVIMAINHEADER, 56 bytes)
    wFCC(hdr + 24, "avih");
    wLE32(hdr + 28, 56);
    wLE32(hdr + 32, usPerFrame);      // dwMicroSecPerFrame
    wLE32(hdr + 36, 0);               // dwMaxBytesPerSec
    wLE32(hdr + 40, 0);               // dwPaddingGranularity
    wLE32(hdr + 44, 0x10);            // dwFlags = AVIF_HASINDEX
    wLE32(hdr + 48, totalFrames);     // dwTotalFrames
    wLE32(hdr + 52, 0);               // dwInitialFrames
    wLE32(hdr + 56, 1);               // dwStreams
    wLE32(hdr + 60, sugBuf);          // dwSuggestedBufferSize
    wLE32(hdr + 64, w);               // dwWidth
    wLE32(hdr + 68, h);               // dwHeight
    // dwReserved[4] = 0 (already cleared by memset)

    // [88] LIST strl  (content = 4 + 64 + 48 = 116)
    wFCC(hdr + 88, "LIST");
    wLE32(hdr + 92, 116);
    wFCC(hdr + 96, "strl");

    // [100] strh (AVISTREAMHEADER, 56 bytes)
    wFCC(hdr + 100, "strh");
    wLE32(hdr + 104, 56);
    wFCC(hdr + 108, "vids");          // fccType
    wFCC(hdr + 112, "MJPG");          // fccHandler
    // dwFlags, wPriority, wLanguage, dwInitialFrames = 0
    wLE32(hdr + 128, 1);              // dwScale
    wLE32(hdr + 132, fps);            // dwRate
    // dwStart = 0
    wLE32(hdr + 140, totalFrames);    // dwLength
    wLE32(hdr + 144, sugBuf);         // dwSuggestedBufferSize
    wLE32(hdr + 148, 0xFFFFFFFF);     // dwQuality = −1
    // dwSampleSize = 0, rcFrame.left/top = 0
    wLE16(hdr + 160, w);              // rcFrame.right
    wLE16(hdr + 162, h);              // rcFrame.bottom

    // [164] strf (BITMAPINFOHEADER, 40 bytes)
    wFCC(hdr + 164, "strf");
    wLE32(hdr + 168, 40);
    wLE32(hdr + 172, 40);             // biSize
    wLE32(hdr + 176, w);              // biWidth
    wLE32(hdr + 180, h);              // biHeight
    wLE16(hdr + 184, 1);              // biPlanes
    wLE16(hdr + 186, 24);             // biBitCount
    wFCC(hdr + 188, "MJPG");          // biCompression
    wLE32(hdr + 192, (uint32_t)w * h * 3); // biSizeImage
    // biX/YPelsPerMeter, biClrUsed, biClrImportant = 0

    // [212] LIST movi header
    wFCC(hdr + 212, "LIST");
    wLE32(hdr + 216, moviContentSize); // includes the 4-byte "movi" tag
    wFCC(hdr + 220, "movi");
}

// ---------------------------------------------------------------------------
// Fase 2 (Tarefa 5): H.264-in-AVI header (480 bytes, fixed layout).
//
// Same skeleton as buildAviHeader(), but fccHandler/biCompression are `H264`
// and strf carries the SPS/PPS sets after the 40-byte BITMAPINFOHEADER. The
// extradata area is a FIXED 256-byte reservation (spspps padded with zeros):
// placeholder (start, sets still unknown) and final (finish) headers share
// the size, so '00dc' frame offsets never move. See camera_p4_h264.h.
// ---------------------------------------------------------------------------
static void buildAviHeaderH264(uint8_t* hdr, uint32_t fps, uint32_t totalFrames,
                               uint16_t w, uint16_t h, uint32_t moviContentSize,
                               const uint8_t* spspps, size_t spsppsLen) {
    memset(hdr, 0, CAM_H264_AVI_HDR_BYTES);

    uint32_t usPerFrame = (fps > 0) ? (1000000UL / fps) : 66666UL;
    uint32_t sugBuf     = (uint32_t)w * h * 2;
    uint32_t riffSize   = cameraH264AviRiffSize(moviContentSize);

    // [0] RIFF AVI header
    wFCC(hdr + 0,  "RIFF");
    wLE32(hdr + 4,  riffSize);
    wFCC(hdr + 8,  "AVI ");

    // [12] LIST hdrl (content = 448)
    wFCC(hdr + 12, "LIST");
    wLE32(hdr + 16, CAM_H264_AVI_HDRL_CONTENT);
    wFCC(hdr + 20, "hdrl");

    // [24] avih (AVIMAINHEADER, 56 bytes)
    wFCC(hdr + 24, "avih");
    wLE32(hdr + 28, 56);
    wLE32(hdr + 32, usPerFrame);      // dwMicroSecPerFrame
    wLE32(hdr + 36, 0);               // dwMaxBytesPerSec
    wLE32(hdr + 40, 0);               // dwPaddingGranularity
    wLE32(hdr + 44, 0x10);            // dwFlags = AVIF_HASINDEX
    wLE32(hdr + 48, totalFrames);     // dwTotalFrames
    wLE32(hdr + 52, 0);               // dwInitialFrames
    wLE32(hdr + 56, 1);               // dwStreams
    wLE32(hdr + 60, sugBuf);          // dwSuggestedBufferSize
    wLE32(hdr + 64, w);               // dwWidth
    wLE32(hdr + 68, h);               // dwHeight

    // [88] LIST strl (content = 372)
    wFCC(hdr + 88, "LIST");
    wLE32(hdr + 92, CAM_H264_AVI_STRL_CONTENT);
    wFCC(hdr + 96, "strl");

    // [100] strh (AVISTREAMHEADER, 56 bytes)
    wFCC(hdr + 100, "strh");
    wLE32(hdr + 104, 56);
    wFCC(hdr + 108, "vids");          // fccType
    wFCC(hdr + 112, CAM_H264_AVI_FCC); // fccHandler = H264
    wLE32(hdr + 128, 1);              // dwScale
    wLE32(hdr + 132, fps);            // dwRate
    wLE32(hdr + 140, totalFrames);    // dwLength
    wLE32(hdr + 144, sugBuf);         // dwSuggestedBufferSize
    wLE32(hdr + 148, 0xFFFFFFFF);     // dwQuality = -1
    wLE16(hdr + 160, w);              // rcFrame.right
    wLE16(hdr + 162, h);              // rcFrame.bottom

    // [164] strf (BITMAPINFOHEADER + reserved SPS/PPS area)
    wFCC(hdr + 164, "strf");
    wLE32(hdr + 168, CAM_H264_AVI_STRF_CONTENT);
    wLE32(hdr + 172, 40);             // biSize
    wLE32(hdr + 176, w);              // biWidth
    wLE32(hdr + 180, h);              // biHeight
    wLE16(hdr + 184, 1);              // biPlanes
    wLE16(hdr + 186, 24);             // biBitCount
    wFCC(hdr + 188, CAM_H264_AVI_FCC); // biCompression = H264
    wLE32(hdr + 192, (uint32_t)w * h * 3 / 2); // biSizeImage (12-bit YUV)
    if (spspps && spsppsLen > 0) {
        size_t n = spsppsLen > CAM_H264_SPSPPS_MAX ? CAM_H264_SPSPPS_MAX : spsppsLen;
        memcpy(hdr + CAM_H264_AVI_EXT_OFF, spspps, n);
    }

    // [468] LIST movi header
    wFCC(hdr + CAM_H264_AVI_HDR_BYTES - 12, "LIST");
    wLE32(hdr + CAM_H264_AVI_HDR_BYTES - 8, moviContentSize);
    wFCC(hdr + CAM_H264_AVI_HDR_BYTES - 4, "movi");
}

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
#if ENABLE_SDCARD==1
static File     s_file;
#endif
static String   s_path;
static char     s_tag[8] = {0};  // origin label for the final filename
static uint32_t s_frameCount     = 0;
static uint32_t s_frameChunkBytes= 0;  // sum of (8 + padded_jpeg_size) for each frame
static uint32_t s_startMs        = 0;
static uint16_t s_width          = 0;
static uint16_t s_height         = 0;
static uint8_t  s_fps            = 0;
static uint8_t  s_framesizeVal   = 0;
static bool     s_h264           = false;  // Fase 1: active file is H.264
static uint8_t  s_h264Container  = 0;      // Fase 2: 0=raw `.264`, 1=H.264-in-AVI
static bool     s_h264StartOk    = false;  // first-blob SPS/PPS+IDR scan done
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
static uint8_t  s_h264Ext[CAM_H264_SPSPPS_MAX];  // Fase 2: stashed SPS/PPS for strf
static uint16_t s_h264ExtLen     = 0;
#endif
static bool     s_active         = false;
static bool     s_preNtp         = false;  // open file staged under /CAM/PENDING (clock not ready)
static bool     s_migratedAfterSync = false; // one-pass migration of stale /CAM/PENDING done

// Timelapse / dashcam tick state
static uint32_t s_lastTlMs       = 0;
static bool     s_tlActive       = false;  // this tick owns the AVI for timelapse
static String   s_tlDay;                   // YYYYMMDD of the open TL file
static bool     s_dashOwn        = false;  // this tick owns the AVI for dashcam
static uint32_t s_lastFrameMs    = 0;
static String   s_lastTlPath     = "";  // last timelapse capture filename (empty = none)

#if ENABLE_SDCARD==1
// Write staging buffer (PSRAM preferred) to reduce direct SD writes per frame.
static uint8_t* s_ioBuf          = nullptr;
// PSRAM ring (shock absorber): frames land here when the SD path is slower
// than the capture pump; cameraRecordTick() drains it into the writer queue.
// Sized at record start from free PSRAM (P4 cap 12 MB, S3 cap 2 MB, 256 KB
// floor — below it the ring stays off and the old direct path runs).
static CamRing          s_ring;
static uint8_t*         s_ringBuf      = nullptr;
static bool             s_ringOn       = false;
static SemaphoreHandle_t s_ringMux     = nullptr;
static size_t   s_ioUsed         = 0;
static uint32_t s_ioFlushCount   = 0;
static uint32_t s_ioWriteMsTotal = 0;
static uint32_t s_frameProcMsTot = 0;
static volatile bool s_writerBusy = false;

struct CamWriteItem {
    uint8_t slotIdx;
    bool fromSlot;
    uint8_t* data;
    size_t len;
};

static QueueHandle_t s_writeQueue = nullptr;
static QueueHandle_t s_freeSlotQueue = nullptr;
static TaskHandle_t  s_writerTask = nullptr;
static uint32_t s_dropLogLastMs = 0;
static uint32_t s_dropCountSinceLog = 0;
static uint8_t s_pressureLevel = 0;
static uint32_t s_lastDurabilityFlushMs = 0;
static uint8_t s_flushMode = 0xFF;

struct CamPacketSlot {
    uint8_t* buf;
    size_t cap;
};
static CamPacketSlot s_packetSlots[CAM_WRITE_Q_DEPTH] = {};

// Forward declarations used by writer task.
static bool cameraRecordFlushIoBuffer(bool flushFile);
static bool cameraRecordWriteBuffered(const uint8_t* data, size_t len);

static void cameraRecordLogDropThrottled() {
    s_dropCountSinceLog++;
    uint32_t now = millis();
    if (s_dropLogLastMs == 0 || (now - s_dropLogLastMs) >= 1000UL) {
        char msg[80];
        snprintf(msg, sizeof(msg), "[CAM] Write queue full: dropped=%lu",
                 (unsigned long)s_dropCountSinceLog);
        directLogPrintln(msg);
        s_dropCountSinceLog = 0;
        s_dropLogLastMs = now;
    }
}

static bool cameraRecordEnsurePacketSlotCapacity(uint8_t slotIdx, size_t needLen) {
    if (slotIdx >= CAM_WRITE_Q_DEPTH) return false;
    CamPacketSlot& s = s_packetSlots[slotIdx];
    if (s.buf && s.cap >= needLen) return true;

    uint8_t* next = (uint8_t*)(psramFound() ? ps_malloc(needLen) : malloc(needLen));
    if (!next) return false;
    if (s.buf) free(s.buf);
    s.buf = next;
    s.cap = needLen;
    return true;
}

static bool cameraRecordEnsurePacketPool() {
    if (!s_freeSlotQueue) {
        s_freeSlotQueue = xQueueCreate(CAM_WRITE_Q_DEPTH, sizeof(uint8_t));
        if (!s_freeSlotQueue) {
            directLogPrintln("[CAM] Failed to create free-slot queue");
            return false;
        }
        for (uint8_t i = 0; i < CAM_WRITE_Q_DEPTH; i++) {
            xQueueSend(s_freeSlotQueue, &i, 0);
        }
    }
    return true;
}

static uint8_t cameraRecordBackpressureScale() {
    if (!s_writeQueue || !s_writerTask) return 1;
    UBaseType_t qCount = uxQueueMessagesWaiting(s_writeQueue);

    uint8_t level = 0;
    uint8_t scale = 1;
    if (qCount >= (CAM_WRITE_Q_DEPTH - 1)) {
        level = 3;
        scale = 4;
    } else if (qCount >= (CAM_WRITE_Q_DEPTH - 2)) {
        level = 2;
        scale = 2;
    } else if (qCount >= 1) {
        level = 1;
        scale = 1;
    }

    if (level != s_pressureLevel) {
        char msg[72];
        snprintf(msg, sizeof(msg), "[CAM] Backpressure level=%u queue=%u/%u",
                 (unsigned)level, (unsigned)qCount, (unsigned)CAM_WRITE_Q_DEPTH);
        directLogPrintln(msg);
        s_pressureLevel = level;
    }
    return scale;
}

static const char* cameraRecordFlushModeName(uint8_t mode) {
    switch (mode) {
        case 0: return "normal";
        case 1: return "balanced";
        case 2: return "high";
        default: return "unknown";
    }
}

static void cameraRecordComputeFlushPolicy(uint16_t& flushEvery, uint32_t& minGapMs,
                                           uint8_t& mode, UBaseType_t& qCount,
                                           uint32_t& avgWriteMs) {
    flushEvery = 32;
    minGapMs = 4000UL;
    mode = 0;

    avgWriteMs = s_ioFlushCount ? (s_ioWriteMsTotal / s_ioFlushCount) : 0;
    qCount = s_writeQueue ? uxQueueMessagesWaiting(s_writeQueue) : 0;

    if (qCount >= (CAM_WRITE_Q_DEPTH - 1) || avgWriteMs >= 24) {
        flushEvery = 96;
        minGapMs = 9000UL;
        mode = 2;
    } else if (qCount >= (CAM_WRITE_Q_DEPTH - 2) || avgWriteMs >= 16) {
        flushEvery = 64;
        minGapMs = 6000UL;
        mode = 1;
    }

    if (mode != s_flushMode) {
        char msg[96];
        snprintf(msg, sizeof(msg), "[CAM] Flush mode=%s queue=%u/%u avgWrite=%lums",
                 cameraRecordFlushModeName(mode), (unsigned)qCount,
                 (unsigned)CAM_WRITE_Q_DEPTH, (unsigned long)avgWriteMs);
        directLogPrintln(msg);
        s_flushMode = mode;
    }
}

static uint16_t cameraRecordAdaptiveFlushFrames() {
    uint16_t flushEvery = 32;
    uint32_t minGapMs = 4000UL;
    uint8_t mode = 0;
    UBaseType_t qCount = 0;
    uint32_t avgWriteMs = 0;
    cameraRecordComputeFlushPolicy(flushEvery, minGapMs, mode, qCount, avgWriteMs);
    return flushEvery;
}

static bool cameraRecordShouldDurabilityFlush() {
    uint32_t now = millis();
    uint16_t flushEvery = 32;
    uint32_t minGapMs = 4000UL;
    uint8_t mode = 0;
    UBaseType_t qCount = 0;
    uint32_t avgWriteMs = 0;
    cameraRecordComputeFlushPolicy(flushEvery, minGapMs, mode, qCount, avgWriteMs);
    (void)flushEvery;
    (void)mode;
    (void)avgWriteMs;

    if (qCount >= (CAM_WRITE_Q_DEPTH - 1)) return false;

    if (s_lastDurabilityFlushMs == 0 || (now - s_lastDurabilityFlushMs) >= minGapMs) {
        s_lastDurabilityFlushMs = now;
        return true;
    }
    return false;
}

static void cameraRecordWriterTask(void* /*param*/) {
    CamWriteItem item;
    while (true) {
        if (xQueueReceive(s_writeQueue, &item, portMAX_DELAY) != pdTRUE) continue;
        if (item.len == 0) continue;

        s_writerBusy = true;
        const uint8_t* writePtr = nullptr;
        if (item.fromSlot) {
            if (item.slotIdx < CAM_WRITE_Q_DEPTH) {
                writePtr = s_packetSlots[item.slotIdx].buf;
            }
        } else {
            writePtr = item.data;
        }

        bool ok = (writePtr != nullptr) && cameraRecordWriteBuffered(writePtr, item.len);
        if (!item.fromSlot && item.data) {
            free(item.data);
        }

        if (!ok) {
            directLogPrintln("[CAM] Writer task: buffered write failed");
        } else {
            // Count only frames that actually reached the SD write pipeline.
            s_frameCount++;
            s_frameChunkBytes += (uint32_t)item.len;
        }

        if (item.fromSlot && s_freeSlotQueue) {
            uint8_t idx = item.slotIdx;
            xQueueSend(s_freeSlotQueue, &idx, 0);
        }

        // Adaptive flush: prioritize throughput under SD pressure, keep periodic durability flushes.
        uint16_t flushEvery = cameraRecordAdaptiveFlushFrames();
        if (s_frameCount > 0 && (s_frameCount % flushEvery) == 0) {
            if (!cameraRecordFlushIoBuffer(false)) {
                directLogPrintln("[CAM] Writer task: SD flush failed");
            }
        }
        if (cameraRecordShouldDurabilityFlush()) {
            if (!cameraRecordFlushIoBuffer(true)) {
                directLogPrintln("[CAM] Writer task: durability flush failed");
            }
        }
        s_writerBusy = false;
    }
}

static bool cameraRecordEnsureWriterTask() {
    if (!s_writeQueue) {
        s_writeQueue = xQueueCreate(CAM_WRITE_Q_DEPTH, sizeof(CamWriteItem));
        if (!s_writeQueue) {
            directLogPrintln("[CAM] Failed to create write queue");
            return false;
        }
    }

    if (!cameraRecordEnsurePacketPool()) {
        return false;
    }

    if (!s_writerTask) {
        BaseType_t rc = xTaskCreatePinnedToCore(
            cameraRecordWriterTask,
            "CamRecWr",
            6144,
            nullptr,
            1,
            &s_writerTask,
            0);
        if (rc != pdPASS) {
            s_writerTask = nullptr;
            directLogPrintln("[CAM] Failed to create writer task");
            return false;
        }
    }

    return true;
}

static void cameraRecordDrainWriteQueue(uint32_t maxWaitMs) {
    uint32_t start = millis();
    while ((millis() - start) < maxWaitMs) {
        UBaseType_t qCount = s_writeQueue ? uxQueueMessagesWaiting(s_writeQueue) : 0;
        if (qCount == 0 && !s_writerBusy) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static bool cameraRecordEnsureIoBuffer() {
    if (s_ioBuf) return true;
    s_ioBuf = (uint8_t*)(psramFound() ? ps_malloc(CAM_AVI_IOBUF_SIZE) : malloc(CAM_AVI_IOBUF_SIZE));
    if (!s_ioBuf) {
        directLogPrintln("[CAM] AVI iobuf alloc failed; using direct SD writes");
        return false;
    }
    return true;
}

static bool cameraRecordFlushIoBuffer(bool flushFile) {
    if (!s_file) return false;
    if (!s_ioBuf || s_ioUsed == 0) {
        if (flushFile) s_file.flush();
        return true;
    }

    uint32_t t0 = millis();
    size_t wr = s_file.write(s_ioBuf, s_ioUsed);
    s_ioWriteMsTotal += (millis() - t0);
    if (wr != s_ioUsed) return false;
    s_ioUsed = 0;
    s_ioFlushCount++;

    if (flushFile) s_file.flush();
    return true;
}

static bool cameraRecordWriteBuffered(const uint8_t* data, size_t len) {
    if (!s_file) return false;

    // Fallback: no staging buffer available.
    if (!s_ioBuf) {
        return s_file.write(data, len) == len;
    }

    while (len > 0) {
        size_t space = CAM_AVI_IOBUF_SIZE - s_ioUsed;
        if (space == 0) {
            if (!cameraRecordFlushIoBuffer(false)) return false;
            space = CAM_AVI_IOBUF_SIZE;
        }

        size_t n = (len < space) ? len : space;
        memcpy(s_ioBuf + s_ioUsed, data, n);
        s_ioUsed += n;
        data += n;
        len -= n;
    }
    return true;
}
#endif

// ---------------------------------------------------------------------------
// cameraRecordStart
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// PSRAM ring lifecycle. Sized once per session from free PSRAM so both boards
// share the code: min(boardCap, free/3), 256 KB floor. P4 (32 MB PSRAM) caps
// at 12 MB (~8 min at 800x640 MJPEG, ~30 s of 1080p H.264 @4 Mbps); S3 N16R8
// (8 MB PSRAM) caps at 2 MB (~15-60 s at S3 byte rates).
// ---------------------------------------------------------------------------
static void cameraRecordRingEnd();
static void cameraRecordRingBegin() {
    cameraRecordRingEnd();
    size_t freePs = psramFound() ? (size_t)ESP.getFreePsram() : 0;
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    size_t cap = CAM_RING_CAP_P4;
#else
    size_t cap = CAM_RING_CAP_S3;
#endif
    size_t want = cameraRingPickSize(freePs, cap);
    if (!want) return;  // too little headroom: direct path, old behaviour
    uint8_t* buf = (uint8_t*)ps_malloc(want);
    if (!buf) return;
    if (!s_ringMux) s_ringMux = xSemaphoreCreateMutex();
    if (!s_ringMux) { free(buf); return; }
    cameraRingInit(&s_ring, buf, want);
    s_ringBuf = buf;
    s_ringOn = true;
}

static void cameraRecordRingEnd() {
    if (s_ringMux) {
        xSemaphoreTake(s_ringMux, portMAX_DELAY);
        s_ringOn = false;
        if (s_ringBuf) { free(s_ringBuf); s_ringBuf = nullptr; }
        cameraRingInit(&s_ring, nullptr, 0);
        xSemaphoreGive(s_ringMux);
    } else {
        s_ringOn = false;
        if (s_ringBuf) { free(s_ringBuf); s_ringBuf = nullptr; }
    }
}

// Push one framed record ([len][payload]) into the ring. Thread-safe;
// oldest-first on overflow. False when the ring is off or the frame can
// never fit.
static bool cameraRecordRingPush(const uint8_t* data, size_t len) {
    if (!s_ringOn || !s_ringMux) return false;
    bool ok = false;
    if (xSemaphoreTake(s_ringMux, pdMS_TO_TICKS(5)) == pdTRUE) {
        ok = cameraRingPush(&s_ring, data, len);
        xSemaphoreGive(s_ringMux);
    }
    return ok;
}

// Drain ring head(s) into the writer queue (single writer stays the only SD
// author). Peek + copy + submit, pop only on submit success so a full queue
// keeps the frame for the next tick. Bounded per call — never stalls the loop.
// Returns frames moved.
static uint32_t cameraRecordRingDrain(uint32_t maxFrames) {
    uint32_t moved = 0;
    if (!s_ringOn || !s_ringMux || !s_writeQueue || !s_writerTask) return 0;
    while (moved < maxFrames) {
        uint32_t len = 0;
        if (xSemaphoreTake(s_ringMux, pdMS_TO_TICKS(5)) != pdTRUE) break;
        // Peek length first with a stack probe (payload copied after sizing).
        uint8_t probe[4];
        size_t tail = cameraRingTail(&s_ring);
        bool has = cameraRingPeekLen(&s_ring, tail, &len) && len > 0 &&
                   len <= (4UL * 1024UL * 1024UL);
        xSemaphoreGive(s_ringMux);
        if (!has) break;
        uint8_t* copy = (uint8_t*)ps_malloc(len);
        if (!copy) break;
        if (xSemaphoreTake(s_ringMux, pdMS_TO_TICKS(5)) != pdTRUE) { free(copy); break; }
        size_t got = cameraRingPeek(&s_ring, copy, len);
        xSemaphoreGive(s_ringMux);
        if (got != len) { free(copy); break; }  // raced/consumed: retry later
        CamWriteItem item = { 0, false, copy, (uint32_t)len };
        if (xQueueSend(s_writeQueue, &item, 0) != pdTRUE) {
            free(copy);  // queue still full: keep it ringed for next tick
            break;
        }
        if (xSemaphoreTake(s_ringMux, pdMS_TO_TICKS(5)) != pdTRUE) break;
        cameraRingPop(&s_ring);  // submitted: now drop from ring
        xSemaphoreGive(s_ringMux);
        moved++;
    }
    return moved;
}

// Ring occupancy for the status page (bytes held + lifetime drops).
void cameraRecordRingStats(uint32_t* outUsed, uint32_t* outDropped) {
    uint32_t u = 0, d = 0;
    if (s_ringOn && s_ringMux && xSemaphoreTake(s_ringMux, pdMS_TO_TICKS(5)) == pdTRUE) {
        u = (uint32_t)s_ring.used;
        d = s_ring.dropped;
        xSemaphoreGive(s_ringMux);
    }
    if (outUsed) *outUsed = u;
    if (outDropped) *outDropped = d;
}

bool cameraRecordStartEx(uint8_t fps, uint8_t framesizeVal, uint16_t w, uint16_t h,
                         const char* tag, CameraRecordStartResult* outCause) {
    if (outCause) *outCause = CAM_REC_OK;
    if (s_active) {
        if (outCause) *outCause = CAM_REC_ALREADY_ACTIVE;
        return false;
    }
#if ENABLE_SDCARD==1
    if (!sdcardShouldUseSD()) {
        if (outCause) *outCause = CAM_REC_SD_UNAVAILABLE;
        directLogPrintln("[CAM] SD unavailable — cannot start recording");
        return false;
    }

    s_fps           = fps ? fps : 15;
    s_framesizeVal  = framesizeVal;
    s_width         = w;
    s_height        = h;
    s_frameCount    = 0;
    s_frameChunkBytes = 0;
    s_ioUsed        = 0;
    s_ioFlushCount  = 0;
    s_ioWriteMsTotal = 0;
    s_frameProcMsTot = 0;
    s_pressureLevel = 0;
    s_lastDurabilityFlushMs = 0;
    s_flushMode = 0xFF;

    // Build folder: pre-NTP the date is meaningless → stage under /CAM/PENDING
    // (never the bogus 1970-01-01 folder); synced → /CAM/YYYY-MM-DD/. Finished
    // staged files migrate into the correct dated folder once the clock syncs.
    time_t now = time(nullptr);
    s_preNtp = !cameraClockReady(now);
    char dayStr[16];
    cameraDayTimeParts(now, dayStr, nullptr);
    char readyFolder[40];
    snprintf(readyFolder, 40, "%s/%s", CAM_REC_ROOT, dayStr);
    char folder[40];
    cameraRecordPickFolder(now, readyFolder, folder);
    // mkdir return values are checked, but a false return is only treated as a
    // failure when the folder still does not exist: some SD/FS layers report a
    // failed mkdir for a folder that was actually created (seen on P4 .18).
    if (!SD_MMC.exists(CAM_REC_ROOT)) {
        SD_MMC.mkdir(CAM_REC_ROOT);
        if (!SD_MMC.exists(CAM_REC_ROOT)) {
            if (outCause) *outCause = CAM_REC_MKDIR_ROOT;
            char m[96]; snprintf(m, sizeof(m), "[CAM] Record start failed (mkdir %s)", CAM_REC_ROOT);
            directLogPrintln(m);
            return false;
        }
    }
    if (!SD_MMC.exists(folder)) {
        SD_MMC.mkdir(folder);
        if (!SD_MMC.exists(folder)) {
            if (outCause) *outCause = CAM_REC_MKDIR_FOLDER;
            char m[128]; snprintf(m, sizeof(m), "[CAM] Record start failed (mkdir %s)", folder);
            directLogPrintln(m);
            return false;
        }
    }

    // Filename base: pre-NTP "NOSYNC_<uptimeSec>" (no fake date inside the
    // name either); synced "YYYY-MM-DD_HHMMSS". Duration + tag appended on
    // finish (YYYY-MM-DD_HHMMSS_RES_FPS_<DUR>s_<TAG>.avi, MJPEG2SD-style).
    char base[40];
    cameraRecordBaseName(now, (uint32_t)(millis() / 1000UL), base);
    s_path = String(folder) + "/" + base + "_"
             + cameraRecordResLabel(w, h) + "_"
             + String(s_fps) + ".avi";
    if (tag && tag[0]) {
        snprintf(s_tag, sizeof(s_tag), "%s", tag);
    } else {
        s_tag[0] = 0;
    }

    s_file = SD_MMC.open(s_path.c_str(), FILE_WRITE);
    if (!s_file) {
        if (outCause) *outCause = CAM_REC_OPEN_FAILED;
        char m[128]; snprintf(m, sizeof(m), "[CAM] Cannot create AVI file on SD: %s", s_path.c_str());
        directLogPrintln(m);
        return false;
    }

    cameraRecordEnsureIoBuffer();
    cameraRecordEnsureWriterTask();

    // Write 224-byte placeholder header (all counts = 0, moviContent = 4).
    uint8_t hdr[HEADER_TOTAL];
    buildAviHeader(hdr, s_fps, 0, s_width, s_height, 4);
    if (!cameraRecordWriteBuffered(hdr, HEADER_TOTAL) || !cameraRecordFlushIoBuffer(true)) {
        s_file.close();
        if (outCause) *outCause = CAM_REC_HEADER_FAILED;
        directLogPrintln("[CAM] Failed writing AVI header");
        return false;
    }

    cameraRecordRingBegin();  // PSRAM shock absorber (no-op when too little free)
    s_active  = true;
    s_startMs = millis();
    s_lastFrameMs = s_startMs;
    directLogPrintln("[CAM] Recording started");
    return true;
#else
    if (outCause) *outCause = CAM_REC_SD_UNAVAILABLE;
    return false;
#endif
}

bool cameraRecordStart(uint8_t fps, uint8_t framesizeVal, uint16_t w, uint16_t h, const char* tag) {
    return cameraRecordStartEx(fps, framesizeVal, w, h, tag, nullptr);
}

// ---------------------------------------------------------------------------
// Fase 1 (Tarefa 5): H.264 recording (CAM_P4 / P4 MIPI only).
// Fase 2: container 0=raw Annex B `.264`, 1=H.264-in-AVI (`.avi`, `H264`
// fccHandler/biCompression + SPS/PPS extradata in strf, 480-byte fixed
// header so frame offsets never move). Raw stays the fallback default.
// ---------------------------------------------------------------------------
bool cameraRecordStartH264(uint8_t fps, uint8_t framesizeVal, uint16_t w, uint16_t h,
                           uint32_t bitrateKbps, uint8_t container, const char* tag,
                           CameraRecordStartResult* outCause) {
    if (outCause) *outCause = CAM_REC_OK;
    if (s_active) {
        if (outCause) *outCause = CAM_REC_ALREADY_ACTIVE;
        return false;
    }
#if ENABLE_SDCARD==1
    if (!sdcardShouldUseSD()) {
        if (outCause) *outCause = CAM_REC_SD_UNAVAILABLE;
        directLogPrintln("[CAM] SD unavailable — cannot start H.264 recording");
        return false;
    }
#if !defined(TARGET_BOARD_P4) || TARGET_BOARD_P4 != 1
    if (outCause) *outCause = CAM_REC_H264_UNAVAILABLE;
    directLogPrintln("[CAM] H.264 needs the P4 MIPI source");
    return false;
#else
    if (!p4H264HwAvailable()) {
        if (outCause) *outCause = CAM_REC_H264_UNAVAILABLE;
        directLogPrintln("[CAM] H.264 encoder unavailable in this core");
        return false;
    }
    if (!cameraH264ResSupported(w, h)) {
        if (outCause) *outCause = CAM_REC_H264_ENCODER_FAIL;
        directLogPrintln("[CAM] H.264 res over HW limit (1920 wide max)");
        return false;
    }

    s_fps           = fps ? fps : 12;
    s_framesizeVal  = framesizeVal;
    s_width         = w;
    s_height        = h;
    s_frameCount    = 0;
    s_frameChunkBytes = 0;
    s_ioUsed        = 0;
    s_ioFlushCount  = 0;
    s_ioWriteMsTotal = 0;
    s_frameProcMsTot = 0;
    s_pressureLevel = 0;
    s_lastDurabilityFlushMs = 0;
    s_flushMode = 0xFF;
    s_h264 = false;
    s_h264Container = cameraH264ContainerClamp((int)container);
    s_h264StartOk = false;
    s_h264ExtLen = 0;

    time_t now = time(nullptr);
    s_preNtp = !cameraClockReady(now);
    char dayStr[16];
    cameraDayTimeParts(now, dayStr, nullptr);
    char readyFolder[40];
    snprintf(readyFolder, 40, "%s/%s", CAM_REC_ROOT, dayStr);
    char folder[40];
    cameraRecordPickFolder(now, readyFolder, folder);
    if (!SD_MMC.exists(CAM_REC_ROOT)) {
        SD_MMC.mkdir(CAM_REC_ROOT);
        if (!SD_MMC.exists(CAM_REC_ROOT)) {
            if (outCause) *outCause = CAM_REC_MKDIR_ROOT;
            return false;
        }
    }
    if (!SD_MMC.exists(folder)) {
        SD_MMC.mkdir(folder);
        if (!SD_MMC.exists(folder)) {
            if (outCause) *outCause = CAM_REC_MKDIR_FOLDER;
            return false;
        }
    }

    char base[40];
    cameraRecordBaseName(now, (uint32_t)(millis() / 1000UL), base);
    char fname[64];
    const char* resLabel = cameraRecordResLabel(w, h);
    if (s_h264Container == CAM_H264_CONT_AVI) {
        snprintf(fname, sizeof(fname), "%s_%s_%u.avi", base,
                 resLabel, (unsigned)s_fps);
    } else {
        cameraH264FileName(base, resLabel, s_fps, fname);
    }
    s_path = String(folder) + "/" + fname;
    if (tag && tag[0]) {
        snprintf(s_tag, sizeof(s_tag), "%s", tag);
    } else {
        s_tag[0] = 0;
    }

    s_file = SD_MMC.open(s_path.c_str(), FILE_WRITE);
    if (!s_file) {
        if (outCause) *outCause = CAM_REC_OPEN_FAILED;
        return false;
    }

    cameraRecordEnsureIoBuffer();
    // AVI container: 480-byte placeholder header (SPS/PPS still unknown —
    // zero-padded reservation; rewritten with the stashed sets on finish).
    // Raw container: no header, NALs stream verbatim (Fase 1 behaviour).
    if (s_h264Container == CAM_H264_CONT_AVI) {
        cameraRecordEnsureWriterTask();
        uint8_t hdr[CAM_H264_AVI_HDR_BYTES];
        buildAviHeaderH264(hdr, s_fps, 0, s_width, s_height, 4, nullptr, 0);
        if (!cameraRecordWriteBuffered(hdr, CAM_H264_AVI_HDR_BYTES) ||
            !cameraRecordFlushIoBuffer(true)) {
            s_file.close();
            SD_MMC.remove(s_path.c_str());
            s_path = "";
            if (outCause) *outCause = CAM_REC_HEADER_FAILED;
            directLogPrintln("[CAM] Failed writing H264 AVI header");
            return false;
        }
    }

    if (!p4H264Begin(w, h, s_fps, bitrateKbps)) {
        s_file.close();
        SD_MMC.remove(s_path.c_str());
        s_path = "";
        if (outCause) *outCause = CAM_REC_H264_ENCODER_FAIL;
        directLogPrintln("[CAM] H.264 encoder open failed");
        return false;
    }

    cameraRecordRingBegin();  // PSRAM shock absorber (no-op when too little free)
    s_h264   = true;
    s_active = true;
    s_startMs = millis();
    s_lastFrameMs = s_startMs;
    directLogPrintln(s_h264Container == CAM_H264_CONT_AVI
                     ? "[CAM] H.264 recording started (AVI)"
                     : "[CAM] H.264 recording started (.264)");
    return true;
#endif
#else
    if (outCause) *outCause = CAM_REC_SD_UNAVAILABLE;
    return false;
#endif
}

// ---------------------------------------------------------------------------
// cameraRecordFrame
// ---------------------------------------------------------------------------
void cameraRecordFrame(const uint8_t* jpegData, size_t jpegLen) {
#if ENABLE_SDCARD==1
    if (!s_active || !s_file) return;
    uint32_t tFrame = millis();
    size_t paddedLen = (jpegLen + 1) & ~1UL;
    size_t totalLen = FRAME_HDR_BYTES + paddedLen;

    bool queued = false;
    uint8_t slotIdx = 0xFF;
    if (s_writeQueue && s_writerTask) {
        if (s_freeSlotQueue &&
            xQueueReceive(s_freeSlotQueue, &slotIdx, pdMS_TO_TICKS(CAM_WRITE_Q_SEND_TIMEOUT_MS)) == pdTRUE) {
            if (cameraRecordEnsurePacketSlotCapacity(slotIdx, totalLen)) {
                uint8_t* packet = s_packetSlots[slotIdx].buf;
                wFCC(packet, "00dc");
                wLE32(packet + 4, (uint32_t)jpegLen);
                memcpy(packet + FRAME_HDR_BYTES, jpegData, jpegLen);
                if (paddedLen != jpegLen) packet[FRAME_HDR_BYTES + jpegLen] = 0;

                CamWriteItem item = { slotIdx, true, nullptr, totalLen };
                queued = (xQueueSend(s_writeQueue, &item,
                                     pdMS_TO_TICKS(CAM_WRITE_Q_SEND_TIMEOUT_MS)) == pdTRUE);
            }

            if (!queued && s_freeSlotQueue) {
                xQueueSend(s_freeSlotQueue, &slotIdx, 0);
            }
        }
    }

    if (!queued) {
        // If writer path is unavailable, use synchronous fallback.
        if (!s_writeQueue || !s_writerTask) {
            uint8_t* packet = (uint8_t*)(psramFound() ? ps_malloc(totalLen) : malloc(totalLen));
            if (!packet) {
                directLogPrintln("[CAM] Frame packet alloc failed");
                return;
            }
            wFCC(packet, "00dc");
            wLE32(packet + 4, (uint32_t)jpegLen);
            memcpy(packet + FRAME_HDR_BYTES, jpegData, jpegLen);
            if (paddedLen != jpegLen) packet[FRAME_HDR_BYTES + jpegLen] = 0;

            if (!cameraRecordWriteBuffered(packet, totalLen)) {
                free(packet);
                directLogPrintln("[CAM] Sync fallback write failed");
                return;
            }
            free(packet);

            s_frameCount++;
            s_frameChunkBytes += (uint32_t)totalLen;
            uint16_t flushEvery = cameraRecordAdaptiveFlushFrames();
            if ((s_frameCount % flushEvery) == 0) {
                if (!cameraRecordFlushIoBuffer(false)) {
                    directLogPrintln("[CAM] Sync fallback flush failed");
                }
            }
            if (cameraRecordShouldDurabilityFlush()) {
                if (!cameraRecordFlushIoBuffer(true)) {
                    directLogPrintln("[CAM] Sync fallback durability flush failed");
                }
            }
        } else {
            // Queue full (slow/dying SD): spill the framed chunk to the PSRAM
            // ring instead of dropping it — the tick drain feeds it back when
            // the queue has room. Newest data still wins if the ring fills.
            uint8_t* packet = (uint8_t*)(psramFound() ? ps_malloc(totalLen) : malloc(totalLen));
            if (packet) {
                wFCC(packet, "00dc");
                wLE32(packet + 4, (uint32_t)jpegLen);
                memcpy(packet + FRAME_HDR_BYTES, jpegData, jpegLen);
                if (paddedLen != jpegLen) packet[FRAME_HDR_BYTES + jpegLen] = 0;
                if (cameraRecordRingPush(packet, totalLen)) {
                    free(packet);
                    s_frameProcMsTot += (millis() - tFrame);
                    return;
                }
                free(packet);
            }
            // Ring off or full beyond recovery: drop newest frame.
            cameraRecordLogDropThrottled();
            return;
        }
    }

    s_frameProcMsTot += (millis() - tFrame);
#else
    (void)jpegData;
    (void)jpegLen;
#endif
}

// ---------------------------------------------------------------------------
// Fase 1 (Tarefa 5): append Annex B NALs to the active H.264 recording.
// Fase 2: the AVI container wraps each blob in a `00dc` chunk (WORD-padded,
// via the shared JPEG-chunk path — it is payload-agnostic); raw keeps the
// verbatim sync-buffered path. Either way the SPS/PPS sets are stashed from
// the first blob that carries them (strf extradata on AVI finish).
// ---------------------------------------------------------------------------
void cameraRecordFrameH264(const uint8_t* nalData, size_t nalLen) {
#if ENABLE_SDCARD==1
    if (!s_active || !s_h264 || !s_file) return;
    if (!nalData || nalLen == 0) return;
    uint32_t tFrame = millis();
    // First-blob sanity: the HW encoder must open with SPS/PPS+IDR (it
    // prepends parameter sets to every IDR automatically). Warn once —
    // never fail the session — so a bad blob is diagnosable on Serial.
    if (!s_h264StartOk) {
        s_h264StartOk = true;
        if (!cameraH264HasStreamStart(nalData, nalLen)) {
            directLogPrintln("[CAM] H.264 first blob has no SPS/PPS+IDR");
        }
    }
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    if (s_h264ExtLen == 0) {
        size_t n = cameraH264ExtractSpsPps(nalData, nalLen, s_h264Ext, sizeof(s_h264Ext));
        if (n > 0) s_h264ExtLen = (uint16_t)n;
    }
#endif
    // AVI container: shared `00dc` chunk path (counts + flush included).
    if (s_h264Container == CAM_H264_CONT_AVI) {
        cameraRecordFrame(nalData, nalLen);
        s_frameProcMsTot += (millis() - tFrame);
        return;
    }
    // Ring path (no AVI chunk queue — NALs go verbatim). When the ring is
    // on, capture never waits on the card: the tick drain writes through the
    // same buffered pipeline. Falls back to the sync path with the ring off.
    if (s_ringOn) {
        if (cameraRecordRingPush(nalData, nalLen)) {
            s_frameProcMsTot += (millis() - tFrame);
            return;
        }
        // Ring push failed (single blob bigger than the ring): sync fallback.
    }
    if (!cameraRecordWriteBuffered(nalData, nalLen)) {
        directLogPrintln("[CAM] H.264 write failed");
        return;
    }
    s_frameCount++;
    s_frameChunkBytes += (uint32_t)nalLen;
    uint16_t flushEvery = cameraRecordAdaptiveFlushFrames();
    if ((s_frameCount % flushEvery) == 0) {
        if (!cameraRecordFlushIoBuffer(false)) {
            directLogPrintln("[CAM] H.264 flush failed");
        }
    }
    if (cameraRecordShouldDurabilityFlush()) {
        if (!cameraRecordFlushIoBuffer(true)) {
            directLogPrintln("[CAM] H.264 durability flush failed");
        }
    }
    s_frameProcMsTot += (millis() - tFrame);
#else
    (void)nalData;
    (void)nalLen;
#endif
}

// ---------------------------------------------------------------------------
// cameraRecordFinish
// ---------------------------------------------------------------------------
bool cameraRecordFinish() {
#if ENABLE_SDCARD==1
    if (!s_active) return false;
    s_active = false;

    if (!s_file) return false;

    // Drain the PSRAM ring into the writer queue (bounded — a dead card must
    // not wedge the stop handler), then wait for queued packets as before.
    {
        uint32_t t0 = millis();
        while (s_ringOn && (millis() - t0) < 4000) {
            if (cameraRecordRingDrain(8) == 0) break;
        }
    }
    cameraRecordDrainWriteQueue(3000);
    cameraRecordRingEnd();

    if (!cameraRecordFlushIoBuffer(true)) {
        directLogPrintln("[CAM] Final SD flush failed");
    }

    // Fase 1 (Tarefa 5): `.264` has no RIFF header to rewrite — the NALs are
    // already verbatim on SD. Empty sessions leave no file behind.
    // Fase 2: H.264-in-AVI shares the AVI tail below (480-byte header with
    // the stashed SPS/PPS); only raw `.264` returns early here.
    if (s_h264 && s_h264Container == CAM_H264_CONT_RAW) {
        size_t finalSize = s_file.size();
        s_file.close();
        storNoteSdWrite(finalSize);
        p4H264End();
        s_h264 = false;
        if (s_frameCount == 0) {
            SD_MMC.remove(s_path.c_str());
            s_path = "";
            s_preNtp = false;
            directLogPrintln("[CAM] H.264 session empty — file removed");
            return false;
        }
        uint32_t durSecs = (millis() - s_startMs) / 1000UL;
        if (s_path.endsWith(".264")) {
            String base = s_path.substring(0, s_path.length() - 4);
            String finalPath = base + "_" + String(durSecs) + "s";
            if (s_tag[0]) finalPath += String("_") + s_tag;
            finalPath += ".264";
            if (SD_MMC.rename(s_path.c_str(), finalPath.c_str())) {
                s_path = finalPath;
            }
        }
    } else {
    // moviContentSize = 4("movi" tag) + all frame chunk bytes
    uint32_t moviContent = 4UL + s_frameChunkBytes;

    // Seek to start and overwrite header with correct values. H.264-in-AVI
    // (Fase 2) rewrites the 480-byte header with the SPS/PPS stashed from
    // the first IDR (zero-padded when the session never produced sets —
    // the encoder opens with IDR, so this is only a broken-pipe fallback).
    s_file.seek(0);
    if (s_h264) {
        uint8_t hdr[CAM_H264_AVI_HDR_BYTES];
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
        buildAviHeaderH264(hdr, s_fps, s_frameCount, s_width, s_height,
                           moviContent, s_h264ExtLen ? s_h264Ext : nullptr,
                           s_h264ExtLen);
#else
        buildAviHeaderH264(hdr, s_fps, s_frameCount, s_width, s_height,
                           moviContent, nullptr, 0);
#endif
        s_file.write(hdr, CAM_H264_AVI_HDR_BYTES);
    } else {
        uint8_t hdr[HEADER_TOTAL];
        buildAviHeader(hdr, s_fps, s_frameCount, s_width, s_height, moviContent);
        s_file.write(hdr, HEADER_TOTAL);
    }
    s_file.flush();
    size_t finalSize = s_file.size();
    s_file.close();
    storNoteSdWrite(finalSize);
    if (s_h264) { p4H264End(); s_h264 = false; }

    // Final rename: append duration + origin tag
    // (YYYYMMDD_HHMMSS_RES_FPS_<DUR>s_<TAG>.avi). Keeps the folder readable
    // without opening files; playback parses fps from the same position.
    uint32_t durSecs = (millis() - s_startMs) / 1000UL;
    if (s_path.endsWith(".avi")) {
        String base = s_path.substring(0, s_path.length() - 4);
        String finalPath = base + "_" + String(durSecs) + "s";
        if (s_tag[0]) finalPath += String("_") + s_tag;
        finalPath += ".avi";
        if (SD_MMC.rename(s_path.c_str(), finalPath.c_str())) {
            s_path = finalPath;
        }
    }
    }  // end AVI branch

    // Pre-NTP file staged under /CAM/PENDING: now that the clock is ready,
    // move it into today's dated folder so everything lives in the system
    // folder with the correct date. Cross-directory rename is fine on FAT
    // (same mechanism as RS232's fixLogFilenameAfterNtp).
    if (s_preNtp && s_path.startsWith(String(CAM_REC_STAGE_FOLDER) + "/")) {
        time_t nowT = time(nullptr);
        if (cameraClockReady(nowT)) {
            char dayStr[16];
            cameraDayTimeParts(nowT, dayStr, nullptr);
            String destFolder = String(CAM_REC_ROOT) + "/" + dayStr;
            if (!SD_MMC.exists(destFolder.c_str())) SD_MMC.mkdir(destFolder.c_str());
            int sl = s_path.lastIndexOf('/');
            String name = sl >= 0 ? s_path.substring(sl + 1) : s_path;
            char prefix[40];
            cameraRecordMigratedPrefix(nowT, prefix);
            String dest = destFolder + "/" + prefix + name;
            if (!SD_MMC.exists(dest.c_str()) && SD_MMC.rename(s_path.c_str(), dest.c_str())) {
                s_path = dest;
                directLogPrintln("[CAM] Migrated pre-NTP file to dated folder");
            }
        }
    }
    s_preNtp = false;

    char msg[128];
    snprintf(msg, sizeof(msg), "[CAM] Saved %u frames → %s",
             (unsigned)s_frameCount, s_path.c_str());
    directLogPrintln(msg);
    if (s_frameCount > 0) {
        char perf[120];
        snprintf(perf, sizeof(perf),
                 "[CAM] I/O flushes=%lu avgWrite=%lums avgFrameProc=%lums",
                 (unsigned long)s_ioFlushCount,
                 (unsigned long)(s_ioFlushCount ? (s_ioWriteMsTotal / s_ioFlushCount) : 0),
                 (unsigned long)(s_frameProcMsTot / s_frameCount));
        directLogPrintln(perf);
    }
    return true;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// State queries
// ---------------------------------------------------------------------------
bool          cameraRecordActive()      { return s_active; }
bool          cameraRecordIsH264()      { return s_active && s_h264; }
uint8_t       cameraRecordContainer()   { return (s_active && s_h264) ? s_h264Container : 0; }
const String& cameraRecordPath()        { return s_path; }
uint32_t      cameraRecordElapsedSecs() { return s_active ? (millis() - s_startMs) / 1000UL : 0; }
uint32_t      cameraRecordFrameCount()  { return s_frameCount; }

// ---------------------------------------------------------------------------
// Timelapse: AVI rotation lives in cameraRecordTick() (10fps _T AVI, day-rotated);
// the legacy single-JPEG cameraTimelapseSave was removed (no callers).
// ---------------------------------------------------------------------------
bool cameraTimelapseActive() {
    return cameraGetConfig().timelapse_secs > 0;
}

uint32_t cameraTimelapseNextInSecs() {
    const CameraConfig& cfg = cameraGetConfig();
    if (cfg.timelapse_secs == 0 || s_lastTlMs == 0) return 0;
    uint32_t intervalMs = (uint32_t)cfg.timelapse_secs * 1000UL;
    uint32_t nowMs = millis();
    uint32_t nextMs = s_lastTlMs + intervalMs;
    if (nextMs <= nowMs) return 0;  // due now (loop tick will capture shortly)
    return (nextMs - nowMs + 999) / 1000;  // ceil to whole seconds
}

const String& cameraTimelapseLastPath() {
    return s_lastTlPath;
}

// ---------------------------------------------------------------------------
// cameraMotionSnapSave — single JPEG to /CAM/MOTION/ (motion snap action)
// ---------------------------------------------------------------------------
bool cameraMotionSnapSave(const uint8_t* jpegData, size_t jpegLen, String* outPath) {
#if ENABLE_SDCARD==1
    if (!sdcardShouldUseSD()) return false;
    if (!SD_MMC.exists(CAM_REC_ROOT))   SD_MMC.mkdir(CAM_REC_ROOT);

    time_t now = time(nullptr);
    char base[40];
    cameraRecordBaseName(now, (uint32_t)(millis() / 1000UL), base);
    char folder[40];
    cameraRecordPickFolder(now, CAM_REC_ROOT "/MOTION", folder);
    if (!SD_MMC.exists(folder))         SD_MMC.mkdir(folder);
    String path = String(folder) + "/" + base + ".jpg";

    File f = SD_MMC.open(path.c_str(), FILE_WRITE);
    if (!f) return false;
    f.write(jpegData, jpegLen);
    f.close();
    storNoteSdWrite(jpegLen);
    if (outPath) *outPath = path;
    directLogPrintln("[CAM] Motion snap saved");
    return true;
#else
    (void)jpegData;
    (void)jpegLen;
    (void)outPath;
    return false;
#endif
}

// ---------------------------------------------------------------------------
// cameraRecordMigratePending — one-pass move of stale /CAM/PENDING files into
// today's dated folder once the clock is ready. Covers files finished while
// still pre-NTP (snapshots, AVI sessions closed before sync, or leftovers from
// a previous power cycle). The ACTIVE file is skipped here — cameraRecordFinish()
// migrates it when its session ends. Runs at most once per boot.
// ---------------------------------------------------------------------------
static void cameraRecordMigratePending() {
    if (!sdcardShouldUseSD()) return;
    time_t now = time(nullptr);
    if (!cameraClockReady(now) || !SD_MMC.exists(CAM_REC_STAGE_FOLDER)) {
        s_migratedAfterSync = true;
        return;
    }
    char dayStr[16];
    cameraDayTimeParts(now, dayStr, nullptr);
    String destFolder = String(CAM_REC_ROOT) + "/" + dayStr;
    if (!SD_MMC.exists(CAM_REC_ROOT)) SD_MMC.mkdir(CAM_REC_ROOT);
    if (!SD_MMC.exists(destFolder.c_str())) SD_MMC.mkdir(destFolder.c_str());
    char prefix[40];
    cameraRecordMigratedPrefix(now, prefix);

    bool anyFailed = false;
    bool anyMoved = false;
    if (sdMutexLock(pdMS_TO_TICKS(2000))) {
        File d = SD_MMC.open(CAM_REC_STAGE_FOLDER);
        if (d && d.isDirectory()) {
            File f = d.openNextFile();
            while (f) {
                String full = String(f.name());
                int sl = full.lastIndexOf('/');
                String name = sl >= 0 ? full.substring(sl + 1) : full;
                if (!f.isDirectory() && !(s_active && full == s_path)) {
                    String dest = destFolder + "/" + prefix + name;
                    if (!SD_MMC.exists(dest.c_str()) && SD_MMC.rename(full.c_str(), dest.c_str())) {
                        char msg[160];
                        snprintf(msg, sizeof(msg), "[CAM] Migrated stage file %s → folder %s", name.c_str(), dayStr);
                        directLogPrintln(msg);
                        anyMoved = true;
                    } else {
                        anyFailed = true;
                    }
                }
                f.close();
                f = d.openNextFile();
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
        if (d) d.close();
        sdMutexUnlock();
    }
    // Drop the (now empty) stage folder; ok if it fails (not fatal).
    if (anyMoved) SD_MMC.rmdir(CAM_REC_STAGE_FOLDER);
    if (!anyFailed) s_migratedAfterSync = true;
}

// ---------------------------------------------------------------------------
// cameraRecordTick — called from cameraLoop()
// Drives dashcam auto-start/restart and periodic timelapse capture.
// ---------------------------------------------------------------------------
// ---- Photo fast path ------------------------------------------------------
// A snapshot POST used to block on the SD lock (up to 2 s) + the write before
// answering. Now the capture is stashed in one of two PSRAM slots, the
// handler answers immediately, and the tick saver below writes it out. When
// both slots are busy the handler falls back to the old synchronous write
// (slow but never loses the photo). Paths are reserved at capture time so
// the immediate answer names the real file.
#if ENABLE_SDCARD==1
struct PhotoSlot { uint8_t* buf = nullptr; size_t len = 0; String path; bool pending = false; };
static PhotoSlot s_photoSlots[2];

// Hand ownership of an owned JPEG copy to the background saver. False = both
// slots busy (caller keeps the buffer and writes it synchronously).
bool cameraPhotoSubmit(uint8_t* img, size_t len, const String& path) {
    if (!img || !len || !path.length()) return false;
    for (uint8_t i = 0; i < 2; i++) {
        if (!s_photoSlots[i].pending) {
            s_photoSlots[i].buf = img;
            s_photoSlots[i].len = len;
            s_photoSlots[i].path = path;
            s_photoSlots[i].pending = true;
            return true;
        }
    }
    return false;
}

// Last-photo RAM copy for `/api/camera/last-photo` (viewable even when the
// SD save is still queued or the card is dead). Capped: bigger frames skip
// the RAM copy but still save to SD normally.
#define PHOTO_RAM_MAX (512UL * 1024UL)
static uint8_t* s_lastPhoto = nullptr;
static size_t   s_lastPhotoLen = 0;

void cameraPhotoRemember(const uint8_t* img, size_t len) {
    if (!img || !len || len > PHOTO_RAM_MAX) return;
    uint8_t* nb = (uint8_t*)(psramFound() ? ps_malloc(len) : malloc(len));
    if (!nb) return;
    memcpy(nb, img, len);
    if (s_lastPhoto) free(s_lastPhoto);
    s_lastPhoto = nb;
    s_lastPhotoLen = len;
}

bool cameraPhotoLastJson(uint8_t const** outData, size_t* outLen) {
    if (!s_lastPhoto || !s_lastPhotoLen || !outData || !outLen) return false;
    *outData = s_lastPhoto;
    *outLen = s_lastPhotoLen;
    return true;
}

uint8_t cameraPhotoPending() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < 2; i++) n += s_photoSlots[i].pending ? 1 : 0;
    return n;
}

// Background saver: one SD write attempt per tick max, short lock (never
// stalls the loop). Failures stay queued and retry; the card catching up
// drains them in order.
static void cameraPhotoDrain() {
    extern bool acquireSDAccess(const char* owner, uint32_t timeoutMs);
    extern void releaseSDAccess();
    for (uint8_t i = 0; i < 2; i++) {
        if (!s_photoSlots[i].pending) continue;
        if (!sdcardShouldUseSD()) return;
        if (!acquireSDAccess("camera-photo-bg", 200)) return;  // busy: next tick
        bool ok = false;
        String dir = String(CAM_REC_ROOT) + "/PHOTOS";
        if (!SD_MMC.exists(CAM_REC_ROOT)) SD_MMC.mkdir(CAM_REC_ROOT);
        if (!SD_MMC.exists(dir.c_str())) SD_MMC.mkdir(dir.c_str());
        File f = SD_MMC.open(s_photoSlots[i].path.c_str(), FILE_WRITE);
        if (f) {
            ok = (f.write(s_photoSlots[i].buf, s_photoSlots[i].len) == s_photoSlots[i].len);
            f.close();
            if (ok) storNoteSdWrite(s_photoSlots[i].len);
        }
        releaseSDAccess();
        if (!ok) return;  // card down: keep queued, retry later
        free(s_photoSlots[i].buf);
        s_photoSlots[i].buf = nullptr;
        s_photoSlots[i].len = 0;
        s_photoSlots[i].pending = false;
        return;  // one save per tick
    }
}
#endif // ENABLE_SDCARD==1

void cameraRecordTick(uint32_t nowMs) {
    const CameraConfig& cfg = cameraGetConfig();
#if ENABLE_SDCARD==1
    cameraPhotoDrain();
#endif

    // Keep the card fed from the ring whenever a session owns the file:
    // 8 frames per loop is plenty (the writer task, not the loop, does the
    // waiting), and a full queue simply defers to the next tick.
    if (s_active && s_file) cameraRecordRingDrain(8);

    // Once the clock is ready, migrate any pre-NTP staged files (from this
    // boot or a previous one) into the correct dated folder — single pass.
    if (!s_migratedAfterSync && cameraClockReady(time(nullptr))) {
        cameraRecordMigratePending();
    }

    // ---- Timelapse (AVI, _T suffix) ----
    // One frame per interval appended to a 10fps AVI: smooth timelapse video
    // instead of loose JPEGs. Rotates on day change; yields to any manual /
    // motion / dashcam session already running.
    if (cfg.timelapse_secs > 0) {
        uint32_t intervalMs = (uint32_t)cfg.timelapse_secs * 1000UL;
        if (s_lastTlMs == 0 || (nowMs - s_lastTlMs) >= intervalMs) {
            s_lastTlMs = nowMs;
            time_t nowT = time(nullptr);
            struct tm ti;
            localtime_r(&nowT, &ti);
            char dayStr[16];
            strftime(dayStr, sizeof(dayStr), "%Y-%m-%d", &ti);
            // Day rotation: close yesterday's TL file so the new one lands
            // in today's folder with today's date.
            if (s_tlActive && s_active && s_tlDay != String(dayStr)) {
                cameraRecordFinish();
                s_tlActive = false;
            }
            if (!s_active) {
                uint16_t w, h;
                cameraGetOutputDimensions(w, h);
                if (cameraRecordStart(10, (uint8_t)cfg.framesize, w, h, "TL")) {
                    s_tlActive = true;
                    s_tlDay = String(dayStr);
                    s_lastTlPath = s_path;
                }
            }
            if (s_tlActive && s_active) {
                // Single pump: cache when live, direct when idle (never in
                // a motion grayscale window).
                if (cameraStreamIsActive()) {
                    size_t len = 0;
                    const uint8_t* src = cameraStreamFrame(len);
                    if (src && len > 0) cameraRecordFrame(src, len);
                } else if (!cameraMotionSamplingActive()) {
                    camera_fb_t* fb = cameraCaptureFrame();
                    if (fb) {
                        cameraRecordFrame(fb->buf, fb->len);
                        esp_camera_fb_return(fb);
                    }
                }
                s_lastTlPath = s_path;
            }
            // else: another session owns the recorder — skip this interval.
        }
    } else {
        s_lastTlMs = 0;
        if (s_tlActive && s_active) {
            cameraRecordFinish();
            s_lastTlPath = s_path;
        }
        s_tlActive = false;
    }

    // ---- Dashcam ----
    if (cfg.dashcam_mins > 0) {
        // Dashcam wins over timelapse (mutually exclusive, MJPEG2SD rule):
        // close our TL file so the DASH session below can start clean.
        if (s_tlActive && s_active) {
            cameraRecordFinish();
            s_tlActive = false;
        }
        if (!s_active) {
            // Start a new file.
            uint16_t w, h;
            cameraGetOutputDimensions(w, h);
            if (cameraRecordStart(cfg.fps, (uint8_t)cfg.framesize, w, h, "DASH")) {
                s_dashOwn = true;
            }
            s_lastFrameMs = nowMs;
        } else {
            // Append one frame at the configured target FPS.
            // Single pump (see timelapse): cache when live, direct when idle.
            uint32_t intervalMs = (cfg.fps > 0) ? (1000UL / cfg.fps) : 67UL;
            intervalMs *= cameraRecordBackpressureScale();
            if ((nowMs - s_lastFrameMs) >= intervalMs) {
                s_lastFrameMs = nowMs;
                if (cameraStreamIsActive()) {
                    size_t len = 0;
                    const uint8_t* src = cameraStreamFrame(len);
                    if (src && len > 0) cameraRecordFrame(src, len);
                } else if (!cameraMotionSamplingActive()) {
                    camera_fb_t* fb = cameraCaptureFrame();
                    if (fb) {
                        cameraRecordFrame(fb->buf, fb->len);
                        esp_camera_fb_return(fb);
                    }
                }
            }

            // Auto-rotate: close current file and let next tick open a new one.
            uint32_t limitMs = (uint32_t)cfg.dashcam_mins * 60UL * 1000UL;
            if ((nowMs - s_startMs) >= limitMs) {
                cameraRecordFinish();
            }
        }
    } else {
        // Dashcam disabled — close only a leftover dashcam file. Manual,
        // motion and timelapse sessions are owned elsewhere and immune here
        // (previously this killed manual recordings on the next tick).
        if (s_active && s_dashOwn) {
            cameraRecordFinish();
        }
        s_dashOwn = false;
        s_lastFrameMs = 0;
    }

    // ---- Generic idle pump ----
    // An active session with no other feeder (dashcam off, stream off, e.g.
    // a manual start or a motion session while idle) would otherwise record
    // zero frames. Feed it here at the session fps.
    if (s_active && cfg.dashcam_mins == 0 && !s_tlActive &&
        !cameraStreamIsActive() && !cameraMotionSamplingActive()) {
        uint32_t intervalMs = (s_fps > 0) ? (1000UL / s_fps) : 100UL;
        if (s_lastFrameMs == 0 || (nowMs - s_lastFrameMs) >= intervalMs) {
            s_lastFrameMs = nowMs;
            camera_fb_t* fb = cameraCaptureFrame();
            if (fb) {
                cameraRecordFrame(fb->buf, fb->len);
                esp_camera_fb_return(fb);
            }
        }
    }
}

#endif // ENABLE_CAMERA==1
