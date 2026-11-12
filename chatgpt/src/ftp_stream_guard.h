// ftp_stream_guard.h — storage-lock scope for the FTP upload stream.
//
// ftpRunCycle used to hold the SD mutex for the WHOLE scan+upload cycle and
// ftpUploadFileImpl held it across ftpStoreStream, i.e. across every network
// write. On .9 the monitor task therefore held the lock for 79 s, 94 s and
// 110 s and was force-reset by the SD watchdog at 120 s ("Mutex recovered;
// reboot advisable") — the watchdog was right, the hold was too long.
//
// The invariant now: storage is touched only while the lock is held, and the
// lock is NEVER held across a network write. Maximum contiguous hold is one
// 8 KB chunk read (a few ms), whatever the file size or link speed.
//
// This header is FreeRTOS-free so the scope rule can be unit-tested natively
// with an injected clock; ftp_client.cpp supplies the real lock/read calls.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Longest hold the SD watchdog tolerates before it logs (30 s) / recovers
// (120 s). Keeping every FTP hold orders of magnitude below this is the point.
static const uint32_t FTP_SCOPE_WATCH_WARN_MS = 30000UL;

struct FtpSourceOps {
    // Storage lock: true when acquired (bounded wait), false when busy.
    bool (*lock)(void* ctx);
    void (*unlock)(void* ctx);
    // Reads up to len bytes; <= 0 means EOF/error.
    int (*read)(void* ctx, uint8_t* buf, size_t len);
    void* ctx;
};

// Reads one chunk with the storage lock held for the read only.
// Returns bytes read (0 = EOF, error or lock busy). On a busy lock nothing is
// read and nothing is unlocked, so the caller can retry the whole file later
// without leaving the mutex state inconsistent.
inline size_t ftpReadChunkScoped(const FtpSourceOps& ops, uint8_t* buf, size_t len) {
    if (!ops.read || !buf || len == 0) return 0;
    if (ops.lock) {
        if (!ops.lock(ops.ctx)) return 0;  // busy: no read, no unlock
        int got = ops.read(ops.ctx, buf, len);
        if (ops.unlock) ops.unlock(ops.ctx);
        return got > 0 ? (size_t)got : 0;
    }
    int got = ops.read(ops.ctx, buf, len);
    return got > 0 ? (size_t)got : 0;
}

// True when a hold is short enough for the watchdog never to warn.
inline bool ftpHoldWithinWatchdog(uint32_t holdMs) {
    return holdMs < FTP_SCOPE_WATCH_WARN_MS;
}
