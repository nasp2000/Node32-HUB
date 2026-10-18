#pragma once

#include <stdint.h>
#include <stddef.h>

// ============================================================================
// wav_parser.h — first-party RIFF/WAV header parser.
//
// Extracts the format (sample rate, channels, bits per sample) and the data
// chunk offset/size from an uncompressed PCM WAV stream. Supports canonical
// RIFF WAVE files with a 'fmt ' chunk followed by a 'data' chunk; skips any
// interleaved chunks (LIST, INFO, etc.).
// ============================================================================

namespace wav {

struct Format {
    bool     valid;        // header parsed OK
    uint32_t sampleRate;
    uint16_t channels;
    uint16_t bitsPerSample;
    uint16_t blockAlign;   // bytes per frame
    uint32_t dataOffset;   // byte offset of the 'data' chunk payload
    uint32_t dataSize;     // payload bytes
};

// Consume a little-endian u32/u16 from a buffer.
inline uint32_t rdU32(const uint8_t* b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

inline uint16_t rdU16(const uint8_t* b) {
    return (uint16_t)(b[0] | (b[1] << 8));
}

// Parse the header from the first `len` bytes of the file.
// Returns a Format struct; `len` must cover at least 44 bytes (12 RIFF + 8 fmt
// header + 16 fmt body + 8 data header) for a canonical file.
inline Format parse(const uint8_t* buf, size_t len) {
    Format f;
    f.valid = false;
    f.sampleRate = 0;
    f.channels = 0;
    f.bitsPerSample = 0;
    f.blockAlign = 0;
    f.dataOffset = 0;
    f.dataSize = 0;
    if (!buf || len < 44) return f;

    // RIFF header: "RIFF" .... "WAVE".
    if (buf[0] != 'R' || buf[1] != 'I' || buf[2] != 'F' || buf[3] != 'F') return f;
    if (buf[8] != 'W' || buf[9] != 'A' || buf[10] != 'V' || buf[11] != 'E') return f;

    // Walk chunks from offset 12 until the 'data' chunk.
    uint32_t off = 12;
    while (off + 8 <= (uint32_t)len) {
        uint32_t chunkId = rdU32(buf + off);
        uint32_t chunkSize = rdU32(buf + off + 4);
        if (chunkId == 0x20746D66 /* "fmt " */ && chunkSize >= 16) {
            if (off + 8 + chunkSize > (uint32_t)len) return f; // fmt truncated
            uint16_t audioFmt = rdU16(buf + off + 8);
            if (audioFmt != 1 /* PCM */) return f; // no compressed formats here
            f.channels = rdU16(buf + off + 10);
            f.sampleRate = rdU32(buf + off + 12);
            f.blockAlign = rdU16(buf + off + 20);
            f.bitsPerSample = rdU16(buf + off + 22);
        } else if (chunkId == 0x61746164 /* "data" */) {
            f.dataOffset = off + 8;
            f.dataSize = chunkSize;
            if (f.channels == 0 || f.sampleRate == 0 || f.bitsPerSample == 0) {
                return f; // data before fmt (or invalid fmt) — refuse
            }
            f.valid = true;
            return f;
        }
        // Advance past this chunk (chunks are word-aligned).
        off += 8 + chunkSize + (chunkSize & 1);
    }
    return f; // 'data' chunk not found within the window
}

} // namespace wav
