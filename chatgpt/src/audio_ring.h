#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// ============================================================================
// audio_ring.h — small PSRAM/RAM ring buffer + producer pump, pure and
// self-contained (no ESP32 dependencies) so the same code runs on-device
// (audio_source_sd_ring.cpp) and in native doctest builds.
//
// Capacity must be a power of two: wrap arithmetic is a mask. One slot is
// always kept empty so `space()` can distinguish "full" from "empty".
// Producer (fill task) and consumer (decode loop) are separate tasks; the
// index fields are volatile exactly like the HTTP ring source.
// ============================================================================

class RingBuffer {
public:
    RingBuffer() : _buf(nullptr), _cap(0), _mask(0), _rd(0), _wr(0) {}

    // Attach a pre-allocated buffer. capPow2 must be a power of two.
    void init(uint8_t* buf, uint32_t capPow2) {
        _buf = buf;
        _cap = capPow2;
        _mask = capPow2 - 1;
        _rd = 0;
        _wr = 0;
    }

    uint8_t* raw() const { return _buf; }
    uint32_t cap() const { return _cap; }
    uint32_t used() const { return (_wr - _rd) & _mask; }
    uint32_t space() const { return (_rd - _wr - 1) & _mask; }
    void reset() { _rd = 0; _wr = 0; }

    // Write up to `n` bytes, handling wrap-around. Returns bytes written.
    uint32_t write(const uint8_t* src, uint32_t n) {
        if (!_buf || n == 0) return 0;
        uint32_t avail = space();
        if (n > avail) n = avail;
        uint32_t written = 0;
        while (n) {
            uint32_t idx = _wr & _mask;
            uint32_t span = _cap - idx;
            if (span > n) span = n;
            memcpy(_buf + idx, src, span);
            _wr += span;
            src += span;
            n -= span;
            written += span;
        }
        return written;
    }

    // Read up to `n` bytes, handling wrap-around. Returns bytes read.
    uint32_t read(uint8_t* dst, uint32_t n) {
        if (!_buf || n == 0) return 0;
        uint32_t avail = used();
        if (n > avail) n = avail;
        uint32_t got = 0;
        while (n) {
            uint32_t idx = _rd & _mask;
            uint32_t span = _cap - idx;
            if (span > n) span = n;
            memcpy(dst, _buf + idx, span);
            _rd += span;
            dst += span;
            n -= span;
            got += span;
        }
        return got;
    }

    // Direct producer access: a single contiguous free region starts at
    // raw()+writeIndex() and is writeSpan() bytes long. The producer writes
    // into it then advances with commitWrite() (used by ringFillFrom so a
    // file/socket can read straight into the ring with no extra copy).
    uint32_t writeIndex() const { return _wr & _mask; }
    uint32_t writeSpan() const {
        if (!_buf) return 0;
        uint32_t sp = space();
        uint32_t toEnd = _cap - writeIndex();
        return (sp < toEnd) ? sp : toEnd;
    }
    void commitWrite(uint32_t n) { _wr += n; }

private:
    uint8_t* _buf;
    uint32_t _cap;
    uint32_t _mask;
    volatile uint32_t _rd;
    volatile uint32_t _wr;
};

// Producer callback for ringFillFrom: read up to `len` bytes into `buf`.
// Returns bytes read (>0), 0 = temporarily unavailable (retry later), -1 =
// end of stream (no more bytes will ever come).
typedef int (*RingReadFn)(void* ctx, uint8_t* buf, uint32_t len);

// Fill the ring from a producer until the ring is full, the producer signals
// EOF, or `maxTotal` bytes have been written (maxTotal == 0 = unbounded).
// `maxChunk` caps each producer call so SD/network mutexes are held for short
// bursts. Short reads (>0 but less than requested) just keep the loop going —
// the caller drains and calls again. Returns bytes written; *eofOut is set 1
// when the producer hit EOF.
inline uint32_t ringFillFrom(RingBuffer& ring, RingReadFn readFn, void* ctx,
                             uint32_t maxChunk, uint32_t maxTotal, int* eofOut) {
    if (eofOut) *eofOut = 0;
    if (!ring.raw()) return 0;
    uint32_t total = 0;
    while (ring.space() > 0 && (maxTotal == 0 || total < maxTotal)) {
        uint32_t span = ring.writeSpan();
        if (span > maxChunk) span = maxChunk;
        if (maxTotal && total + span > maxTotal) span = maxTotal - total;
        if (span == 0) break;
        int n = readFn(ctx, ring.raw() + ring.writeIndex(), span);
        if (n < 0) {
            if (eofOut) *eofOut = 1;
            return total;
        }
        if (n == 0) return total;   // pause — retry on the next call
        ring.commitWrite((uint32_t)n);
        total += (uint32_t)n;
    }
    return total;
}
