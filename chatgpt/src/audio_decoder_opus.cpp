#include "audio_decoder_opus.h"

#include "feature_flags.h"
#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

// esp_audio_codec (Espressif Modified MIT) — vendored prebuilt archive for
// n16r8, force-linked by scripts/link_esp_audio_codec.py. The includes stay
// inside the pack guards so envs without the vendored headers (e.g. esp32p4,
// which compiles this TU to an empty unit) do not need them. The OPUS backend
// is the libopus decoding core (opus_decode_native inside the archive).
#include "esp_audio_dec.h"
#include "esp_opus_dec.h"

#include "psram_helpers.h"  // heap_caps_malloc
#include "commands.h"       // sendVirtualMessage + LogLevel

// OGG read chunk: several pages per pump (Opus pages are small).
static const uint32_t kOpusChunkSize = 4 * 1024;
// One staged audio packet: Opus packets are at most ~1500 bytes.
static const uint32_t kOpusPktSize = 2048;
// Decoded PCM: 120 ms @48 kHz stereo 16-bit (max Opus frame) + headroom.
static const uint32_t kOpusPcmCapacity = 5760 + 960;

OggOpusDecoder::OggOpusDecoder()
    : AudioDecoder()
    , _opus(nullptr)
    , _chunkBuf(nullptr)
    , _pktBuf(nullptr)
    , _pktLen(0)
    , _pktReady(false)
    , _pcmBuf(nullptr)
    , _pcmSamples(0)
    , _pcmPos(0)
    , _pcmChannels(2)
    , _preSkipLeft(0)
    , _serial(0)
    , _opened(false)
    , _eof(false)
    , _channels(2) {
}

OggOpusDecoder::~OggOpusDecoder() {
    stop();
}

bool OggOpusDecoder::openInner(const opus::Head& head) {
    if (_opus) {
        esp_opus_dec_close(_opus);
        _opus = nullptr;
    }
    esp_opus_dec_cfg_t cfg = ESP_OPUS_DEC_CONFIG_DEFAULT();
    cfg.sample_rate = 48000;  // Opus always decodes at 48 kHz
    cfg.channel = head.channels;
    cfg.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_INVALID;  // auto (<= 60 ms out)
    cfg.self_delimited = false;
    if (esp_opus_dec_open(&cfg, sizeof(cfg), &_opus) != ESP_AUDIO_ERR_OK || !_opus) {
        _opus = nullptr;
        sendVirtualMessage(F("[OPUS] esp_opus_dec_open failed"), WARN, false, true);
        return false;
    }
    _channels = head.channels;
    _preSkipLeft = head.preSkip;
    _opened = true;
    output->SetRate(48000);
    output->SetChannels(2);
    char dbg[96];
    snprintf(dbg, sizeof(dbg), "[OPUS] opened: %u ch, pre-skip %u", (unsigned)head.channels,
             (unsigned)head.preSkip);
    sendVirtualMessage(String(dbg), INFO, false, true);
    return true;
}

// Read the next source chunk through the OGG Reader. Stages at most one
// audio packet (copies it — Reader packet pointers are callback-scoped) and
// opens (or re-opens, on chained streams) the inner decoder on OpusHead.
bool OggOpusDecoder::pumpPackets() {
    if (_pktReady) return true;
    if (!file || !_chunkBuf || !_pktBuf) return false;
    uint32_t got = file->read(_chunkBuf, kOpusChunkSize);
    if (got == 0) {
        _eof = true;
        return _pktReady;
    }
    uint32_t serialBefore = _reader.serial();
    bool headFailed = false;
    _reader.feed(_chunkBuf, got,
                 [&](const uint8_t* pkt, size_t len, uint32_t idx, uint64_t granule, bool trunc) {
                     (void)granule;
                     if (headFailed) return;
                     if (idx == 0) {
                         if (trunc || len > kOpusPktSize) {
                             headFailed = true;
                             return;
                         }
                         opus::Head h = opus::parseHead(pkt, len);
                         if (!h.valid) {
                             headFailed = true;
                             return;
                         }
                         if (!_opened || _serial != _reader.serial()) {
                             _serial = _reader.serial();
                             if (!openInner(h)) headFailed = true;
                         }
                         return;
                     }
                     if (idx == 1) return;  // OpusTags: skip
                     if (trunc || len == 0 || len > kOpusPktSize) return;  // corrupt: skip
                     if (!_pktReady) {
                         memcpy(_pktBuf, pkt, len);
                         _pktLen = len;
                         _pktReady = true;
                     }
                 });
    (void)serialBefore;
    if (headFailed) {
        sendVirtualMessage(F("[OPUS] invalid OpusHead"), WARN, false, true);
        return false;
    }
    return true;
}

bool OggOpusDecoder::begin(AudioSource* source, AudioOutput* output) {
    if (!source || !output) return false;

    file = source;
    this->output = output;

    if (psramFound()) {
        if (!_chunkBuf)
            _chunkBuf = (uint8_t*)heap_caps_malloc(kOpusChunkSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_pktBuf)
            _pktBuf = (uint8_t*)heap_caps_malloc(kOpusPktSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_pcmBuf)
            _pcmBuf = (int16_t*)heap_caps_malloc(kOpusPcmCapacity * 2 * sizeof(int16_t),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!_chunkBuf)
        _chunkBuf = (uint8_t*)heap_caps_malloc(kOpusChunkSize, MALLOC_CAP_8BIT);
    if (!_pktBuf)
        _pktBuf = (uint8_t*)heap_caps_malloc(kOpusPktSize, MALLOC_CAP_8BIT);
    if (!_pcmBuf)
        _pcmBuf = (int16_t*)heap_caps_malloc(kOpusPcmCapacity * 2 * sizeof(int16_t), MALLOC_CAP_8BIT);
    if (!_chunkBuf || !_pktBuf || !_pcmBuf) return false;

    // Configure the output BEFORE begin() so the I2S driver knows the bit
    // depth and channel count.
    output->SetBitsPerSample(16);
    output->SetChannels(2);
    if (!output->begin()) return false;

    _reader.reset();
    _pktLen = 0;
    _pktReady = false;
    _pcmSamples = 0;
    _pcmPos = 0;
    _pcmChannels = 2;
    _preSkipLeft = 0;
    _serial = 0;
    _opened = false;
    _eof = false;
    _channels = 2;
    running = true;
    lastSample[0] = 0;
    lastSample[1] = 0;

    // Open eagerly on the stream head so a bad file fails fast in begin()
    // (same contract as the AAC/FLAC/WAV decoders).
    if (!pumpPackets() || !_opened) {
        if (!_opened) sendVirtualMessage(F("[OPUS] no OpusHead in stream head"), WARN, false, true);
        running = false;
        return _opened;
    }
    return true;
}

bool OggOpusDecoder::loop() {
    if (!running || !file || !output || !_opus || !_pcmBuf) {
        // Not opened yet (chained serial seen before any head, or begin()
        // deferred): keep pumping until the head arrives or the source ends.
        if (running && file && output && !_opened) {
            if (!pumpPackets()) {
                running = false;
                return false;
            }
            if (!_opened) {
                if (_eof && !_pktReady) {
                    running = false;
                    return false;
                }
                return true;  // wait for the head
            }
        } else {
            running = false;
            return false;
        }
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
                return true;  // DMA full — yield, resume here next call
            }
            _pcmPos++;
        }
        _pcmSamples = 0;
        _pcmPos = 0;
    }

    // ---- 2) Decode the next staged Opus packet ----------------------------
    if (!_pktReady) {
        if (!pumpPackets()) {
            running = false;
            return false;
        }
        if (!_pktReady) {
            if (_eof) {
                running = false;
                return false;
            }
            return true;  // wait for more OGG pages
        }
    }

    esp_audio_dec_in_raw_t raw;
    raw.buffer = _pktBuf;
    raw.len = (uint32_t)_pktLen;
    raw.consumed = 0;
    raw.frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE;

    esp_audio_dec_out_frame_t out;
    out.buffer = (uint8_t*)_pcmBuf;
    out.len = kOpusPcmCapacity * 2 * sizeof(int16_t);
    out.needed_size = 0;
    out.decoded_size = 0;

    esp_audio_dec_info_t info;
    esp_audio_err_t err = esp_opus_dec_decode(_opus, &raw, &out, &info);

    _pktReady = false;
    _pktLen = 0;
    if (err != ESP_AUDIO_ERR_OK) {
        return true;  // corrupt packet — skip, keep the stream alive
    }

    uint32_t samples = (uint32_t)out.decoded_size / (2 * (uint16_t)sizeof(int16_t));
    if (samples > kOpusPcmCapacity) samples = kOpusPcmCapacity;
    // Drop the RFC 7845 pre-skip from the start of the stream.
    if (_preSkipLeft > 0 && samples > 0) {
        uint32_t drop = (_preSkipLeft < samples) ? _preSkipLeft : samples;
        uint32_t keep = samples - drop;
        if (keep > 0) memmove(_pcmBuf, _pcmBuf + drop * 2, keep * 2 * sizeof(int16_t));
        samples = keep;
        _preSkipLeft -= drop;
    }
    _pcmSamples = samples;
    _pcmPos = 0;
    _pcmChannels = (info.channel == 1) ? 1 : 2;

    if (_pcmSamples > 0) {
        const int ch = _pcmChannels;
        const int32_t n = (int32_t)_pcmSamples - 1;
        if (ch == 1) lastSample[0] = lastSample[1] = _pcmBuf[n];
        else {
            lastSample[0] = _pcmBuf[n * 2 + 0];
            lastSample[1] = _pcmBuf[n * 2 + 1];
        }
        return true;  // feed the freshly decoded PCM next call
    }
    return true;
}

bool OggOpusDecoder::isRunning() {
    return running;
}

bool OggOpusDecoder::stop() {
    running = false;
    if (_opus) {
        esp_opus_dec_close(_opus);
        _opus = nullptr;
    }
    if (_chunkBuf) {
        heap_caps_free(_chunkBuf);
        _chunkBuf = nullptr;
    }
    if (_pktBuf) {
        heap_caps_free(_pktBuf);
        _pktBuf = nullptr;
    }
    if (_pcmBuf) {
        heap_caps_free(_pcmBuf);
        _pcmBuf = nullptr;
    }
    _pktLen = 0;
    _pktReady = false;
    _pcmSamples = 0;
    _pcmPos = 0;
    _opened = false;
    _eof = false;
    return true;
}

void OggOpusDecoder::desync() {
    // A fresh open on the next begin() is required (the source position is
    // owned by the ring/cache upstream); simplest is to stop — the runtime
    // reopens on the next play request.
    if (_opus) {
        esp_opus_dec_close(_opus);
        _opus = nullptr;
    }
    _reader.reset();
    _pktLen = 0;
    _pktReady = false;
    _pcmSamples = 0;
    _pcmPos = 0;
    _opened = false;
    _eof = false;
    running = false;
}

#endif  // PACK_RADIO
#endif  // ENABLE_UDA1334A == 1
