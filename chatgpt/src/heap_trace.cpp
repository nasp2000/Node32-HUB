// heap_trace.cpp — boot-time heap checkpoints (measured), see heap_trace.h.
//
// Kept in its own translation unit (not a header-only static) because
// system_status_payload.h is included from several TUs (ethernet.cpp + the web
// handlers) while the checkpoints are recorded from the setup/impl TU.

#include "heap_trace.h"

#include <esp_heap_caps.h>

static HeapTracePoint s_trace[HEAP_TRACE_MAX];
static int s_count = 0;

void heapTraceReset() {
    s_count = 0;
    memset(s_trace, 0, sizeof(s_trace));
}

void heapTraceMark(const char* label) {
    if (s_count >= HEAP_TRACE_MAX) return;
    HeapTracePoint& p = s_trace[s_count++];
    p.label      = label ? label : "";
    p.heapFree   = (uint32_t)ESP.getFreeHeap();
    p.dmaFree    = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_DMA);
    p.largestInt = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    p.psramFree  = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

int heapTraceCount() {
    return s_count;
}

const HeapTracePoint* heapTraceAt(int i) {
    if (i < 0 || i >= s_count) return nullptr;
    return &s_trace[i];
}

int32_t heapTraceDeltaAt(int i) {
    if (i <= 0 || i >= s_count) return 0;
    const uint32_t prev = s_trace[i - 1].heapFree;
    const uint32_t cur  = s_trace[i].heapFree;
    return (cur <= prev) ? (int32_t)(prev - cur) : 0;
}

const HeapTracePoint* heapTraceFind(const char* label) {
    if (!label) return nullptr;
    for (int i = 0; i < s_count; i++) {
        if (s_trace[i].label && strcmp(s_trace[i].label, label) == 0) return &s_trace[i];
    }
    return nullptr;
}

int32_t heapTraceDeltaFor(const char* label) {
    if (!label) return 0;
    for (int i = 0; i < s_count; i++) {
        if (s_trace[i].label && strcmp(s_trace[i].label, label) == 0) return heapTraceDeltaAt(i);
    }
    return 0;
}
