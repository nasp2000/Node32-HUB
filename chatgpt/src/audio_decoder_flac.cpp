#include "audio_decoder_flac.h"

// dr_flac must be materialised in exactly one TU. The include stays outside the
// pack guards so the PlatformIO LDF conditional scanner sees the dependency.
#include "dr_flac.h"

#include "feature_flags.h"
#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

#include "psram_helpers.h"  // heap_caps_malloc
#include "commands.h"       // sendVirtualMessage + LogLevel

// One decode chunk = 4096 PCM frames (interleaved). At 44.1kHz stereo that's
// ~93ms of audio — plenty without stalling the loop task for too long.
static const uint32_t kFlacChunkFrames = 4096;

// ---- dr_flac I/O callbacks adapted to AudioSource --------------------------

static size_t flacReadProc(void* userData, void* bufferOut, size_t bytesToRead) {
    AudioSource* src = static_cast<AudioSource*>(userData);
    if (!src || !src->isOpen() || bytesToRead == 0) return 0;
    uint32_t got = src->read(bufferOut, (uint32_t)bytesToRead);
    return (size_t)got;
}

static drflac_bool32 flacSeekProc(void* userData, int offset, drflac_seek_origin origin) {
    AudioSource* src = static_cast<AudioSource*>(userData);
    if (!src) return 0;
    int dir = (origin == DRFLAC_SEEK_SET) ? SEEK_SET : SEEK_CUR;
    if (origin == DRFLAC_SEEK_END && src->getSize() > 0) {
        return src->seek((int32_t)src->getSize() + offset, SEEK_SET) ? 1 : 0;
    }
    return src->seek(offset, dir) ? 1 : 0;
}

static drflac_bool32 flacTellProc(void* userData, drflac_int64* offset) {
    AudioSource* src = static_cast<AudioSource*>(userData);
    if (!src) return 0;
    *offset = (drflac_int64)src->getPos();
    return 1;
}

// ---- dr_flac allocation callbacks -> PSRAM-first ---------------------------
// The decoder's internal state (~tens of KB) must not live in the scarce
// internal DRAM. Route every allocation through heap_caps with a PSRAM
// preference and DRAM fallback (heap_caps_free handles both pools).
static void* flacMallocCb(size_t sz, void* userData) {
    (void)userData;
    if (psramFound()) {
        void* p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) return p;
    }
    return heap_caps_malloc(sz, MALLOC_CAP_8BIT);
}

static void* flacReallocCb(void* p, size_t sz, void* userData) {
    (void)userData;
    if (psramFound()) {
        void* q = heap_caps_realloc(p, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (q) return q;
    }
    return heap_caps_realloc(p, sz, MALLOC_CAP_8BIT);
}

static void flacFreeCb(void* p, void* userData) {
    (void)userData;
    heap_caps_free(p);
}

// ---------------------------------------------------------------------------

FlacDecoder::FlacDecoder()
    : AudioDecoder()
    , _flac(nullptr)
    , _pcmBuf(nullptr)
    , _pcmFrames(0)
    , _pcmPos(0)
    , _lastRate(0)
    , _eof(false)
{
}

FlacDecoder::~FlacDecoder() {
    stop();
}

bool FlacDecoder::begin(AudioSource* source, AudioOutput* output) {
    if (!source || !output) return false;

    file = source;
    this->output = output;

    if (!_pcmBuf) {
        size_t bytes = kFlacChunkFrames * 2 /* max 2ch for our scope */ * sizeof(int);
        if (psramFound())
            _pcmBuf = (int*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_pcmBuf)
            _pcmBuf = (int*)heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
        if (!_pcmBuf) return false;
    }

    // Configure the output before begin() so the I2S driver knows the bit
    // depth and channel count.
    output->SetBitsPerSample(16);
    output->SetChannels(2);
    if (!output->begin()) return false;

    static const drflac_allocation_callbacks kAlloc = {
        /* .pUserData = */ nullptr,
        /* .onMalloc  = */ flacMallocCb,
        /* .onRealloc = */ flacReallocCb,
        /* .onFree    = */ flacFreeCb,
    };
    _flac = drflac_open(flacReadProc, flacSeekProc, flacTellProc, source, &kAlloc);
    if (!_flac) {
        // Note: for an SD file the header must be valid; report and fail cleanly.
        sendVirtualMessage(F("[FLAC] drflac_open failed (invalid FLAC?)"), WARN, false, true);
        return false;
    }

    // Restrict to what the runtime safety check allows: 44.1/48 kHz 16-bit.
    const drflac* flac = (const drflac*)_flac;
    if (flac->channels > 2) {
        sendVirtualMessage(F("[FLAC] >2 channels not supported"), WARN, false, true);
        drflac_close((drflac*)_flac);
        _flac = nullptr;
        return false;
    }

    _pcmFrames = 0;
    _pcmPos = 0;
    _lastRate = 0;
    _eof = false;
    running = true;
    lastSample[0] = 0;
    lastSample[1] = 0;

    char dbg[96];
    snprintf(dbg, sizeof(dbg), "[FLAC] opened: %u Hz, %u ch, %llu frames",
             (unsigned)flac->sampleRate, (unsigned)flac->channels,
             (unsigned long long)flac->totalPCMFrameCount);
    sendVirtualMessage(String(dbg), INFO, false, true);
    return true;
}

bool FlacDecoder::loop() {
    if (!running || !file || !output || !_flac || !_pcmBuf) {
        running = false;
        return false;
    }

    drflac* flac = (drflac*)_flac;

    // ---- 1) Feed pending decoded PCM with backpressure ----------------------
    if (_pcmPos < _pcmFrames) {
        const unsigned ch = flac->channels;
        while (_pcmPos < _pcmFrames) {
            int32_t l, r;
            if (ch == 1) {
                l = r = _pcmBuf[_pcmPos];               // mono -> both
            } else {
                l = _pcmBuf[_pcmPos * 2 + 0];
                r = _pcmBuf[_pcmPos * 2 + 1];
            }
            // dr_flac decodes to 32-bit samples; scale to 16-bit.
            int16_t s[2] = { (int16_t)(l >> 16), (int16_t)(r >> 16) };
            if (!output->ConsumeSample(s)) {
                return true; // DMA full — yield, resume here next call
            }
            _pcmPos++;
        }
        _pcmFrames = 0;
        _pcmPos = 0;
    }

    // ---- 2) Decode the next chunk -------------------------------------------
    if (_eof) {
        running = false;
        return false;
    }

    if (flac->sampleRate != (unsigned)_lastRate) {
        output->SetRate((int)flac->sampleRate);
        _lastRate = (int)flac->sampleRate;
    }

    drflac_uint64 framesRead = drflac_read_pcm_frames_s32(flac, kFlacChunkFrames, _pcmBuf);
    if (framesRead == 0) {
        _eof = true;
        running = false;
        return false;
    }
    _pcmFrames = (uint32_t)framesRead;
    _pcmPos = 0;

    unsigned ch = flac->channels;
    int32_t l, r;
    if (ch == 1) { l = r = _pcmBuf[(framesRead - 1)]; }
    else { l = _pcmBuf[(framesRead - 1) * 2 + 0]; r = _pcmBuf[(framesRead - 1) * 2 + 1]; }
    lastSample[0] = (int16_t)(l >> 16);
    lastSample[1] = (int16_t)(r >> 16);

    return true; // feed the freshly decoded chunk on the next loop() call
}

bool FlacDecoder::isRunning() {
    return running;
}

bool FlacDecoder::stop() {
    running = false;
    if (_flac) {
        drflac_close((drflac*)_flac);
        _flac = nullptr;
    }
    if (_pcmBuf) {
        heap_caps_free(_pcmBuf);
        _pcmBuf = nullptr;
    }
    _pcmFrames = 0;
    _pcmPos = 0;
    _eof = false;
    return true;
}

void FlacDecoder::desync() {
    // dr_flac keeps its own position; for our SD-only FLAC scope, a fresh
    // open on seek would be required — simplest is to stop (runtime reopens).
    if (_flac) {
        drflac_close((drflac*)_flac);
        _flac = nullptr;
    }
    _pcmFrames = 0;
    _pcmPos = 0;
    _eof = false;
    running = false;
}

#endif // PACK_RADIO
#endif // ENABLE_UDA1334A == 1
