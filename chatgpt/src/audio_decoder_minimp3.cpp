#include "audio_decoder_minimp3.h"

// Included unconditionally (outside the ENABLE_* guards) so the PlatformIO LDF
// conditional scanner can see the dependency and link the local minimp3 library
// even when pack macros are not defined during the LDF scan (they come from
// headers, not -D flags — same issue as Phase 1 ESP_I2S).
#include "minimp3.h"

#include "feature_flags.h"
#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

// Materialise the minimp3 implementation in exactly one TU (this one).
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "psram_helpers.h"  // heap_caps_malloc
#include "commands.h"       // sendVirtualMessage + LogLevel (INFO/WARN/ERROR)

// Printed once per playback session so the first decoded frame's real rate is
// visible for BOTH streams and SD files (a plain `static` inside loop() only
// ever printed for the first session and hid e.g. a 48 kHz local file).
static bool s_firstFrameLogged = false;

// MPEG audio frame size in bytes for the header at `h` (MPEG-1/2/2.5, layers
// I/II/III), or 0 when `h` is not a valid frame header (garbage, ID3 data,
// reserved bitrate/samplerate, free format...). Used by the decoder to avoid
// handing minimp3 a partial frame: Layer-III keeps a cross-frame bit reservoir
// and its fast path relies on the buffer starting at a frame boundary, so a
// truncated frame at the rolling-buffer edge forces a resync that both drops
// the reservoir and can lock onto a false sync in the tail garbage — silently
// skipping ~12% of the frames (the "plays too fast / ends early" bug).
static uint32_t mpegFrameSize(const uint8_t* h) {
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return 0;
    const int version = (h[1] >> 3) & 3;   // 3=MPEG1, 2=MPEG2, 0=MPEG2.5
    const int layer   = (h[1] >> 1) & 3;   // 1=L3, 2=L2, 3=L1, 0=reserved
    if (layer == 0) return 0;
    const int brIdx = (h[2] >> 4) & 0xF;
    const int srIdx = (h[2] >> 2) & 3;
    const int pad   = (h[2] >> 1) & 1;
    static const uint16_t kBitrate[2][16] = {
        {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0}, // MPEG-1
        {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0}      // MPEG-2/2.5
    };
    static const uint16_t kRate[3][4] = {
        {11025,12000,8000,0},   // MPEG-2.5
        {22050,24000,16000,0},  // MPEG-2
        {44100,48000,32000,0}   // MPEG-1
    };
    const int br = kBitrate[(version == 3) ? 0 : 1][brIdx];
    const int rate = kRate[(version == 3) ? 2 : ((version == 2) ? 1 : 0)][srIdx];
    if (!br || !rate) return 0;
    if (layer == 3) return (12u * br * 1000u / rate + pad) * 4u;              // Layer I
    const uint32_t mult = (version == 3) ? 144u : 72u;                        // Layer II/III
    return mult * br * 1000u / rate + pad;
}

Minimp3Decoder::Minimp3Decoder()
    : AudioDecoder()
    , _mp3dec(nullptr)
    , _scratch(nullptr)
    , _inBuf(nullptr)
    , _pcmBuf(nullptr)
    , _inLen(0)
    , _inPos(0)
    , _eof(false)
    , _lastRate(0)
    , _pcmSamples(0)
    , _pcmPos(0)
    , _pcmChannels(2)
{
}

Minimp3Decoder::~Minimp3Decoder() {
    stop();
}

bool Minimp3Decoder::begin(AudioSource* source, AudioOutput* output) {
    if (!source || !output) return false;

    file = source;
    this->output = output;

    // Configure the output BEFORE begin() so the I2S driver knows the bit depth
    // and channel count (otherwise it may default to 32-bit and produce garbled,
    // sped-up audio).
    output->SetBitsPerSample(16);
    output->SetChannels(2);
    if (!output->begin()) return false;

    // Allocate the minimp3 decoder state (small struct, can live on the heap).
    if (!_mp3dec) {
        _mp3dec = malloc(sizeof(mp3dec_t));
        if (!_mp3dec) return false;
    }
    mp3dec_init((mp3dec_t*)_mp3dec);

    // Rolling input buffer: >= 10 consecutive MP3 frames for reliable sync.
    static const uint32_t kInBufSize = 16 * 1024;
    if (!_inBuf) {
        if (psramFound())
            _inBuf = (uint8_t*)heap_caps_malloc(kInBufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_inBuf)
            _inBuf = (uint8_t*)heap_caps_malloc(kInBufSize, MALLOC_CAP_8BIT);
        if (!_inBuf) return false;
    }

    // PCM output buffer: MAX_SAMPLES_PER_FRAME stereo samples.
    if (!_pcmBuf) {
        size_t pcmBytes = MINIMP3_MAX_SAMPLES_PER_FRAME * 2 * sizeof(int16_t);
        if (psramFound())
            _pcmBuf = (int16_t*)heap_caps_malloc(pcmBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_pcmBuf)
            _pcmBuf = (int16_t*)heap_caps_malloc(pcmBytes, MALLOC_CAP_8BIT);
        if (!_pcmBuf) return false;
    }

    // Allocate the decoder scratch (~16 KB) once in PSRAM so the loop task
    // stack stays small. Reused across every frame — zero malloc/free churn.
    if (!_scratch) {
        if (psramFound())
            _scratch = heap_caps_calloc(1, sizeof(mp3dec_scratch_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_scratch)
            _scratch = heap_caps_calloc(1, sizeof(mp3dec_scratch_t), MALLOC_CAP_8BIT);
        if (!_scratch) return false;
    }

    _inLen = 0;
    _inPos = 0;
    _eof = false;
    _lastRate = 0;
    _pcmSamples = 0;
    _pcmPos = 0;
    _pcmChannels = 2;
    _totalPcmSamples = 0;
    _sourceBytes = 0;
    _consumedBytes = 0;
    _id3Remain = 0;
    _lastStatMs = 0;
    _startMs = millis();
    s_firstFrameLogged = false;   // per-session: (re)print the frame report below
    running = true;
    lastSample[0] = 0;
    lastSample[1] = 0;
    return true;
}

bool Minimp3Decoder::loop() {
    if (!running || !file || !output || !_inBuf || !_pcmBuf) {
        running = false;
        return false;
    }

    mp3dec_t* dec = (mp3dec_t*)_mp3dec;
    static const uint32_t kInBufSize = 16 * 1024;

    while (running) {
        // ---- 1) Feed any pending decoded PCM, with backpressure ---------------
        // ConsumeSample uses i2s_write(timeout=0): it returns false when the DMA
        // buffer is full. We must STOP and yield in that case, then resume from
        // _pcmPos on the next loop() call — otherwise samples are silently
        // dropped and the audio sounds garbled/fast.
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

        // ---- 2) Top up the rolling buffer from the source --------------------
        // No isOpen() guard here: for the ring source, EOF is signalled by
        // read() returning 0 while isOpen() flips false (task gave up or the
        // finite file drained). Skipping the read when isOpen() goes false
        // would leave _eof unset and the decoder spinning forever.
        if (_inLen < kInBufSize && !_eof) {
            if (_inPos > 0 && _inLen > _inPos) {
                memmove(_inBuf, _inBuf + _inPos, _inLen - _inPos);
                _inLen -= _inPos;
                _inPos = 0;
            } else if (_inPos > 0) {
                _inLen = 0;
                _inPos = 0;
            }
            uint32_t got = file->read(_inBuf + _inLen, kInBufSize - _inLen);
            _sourceBytes += got;
            if (got == 0) {
                // Only a real end-of-stream must set _eof. A ring HTTP source
                // (radio) may return 0 transiently while its background task
                // reconnects — treating that as EOF would tear the stream down
                // and the runtime would sit out its reconnect backoff (gap).
                uint32_t sz = file->getSize();
                bool realEof = false;
                if (sz > 0 && file->getPos() >= sz) realEof = true;    // finite file done
                else if (!file->isOpen()) realEof = true;              // source closed/dead
                if (realEof) _eof = true;
            } else {
                _inLen += got;
            }
        }

        const uint8_t* avail = _inBuf + _inPos;
        uint32_t availLen = _inLen - _inPos;

        // ---- Skip a leading ID3v2 tag in bulk -------------------------------
        // Fed to minimp3 in 16 KB chunks, tag bytes make find_frame scan
        // garbage and can false-sync near a buffer end; the failed-scan guard
        // below then falls back to a 1-byte-at-a-time skip (slow). Detect the
        // tag from the already-buffered bytes and discard it all at once. Safe
        // for streams too — the bytes are buffered, never lost.
        if (_id3Remain > 0) {
            if (availLen == 0) return true; // wait for more data
            uint32_t take = (availLen < _id3Remain) ? availLen : _id3Remain;
            _inPos += take;
            _id3Remain -= take;
            if (_inPos >= _inLen) { _inLen = 0; _inPos = 0; }
            continue;
        }
        if (_totalPcmSamples == 0 && availLen >= 10 &&
            avail[0] == 'I' && avail[1] == 'D' && avail[2] == '3') {
            _id3Remain = 10u
                | ((uint32_t)(avail[6] & 0x7f) << 21)
                | ((uint32_t)(avail[7] & 0x7f) << 14)
                | ((uint32_t)(avail[8] & 0x7f) << 7)
                | ((uint32_t)(avail[9] & 0x7f));
            uint32_t take = (availLen < _id3Remain) ? availLen : _id3Remain;
            _inPos += take;
            _id3Remain -= take;
            if (_inPos >= _inLen) { _inLen = 0; _inPos = 0; }
            continue;
        }

        // ---- 3) Decode one frame ---------------------------------------------
        // Never hand minimp3 a truncated frame: minimp3's fast path expects
        // the buffer to start at a frame boundary AND to hold the frame plus
        // the next 4-byte header (frame_bytes + 4 <= avail). When only part is
        // buffered (rolling-buffer edge), compact + refill and retry instead
        // of feeding it — minimp3 would otherwise resync (dropping the
        // Layer-III bit reservoir) and can lock onto a false sync in the tail
        // garbage, silently skipping ~12% of the frames (the "plays too fast /
        // ends early" bug).
        if (!_eof && availLen >= 4) {
            const uint32_t fsz = mpegFrameSize(avail);
            if (fsz && availLen < fsz + 4) {
                if (_inPos > 0 && _inLen > _inPos) {
                    memmove(_inBuf, _inBuf + _inPos, _inLen - _inPos);
                    _inLen -= _inPos;
                    _inPos = 0;
                } else if (_inPos > 0) {
                    _inLen = 0;
                    _inPos = 0;
                }
                const uint32_t before = _inLen;
                if (_inLen < kInBufSize) {
                    uint32_t got = file->read(_inBuf + _inLen, kInBufSize - _inLen);
                    _sourceBytes += got;
                    if (got == 0) {
                        uint32_t sz = file->getSize();
                        if (sz > 0 && file->getPos() >= sz) _eof = true;
                        else if (!file->isOpen()) _eof = true;
                    } else {
                        _inLen += got;
                    }
                }
                if (_inLen > before) continue; // more data arrived, retry the frame
                // No new data: at EOF decode the final (possibly truncated)
                // frame; otherwise the source is stalled — yield and retry the
                // same partial frame later instead of feeding minimp3 a
                // truncated buffer (which would resync and can false-sync).
                if (!_eof) return true;
            }
        }

        // Recompute after the compact above (it may have moved _inPos/_inLen).
        avail = _inBuf + _inPos;
        availLen = _inLen - _inPos;
        if (availLen == 0) {
            if (_eof) {
                running = false;
                reportStats("EOF");
                _totalPcmSamples = 0; // suppress the follow-up STOPPED report
                break;
            }
            return true; // wait for more data
        }

        mp3dec_frame_info_t info;
        memset(&info, 0, sizeof(info));
        int samples = mp3dec_decode_frame_s(dec, avail, availLen, _pcmBuf, &info, (mp3dec_scratch_t*)_scratch);

        if (info.frame_bytes > 0) {
            // minimp3 sets frame_bytes = availLen when find_frame could not
            // validate any frame in the whole buffer (it scans to the end).
            // Advancing past that would discard real frame bytes at an
            // ID3/garbage -> audio transition (or a resync), losing audio.
            // Only advance when a real frame was decoded (samples > 0) or a
            // genuinely-sized skipped frame was consumed (< availLen).
            if (samples > 0 || info.frame_bytes < availLen) {
                _inPos += (uint32_t)info.frame_bytes;
                _consumedBytes += (uint32_t)info.frame_bytes;
                if (_inPos >= _inLen) { _inLen = 0; _inPos = 0; }
            }

            if (samples > 0) {
                // Diagnostic: log the first decoded frame so we can verify
                // minimp3 is producing correct output. Per-session now (reset
                // in begin()/desync()) so local-file playbacks also report.
                if (!s_firstFrameLogged) {
                    char dbg[128];
                    snprintf(dbg, sizeof(dbg),
                        "[MP3] first frame: rate=%d ch=%d samples=%d bite=%dk pcm[0]=%d pcm[10]=%d pcm[100]=%d",
                        info.hz, info.channels, samples, info.bitrate_kbps,
                        (int)_pcmBuf[0], (int)_pcmBuf[10], (int)_pcmBuf[100]);
                    sendVirtualMessage(String(dbg), INFO, false, true);
                    s_firstFrameLogged = true;
                }
                // Report the sample rate on first frame or when it changes
                // (SetRate only on rate change, not every frame, so
                // i2s_set_sample_rates doesn't glitch).
                if (info.hz != _lastRate) {
                    output->SetRate(info.hz);
                    _lastRate = info.hz;
                    char dbg[96];
                    snprintf(dbg, sizeof(dbg), "[MP3] sample rate set to %d Hz", info.hz);
                    sendVirtualMessage(String(dbg), INFO, false, true);
                }
                _pcmSamples = (uint32_t)samples;
                _pcmPos = 0;
                _pcmChannels = info.channels;
                _totalPcmSamples += (uint64_t)samples;
                lastSample[0] = (info.channels == 1) ? _pcmBuf[samples - 1]
                              : _pcmBuf[(samples - 1) * 2 + 0];
                lastSample[1] = (info.channels == 1) ? _pcmBuf[samples - 1]
                              : _pcmBuf[(samples - 1) * 2 + 1];

                // Periodic progress report (every ~30 s of wall time) so live
                // streams — which never reach EOF — can be verified on the
                // Serial/Web monitor: ratio ≈ 1.0 means no frames lost, while
                // a dropped-frame bug (the old ~12% loss) shows ratio ≈ 0.88.
                if (_lastStatMs == 0) _lastStatMs = millis();
                if ((millis() - _lastStatMs) >= 30000UL) {
                    _lastStatMs = millis();
                    const double expSec = (double)_totalPcmSamples / (double)info.hz;
                    const double wallSec = (double)(millis() - _startMs) / 1000.0;
                    const double ratio = (wallSec > 0.0) ? expSec / wallSec : 0.0;
                    char dbg[128];
                    snprintf(dbg, sizeof(dbg),
                        "[MP3] progress smp=%llu exp=%.1fs wall=%.1fs ratio=%.3f",
                        (unsigned long long)_totalPcmSamples, expSec, wallSec, ratio);
                    sendVirtualMessage(String(dbg), INFO, false, true);
                }
                continue; // loop back to feed the freshly decoded PCM
            }
            // 0 samples + frame_bytes > 0: a genuinely-sized skipped frame
            // (ID3 chunk / non-decodable layer) was consumed and _inPos was
            // advanced — decode the next thing. If instead the whole buffer
            // was scanned without a frame (frame_bytes == availLen), we kept
            // the data and fall through to the need-more-data handling below.
            if (info.frame_bytes < availLen) continue;
        }

        // frame_bytes == 0: need more data or end of file.
        if (_eof) {
            running = false;
            reportStats("EOF");
            _totalPcmSamples = 0; // suppress the follow-up STOPPED report
            break;
        }
        if (_inPos > 0 && _inLen == _inPos) { _inLen = 0; _inPos = 0; }
        if (_inLen == kInBufSize && availLen == kInBufSize) _inPos++;
        if (_inPos > 0 && _inLen == kInBufSize) {
            memmove(_inBuf, _inBuf + _inPos, _inLen - _inPos);
            _inLen -= _inPos;
            _inPos = 0;
        }
        return true; // wait for more data
    }

    running = false;
    return false;
}

bool Minimp3Decoder::isRunning() {
    return running;
}

bool Minimp3Decoder::stop() {
    reportStats("STOPPED");
    _totalPcmSamples = 0; // suppress duplicate report from the destructor
    running = false;
    if (_inBuf) { heap_caps_free(_inBuf); _inBuf = nullptr; }
    if (_pcmBuf) { heap_caps_free(_pcmBuf); _pcmBuf = nullptr; }
    if (_scratch) { heap_caps_free(_scratch); _scratch = nullptr; }
    if (_mp3dec) { free(_mp3dec); _mp3dec = nullptr; }
    _inLen = 0;
    _inPos = 0;
    _eof = false;
    return true;
}

void Minimp3Decoder::reportStats(const char* reason) {
    if (_lastRate <= 0 || _totalPcmSamples == 0) return;
    const double expSec = (double)_totalPcmSamples / (double)_lastRate;
    const uint32_t wallMs = millis() - _startMs;
    const double wallSec = (double)wallMs / 1000.0;
    const double ratio = (wallSec > 0.0) ? expSec / wallSec : 0.0;
    char dbg[128];
    // Short lines (under ~90 chars) so the web Serial Monitor never clips them.
    snprintf(dbg, sizeof(dbg),
        "[MP3] %s src=%llu consumed=%llu pos=%u/%u",
        reason, (unsigned long long)_sourceBytes, (unsigned long long)_consumedBytes,
        file ? (unsigned)file->getPos() : 0U, file ? (unsigned)file->getSize() : 0U);
    sendVirtualMessage(String(dbg), INFO, false, true);
    snprintf(dbg, sizeof(dbg),
        "[MP3] %s smp=%llu@%dHz exp=%.2fs wall=%.2fs ratio=%.4f",
        reason, (unsigned long long)_totalPcmSamples, _lastRate, expSec, wallSec, ratio);
    sendVirtualMessage(String(dbg), INFO, false, true);
}

void Minimp3Decoder::desync() {
    // Reset the decoder state at the current stream position (e.g. after a seek)
    if (_mp3dec) mp3dec_init((mp3dec_t*)_mp3dec);
    _inLen = 0;
    _inPos = 0;
    _eof = false;
    _lastRate = 0;
    _pcmSamples = 0;
    _pcmPos = 0;
    _totalPcmSamples = 0;
    _sourceBytes = 0;
    _consumedBytes = 0;
    _id3Remain = 0;
    _lastStatMs = 0;
    _startMs = millis();
    s_firstFrameLogged = false;
}

#endif // PACK_RADIO
#endif // ENABLE_UDA1334A == 1
