#pragma once

#include <stdint.h>
#include <stddef.h>

// ============================================================================
// adts_parser.h — first-party ADTS frame parser for AAC-LC/HE-AAC streams.
//
// Radio/SD AAC sources carry AAC data wrapped in ADTS headers (0xFFF sync).
// This parser finds the next frame boundary in a byte stream and decodes the
// per-frame header fields (sample rate, channel count, frame length) needed
// by the AAC decoder wrapper.
// ============================================================================

namespace adts {

// Sample-rate index table (ADTS sampling_frequency_index -> Hz).
inline uint32_t sampleRateForIndex(uint8_t idx) {
    static const uint32_t kRates[] = {
        96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
        16000, 12000, 11025, 8000,  7350,  0,     0,     0,
    };
    if (idx > 15) return 0;
    return kRates[idx];
}

// Result of parsing a single ADTS frame header.
struct Frame {
    bool     valid;      // header parsed OK
    uint16_t frameLength; // total frame length including the ADTS header
    uint32_t sampleRate;  // Hz (0 if unknown index)
    uint8_t  channels;    // 1 = mono, 2 = stereo
    uint8_t  profile;     // 0=AAC Main, 1=AAC LC, 2=SSR, 3=LTP
    uint8_t  headerLen;   // bytes consumed by the ADTS header (7 or 9)
};

// Scan `buf` (len bytes) for the next ADTS sync (0xFFF) and parse its header.
// Returns the frame info and the byte offset of the sync word in *outOffset.
inline Frame findAndParse(const uint8_t* buf, size_t len, size_t* outOffset) {
    Frame f;
    f.valid = false;
    f.frameLength = 0;
    f.sampleRate = 0;
    f.channels = 0;
    f.profile = 0;
    f.headerLen = 0;
    if (outOffset) *outOffset = 0;
    if (!buf || len < 7) return f;

    for (size_t i = 0; i + 7 <= len; i++) {
        // Sync: first 12 bits must be 0xFFF.
        if (buf[i] != 0xFF || (buf[i + 1] & 0xF0) != 0xF0) continue;
        // layer field must be '00' (bits 3-2 of byte 1).
        if ((buf[i + 1] & 0x06) != 0x00) continue;
        // protection_absent bit (bit 0 of byte 1): 1 = no CRC (7-byte header).
        const uint8_t hasCrc = buf[i + 1] & 0x01;

        const uint8_t profileRaw = (buf[i + 2] >> 6) & 0x03;   // profile_ObjectType-1
        const uint8_t sfIdx      = (buf[i + 2] >> 2) & 0x0F;   // sampling_frequency_index
        const uint8_t chanCfg    = ((buf[i + 2] & 0x01) << 2) | (buf[i + 3] >> 6); // channel_configuration

        f.valid = true;
        f.profile = (uint8_t)(profileRaw + 1);
        f.sampleRate = sampleRateForIndex(sfIdx);
        f.channels = (chanCfg == 1) ? 1 : (chanCfg == 2) ? 2 : (uint8_t)chanCfg;
        f.headerLen = (hasCrc ? 7 : 9);

        // frame_length: 13 bits spanning byte 3 (bits 1-0), byte 4, byte 5 (bits 7-5).
        f.frameLength = (uint16_t)(((buf[i + 3] & 0x03) << 11) |
                                   (buf[i + 4] << 3) |
                                   (buf[i + 5] >> 5));
        if (outOffset) *outOffset = i;
        return f;
    }
    return f;
}

// Scan-only variant: returns the offset of the next sync word without parsing
// (used to discard ID3/garbage between frames). Returns (size_t)-1 if none.
inline size_t findSync(const uint8_t* buf, size_t len) {
    if (!buf) return (size_t)-1;
    for (size_t i = 0; i + 1 < len; i++) {
        if (buf[i] == 0xFF && (buf[i + 1] & 0xF0) == 0xF0) return i;
    }
    return (size_t)-1;
}

} // namespace adts
