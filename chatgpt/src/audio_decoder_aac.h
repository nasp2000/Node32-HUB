#pragma once

#include "feature_flags.h"

#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

#include "audio_core.h"

// ============================================================================
// AacDecoder — first-party AAC decoder wrapper.
//
// Implements the AudioDecoder interface (begin/loop/isRunning/stop) on top of
// the Espressif esp_audio_codec AAC decoder (LicenseRef-Espressif-Modified-MIT,
// ESP32-only — OK for this project). ADTS frame extraction is first-party
// (adts_parser.h).
//
// Memory: the ADTS input buffer and the decoded PCM output are allocated in
// PSRAM (heap_caps_malloc SPIRAM). The decoder's own internal state (~51 KB)
// is allocated by the precompiled lib in internal RAM and lives only while the
// decoder is open (freed on stop()).
// ============================================================================

class AacDecoder : public AudioDecoder {
public:
    AacDecoder();
    virtual ~AacDecoder() override;
    bool begin(AudioSource* source, AudioOutput* output) override;
    bool loop() override;
    bool isRunning() override;
    bool stop() override;
    void desync() override;

private:
    void*  _aac;         // esp_audio_dec_handle_t (internal RAM, freed on stop)
    uint8_t* _inBuf;     // ADTS input buffer (PSRAM, 4 KB)
    int16_t* _pcmBuf;    // decoded PCM output (PSRAM)
    uint32_t _inLen;     // bytes currently in _inBuf
    uint32_t _inPos;     // read offset within _inBuf
    bool     _eof;       // source exhausted
    int      _lastRate;  // sample-rate change detection
    int      _lastCh;    // channel-count change detection
    uint32_t _pcmSamples; // decoded samples pending in _pcmBuf
    uint32_t _pcmPos;     // next sample index to feed to the output
    int      _pcmChannels; // channels of the pending decoded frame
};

#endif
#endif
