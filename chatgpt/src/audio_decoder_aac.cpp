#include "audio_decoder_aac.h"

#include "feature_flags.h"
#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

// esp_audio_codec (Espressif Modified MIT) — vendored prebuilt archive for
// n16r8, force-linked by scripts/link_esp_audio_codec.py. The includes stay
// inside the pack guards so envs without the vendored headers (e.g. esp32p4,
// which compiles this TU to an empty unit) do not need them.
#include "esp_audio_dec.h"
#include "esp_aac_dec.h"

#include "adts_parser.h"
#include "psram_helpers.h"  // heap_caps_malloc
#include "commands.h"       // sendVirtualMessage + LogLevel

// One AAC frame (1024 samples/channel) stereo 16-bit = 4 KB. Allow a few
// frames so the decoder has headroom; sized as a power of two for alignment.
static const uint32_t kAacInBufSize = 4 * 1024;      // ADTS input (PSRAM)
static const uint32_t kAacPcmCapacity = 2048;       // max samples/ch/frame

AacDecoder::AacDecoder()
    : AudioDecoder()
    , _aac(nullptr)
    , _inBuf(nullptr)
    , _pcmBuf(nullptr)
    , _inLen(0)
    , _inPos(0)
    , _eof(false)
    , _lastRate(0)
    , _lastCh(0)
    , _pcmSamples(0)
    , _pcmPos(0)
    , _pcmChannels(2)
{
}

AacDecoder::~AacDecoder() {
    stop();
}

bool AacDecoder::begin(AudioSource* source, AudioOutput* output) {
    if (!source || !output) return false;

    file = source;
    this->output = output;

    if (!_inBuf) {
        if (psramFound())
            _inBuf = (uint8_t*)heap_caps_malloc(kAacInBufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_inBuf)
            _inBuf = (uint8_t*)heap_caps_malloc(kAacInBufSize, MALLOC_CAP_8BIT);
        if (!_inBuf) return false;
    }
    if (!_pcmBuf) {
        size_t bytes = kAacPcmCapacity * 2 /* max 2ch */ * sizeof(int16_t);
        if (psramFound())
            _pcmBuf = (int16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_pcmBuf)
            _pcmBuf = (int16_t*)heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
        if (!_pcmBuf) return false;
    }

    // Configure the output before begin() so the I2S driver knows the bit
    // depth and channel count.
    output->SetBitsPerSample(16);
    output->SetChannels(2);
    if (!output->begin()) return false;

    // ADTS-header present by default (radio streams and .m4a/.aac files).
    esp_audio_err_t err = esp_aac_dec_open(NULL, 0, &_aac);
    if (err != ESP_AUDIO_ERR_OK || !_aac) {
        sendVirtualMessage(F("[AAC] esp_aac_dec_open failed"), WARN, false, true);
        _aac = nullptr;
        return false;
    }

    _inLen = 0;
    _inPos = 0;
    _eof = false;
    _lastRate = 0;
    _lastCh = 0;
    _pcmSamples = 0;
    _pcmPos = 0;
    _pcmChannels = 2;
    running = true;
    lastSample[0] = 0;
    lastSample[1] = 0;

    sendVirtualMessage(F("[AAC] decoder opened"), INFO, false, true);
    return true;
}

// Move any unconsumed tail to the front of _inBuf, then top up from the source.
static bool aacRefill(AudioSource* src, uint8_t* inBuf, uint32_t inBufSize,
                      uint32_t& inLen, uint32_t& inPos, bool& eof) {
    if (inLen > inPos) {
        memmove(inBuf, inBuf + inPos, inLen - inPos);
        inLen -= inPos;
    } else {
        inLen = 0;
    }
    inPos = 0;
    if (inLen >= inBufSize) return true; // full

    uint32_t want = inBufSize - inLen;
    uint32_t got = src->read(inBuf + inLen, want);
    inLen += got;
    if (got == 0) eof = true;
    return true;
}

bool AacDecoder::loop() {
    if (!running || !file || !output || !_aac || !_inBuf || !_pcmBuf) {
        running = false;
        return false;
    }

    // ---- 1) Feed any pending decoded PCM with backpressure ----------------
    if (_pcmPos < _pcmSamples) {
        const int ch = _pcmChannels;
        while (_pcmPos < _pcmSamples) {
            int16_t s[2] = { 0, 0 };
            if (ch == 1) {
                s[0] = s[1] = _pcmBuf[_pcmPos];
            } else {
                s[0] = _pcmBuf[_pcmPos * 2 + 0];
                s[1] = _pcmBuf[_pcmPos * 2 + 1];
            }
            if (!output->ConsumeSample(s)) {
                return true; // DMA full — yield, resume here next call
            }
            _pcmPos++;
        }
        _pcmSamples = 0;
        _pcmPos = 0;
    }

    // ---- 2) Decode the next ADTS frame -------------------------------------
    while (true) {
        aacRefill(file, _inBuf, kAacInBufSize, _inLen, _inPos, _eof);
        if (_inLen - _inPos == 0) {
            if (_eof) { running = false; return false; }
            return true; // wait for more data
        }

        size_t syncOff = (size_t)-1;
        adts::Frame fr = adts::findAndParse(_inBuf + _inPos, _inLen - _inPos, &syncOff);
        if (!fr.valid) {
            // No sync in the buffered window: discard everything (ID3/garbage)
            // unless the buffer is exactly full (keep last 2 bytes for a
            // partial 0xFFF at the boundary).
            if (_inLen - _inPos >= kAacInBufSize) {
                _inLen = (_inLen > 2) ? 2 : _inLen;
                _inPos = 0;
                continue;
            }
            return true; // wait for more bytes
        }
        _inPos += syncOff;

        // Need the whole frame before decoding.
        if (fr.frameLength < fr.headerLen) { _inPos += fr.headerLen; continue; }
        if (_inLen - _inPos < fr.frameLength) {
            if (_eof) {
                // Trailing partial frame: stop cleanly.
                running = false;
                return false;
            }
            return true; // wait for the rest of the frame
        }

        // Sample-rate / channel updates drive the I2S clock.
        if (fr.sampleRate && fr.sampleRate != (uint32_t)_lastRate) {
            output->SetRate((int)fr.sampleRate);
            _lastRate = (int)fr.sampleRate;
            char dbg[80];
            snprintf(dbg, sizeof(dbg), "[AAC] sample rate set to %u Hz", (unsigned)fr.sampleRate);
            sendVirtualMessage(String(dbg), INFO, false, true);
        }
        if (fr.channels && fr.channels != (uint8_t)_lastCh) {
            output->SetChannels(fr.channels);
            _lastCh = fr.channels;
        }
        _pcmChannels = (fr.channels == 1) ? 1 : 2;

        esp_audio_dec_in_raw_t raw;
        raw.buffer = _inBuf + _inPos;
        raw.len = fr.frameLength;
        raw.consumed = 0;
        raw.frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE;

        esp_audio_dec_out_frame_t out;
        out.buffer = (uint8_t*)_pcmBuf;
        out.len = kAacPcmCapacity * 2 * sizeof(int16_t);
        out.needed_size = 0;
        out.decoded_size = 0;

        esp_audio_dec_info_t info;
        esp_audio_err_t err = esp_aac_dec_decode(_aac, &raw, &out, &info);

        // Frame consumed (even on decode error) — advance past it.
        _inPos += raw.consumed ? raw.consumed : fr.frameLength;
        if (err == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
            // Output buffer too small for one frame — drop the frame.
            continue;
        }
        if (err != ESP_AUDIO_ERR_OK) {
            // Corrupt frame — skip and try the next one.
            continue;
        }

        if (info.bits_per_sample == 16) {
            _pcmSamples = (uint32_t)out.decoded_size / (2 * (uint16_t)sizeof(int16_t));
        } else {
            _pcmSamples = 0; // 24/32-bit not supported in this scope
        }
        _pcmPos = 0;
        _pcmChannels = (info.channel == 1) ? 1 : 2;

        if (_pcmSamples > 0) {
            const int ch = _pcmChannels;
            const int32_t n = (int32_t)_pcmSamples - 1;
            if (ch == 1) lastSample[0] = lastSample[1] = _pcmBuf[n];
            else { lastSample[0] = _pcmBuf[n * 2 + 0]; lastSample[1] = _pcmBuf[n * 2 + 1]; }
            return true; // feed the freshly decoded PCM next call
        }
        // 0 samples + consumed frame: skip; loop to find the next frame.
    }
}

bool AacDecoder::isRunning() {
    return running;
}

bool AacDecoder::stop() {
    running = false;
    if (_aac) {
        esp_aac_dec_close(_aac);
        _aac = nullptr;
    }
    if (_inBuf) {
        heap_caps_free(_inBuf);
        _inBuf = nullptr;
    }
    if (_pcmBuf) {
        heap_caps_free(_pcmBuf);
        _pcmBuf = nullptr;
    }
    _inLen = 0;
    _inPos = 0;
    _eof = false;
    _pcmSamples = 0;
    _pcmPos = 0;
    return true;
}

void AacDecoder::desync() {
    // A fresh open on the next begin() is required (the source position is
    // owned by the ring/cache upstream); simplest is to stop — the runtime
    // reopens on the next play request.
    if (_aac) {
        esp_aac_dec_close(_aac);
        _aac = nullptr;
    }
    _inLen = 0;
    _inPos = 0;
    _eof = false;
    _pcmSamples = 0;
    _pcmPos = 0;
    running = false;
}

#endif // PACK_RADIO
#endif // ENABLE_UDA1334A == 1
