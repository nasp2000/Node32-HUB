// lang_gzip_tx.h — truncation accounting for the /api/lang gzip response.
//
// BACKLOG SD-11: one crawl recorded `net::ERR_CONTENT_LENGTH_MISMATCH` on a
// fetch that most likely served the pre-compressed language JSON (~48 KB
// declared) while the board reported `pressure=shed`. The client got fewer
// bytes than the declared Content-Length and nothing on the device recorded it.
//
// The gzip response is a static PROGMEM buffer served by
// AsyncProgmemResponse, which tracks how many content bytes it handed to TCP
// (`_sentLength`) and whether it reached RESPONSE_END. When the connection
// closes we compare those with the declared length:
//
//   complete && sent == declared  -> finished, nothing to report
//   !complete || sent <  declared -> the transfer stopped short: count it
//
// `!complete` matters on its own: the library counts a byte as "sent" when it
// is copied into the send buffer, so an OOM stall (the per-response send buffer
// could not be allocated) leaves sent == declared while the body never reached
// the socket. That is exactly the class of truncation to watch under heap
// pressure.
//
// Pure (stdint/stdio only) so it is unit-tested on the host:
// tests/native/test_lang_gzip_tx.cpp.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct LangGzipTxStats {
    uint32_t served;         // gzip responses started
    uint32_t completed;      // reached RESPONSE_END with the full body
    uint32_t incomplete;     // stopped short (client gone early or OOM stall)
    uint32_t lastDeclared;   // declared Content-Length of the last truncation
    uint32_t lastSent;       // content bytes the response counted as sent
    uint32_t lastFreeHeap;   // heap sampled when that response started
    uint32_t lastMinFreeHeap;
    uint32_t lastMaxAlloc;
};

static inline void langGzipTxInit(LangGzipTxStats *s) {
    if (!s) return;
    s->served = 0;
    s->completed = 0;
    s->incomplete = 0;
    s->lastDeclared = 0;
    s->lastSent = 0;
    s->lastFreeHeap = 0;
    s->lastMinFreeHeap = 0;
    s->lastMaxAlloc = 0;
}

// Records one finished transfer. Returns true when it was truncated (the caller
// logs it). `freeHeap`/`minFreeHeap`/`maxAlloc` are sampled at serve time, so
// the log reproduces the heap state that made the send fragile.
static inline bool langGzipTxRecord(LangGzipTxStats *s, uint32_t declared, uint32_t sent,
                                    bool complete, uint32_t freeHeap, uint32_t minFreeHeap,
                                    uint32_t maxAlloc) {
    if (!s) return false;
    s->served++;
    const bool truncated = (!complete) || (sent < declared);
    if (!truncated) {
        s->completed++;
        return false;
    }
    s->incomplete++;
    // Keep the figures of the most recent truncation only: they are what the
    // log line and /api/health report.
    s->lastDeclared = declared;
    s->lastSent = sent;
    s->lastFreeHeap = freeHeap;
    s->lastMinFreeHeap = minFreeHeap;
    s->lastMaxAlloc = maxAlloc;
    return true;
}

// One-line report for the serial / web monitor. Returns snprintf()'s would-be
// length (buffer always NUL-terminated).
static inline int langGzipTxFormat(char *out, size_t cap, const LangGzipTxStats *s) {
    if (!out || cap == 0 || !s) return 0;
    const uint32_t shortfall =
        (s->lastDeclared > s->lastSent) ? (s->lastDeclared - s->lastSent) : 0;
    return snprintf(out, cap,
                    "[WEB] /api/lang gzip truncated: declared=%lu sent=%lu shortfall=%lu "
                    "free_heap=%lu min_free=%lu max_alloc=%lu (incomplete=%lu/%lu)",
                    (unsigned long)s->lastDeclared, (unsigned long)s->lastSent,
                    (unsigned long)shortfall, (unsigned long)s->lastFreeHeap,
                    (unsigned long)s->lastMinFreeHeap, (unsigned long)s->lastMaxAlloc,
                    (unsigned long)s->incomplete, (unsigned long)s->served);
}
