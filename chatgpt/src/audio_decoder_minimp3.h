#pragma once

#include "feature_flags.h"

#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

#include "audio_core.h"

// ============================================================================
// Minimp3Decoder — first-party MP3 decoder.
//
// Implements the AudioDecoder interface (begin/loop/isRunning/stop) using
// minimp3 (CC0-1.0, lib/minimp3).
//
// Compatible with AudioSource (s_buffered / ring) and AudioOutput
// (EqAudioOutput).
// ============================================================================

class Minimp3Decoder : public AudioDecoder {
public:
    Minimp3Decoder();
    virtual ~Minimp3Decoder() override;
    bool begin(AudioSource* source, AudioOutput* output) override;
    bool loop() override;
    bool isRunning() override;
    bool stop() override;
    void desync() override;

private:
    void* _mp3dec;    // mp3dec_t* allocated on the heap
    void* _scratch;   // mp3dec_scratch_t* allocated once in PSRAM (~16 KB)
    uint8_t* _inBuf;  // rolling MP3 input buffer (16 KB)
    int16_t* _pcmBuf; // decoded PCM output
    uint32_t _inLen;
    uint32_t _inPos;
    bool     _eof;
    int      _lastRate; // detect sample-rate changes
    uint32_t _pcmSamples; // decoded samples pending in _pcmBuf
    uint32_t _pcmPos;     // next sample index to feed to the output
    int      _pcmChannels; // channels of the pending decoded frame
    uint64_t _totalPcmSamples; // cumulative decoded samples per channel (diagnostics)
    uint32_t _startMs;         // wall clock when begin() ran (diagnostics)
    uint64_t _sourceBytes;     // bytes pulled from the source (diagnostics)
    uint64_t _consumedBytes;   // bytes consumed by decoded MP3 frames (diagnostics)
    uint32_t _id3Remain;       // bytes left of a leading ID3v2 tag being skipped
    uint32_t _lastStatMs;      // wall clock of the last periodic progress report
    void reportStats(const char* reason); // log decode stats on any termination
};

#endif
#endif