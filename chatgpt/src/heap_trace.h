#pragma once

// Boot-time heap checkpoints — MEASURED, not estimated.
//
// /status already shows a per-module table ("Internal Heap Analysis"), but its
// numbers are compile-time guesses in system_status_payload.h ("static estimates
// per compiled module"). This module records the real free-heap at each boot
// step, so the same table can show Measured next to Estimated and the leftover
// "Δ = dynamic heap" bucket stops being anonymous.
//
// Cost: HEAP_TRACE_MAX * sizeof(HeapTracePoint) of .bss (~16 * 24 B = 384 B),
// no allocations, one call per checkpoint. Read back via GET /api/status
// (fields heap_trace[] / dma_free / dma_largest).
//
// Usage: heapTraceReset() first thing in setup(), heapTraceMark("label") after
// each subsystem init. Deltas are (previous free - this free) = what that step
// consumed.

#include <Arduino.h>

// 28 (was 16, 20, 24): A0/A0-b split the boot into more steps (network/NTP/web
// routes/begin), A5 adds five inside the WiFi bring-up and A3 four inside the
// applyRS232Config preamble; the WAVESHARE build (ethernet + SD + proximity)
// records the most marks and must not drop the tail.
#define HEAP_TRACE_MAX 28

struct HeapTracePoint {
    const char* label;    // checkpoint name (static string)
    uint32_t heapFree;    // ESP.getFreeHeap() — the number compared across builds
    uint32_t dmaFree;     // heap_caps_get_free_size(MALLOC_CAP_DMA) — WiFi/SD/DMA pool
    uint32_t largestInt;  // heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)
    uint32_t psramFree;   // heap_caps_get_free_size(MALLOC_CAP_SPIRAM)
};

// Clears the trace (call once, at the very start of setup()).
void heapTraceReset();

// Records a checkpoint. Labels are stored by pointer: pass a string literal.
// Duplicate labels are allowed (the first one wins for heapTraceFind).
void heapTraceMark(const char* label);

// Number of checkpoints recorded so far (<= HEAP_TRACE_MAX).
int heapTraceCount();

// Checkpoint accessors (nullptr when index is out of range).
const HeapTracePoint* heapTraceAt(int i);

// Bytes consumed between checkpoint i-1 and i (0 for i == 0, and 0 when the
// heap grew). Positive = internal heap used by that step.
int32_t heapTraceDeltaAt(int i);

// Lookup by label (exact match). nullptr when that checkpoint did not run.
const HeapTracePoint* heapTraceFind(const char* label);

// Bytes consumed by the step that ends at `label` (0 when absent).
int32_t heapTraceDeltaFor(const char* label);

// Bytes consumed between two checkpoints, both inclusive of their own steps:
// free(from) - free(to), clamped at 0. Lets a coarse bucket (e.g. the whole web
// server init) keep reporting its total after finer marks are inserted inside
// it. 0 when either label is absent or the heap grew.
int32_t heapTraceDeltaSpan(const char* fromLabel, const char* toLabel);
