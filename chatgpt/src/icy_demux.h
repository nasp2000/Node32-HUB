#pragma once

#include <stdint.h>
#include <stddef.h>

// icy_demux — pure Shoutcast/Icecast ICY metadata demuxer.
//
// An ICY stream interleaves: [metaint audio bytes][length byte][metadata
// block][metaint audio bytes][length byte][...]. Each metadata block is
// preceded by a single length byte that is a multiple of 16; the block itself
// is `length` bytes of "Key='value';" pairs, usually StreamTitle='Artist -
// Title'. The stream therefore starts with `metaint` audio bytes BEFORE the
// first length byte.
//
// This module is deliberately free of ESP32 dependencies so the same code
// runs on-device (audio_source_http_ring.cpp) and in native doctest builds.
namespace icy_demux {

// Extract the value of StreamTitle='...' from a raw ICY metadata block.
// Returns the number of characters written (0 when no StreamTitle present).
// `out` is always NUL-terminated when outCap > 0.
int parseTitle(const uint8_t* block, size_t len, char* out, size_t outCap);

// Incremental demuxer that strips the interleaved metadata blocks from a raw
// HTTP byte stream, forwarding only the audio bytes to the caller.
class Demux {
public:
    Demux();

    // (Re)configure the interleave. metaint == 0 disables stripping (pure
    // audio passthrough). An ICY stream always begins with `metaint` audio
    // bytes BEFORE the first length byte (ICY spec), so the demux starts by
    // forwarding `metaint` audio bytes and only then expects a length byte.
    void reset(uint32_t metaint);

    bool active() const { return _metaint > 0; }

    // Feed raw stream bytes. Audio bytes are copied to `out` (up to `outCap`).
    // When a complete metadata block arrives, its StreamTitle is parsed into
    // `title` (if the block carried one) and *metaSeen is set true. If several
    // blocks complete within one call, only the LAST one is surfaced (the
    // caller feeds small socket slices, so at most one block per call in
    // practice; the last title in a window is the current song).
    // Returns the number of audio bytes written to `out`.
    uint32_t feed(const uint8_t* in, size_t len,
                  uint8_t* out, size_t outCap,
                  char* title, size_t titleCap,
                  bool* metaSeen);

private:
    enum State { S_AUDIO = 0, S_META_LEN = 1, S_META_DATA = 2 };

    uint32_t _metaint;
    uint8_t _state;
    uint32_t _audioLeft;   // audio bytes still expected before the next block
    uint16_t _metaLen;     // length of the metadata block being consumed
    uint16_t _metaGot;     // bytes of the current block consumed so far
    uint8_t _metaBuf[512]; // rolling capture of the current block (title is at its start)
};

} // namespace icy_demux
