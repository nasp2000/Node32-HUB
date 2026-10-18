#pragma once

#include "feature_flags.h"

#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

#include "audio_core.h"

// ============================================================================
// FlacDecoder — first-party FLAC decoder.
//
// Implements the AudioDecoder interface (begin/loop/isRunning/stop) using
// dr_flac (public domain / Unlicense). Compatible with AudioSource
// (s_buffered) and AudioOutput (EqAudioOutput).
// ============================================================================

class FlacDecoder : public AudioDecoder {
public:
    FlacDecoder();
    virtual ~FlacDecoder() override;
    bool begin(AudioSource* source, AudioOutput* output) override;
    bool loop() override;
    bool isRunning() override;
    bool stop() override;
    void desync() override;

private:
    void*  _flac;        // drflac* handle (allocated in begin)
    int* _pcmBuf;        // decoded PCM (s32 interleaved) for one chunk
    uint32_t _pcmFrames; // total frames in the decoded chunk
    uint32_t _pcmPos;    // next frame to feed to the output
    int      _lastRate;  // detect rate changes
    bool     _eof;
};

#endif
#endif