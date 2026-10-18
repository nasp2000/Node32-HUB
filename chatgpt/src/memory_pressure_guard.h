#ifndef MEMORY_PRESSURE_GUARD_H
#define MEMORY_PRESSURE_GUARD_H

#include <Arduino.h>
#include <esp_heap_caps.h>

enum MemoryPressureLevel : uint8_t {
    MEMORY_PRESSURE_NORMAL = 0,
    MEMORY_PRESSURE_WARN = 1,
    MEMORY_PRESSURE_SHED = 2,
    MEMORY_PRESSURE_CRITICAL = 3
};

struct MemoryPressureGuardState {
    volatile uint8_t level;
    volatile uint32_t freeHeap;
    volatile uint32_t maxAllocHeap;
    volatile uint32_t lastUpdateMs;
    volatile uint32_t levelSinceMs;
    volatile uint32_t lastTransitionMs;
};

extern MemoryPressureGuardState gMemoryPressureState;

static constexpr size_t MEMORY_PRESSURE_WARN_ENTER_FREE = 110U * 1024U;
static constexpr size_t MEMORY_PRESSURE_WARN_EXIT_FREE = 128U * 1024U;
static constexpr size_t MEMORY_PRESSURE_SHED_ENTER_FREE = 92U * 1024U;
static constexpr size_t MEMORY_PRESSURE_SHED_EXIT_FREE = 112U * 1024U;
static constexpr size_t MEMORY_PRESSURE_CRITICAL_ENTER_FREE = 72U * 1024U;
static constexpr size_t MEMORY_PRESSURE_CRITICAL_EXIT_FREE = 88U * 1024U;

static constexpr size_t MEMORY_PRESSURE_WARN_ENTER_ALLOC = 72U * 1024U;
static constexpr size_t MEMORY_PRESSURE_WARN_EXIT_ALLOC = 88U * 1024U;
static constexpr size_t MEMORY_PRESSURE_SHED_ENTER_ALLOC = 56U * 1024U;
static constexpr size_t MEMORY_PRESSURE_SHED_EXIT_ALLOC = 68U * 1024U;
static constexpr size_t MEMORY_PRESSURE_CRITICAL_ENTER_ALLOC = 40U * 1024U;
static constexpr size_t MEMORY_PRESSURE_CRITICAL_EXIT_ALLOC = 52U * 1024U;

static inline uint8_t memoryPressureFragmentationPct(size_t freeHeap, size_t maxAllocHeap) {
    if (freeHeap == 0 || maxAllocHeap >= freeHeap) return 0;
    return (uint8_t)(((freeHeap - maxAllocHeap) * 100U) / freeHeap);
}

static inline const char *memoryPressureLevelName(MemoryPressureLevel level) {
    switch (level) {
        case MEMORY_PRESSURE_WARN: return "warn";
        case MEMORY_PRESSURE_SHED: return "shed";
        case MEMORY_PRESSURE_CRITICAL: return "critical";
        default: return "normal";
    }
}

static inline MemoryPressureLevel memoryPressureCurrentLevel() {
    return (MemoryPressureLevel)gMemoryPressureState.level;
}

static inline MemoryPressureLevel memoryPressureEvaluateLevel(size_t freeHeap,
                                                              size_t maxAllocHeap,
                                                              MemoryPressureLevel previousLevel) {
    const uint8_t fragPct = memoryPressureFragmentationPct(freeHeap, maxAllocHeap);

    MemoryPressureLevel candidate = MEMORY_PRESSURE_NORMAL;
    if (freeHeap < MEMORY_PRESSURE_WARN_ENTER_FREE ||
        maxAllocHeap < MEMORY_PRESSURE_WARN_ENTER_ALLOC ||
        fragPct >= 52U) {
        candidate = MEMORY_PRESSURE_WARN;
    }
    if (freeHeap < MEMORY_PRESSURE_SHED_ENTER_FREE ||
        maxAllocHeap < MEMORY_PRESSURE_SHED_ENTER_ALLOC ||
        fragPct >= 62U) {
        candidate = MEMORY_PRESSURE_SHED;
    }
    if (freeHeap < MEMORY_PRESSURE_CRITICAL_ENTER_FREE ||
        maxAllocHeap < MEMORY_PRESSURE_CRITICAL_ENTER_ALLOC ||
        fragPct >= 74U) {
        candidate = MEMORY_PRESSURE_CRITICAL;
    }

    if (previousLevel == MEMORY_PRESSURE_CRITICAL && candidate < MEMORY_PRESSURE_CRITICAL) {
        if (freeHeap < MEMORY_PRESSURE_CRITICAL_EXIT_FREE ||
            maxAllocHeap < MEMORY_PRESSURE_CRITICAL_EXIT_ALLOC ||
            fragPct >= 66U) {
            return MEMORY_PRESSURE_CRITICAL;
        }
    }
    if (previousLevel >= MEMORY_PRESSURE_SHED && candidate < MEMORY_PRESSURE_SHED) {
        if (freeHeap < MEMORY_PRESSURE_SHED_EXIT_FREE ||
            maxAllocHeap < MEMORY_PRESSURE_SHED_EXIT_ALLOC ||
            fragPct >= 56U) {
            return MEMORY_PRESSURE_SHED;
        }
    }
    if (previousLevel >= MEMORY_PRESSURE_WARN && candidate < MEMORY_PRESSURE_WARN) {
        if (freeHeap < MEMORY_PRESSURE_WARN_EXIT_FREE ||
            maxAllocHeap < MEMORY_PRESSURE_WARN_EXIT_ALLOC ||
            fragPct >= 46U) {
            return MEMORY_PRESSURE_WARN;
        }
    }

    return candidate;
}

static inline bool memoryPressureUpdateSample(size_t freeHeap,
                                              size_t maxAllocHeap,
                                              unsigned long nowMs) {
    MemoryPressureLevel previous = memoryPressureCurrentLevel();
    MemoryPressureLevel next = memoryPressureEvaluateLevel(freeHeap, maxAllocHeap, previous);

    gMemoryPressureState.freeHeap = (uint32_t)freeHeap;
    gMemoryPressureState.maxAllocHeap = (uint32_t)maxAllocHeap;
    gMemoryPressureState.lastUpdateMs = nowMs;

    if (next != previous) {
        gMemoryPressureState.level = (uint8_t)next;
        gMemoryPressureState.levelSinceMs = nowMs;
        gMemoryPressureState.lastTransitionMs = nowMs;
        return true;
    }

    if (gMemoryPressureState.levelSinceMs == 0) {
        gMemoryPressureState.levelSinceMs = nowMs;
    }
    return false;
}

static inline void memoryPressureRefreshFromSystem(unsigned long minIntervalMs = 1000UL) {
    unsigned long nowMs = millis();
    if (gMemoryPressureState.lastUpdateMs != 0 &&
        (nowMs - gMemoryPressureState.lastUpdateMs) < minIntervalMs) {
        return;
    }
    memoryPressureUpdateSample(ESP.getFreeHeap(), ESP.getMaxAllocHeap(), nowMs);
}

static inline bool memoryPressureShouldRejectHeavyRequest() {
    return memoryPressureCurrentLevel() >= MEMORY_PRESSURE_SHED;
}

static inline unsigned long memoryPressureSuggestedRetryMs() {
    switch (memoryPressureCurrentLevel()) {
        case MEMORY_PRESSURE_CRITICAL: return 5000UL;
        case MEMORY_PRESSURE_SHED: return 2500UL;
        case MEMORY_PRESSURE_WARN: return 1000UL;
        default: return 750UL;
    }
}

// ============================================================================
// Controlled allocation panic policy (Phase 5.1)
// ============================================================================

// Optional hook invoked right before abort(). Set from the main TU to save
// panic context (heap, uptime, pressure level) to NVS before crashing.
extern void (*gMemoryPressurePanicHook)(const char* msg);

[[noreturn]] static inline void memoryPressurePanic(const char* msg) {
    Serial.printf("\n*** MEMORY PANIC *** %s\n", msg);
    Serial.printf("*** Free heap: %u  Max alloc: %u  Pressure: %s\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                  memoryPressureLevelName(memoryPressureCurrentLevel()));
    Serial.println("*** System halted. Check PSRAM/SRAM budget. ***");
    Serial.flush();
    if (gMemoryPressurePanicHook) {
        gMemoryPressurePanicHook(msg);
    }
    abort();
}

static inline void* memoryPressureSafeMalloc(size_t size, uint32_t caps, const char* label) {
    void* ptr = heap_caps_malloc(size, caps);
    if (!ptr) {
        char buf[160];
        snprintf(buf, sizeof(buf), "heap_caps_malloc(%u,0x%x) failed for %s",
                 (unsigned)size, (unsigned)caps, label ? label : "?");
        memoryPressurePanic(buf);
    }
    return ptr;
}

static inline void* memoryPressureSafeCalloc(size_t n, size_t size, uint32_t caps, const char* label) {
    void* ptr = heap_caps_calloc(n, size, caps);
    if (!ptr) {
        char buf[160];
        snprintf(buf, sizeof(buf), "heap_caps_calloc(%u,%u,0x%x) failed for %s",
                 (unsigned)n, (unsigned)size, (unsigned)caps, label ? label : "?");
        memoryPressurePanic(buf);
    }
    return ptr;
}

#endif