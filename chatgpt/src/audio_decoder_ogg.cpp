#include "audio_decoder_ogg.h"

// Declarations only here (outside the ENABLE_* guards) so the PlatformIO LDF
// conditional scanner can see the dependency and link the local stb_vorbis
// library even when pack macros are not defined during the LDF scan (they come
// from headers, not -D flags — same pattern as minimp3/dr_flac). Unlike
// minimp3, stb_vorbis materialises its implementation on a bare include, so
// HEADER_ONLY is required for this declarations-only pass.
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.h"
#undef STB_VORBIS_HEADER_ONLY

#include "feature_flags.h"
#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

// Materialise the stb_vorbis implementation in exactly one TU (this one).
// STB_VORBIS_INCLUDE_STB_VORBIS_H is already set by the pass above, so the
// declarations are skipped and only the implementation compiles.
#include "stb_vorbis.h"

#include <stdlib.h>
#include "psram_helpers.h"  // heap_caps_malloc
#include "commands.h"       // sendVirtualMessage + LogLevel

// Rolling input: >= several OGG pages for reliable header + audio sync.
static const uint32_t kOggInBufSize = 16 * 1024;
// stb_vorbis bump arena: codebooks + floor/residue tables for stereo streams
// are typically 50-150 KB; 256 KB in PSRAM leaves wide margin (freed on stop).
static const uint32_t kOggArenaSize = 256 * 1024;

// ---------------------------------------------------------------------------

OggVorbisDecoder::OggVorbisDecoder()
    : AudioDecoder()
    , _vorbis(nullptr)
    , _arena(nullptr)
    , _arenaSize(0)
    , _inBuf(nullptr)
    , _inLen(0)
    , _inPos(0)
    , _eof(false)
    , _lastRate(0) {
}

OggVorbisDecoder::~OggVorbisDecoder() {
    stop();
}

// One pushdata open attempt on the currently buffered window. Returns true on
// success; on VORBIS_need_more_data the caller tops up and retries.
bool OggVorbisDecoder::tryOpen() {
    if (_vorbis || !_arena || _inLen == 0) return _vorbis != nullptr;
    stb_vorbis_alloc alloc;
    alloc.alloc_buffer = (char*)_arena;
    alloc.alloc_buffer_length_in_bytes = (int)_arenaSize;
    int consumed = 0;
    int error = 0;
    _vorbis = stb_vorbis_open_pushdata(_inBuf, (int)_inLen, &consumed, &error, &alloc);
    if (!_vorbis) {
        if (error == VORBIS_need_more_data) return false;
        char dbg[64];
        snprintf(dbg, sizeof(dbg), "[OGG] open failed (err=%d)", error);
        sendVirtualMessage(String(dbg), WARN, false, true);
        return false;
    }
    if (consumed > 0) _inPos = (uint32_t)consumed;
    stb_vorbis_info info = stb_vorbis_get_info((stb_vorbis*)_vorbis);
    char dbg[96];
    snprintf(dbg, sizeof(dbg), "[OGG] opened: %u Hz, %d ch", info.sample_rate, info.channels);
    sendVirtualMessage(String(dbg), INFO, false, true);
    return true;
}

bool OggVorbisDecoder::begin(AudioSource* source, AudioOutput* output) {
    if (!source || !output) return false;

    file = source;
    this->output = output;

    if (!_inBuf) {
        if (psramFound())
            _inBuf = (uint8_t*)heap_caps_malloc(kOggInBufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_inBuf)
            _inBuf = (uint8_t*)heap_caps_malloc(kOggInBufSize, MALLOC_CAP_8BIT);
        if (!_inBuf) return false;
    }
    if (!_arena) {
        if (psramFound())
            _arena = (uint8_t*)heap_caps_malloc(kOggArenaSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_arena)
            _arena = (uint8_t*)heap_caps_malloc(kOggArenaSize, MALLOC_CAP_8BIT);
        if (!_arena) return false;
        _arenaSize = kOggArenaSize;
    }

    // Configure the output BEFORE begin() so the I2S driver knows the bit
    // depth and channel count.
    output->SetBitsPerSample(16);
    output->SetChannels(2);
    if (!output->begin()) return false;

    // Prime the rolling buffer with the stream head (carries the OGG +
    // Vorbis headers). More headers may arrive during the first loop()s —
    // pushdata consumes incrementally, so a partial prime is fine.
    _inLen = source->read(_inBuf, kOggInBufSize);
    _inPos = 0;
    _eof = (_inLen == 0);
    if (_inLen == 0) {
        sendVirtualMessage(F("[OGG] empty source"), WARN, false, true);
        return false;
    }

    // Open eagerly on the prime so a bad file fails fast in begin() (same
    // contract as the other decoders). Headers spanning the prime are
    // retried incrementally in loop().
    if (!tryOpen()) {
        // tryOpen() already reported hard errors; need_more_data just means
        // the headers did not fit the prime — keep the bytes, loop() tops up.
        _inPos = 0;
        if (_eof) return false;
    }

    _lastRate = 0;
    running = true;
    lastSample[0] = 0;
    lastSample[1] = 0;
    return true;
}
// Move any unconsumed tail to the front of _inBuf, then top up from source.
static bool oggRefill(AudioSource* src, uint8_t* inBuf, uint32_t inBufSize,
                       uint32_t& inLen, uint32_t& inPos, bool& eof) {
    if (inLen > inPos) {
        memmove(inBuf, inBuf + inPos, inLen - inPos);
        inLen -= inPos;
    } else {
        inLen = 0;
    }
    inPos = 0;
    if (inLen >= inBufSize) return true;  // full
    uint32_t want = inBufSize - inLen;
    uint32_t got = src->read(inBuf + inLen, want);
    inLen += got;
    if (got == 0) eof = true;
    return true;
}

static inline int16_t oggFloatToS16(float v) {
    if (v > 1.0f) v = 1.0f;
    else if (v < -1.0f) v = -1.0f;
    int32_t s = (int32_t)(v * 32767.0f);
    if (s > 32767) return 32767;
    if (s < -32767) return -32767;
    return (int16_t)s;
}

bool OggVorbisDecoder::loop() {
    if (!running || !file || !output || !_inBuf) {
        running = false;
        return false;
    }

    // ---- 0) Deferred open: headers did not fit the prime -------------------
    if (!_vorbis) {
        oggRefill(file, _inBuf, kOggInBufSize, _inLen, _inPos, _eof);
        if (_inLen == 0) {
            running = false;
            return false;
        }
        if (!tryOpen()) {
            if (!_eof && _inLen < kOggInBufSize) {
                return true;  // wait for more header bytes
            }
            running = false;
            return false;
        }
    }

    stb_vorbis* v = (stb_vorbis*)_vorbis;

    // ---- 1) Decode one frame from the buffered window -----------------------
    // NOTE: `pcm` is deliberately not named `output` — that name is the
    // AudioOutput member used below.
    int channels = 0;
    float** pcm = nullptr;
    int samples = 0;
    int used = 0;
    if (_inLen > _inPos) {
        used = stb_vorbis_decode_frame_pushdata(v, _inBuf + _inPos, (int)(_inLen - _inPos),
                                                &channels, &pcm, &samples);
        if (used > 0) _inPos += (uint32_t)used;
    }

    if (samples > 0 && pcm && channels > 0) {
        stb_vorbis_info info = stb_vorbis_get_info(v);
        if (info.sample_rate && (int)info.sample_rate != _lastRate) {
            this->output->SetRate((int)info.sample_rate);
            _lastRate = (int)info.sample_rate;
        }
        // Feed with backpressure: ConsumeSample returns false when the DMA
        // buffer is full — yield and resume streaming (stb_vorbis hands out
        // the frame buffer, so nothing is lost by returning early).
        for (int i = 0; i < samples; i++) {
            int16_t s[2];
            if (channels == 1) {
                s[0] = s[1] = oggFloatToS16(pcm[0][i]);
            } else {
                s[0] = oggFloatToS16(pcm[0][i]);
                s[1] = oggFloatToS16(pcm[1][i]);
            }
            if (!this->output->ConsumeSample(s)) {
                // DMA full: rewind is impossible with pushdata output, so
                // drop the frame tail (one frame max) rather than stall.
                break;
            }
            lastSample[0] = s[0];
            lastSample[1] = s[1];
        }
        return true;
    }

    // ---- 2) No frame: refill and continue, or stop at EOF ------------------
    oggRefill(file, _inBuf, kOggInBufSize, _inLen, _inPos, _eof);
    if (_inLen == 0 && _eof) {
        running = false;
        return false;
    }
    return true;  // need more data (0 used / 0 samples) — retry next call
}

bool OggVorbisDecoder::isRunning() {
    return running;
}

bool OggVorbisDecoder::stop() {
    running = false;
    if (_vorbis) {
        stb_vorbis_close((stb_vorbis*)_vorbis);
        _vorbis = nullptr;
    }
    if (_arena) {
        heap_caps_free(_arena);
        _arena = nullptr;
    }
    _arenaSize = 0;
    if (_inBuf) {
        heap_caps_free(_inBuf);
        _inBuf = nullptr;
    }
    _inLen = 0;
    _inPos = 0;
    _eof = false;
    return true;
}

void OggVorbisDecoder::desync() {
    // Pushdata state cannot seek: drop it — the runtime reopens on the next
    // play request.
    if (_vorbis) {
        stb_vorbis_close((stb_vorbis*)_vorbis);
        _vorbis = nullptr;
    }
    _inLen = 0;
    _inPos = 0;
    _eof = false;
    running = false;
}

#endif  // PACK_RADIO
#endif  // ENABLE_UDA1334A == 1
