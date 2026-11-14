// rs232_chart_serve.h — pure pieces of the RS232 chart serve path.
//
// Every .chart API follows the same standard (ARCHITECTURE §6.1): persisted
// history (SD, or the LittleFS mirror at the IDENTICAL path) + the volatile
// PSRAM ring tail, merged into one series and decimated once over the union.
// Modbus and proximity already share one serve function; RS232 had the same
// logic copy-pasted into the WiFi and the Ethernet handler, with no way to
// unit-test it (BACKLOG SD-9).
//
// This header holds the RS232-specific half — live point conversion, column
// derivation and column keys — so the firmware wrappers only gather the data.
// The union itself is chartMergePoints() (chart_ring_serve.h).
//
// Dependency-free (stdint/math/stdio) so it runs on the host.
#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "chart_ring_serve.h"

// Convert one PSRAM live point into the common serve layout.
// Returns false when the point falls outside [fromTs, untilTs); 0 means
// unbounded on that side. The ring always holds "now", so a request for a
// PAST day must pass untilTs (end of that day) — otherwise the response gets
// yesterday's file plus today's tail and the chart jumps across the gap.
// Columns beyond the live schema are NaN (never 0 — a 0 would plot as a real
// reading), and valueCount is capped to the serve layout.
template <class LivePointT>
inline bool rs232ChartCopyLivePoint(const LivePointT& pt, uint32_t fromTs, uint32_t untilTs,
                                    ChartServePoint& out, uint8_t liveMaxCols) {
    if (fromTs != 0 && (uint32_t)pt.timestamp < fromTs) return false;
    if (untilTs != 0 && (uint32_t)pt.timestamp >= untilTs) return false;
    out.timestamp = (uint32_t)pt.timestamp;
    out.valueCount = (pt.valueCount > CHART_SERVE_MAX_COLS) ? CHART_SERVE_MAX_COLS : pt.valueCount;
    for (uint8_t c = 0; c < CHART_SERVE_MAX_COLS; c++) {
        out.values[c] = (c < liveMaxCols) ? pt.values[c] : NAN;
    }
    return true;
}

// Column count for the response: the live schema when it has one, else the
// widest record seen in the file or the tail, clamped to `maxCols`.
inline uint8_t rs232ChartResolveCols(uint8_t colsFromLive,
                                     const ChartServePoint* filePts, uint16_t nFile,
                                     const ChartServePoint* tailPts, uint16_t nTail,
                                     uint8_t maxCols) {
    uint8_t cols = colsFromLive;
    if (cols == 0) {
        for (uint16_t i = 0; i < nFile; i++) {
            if (filePts[i].valueCount > cols) cols = filePts[i].valueCount;
        }
        for (uint16_t i = 0; i < nTail; i++) {
            if (tailPts[i].valueCount > cols) cols = tailPts[i].valueCount;
        }
    }
    return (cols > maxCols) ? maxCols : cols;
}

// Stable column keys/decimals for the RS232 series (rs232_col1..N, 4 decimals).
inline void rs232ChartFillKeys(uint8_t nCols, char (*keys)[16], int8_t* decs) {
    for (uint8_t c = 0; c < nCols; c++) {
        snprintf(keys[c], 16, "rs232_col%u", (unsigned)(c + 1));
        decs[c] = 4;
    }
}
