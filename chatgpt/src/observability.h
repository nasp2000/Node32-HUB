#ifndef OBSERVABILITY_H
#define OBSERVABILITY_H

#include <Arduino.h>
#include <time.h>
#include <stdio.h>

#include "log.h"

// This module is intentionally header-only.
// It includes log.h directly so inline helpers can always use the compact event logger.

// ============================================================================
// Module health tracking (Phase 5.2) — per-module last-alive timestamps
// ============================================================================
enum ModuleHealthId : uint8_t {
    HEALTH_ID_HEARTBEAT = 0,
    HEALTH_ID_SENSORS = 1,
    HEALTH_ID_NETWORK = 2,
    HEALTH_ID_CONFIG = 3,
    HEALTH_ID_SCHEDULER = 4,
    HEALTH_ID_EMAIL = 5,
    HEALTH_ID_RS232 = 6,
    HEALTH_ID_PROXIMITY = 7,
    HEALTH_ID_GRBL = 8,
    HEALTH_ID_USBIP = 9,
    HEALTH_ID_CAMERA = 10,
    HEALTH_ID_CAN = 11,
    HEALTH_ID_COUNT = 12,
    HEALTH_ID_MAX = 16
};

struct ModuleHealthState {
    volatile uint32_t lastAliveMs[HEALTH_ID_MAX];
    uint32_t timeoutMs[HEALTH_ID_MAX];
    bool essential[HEALTH_ID_MAX];
    bool initialized;
};

extern ModuleHealthState gModuleHealth;

static inline void healthModuleInit() {
    if (gModuleHealth.initialized) return;
    gModuleHealth.initialized = true;
    for (uint8_t i = 0; i < HEALTH_ID_MAX; i++) {
        gModuleHealth.lastAliveMs[i] = 0;
        gModuleHealth.essential[i] = false;
        gModuleHealth.timeoutMs[i] = 0;
    }
    // Define timeouts (ms) and essential flag per module
    gModuleHealth.timeoutMs[HEALTH_ID_HEARTBEAT]  = 10000;
    gModuleHealth.essential[HEALTH_ID_HEARTBEAT]   = true;
    gModuleHealth.timeoutMs[HEALTH_ID_SENSORS]     = 60000;
    gModuleHealth.essential[HEALTH_ID_SENSORS]      = false;
    gModuleHealth.timeoutMs[HEALTH_ID_NETWORK]     = 30000;
    gModuleHealth.essential[HEALTH_ID_NETWORK]       = false;
    gModuleHealth.timeoutMs[HEALTH_ID_CONFIG]      = 30000;
    gModuleHealth.essential[HEALTH_ID_CONFIG]        = false;
    gModuleHealth.timeoutMs[HEALTH_ID_SCHEDULER]   = 30000;
    gModuleHealth.essential[HEALTH_ID_SCHEDULER]     = false;
    gModuleHealth.timeoutMs[HEALTH_ID_EMAIL]       = 30000;
    gModuleHealth.essential[HEALTH_ID_EMAIL]         = false;
    gModuleHealth.timeoutMs[HEALTH_ID_RS232]       = 5000;
    gModuleHealth.essential[HEALTH_ID_RS232]         = false;
    gModuleHealth.timeoutMs[HEALTH_ID_PROXIMITY]   = 30000;
    gModuleHealth.essential[HEALTH_ID_PROXIMITY]     = false;
    gModuleHealth.timeoutMs[HEALTH_ID_GRBL]        = 10000;
    gModuleHealth.essential[HEALTH_ID_GRBL]          = false;
    gModuleHealth.timeoutMs[HEALTH_ID_USBIP]       = 10000;
    gModuleHealth.essential[HEALTH_ID_USBIP]         = false;
    gModuleHealth.timeoutMs[HEALTH_ID_CAMERA]      = 10000;
    gModuleHealth.essential[HEALTH_ID_CAMERA]        = false;
    gModuleHealth.timeoutMs[HEALTH_ID_CAN]         = 10000;
    gModuleHealth.essential[HEALTH_ID_CAN]           = false;
}

static inline void healthModuleAlive(ModuleHealthId id) {
    if (id < HEALTH_ID_MAX) {
        gModuleHealth.lastAliveMs[id] = millis();
    }
}

static inline bool healthModuleIsAlive(ModuleHealthId id, unsigned long nowMs) {
    if (id >= HEALTH_ID_MAX) return true;
    if (gModuleHealth.timeoutMs[id] == 0) return true;
    return (nowMs - gModuleHealth.lastAliveMs[id]) < gModuleHealth.timeoutMs[id];
}

static inline bool healthModulesCheckAll(unsigned long nowMs) {
    bool allOk = true;
    for (uint8_t i = 0; i < HEALTH_ID_COUNT; i++) {
        if (gModuleHealth.essential[i] && !healthModuleIsAlive((ModuleHealthId)i, nowMs)) {
            allOk = false;
        }
    }
    return allOk;
}

struct ObservabilityState {
    bool initialized;
    bool lastWifiStaConnected;
    bool lastApActive;
    bool lastEthernetActive;
    bool lastSdMounted;
    bool lastSdEnabled;
    char lastSsid[33];
    char lastIp[24];
    size_t minHeapSeen;
    size_t minPsramSeen;
    uint32_t networkTransitions;
    uint32_t storageTransitions;
    uint32_t heapAlerts;
    uint32_t schedulerOk;
    uint32_t schedulerFail;
    uint32_t emailOk;
    uint32_t emailFail;
    uint32_t configOk;
    uint32_t configFail;
    int lastHeapBucket;
    int lastSummaryDayKey;
    unsigned long lastTickMs;
};

static ObservabilityState gObservability = {};

static inline int observabilityDayKeyNow() {
    time_t now = time(nullptr);
    if (now <= 1000) return -1;
    struct tm info;
    localtime_r(&now, &info);
    return (info.tm_year + 1900) * 10000 + (info.tm_mon + 1) * 100 + info.tm_mday;
}

static inline int observabilityHeapBucket(size_t freeHeap) {
    const size_t kb = freeHeap / 1024U;
    if (kb <= 16U) return 4;
    if (kb <= 24U) return 3;
    if (kb <= 32U) return 2;
    if (kb <= 48U) return 1;
    return 0;
}

static inline void observabilityResetDailyCounters() {
    gObservability.networkTransitions = 0;
    gObservability.storageTransitions = 0;
    gObservability.heapAlerts = 0;
    gObservability.schedulerOk = 0;
    gObservability.schedulerFail = 0;
    gObservability.emailOk = 0;
    gObservability.emailFail = 0;
    gObservability.configOk = 0;
    gObservability.configFail = 0;
}

static inline void observabilityLogBootSnapshot(const char* firmwareVersion,
                                                const char* systemName,
                                                const char* primaryIp,
                                                bool sdEnabled,
                                                bool sdMounted,
                                                bool psramAvailable,
                                                bool rs232Enabled,
                                                bool chartsEnabled,
                                                bool autoEmailEnabled) {
    char details[224];
    snprintf(details, sizeof(details),
             "fw=%s system=%s ip=%s sd=%s/%s psram=%s rs232=%s charts=%s auto_email=%s",
             firmwareVersion ? firmwareVersion : "-",
             systemName && systemName[0] ? systemName : "-",
             primaryIp && primaryIp[0] ? primaryIp : "0.0.0.0",
             sdEnabled ? "on" : "off",
             sdMounted ? "mounted" : "down",
             psramAvailable ? "yes" : "no",
             rs232Enabled ? "on" : "off",
             chartsEnabled ? "on" : "off",
             autoEmailEnabled ? "on" : "off");
    logCompactBufferedEvent("BOOT_SNAPSHOT", details);
}

static inline void observabilityLogSchedulerEvent(const char* family,
                                                  int slot,
                                                  const char* command,
                                                  bool success,
                                                  bool localCommand,
                                                  bool daily) {
    char details[192];
    snprintf(details, sizeof(details),
             "family=%s slot=%d mode=%s daily=%s cmd=%.96s",
             family ? family : "-",
             slot,
             localCommand ? "system" : "remote",
             daily ? "yes" : "no",
             command ? command : "");
    logCompactBufferedEvent(success ? "SCHED_EXEC" : "SCHED_FAIL", details);
    if (success) gObservability.schedulerOk++; else gObservability.schedulerFail++;
}

static inline void observabilityLogEmailPipeline(const char* stage,
                                                 const char* channel,
                                                 const char* subject,
                                                 const char* recipients,
                                                 bool success,
                                                 int attachmentCount) {
    char details[224];
    snprintf(details, sizeof(details),
             "stage=%s channel=%s files=%d subject=%.72s to=%.72s",
             stage ? stage : "-",
             channel ? channel : "-",
             attachmentCount,
             subject ? subject : "",
             recipients ? recipients : "");
    logCompactBufferedEvent(success ? "EMAIL_PIPELINE_OK" : "EMAIL_PIPELINE_FAIL", details);
    if (success) gObservability.emailOk++; else gObservability.emailFail++;
}

static inline void observabilityLogConfigChange(const char* source,
                                                const char* summary,
                                                bool success) {
    char details[224];
    snprintf(details, sizeof(details),
             "source=%s summary=%.160s",
             source ? source : "-",
             summary ? summary : "");
    logCompactBufferedEvent(success ? "CONFIG_CHANGE_OK" : "CONFIG_CHANGE_FAIL", details);
    if (success) gObservability.configOk++; else gObservability.configFail++;
}

static inline void observabilityMaybeLogDailySummary(size_t freeHeap, size_t freePsram) {
    const int dayKey = observabilityDayKeyNow();
    if (dayKey < 0) return;

    if (gObservability.lastSummaryDayKey < 0) {
        gObservability.lastSummaryDayKey = dayKey;
        gObservability.minHeapSeen = freeHeap;
        gObservability.minPsramSeen = freePsram;
        return;
    }

    if (gObservability.minHeapSeen == 0 || freeHeap < gObservability.minHeapSeen) {
        gObservability.minHeapSeen = freeHeap;
    }
    if (freePsram > 0 && (gObservability.minPsramSeen == 0 || freePsram < gObservability.minPsramSeen)) {
        gObservability.minPsramSeen = freePsram;
    }

    if (dayKey == gObservability.lastSummaryDayKey) return;

    char details[224];
    snprintf(details, sizeof(details),
             "day=%d net=%lu storage=%lu sched=%lu/%lu email=%lu/%lu cfg=%lu/%lu heap_alerts=%lu min_heap=%uKB min_psram=%uKB",
             gObservability.lastSummaryDayKey,
             (unsigned long)gObservability.networkTransitions,
             (unsigned long)gObservability.storageTransitions,
             (unsigned long)gObservability.schedulerOk,
             (unsigned long)gObservability.schedulerFail,
             (unsigned long)gObservability.emailOk,
             (unsigned long)gObservability.emailFail,
             (unsigned long)gObservability.configOk,
             (unsigned long)gObservability.configFail,
             (unsigned long)gObservability.heapAlerts,
             (unsigned)(gObservability.minHeapSeen / 1024U),
             (unsigned)(gObservability.minPsramSeen / 1024U));
    logCompactBufferedEvent("DAILY_SUMMARY", details);

    gObservability.lastSummaryDayKey = dayKey;
    observabilityResetDailyCounters();
    gObservability.minHeapSeen = freeHeap;
    gObservability.minPsramSeen = freePsram;
}

static inline void observabilityTick(bool wifiStaConnected,
                                     bool apActive,
                                     bool ethernetActive,
                                     const char* wifiSsid,
                                     const char* primaryIp,
                                     size_t freeHeap,
                                     size_t maxAllocHeap,
                                     size_t freePsram,
                                     bool sdMounted,
                                     bool sdEnabled) {
    unsigned long nowMs = millis();
    if (gObservability.lastTickMs != 0 && (nowMs - gObservability.lastTickMs) < 5000UL) {
        return;
    }
    gObservability.lastTickMs = nowMs;

    const int heapBucket = observabilityHeapBucket(freeHeap);

    if (!gObservability.initialized) {
        gObservability.initialized = true;
        healthModuleInit();
        gObservability.lastWifiStaConnected = wifiStaConnected;
        gObservability.lastApActive = apActive;
        gObservability.lastEthernetActive = ethernetActive;
        gObservability.lastSdMounted = sdMounted;
        gObservability.lastSdEnabled = sdEnabled;
        gObservability.lastHeapBucket = heapBucket;
        gObservability.lastSummaryDayKey = observabilityDayKeyNow();
        gObservability.minHeapSeen = freeHeap;
        gObservability.minPsramSeen = freePsram;
        strncpy(gObservability.lastSsid, wifiSsid ? wifiSsid : "", sizeof(gObservability.lastSsid) - 1);
        gObservability.lastSsid[sizeof(gObservability.lastSsid) - 1] = '\0';
        strncpy(gObservability.lastIp, primaryIp ? primaryIp : "", sizeof(gObservability.lastIp) - 1);
        gObservability.lastIp[sizeof(gObservability.lastIp) - 1] = '\0';
        return;
    }

    if (wifiStaConnected != gObservability.lastWifiStaConnected ||
        apActive != gObservability.lastApActive ||
        ethernetActive != gObservability.lastEthernetActive ||
        sdMounted != gObservability.lastSdMounted ||
        sdEnabled != gObservability.lastSdEnabled ||
        strncmp(gObservability.lastIp, primaryIp ? primaryIp : "", sizeof(gObservability.lastIp) - 1) != 0 ||
        strncmp(gObservability.lastSsid, wifiSsid ? wifiSsid : "", sizeof(gObservability.lastSsid) - 1) != 0) {
        char details[224];
        snprintf(details, sizeof(details),
                 "sta=%s ap=%s eth=%s ssid=%.32s ip=%.23s sd=%s/%s",
                 wifiStaConnected ? "on" : "off",
                 apActive ? "on" : "off",
                 ethernetActive ? "on" : "off",
                 wifiSsid ? wifiSsid : "",
                 primaryIp ? primaryIp : "0.0.0.0",
                 sdEnabled ? "on" : "off",
                 sdMounted ? "mounted" : "down");
        logCompactBufferedEvent("NET_TRANSITION", details);
        gObservability.networkTransitions++;
        gObservability.storageTransitions += (sdMounted != gObservability.lastSdMounted || sdEnabled != gObservability.lastSdEnabled) ? 1U : 0U;
        gObservability.lastWifiStaConnected = wifiStaConnected;
        gObservability.lastApActive = apActive;
        gObservability.lastEthernetActive = ethernetActive;
        gObservability.lastSdMounted = sdMounted;
        gObservability.lastSdEnabled = sdEnabled;
        strncpy(gObservability.lastSsid, wifiSsid ? wifiSsid : "", sizeof(gObservability.lastSsid) - 1);
        gObservability.lastSsid[sizeof(gObservability.lastSsid) - 1] = '\0';
        strncpy(gObservability.lastIp, primaryIp ? primaryIp : "", sizeof(gObservability.lastIp) - 1);
        gObservability.lastIp[sizeof(gObservability.lastIp) - 1] = '\0';
    }

    if (heapBucket > gObservability.lastHeapBucket) {
        char details[160];
        snprintf(details, sizeof(details),
                 "free_heap=%uKB max_alloc=%uKB free_psram=%uKB",
                 (unsigned)(freeHeap / 1024U),
                 (unsigned)(maxAllocHeap / 1024U),
                 (unsigned)(freePsram / 1024U));
        logCompactBufferedEvent("MEM_WATERMARK", details);
        gObservability.heapAlerts++;
    }
    gObservability.lastHeapBucket = heapBucket;

    observabilityMaybeLogDailySummary(freeHeap, freePsram);
}

#endif
