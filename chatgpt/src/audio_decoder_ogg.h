#pragma once

#include "feature_flags.h"

#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

#include "audio_core.h"

// ============================================================================
// OggVorbisDecoder — first-party OGG Vorbis decoder.
//
// Implements the AudioDecoder interface (begin/loop/isRunning/stop) using
// stb_vorbis (public domain / Unlicense, lib/stb_vorbis) in pushdata mode:
// raw OGG stream bytes are fed straight from the AudioSource through a
// PSRAM rolling buffer — no seek needed, so both SD files and HTTP streams
// work. Decoded float PCM is converted to 16-bit stereo for the I2S output.
//
// Memory: rolling input (16 KB) in PSRAM; stb_vorbis setup state lives in a
// PSRAM arena (bump allocator, no internal-DRAM pressure, freed on stop()).
// ============================================================================

class OggVorbisDecoder : public AudioDecoder {
 public:
    OggVorbisDecoder();
    virtual ~OggVorbisDecoder() override;
    bool begin(AudioSource* source, AudioOutput* output) override;
    bool loop() override;
    bool isRunning() override;
    bool stop() override;
    void desync() override;

 private:
    bool tryOpen();       // one pushdata open attempt on the buffered window
    void* _vorbis;        // stb_vorbis* handle (allocated in begin)
    uint8_t* _arena;      // stb_vorbis bump arena (PSRAM, 256 KB)
    uint32_t _arenaSize;  // arena bytes
    uint8_t* _inBuf;      // rolling OGG input buffer (16 KB, PSRAM)
    uint32_t _inLen;  // valid bytes in _inBuf
    uint32_t _inPos;  // consumed offset within _inBuf
    bool _eof;        // source exhausted
    int _lastRate;    // sample-rate change detection
};

#endif
#endif
