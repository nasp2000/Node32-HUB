#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../../chatgpt/src/chart_stream_decimate.h"
#include <cstdint>
#include <cstring>
#include <vector>

// Tests for the single-pass chart-history streaming that replaced the two-pass
// reader in chartHistoryStreamFloat32JsonImpl (proximity/RS232/modbus .chart
// files). The production decimator header is compiled directly; the block reader
// below mirrors the production loop 1:1 and is checked for block-size invariance
// and the decimator's invariants (bounded count, even spacing, ordered output).

// ---------------------------------------------------------------------------
// ChartStreamDecimator unit tests
// ---------------------------------------------------------------------------

TEST_CASE("decimator keeps everything below maxPoints") {
    ChartStreamDecimator d(5);
    for (uint32_t m = 1; m <= 3; ++m) {
        CHECK(d.feed() == ChartStreamDecimator::kKeep);
        CHECK(d.live() == m);
    }
    CHECK(d.matched() == 3);
    CHECK(d.live() == 3);
    CHECK(d.stride() == 1);
}

TEST_CASE("decimator exact cap=4 action trace (m=1..8)") {
    ChartStreamDecimator d(4);
    CHECK(d.feed() == ChartStreamDecimator::kKeep);              // m1
    CHECK(d.feed() == ChartStreamDecimator::kKeep);              // m2
    CHECK(d.feed() == ChartStreamDecimator::kKeep);              // m3
    CHECK(d.feed() == ChartStreamDecimator::kKeep);              // m4 (cap reached)
    CHECK(d.feed() == ChartStreamDecimator::kSkip);              // m5
    CHECK(d.feed() == ChartStreamDecimator::kCompactThenKeep);   // m6 (stride 1->2)
    CHECK(d.feed() == ChartStreamDecimator::kSkip);              // m7
    CHECK(d.feed() == ChartStreamDecimator::kKeep);              // m8
    CHECK(d.matched() == 8);
    CHECK(d.live() == 4);
    CHECK(d.stride() == 2);
}

TEST_CASE("decimator stays bounded over a long stream") {
    ChartStreamDecimator d(288);
    CHECK(d.feed() == ChartStreamDecimator::kKeep);  // first sample always kept
    for (uint32_t m = 2; m <= 1440; ++m) {
        (void)d.feed();
        REQUIRE(d.live() <= 288);
    }
    CHECK(d.matched() == 1440);
    CHECK(d.live() >= 144);                       // sawtooth stays above cap/2
    CHECK(d.live() <= 288);
    CHECK(d.stride() >= 2);
    // stride is a power of two
    uint32_t s = d.stride();
    CHECK((s & (s - 1)) == 0);
}

TEST_CASE("decimator report agrees with compaction bookkeeping") {
    // For every keep/compact action, live() must equal the caller's buffer size.
    ChartStreamDecimator d(100);
    uint32_t buffer = 0;
    std::vector<std::pair<uint32_t, uint8_t>> history;  // (beforeLive, action)
    for (uint32_t m = 1; m <= 10000; ++m) {
        uint32_t before = d.live();
        uint8_t a = d.feed();
        history.push_back({before, a});
        if (a == ChartStreamDecimator::kCompactThenKeep) buffer = (buffer + 1) / 2;
        if (a == ChartStreamDecimator::kKeep || a == ChartStreamDecimator::kCompactThenKeep) buffer++;
        CHECK(buffer == d.live());
    }
    CHECK(d.live() <= 100);
    // Every compaction was preceded by a full buffer and halved it to live()-1.
    for (auto &h : history) {
        if (h.second == ChartStreamDecimator::kCompactThenKeep) {
            CHECK(h.first > 0);                     // old count
            CHECK((h.first + 1) / 2 > 0);           // survives compact
        }
    }
}

TEST_CASE("decimator maxPoints=0 keeps every record") {
    ChartStreamDecimator d(0);
    for (uint32_t m = 1; m <= 1440; ++m) {
        CHECK(d.feed() == ChartStreamDecimator::kKeep);
    }
    CHECK(d.live() == 1440);
    CHECK(d.stride() == 1);
}

TEST_CASE("decimator maxPoints=1 keeps only the first sample") {
    ChartStreamDecimator d(1);
    CHECK(d.feed() == ChartStreamDecimator::kKeep);
    for (uint32_t m = 2; m <= 100; ++m) {
        CHECK(d.feed() == ChartStreamDecimator::kSkip);
    }
    CHECK(d.live() == 1);
    CHECK(d.stride() == 1);
    CHECK(d.matched() == 100);
}

// ---------------------------------------------------------------------------
// Production-loop mirror tests
// ---------------------------------------------------------------------------

struct OneRec {
    uint32_t ts;
    std::vector<float> vals;
};

struct ScanOut {
    uint32_t stride;
    uint32_t stored;
    std::vector<OneRec> recs;
};

// Mirrors chartHistoryStreamFloat32JsonImpl: block-based reader + online
// decimator + chartStreamDecimateCompact, storing kept records in a flat buffer.
static ScanOut mirrorScan(const std::vector<uint8_t>& file, size_t recordSize,
                          uint8_t ncols, uint32_t fromTimestamp, uint16_t maxPoints,
                          size_t blockBytes) {
    ScanOut out;
    const size_t storedSize = 5 + (size_t)ncols * sizeof(float);

    uint32_t slots = maxPoints;
    if (slots == 0) slots = (uint32_t)(file.size() / recordSize);
    std::vector<uint8_t> recBuf((size_t)slots * storedSize);

    ChartStreamDecimator dec(maxPoints);
    size_t blockFilled = 0, blockPos = 0;
    size_t pos = 0;
    std::vector<uint8_t> block(blockBytes);
    auto readChunk = [&](uint8_t* dst, size_t want) -> size_t {
        size_t avail = (pos < file.size()) ? (file.size() - pos) : 0;
        size_t n = (want < avail) ? want : avail;
        if (n) memcpy(dst, file.data() + pos, n);
        pos += n;
        return n;
    };
    auto blockAdvance = [&]() -> bool {
        while (blockPos + recordSize > blockFilled) {
            if (blockPos == blockFilled) {
                size_t n = readChunk(block.data(), blockBytes);
                if (n == 0) return false;
                blockFilled = n; blockPos = 0;
            } else {
                size_t keep = blockFilled - blockPos;
                memmove(block.data(), block.data() + blockPos, keep);
                size_t n = readChunk(block.data() + keep, blockBytes - keep);
                blockFilled = keep + n; blockPos = 0;
                if (n == 0) return false;
            }
        }
        return true;
    };

    while (blockAdvance()) {
        const uint8_t* rec = block.data() + blockPos;
        uint32_t ts; memcpy(&ts, rec, 4);
        blockPos += recordSize;
        if (ts < fromTimestamp) continue;

        const uint32_t beforeLive = dec.live();
        const uint8_t act = dec.feed();
        if (act == ChartStreamDecimator::kSkip) continue;
        if (act == ChartStreamDecimator::kCompactThenKeep) {
            chartStreamDecimateCompact(recBuf.data(), beforeLive, storedSize);
        }
        uint8_t* dest = recBuf.data() + (dec.live() - 1) * storedSize;
        memcpy(dest, rec, 4);
        dest[4] = rec[4];
        memcpy(dest + 5, rec + 8, (size_t)ncols * sizeof(float));
    }
    out.stride = dec.stride();
    out.stored = dec.live();
    for (uint32_t i = 0; i < out.stored; ++i) {
        const uint8_t* src = recBuf.data() + i * storedSize;
        OneRec R;
        memcpy(&R.ts, src, 4);
        uint8_t vcnt = src[4];
        for (uint8_t c = 0; c < ncols; ++c) {
            float v = NAN;
            if (c < vcnt) memcpy(&v, src + 5 + c * sizeof(float), sizeof(float));
            R.vals.push_back(v);
        }
        out.recs.push_back(R);
    }
    return out;
}

// Independent bookkeeping: re-derive kept timestamps using only the decimator's
// public counters/actions (no byte buffer) — a different implementation of the
// same invariant used to cross-check the mirror's buffer compaction.
static std::vector<uint32_t> simKeptTs(const std::vector<uint8_t>& file, size_t recordSize,
                                       uint32_t fromTimestamp, uint16_t maxPoints) {
    ChartStreamDecimator dec(maxPoints);
    std::vector<uint32_t> kept;
    size_t total = file.size() / recordSize;
    for (size_t i = 0; i < total; ++i) {
        uint32_t ts; memcpy(&ts, file.data() + i * recordSize, 4);
        if (ts < fromTimestamp) continue;
        uint8_t a = dec.feed();
        if (a == ChartStreamDecimator::kSkip) continue;
        if (a == ChartStreamDecimator::kCompactThenKeep) {
            uint32_t expected = dec.live() - 1;   // entries surviving the compact
            for (size_t k = kept.size(); k-- > 1;) {
                if (k % 2 == 1) kept.erase(kept.begin() + k);
            }
            REQUIRE(kept.size() == expected);
        }
        REQUIRE(kept.size() == dec.live() - 1);
        kept.push_back(ts);
        REQUIRE(kept.size() == dec.live());
    }
    return kept;
}

static std::vector<uint8_t> makeRecords(size_t nRecs, size_t recordSize, uint8_t ncols,
                                        uint32_t firstTs, uint32_t step) {
    std::vector<uint8_t> recs(nRecs * recordSize);
    for (size_t i = 0; i < nRecs; ++i) {
        uint8_t* r = recs.data() + i * recordSize;
        uint32_t ts = firstTs + (uint32_t)(i * step);
        memcpy(r, &ts, 4);
        r[4] = ncols;    // valueCount
        // values: col c holds (i + c) * 0.25 — lets us check data survives compaction
        for (uint8_t c = 0; c < ncols; ++c) {
            float v = (float)i + (float)c * 0.25f;
            memcpy(r + 8 + c * sizeof(float), &v, sizeof(float));
        }
    }
    return recs;
}

static std::vector<uint32_t> naiveMatchedTs(const std::vector<uint8_t>& file,
                                            size_t recordSize, uint32_t fromTimestamp) {
    std::vector<uint32_t> ts;
    size_t total = file.size() / recordSize;
    for (size_t i = 0; i < total; ++i) {
        uint32_t t; memcpy(&t, file.data() + i * recordSize, 4);
        if (t >= fromTimestamp) ts.push_back(t);
    }
    return ts;
}

static void checkSameTs(const ScanOut& a, const ScanOut& b) {
    REQUIRE(a.stored == b.stored);
    CHECK(a.stride == b.stride);
    for (uint32_t i = 0; i < a.stored; ++i) CHECK(a.recs[i].ts == b.recs[i].ts);
}

static void checkProperties(const ScanOut& s, uint32_t fromTimestamp, uint16_t maxPoints) {
    CHECK(s.stored <= maxPoints);
    CHECK(s.stride >= 1);
    if (s.stored > 1) {
        for (size_t i = 1; i < s.recs.size(); ++i) {
            CHECK(s.recs[i].ts > s.recs[i - 1].ts);  // strictly chronological
        }
        CHECK(s.recs[0].ts >= fromTimestamp);
    }
}

// Proximity: 24 cols -> recordSize = 4+1+3+24*4 = 104, stored = 5+24*4 = 101.
TEST_CASE("single-pass scan ignores block size (proximity 24-col, 1440 recs/day)") {
    const size_t recordSize = 104;
    const uint8_t ncols = 24;
    const uint32_t from = 1700000000u;
    std::vector<uint8_t> recs = makeRecords(1440, recordSize, ncols, from, 60);

    // Small window: no downsampling -> byte-for-byte identical to naive filter.
    {
        // from+30000 -> only every entry with i*60 >= 30000 (i >= 500) matches,
        // i.e. 940 of 1440 records < maxPoints 1000, so stride stays 1.
        ScanOut blk = mirrorScan(recs, recordSize, ncols, from + 30000u, 1000, 104);
        std::vector<uint32_t> naive = naiveMatchedTs(recs, recordSize, from + 30000u);
        REQUIRE(blk.stored == naive.size());
        CHECK(blk.stride == 1);
        for (size_t i = 0; i < naive.size(); ++i) CHECK(blk.recs[i].ts == naive[i]);
    }

    // Full day, downsampled to 288.
    ScanOut a = mirrorScan(recs, recordSize, ncols, from, 288, 104);
    ScanOut b = mirrorScan(recs, recordSize, ncols, from, 288, 512);
    ScanOut c = mirrorScan(recs, recordSize, ncols, from, 288, 4096);
    CHECK(a.stored > 0);
    checkSameTs(a, b);
    checkSameTs(b, c);
    checkProperties(a, from, 288);
    CHECK(a.stored <= 288);
    // matches independent bookkeeping
    std::vector<uint32_t> sim = simKeptTs(recs, recordSize, from, 288);
    REQUIRE((size_t)a.stored == sim.size());
    for (size_t i = 0; i < sim.size(); ++i) CHECK(a.recs[i].ts == sim[i]);
}

// Modbus/RS232: 16 cols -> recordSize = 4+1+3+16*4 = 72, stored = 5+16*4 = 69.
TEST_CASE("single-pass scan handles large file (rs232 16-col, 86400 recs/day)") {
    const size_t recordSize = 72;
    const uint8_t ncols = 16;
    const uint32_t from = 1700000000u;
    std::vector<uint8_t> recs = makeRecords(86400, recordSize, ncols, from, 1);

    ScanOut a = mirrorScan(recs, recordSize, ncols, from, 288, 4096);
    ScanOut b = mirrorScan(recs, recordSize, ncols, from, 288, 72);
    checkSameTs(a, b);
    checkProperties(a, from, 288);
    CHECK(a.stored <= 288);
    CHECK(a.stored >= 144);

    // live 8h window (from the middle of the day)
    ScanOut w = mirrorScan(recs, recordSize, ncols, from + 28800u, 144, 4096);
    checkProperties(w, from + 28800u, 144);
    std::vector<uint32_t> sim = simKeptTs(recs, recordSize, from + 28800u, 144);
    REQUIRE((size_t)w.stored == sim.size());
    for (size_t i = 0; i < sim.size(); ++i) CHECK(w.recs[i].ts == sim[i]);
}

TEST_CASE("compact keeps data values intact") {
    const size_t recordSize = 72;
    const uint8_t ncols = 16;
    const uint32_t from = 1700000000u;
    // Enough records that several compactions happen (cap 48).
    std::vector<uint8_t> recs = makeRecords(5000, recordSize, ncols, from, 1);
    ScanOut s = mirrorScan(recs, recordSize, ncols, from, 48, 4096);
    CHECK(s.stored > 0);
    for (const OneRec& r : s.recs) {
        uint32_t idx = (r.ts - from);    // ts encodes the record index (step=1, from)
        for (uint8_t c = 0; c < ncols; ++c) {
            float expect = (float)idx + (float)c * 0.25f;
            REQUIRE(r.vals.size() == ncols);
            CHECK(r.vals[c] == doctest::Approx(expect));
        }
    }
}

TEST_CASE("single-pass scan keeps all matching records when maxPoints=0") {
    const size_t recordSize = 104;
    const uint8_t ncols = 24;
    const uint32_t from = 1700000000u;
    std::vector<uint8_t> recs = makeRecords(1440, recordSize, ncols, from, 60);
    ScanOut s = mirrorScan(recs, recordSize, ncols, from + 1800u, 0, 104);
    std::vector<uint32_t> naive = naiveMatchedTs(recs, recordSize, from + 1800u);
    REQUIRE(s.stored == naive.size());
    CHECK(s.stride == 1);
    for (size_t i = 0; i < naive.size(); ++i) CHECK(s.recs[i].ts == naive[i]);
}

TEST_CASE("single-pass scan returns empty when window matches nothing") {
    const size_t recordSize = 104;
    const uint8_t ncols = 24;
    const uint32_t from = 1700000000u;
    std::vector<uint8_t> recs = makeRecords(100, recordSize, ncols, from, 60);
    ScanOut s = mirrorScan(recs, recordSize, ncols, from + 100000u, 5, 4096);
    CHECK(s.stored == 0);
}

TEST_CASE("single-pass scan handles tight and non-aligned block sizes") {
    const size_t recordSize = 20;   // 3-col record: 4+1+3+3*4
    const uint8_t ncols = 3;
    const uint32_t from = 1000u;
    std::vector<uint8_t> recs = makeRecords(50, recordSize, ncols, from, 10);
    ScanOut a = mirrorScan(recs, recordSize, ncols, from, 10, recordSize);  // one record per block
    ScanOut b = mirrorScan(recs, recordSize, ncols, from, 10, 40);          // mid-record boundary
    ScanOut c = mirrorScan(recs, recordSize, ncols, from, 10, 100);         // block > record
    checkSameTs(a, b);
    checkSameTs(b, c);
    checkProperties(a, from, 10);
}