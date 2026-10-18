#pragma once

#include "feature_flags.h"

#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

#include "audio_core.h"

// ============================================================================
// WavDecoder — first-party WAV (PCM) decoder.
//
// Implements the AudioDecoder interface (begin/loop/isRunning/stop) for
// uncompressed 16-bit PCM WAV files (the format supported by the old stack's
// WAV path and by the runtime safety check). RIFF/fmt/data parsing is
// first-party (wav_parser.h); decoded PCM is streamed chunk by chunk.
//
// Memory: all buffers are allocated in PSRAM when available (no internal-RAM
// decoder state — WAV is just a memcpy from source to output).
// ============================================================================

class WavDecoder : public AudioDecoder {
public:
    WavDecoder();
    virtual ~WavDecoder() override;
    bool begin(AudioSource* source, AudioOutput* output) override;
    bool loop() override;
    bool isRunning() override;
    bool stop() override;
    void desync() override;

private:
    uint8_t* _chunkBuf;  // PCM read buffer (PSRAM)
    uint32_t _chunkLen;  // valid bytes in _chunkBuf
    uint32_t _chunkPos;  // next sample offset within _chunkBuf
    uint32_t _dataLeft;  // remaining data-chunk bytes
    uint32_t _sampleRate;
    uint8_t  _channels;
    uint8_t  _bitsPerSample;
    bool     _eof;
    int      _lastRate;
    bool     _wavValid;  // header parsed OK
};

#endif
#endif
