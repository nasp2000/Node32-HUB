#pragma once

#include "feature_flags.h"

#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

#include "audio_core.h"
#include "ogg_parser.h"

// ============================================================================
// OggOpusDecoder — first-party OGG Opus decoder.
//
// Implements the AudioDecoder interface (begin/loop/isRunning/stop) in two
// stages: the first-party ogg::Reader (ogg_parser.h) de-pages the OGG stream
// into packets, and the libopus core — via the vendored Espressif
// esp_audio_codec OPUS backend (esp_opus_dec_*, Espressif Modified MIT,
// MIT-compatible, same licensing as the AAC path) — decodes each audio
// packet to 16-bit PCM.
//
// Packet layout (RFC 7845): index 0 = OpusHead (channels + pre-skip),
// index 1 = OpusTags (skipped), index >= 2 = audio. The OpusHead pre-skip
// samples are dropped from the start of the decoded output. Opus always
// decodes at 48 kHz; mono is duplicated to stereo for the I2S output.
//
// Memory: rolling OGG read chunk (4 KB) + one audio packet slot (2 KB) +
// decoded PCM (120 ms @48 kHz stereo = ~23 KB), all PSRAM-first. The
// decoder's own internal state lives only while open (freed on stop()).
// ============================================================================

class OggOpusDecoder : public AudioDecoder {
 public:
    OggOpusDecoder();
    virtual ~OggOpusDecoder() override;
    bool begin(AudioSource* source, AudioOutput* output) override;
    bool loop() override;
    bool isRunning() override;
    bool stop() override;
    void desync() override;

 private:
    bool openInner(const opus::Head& head);
    bool pumpPackets();  // read source -> Reader -> stage one audio packet

    void* _opus;          // esp_audio_dec_handle_t (freed on stop)
    ogg::Reader _reader;  // OGG de-pager (owns an 8 KB reassembly buffer)
    uint8_t* _chunkBuf;   // OGG read chunk from the source (PSRAM, 4 KB)
    uint8_t* _pktBuf;     // staged audio packet (PSRAM, 2 KB)
    size_t _pktLen;       // staged packet length (0 = none staged)
    bool _pktReady;       // a fresh audio packet awaits decode
    int16_t* _pcmBuf;     // decoded PCM output (PSRAM)
    uint32_t _pcmSamples;  // decoded samples pending in _pcmBuf
    uint32_t _pcmPos;      // next sample index to feed to the output
    int _pcmChannels;      // channels of the pending decoded frame
    uint32_t _preSkipLeft;  // pre-skip samples still to drop (48 kHz domain)
    uint32_t _serial;       // OGG serial of the open stream (chain detect)
    bool _opened;           // esp decoder open for the current chain
    bool _eof;              // source exhausted
    uint8_t _channels;      // channels from OpusHead (1 or 2)
};

#endif
#endif
