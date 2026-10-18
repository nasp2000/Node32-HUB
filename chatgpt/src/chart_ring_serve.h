// chart_ring_serve.h — pure PSRAM-ring serve math for .chart endpoints (header-only).
//
// Standard (all packs, all sources): every .chart API serves PSRAM-first when
// the volatile ring covers the requested range, else the persisted file
// (SD 1-min records, or the LittleFS 30-min mirror at the IDENTICAL path),
// else whatever the ring holds (partial), else empty. Same rule for
// proximity, Modbus and RS232 — no source behaves differently without SD.
//
// Pure / dependency-free (stdint only) so it is unit-tested on the host
// (tests/native/test_chart_ring_serve.cpp). The firmware wrappers live next
// to the rings (data_sd_pipeline_impl.h) and the file streams
// (chart_history.cpp); this header only decides stride + picked indices.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Stride + emitted count for `matchCount` ring points under a `maxOut`
// budget. Same semantics as the RS232 live path and ChartStreamDecimator:
// stride = ceil(matches/maxOut), kept = every stride-th point (oldest-first).
// maxOut == 0 means "no budget" (nothing served, stride 1).
static inline void chartRingServePlan(uint32_t matchCount, uint32_t maxOut,
                                      uint32_t* strideOut, uint32_t* countOut) {
    uint32_t stride = 1, count = matchCount;
    if (maxOut == 0) {
        count = 0;
    } else if (matchCount > maxOut) {
        stride = (matchCount + maxOut - 1) / maxOut;
        count = (matchCount + stride - 1) / stride;
    }
    if (strideOut) *strideOut = stride;
    if (countOut) *countOut = count;
}

// Fill `idxOut` (cap `idxCap` entries) with the ring-relative indices of the
// kept points for the plan above. Returns the number of indices written
// (never more than idxCap; extra points are dropped from the OLDEST side so
// the tail — the most recent data — always survives).
static inline uint32_t chartRingServePick(uint32_t matchCount, uint32_t stride,
                                          uint32_t* idxOut, uint32_t idxCap) {
    if (!idxOut || idxCap == 0 || matchCount == 0) return 0;
    if (stride == 0) stride = 1;
    uint32_t total = (matchCount + stride - 1) / stride;
    uint32_t skip = (total > idxCap) ? (total - idxCap) : 0;
    uint32_t n = 0;
    for (uint32_t k = skip; k < total && n < idxCap; k++) {
        idxOut[n++] = k * stride;
    }
    return n;
}

// True when the ring (oldest timestamp `ringOldest`, `ringSize` points)
// covers the whole requested window [fromTs, nowTs]. Empty rings never cover.
static inline bool chartRingCoversRange(uint32_t ringOldest, uint32_t ringSize,
                                        uint32_t fromTs, uint32_t nowTs) {
    if (ringSize == 0) return false;
    if (fromTs != 0 && ringOldest > fromTs) return false;
    (void)nowTs;
    return true;
}

// One merged point (file history + ring tail share this layout).
#define CHART_SERVE_MAX_COLS 32
typedef struct {
    uint32_t timestamp;
    uint8_t  valueCount;
    float    values[CHART_SERVE_MAX_COLS];
} ChartServePoint;

// Merge file history (ascending) with the ring tail (ascending) into `out`
// (ascending, room for `cap` points). Ring wins equal timestamps. The tail is
// dropped from the OLDEST side when the union exceeds `cap`, so the most
// recent minute is always served. Returns the merged count.
static inline uint32_t chartMergePoints(const ChartServePoint* filePts, uint32_t nFile,
                                        const ChartServePoint* ringPts, uint32_t nRing,
                                        ChartServePoint* out, uint32_t cap) {
    if (!out || cap == 0) return 0;
    uint32_t i = 0, j = 0, n = 0;
    // Skip heads so the union fits cap (oldest goes first).
    uint32_t total = nFile + nRing;
    uint32_t skip = (total > cap) ? (total - cap) : 0;
    while (skip > 0) {
        if (j >= nRing || (i < nFile && filePts[i].timestamp <= ringPts[j].timestamp)) { i++; }
        else { j++; }
        skip--;
    }
    while (i < nFile || j < nRing) {
        if (j >= nRing || (i < nFile && filePts[i].timestamp < ringPts[j].timestamp)) {
            out[n++] = filePts[i++];
        } else if (i < nFile && filePts[i].timestamp == ringPts[j].timestamp) {
            out[n++] = ringPts[j++]; // ring wins ties
            i++;
        } else {
            out[n++] = ringPts[j++];
        }
    }
    return n;
}

#ifdef __cplusplus
}
#endif
