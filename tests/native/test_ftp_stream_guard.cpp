// Native tests for the FTP upload lock scope (chatgpt/src/ftp_stream_guard.h).
//
// Context (live .9, 2026-11-12): the FTP monitor held the SD mutex for the whole
// scan+upload cycle — 79 s, 94 s, 110 s — and the SD watchdog force-released it
// at 120 s. The lock must cover storage access only: one scan, or one chunk
// read. The network write must never run with the lock held.
//
// These tests drive the production helper with an injected clock and a fake
// storage, and simulate the transfer loop shape from ftpStoreStream
// (read a chunk -> write it to the socket, repeatedly).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "ftp_stream_guard.h"

#include <vector>

namespace {

// Simulated storage + clock. readCostMs models a slow SD read; sendCostMs is
// the network write time (tens to hundreds of ms per chunk on a real link).
struct FakeStorage {
    size_t total = 0;          // bytes to serve
    size_t served = 0;
    uint32_t nowMs = 0;
    uint32_t readCostMs = 4;
    uint32_t sendCostMs = 250;
    bool busy = false;         // lock acquisition refused

    bool held = false;
    uint32_t heldSince = 0;
    uint32_t maxHoldMs = 0;
    uint32_t holds = 0;
    uint32_t unlockedWithoutLock = 0;
    uint32_t holdsDuringSend = 0;
};

bool fakeLock(void* ctx) {
    FakeStorage* s = (FakeStorage*)ctx;
    if (s->busy) return false;
    REQUIRE_FALSE(s->held);           // recursive nesting would hide the bug
    s->held = true;
    s->heldSince = s->nowMs;
    s->holds++;
    return true;
}

void fakeUnlock(void* ctx) {
    FakeStorage* s = (FakeStorage*)ctx;
    if (!s->held) { s->unlockedWithoutLock++; return; }
    const uint32_t heldFor = s->nowMs - s->heldSince;
    if (heldFor > s->maxHoldMs) s->maxHoldMs = heldFor;
    s->held = false;
}

int fakeRead(void* ctx, uint8_t* buf, size_t len) {
    FakeStorage* s = (FakeStorage*)ctx;
    s->nowMs += s->readCostMs;        // the SD read itself
    if (s->served >= s->total) return 0;
    size_t n = s->total - s->served;
    if (n > len) n = len;
    for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)(s->served + i);
    s->served += n;
    return (int)n;
}

// Mirrors ftpStoreStream: reader() then write the chunk to the data socket.
void fakeTransfer(FakeStorage& st, size_t chunkCap) {
    const FtpSourceOps ops{ fakeLock, fakeUnlock, fakeRead, &st };
    std::vector<uint8_t> chunk(chunkCap);
    while (st.served < st.total) {
        if (st.held) st.holdsDuringSend++;
        size_t got = ftpReadChunkScoped(ops, chunk.data(), chunk.size());
        if (!got) break;
        if (st.held) st.holdsDuringSend++;   // lock leaked into the send path
        st.nowMs += st.sendCostMs;            // network write, lock must be free
    }
}

// The pre-fix behaviour: lock once for the whole transfer.
void legacyTransfer(FakeStorage& st, size_t chunkCap) {
    st.held = true;
    st.heldSince = st.nowMs;
    st.holds++;
    std::vector<uint8_t> chunk(chunkCap);
    while (st.served < st.total) {
        int got = fakeRead(&st, chunk.data(), chunk.size());
        if (got <= 0) break;
        st.nowMs += st.sendCostMs;
    }
    const uint32_t heldFor = st.nowMs - st.heldSince;
    if (heldFor > st.maxHoldMs) st.maxHoldMs = heldFor;
    st.held = false;
}

} // namespace

TEST_CASE("legacy whole-cycle hold exceeds the watchdog (the bug this fixes)") {
    // 4 MB cap, 8 KB chunks, slow link: exactly the .9 shape.
    FakeStorage st;
    st.total = 4u * 1024u * 1024u;
    legacyTransfer(st, 8192);
    CHECK(st.maxHoldMs > FTP_SCOPE_WATCH_WARN_MS);   // would warn / be force-reset
    CHECK(st.maxHoldMs > 120000u);                   // the observed 79-123 s holds
    CHECK(ftpHoldWithinWatchdog(st.maxHoldMs) == false);
}

TEST_CASE("scoped read: lock held only for the read, never across the write") {
    FakeStorage st;
    st.total = 4u * 1024u * 1024u;
    fakeTransfer(st, 8192);

    CHECK(st.served == st.total);                    // whole file still transferred
    CHECK(st.holds == 512);                          // one hold per 8 KB chunk
    CHECK(st.holdsDuringSend == 0);                  // never held during a write
    CHECK(st.held == false);                         // nothing left locked
    CHECK(st.unlockedWithoutLock == 0);              // balanced lock/unlock
    CHECK(st.maxHoldMs == st.readCostMs);            // hold == one read, not the file
    CHECK(ftpHoldWithinWatchdog(st.maxHoldMs));      // orders of magnitude below 30 s
}

TEST_CASE("scoped read scales with the link, not with the file") {
    // A very slow link (2 s per 8 KB write) must not lengthen any hold.
    FakeStorage st;
    st.total = 1024u * 1024u;
    st.sendCostMs = 2000;
    fakeTransfer(st, 8192);
    CHECK(st.served == st.total);
    CHECK(st.maxHoldMs == st.readCostMs);
    CHECK(st.holdsDuringSend == 0);
}

TEST_CASE("busy storage: no read, no unlock, no lock leak") {
    FakeStorage st;
    st.total = 64u * 1024u;
    st.busy = true;
    const FtpSourceOps ops{ fakeLock, fakeUnlock, fakeRead, &st };
    uint8_t buf[8192];

    CHECK(ftpReadChunkScoped(ops, buf, sizeof(buf)) == 0);
    CHECK(st.served == 0);                           // nothing read while busy
    CHECK(st.holds == 0);
    CHECK(st.held == false);
    CHECK(st.unlockedWithoutLock == 0);              // same rule as ftpCtxUnlock

    // The lock becomes free on a later cycle: the same ops work again.
    st.busy = false;
    CHECK(ftpReadChunkScoped(ops, buf, sizeof(buf)) == 8192);
    CHECK(st.served == 8192);
    CHECK(st.held == false);
}

TEST_CASE("no lock needed (PSRAM source): read still works, nothing unlocked") {
    FakeStorage st;
    st.total = 16u * 1024u;
    const FtpSourceOps ops{ nullptr, nullptr, fakeRead, &st };
    uint8_t buf[4096];
    CHECK(ftpReadChunkScoped(ops, buf, sizeof(buf)) == 4096);
    CHECK(st.unlockedWithoutLock == 0);
    CHECK(st.held == false);
}
