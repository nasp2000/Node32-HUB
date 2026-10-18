// flash_fallback_cleanup.h — LittleFS fallback auto-cleanup (header-only).
//
// Runs only when SD is NOT the active storage. Same policy as the SD cleanup
// (usage threshold % + keep days, one entry per 6h), but restricted to an
// allowlist of data roots. /config and all other system paths are NEVER
// candidates, so configuration can never be deleted by cleanup.
//
// Header-only (no new build units), like sd_layout.h.
#pragma once

#include <Arduino.h>
#include <vector>
#include <algorithm>
#include "feature_flags.h"

#if ENABLE_FILESYSTEM==1

#include <LittleFS.h>
#include "littlefs_manager.h"
#include "ConfigManager.h"
#include "string_utils.h"
#include "log.h"
#include "sdcard.h"
#include "sd_layout.h"

extern ConfigManager configManager;

// Recursive delete scoped to one dated entry (best effort, yields).
static void flashDeleteRecursive(const String& path) {
    File entry = LittleFS.open(path.c_str());
    if (!entry) return;
    if (!entry.isDirectory()) {
        entry.close();
        LittleFS.remove(path.c_str());
        return;
    }
    File child = entry.openNextFile();
    while (child) {
        String childName = String(child.name());
        bool childIsDir = child.isDirectory();
        child.close();
        // openNextFile() may return bare or full paths — normalise to full.
        String childPath = childName;
        if (childPath.indexOf(path) != 0) {
            childPath = path;
            if (!childPath.endsWith("/")) childPath += "/";
            int slash = childName.lastIndexOf('/');
            if (slash >= 0) childName = childName.substring(slash + 1);
            childPath += childName;
        }
        if (childIsDir) {
            flashDeleteRecursive(childPath);
        } else {
            LittleFS.remove(childPath.c_str());
        }
        child = entry.openNextFile();
        yield();
    }
    entry.close();
    LittleFS.rmdir(path.c_str());
}

// True for paths cleanup must never touch (config, registry, system content).
static bool flashCleanupIsProtected(const String& path) {
    if (path == "/config" || path.startsWith("/config/")) return true;
    if (path.endsWith("pages.reg")) return true;
    if (path == "/falas" || path.startsWith("/falas/")) return true;
    if (path == "/playlists" || path.startsWith("/playlists/")) return true;
    if (path == "/library" || path.startsWith("/library/")) return true;
    return false;
}

static void flashAutoCleanupIfNeeded() {
    // Autoclean is always active. SD owns cleanup while mounted; Flash only
    // takes over when SD is not the active storage.
    if (sdcardShouldUseSD()) return;
    if (!littlefsBegin() || !littlefsMounted()) return;

    static unsigned long lastFlashCleanupMs = 0;
    unsigned long nowMs = millis();
    if (lastFlashCleanupMs != 0 && (nowMs - lastFlashCleanupMs) < 21600000UL) return; // max once per 6 hours
    // Serialize the whole scan+delete cycle vs email/FTP flash readers (same
    // crash guard as the sender). On contention the cycle is skipped and runs
    // 6h later — cleanup is never urgent.
    extern bool lfsMutexLock(TickType_t timeoutTicks);
    extern void lfsMutexUnlock();
    bool clLfsHeld = lfsMutexLock(pdMS_TO_TICKS(10000));
    if (!clLfsHeld) return;

    uint64_t total = littlefsTotalBytes();
    if (total == 0) return;
    uint64_t used = littlefsUsedBytes();
    int threshold = configManager.getSdCleanupThreshold(); // percent, shared with SD
    // Two tiers sharing this threshold:
    //   HARD (usage > threshold): delete the oldest entry, whatever its age.
    //   SOFT (usage <= threshold): only entries older than keep_days go.
    const bool overThreshold = (used * 100ULL / total) >= (uint64_t)threshold;

    int keepDays = configManager.getSdKeepDays();
    // keep_days = 0 (UI "infinity"): no age limit — files stay until usage
    // crosses the threshold, then the oldest goes whatever its age.

    // Allowlist only: fallback data roots. /config is never listed here and
    // flashCleanupIsProtected() double-guards every candidate.
    // Note: /data_fallback/<sys>/ nests one level deeper than the other roots,
    // so undated directories are descended once to find dated entries.
    // The /<sys>/ parity root (identical-path .chart mirrors + dated turno
    // files) is appended dynamically: without it those mirrors would grow
    // until the 1MB reserve gate stops all flash writes.
    const char* flashRoots[] = { "/logs", "/data_fallback", "/sensor_history", "/oee", "/DEBUG" };
    std::vector<String> rootList;
    for (size_t sr = 0; sr < sizeof(flashRoots) / sizeof(flashRoots[0]); sr++) {
        rootList.push_back(String(flashRoots[sr]));
    }
    {
        String sysName = configManager.getSdcardName();
        if (sysName.isEmpty()) sysName = "SD_LOG";
        String sysRoot = String("/") + sysName;
        if (sysRoot.length() > 1) rootList.push_back(sysRoot);
    }
    struct FlashCandidate {
        String path;
        int year, month, day;
        bool isDir;
    };
    std::vector<FlashCandidate> candidates;

    auto considerEntry = [&](const String& fullPath, const String& base, bool entryIsDir) {
        if (sd_layout::isAggregate(base)) return;
        int ymd = sd_layout::parseDateFromName(base);
        if (ymd <= 0) return; // undated, PENDING, nodate: never delete
        if (flashCleanupIsProtected(fullPath)) return;
        FlashCandidate fc;
        fc.path = fullPath;
        fc.isDir = entryIsDir;
        sd_layout::dateYmdParts(ymd, fc.year, fc.month, fc.day);
        candidates.push_back(fc);
    };

    for (size_t r = 0; r < rootList.size(); r++) {
        String rootPath = rootList[r];
        if (flashCleanupIsProtected(String(rootPath))) continue;
        if (!LittleFS.exists(rootPath)) continue;
        File rr = LittleFS.open(rootPath);
        if (!rr || !rr.isDirectory()) { if (rr) rr.close(); continue; }
        File ee = rr.openNextFile();
        while (ee) {
            String name = String(ee.name());
            bool entryIsDir = ee.isDirectory();
            ee.close();
            int slash = name.lastIndexOf('/');
            String base = (slash >= 0) ? name.substring(slash + 1) : name;
            String fullPath = String(rootPath) + "/" + base;
            if (entryIsDir && sd_layout::parseDateFromName(base) <= 0 &&
                !flashCleanupIsProtected(fullPath)) {
                // Undated directory (e.g. /data_fallback/<sys>): descend once.
                File sub = LittleFS.open(fullPath.c_str());
                if (sub && sub.isDirectory()) {
                    File se = sub.openNextFile();
                    while (se) {
                        String sname = String(se.name());
                        bool sIsDir = se.isDirectory();
                        se.close();
                        int sslash = sname.lastIndexOf('/');
                        String sbase = (sslash >= 0) ? sname.substring(sslash + 1) : sname;
                        considerEntry(fullPath + "/" + sbase, sbase, sIsDir);
                        se = sub.openNextFile();
                        yield();
                    }
                }
                if (sub) sub.close();
            } else {
                considerEntry(fullPath, base, entryIsDir);
            }
            ee = rr.openNextFile();
            yield();
        }
        rr.close();
    }

    if (candidates.empty()) { lastFlashCleanupMs = nowMs; lfsMutexUnlock(); return; }

    std::sort(candidates.begin(), candidates.end(), [](const FlashCandidate& a, const FlashCandidate& b) {
        if (a.year != b.year) return a.year < b.year;
        if (a.month != b.month) return a.month < b.month;
        return a.day < b.day;
    });

    time_t nowT = time(nullptr);
    for (const auto& fc : candidates) {
        int ageDays = -1;
        if (nowT > 100000) {
            struct tm folder_tm = {};
            folder_tm.tm_year = fc.year - 1900;
            folder_tm.tm_mon  = fc.month - 1;
            folder_tm.tm_mday = fc.day;
            folder_tm.tm_hour = 12;
            time_t folder_t = mktime(&folder_tm);
            if (folder_t > 0) {
                ageDays = (int)(difftime(nowT, folder_t) / 86400.0);
            }
        }
        // HARD tier: oldest goes whatever its age. SOFT tier: only past keep_days.
        if (!overThreshold && !sd_layout::cleanupDeleteAllowed(ageDays, keepDays)) continue;
        flashDeleteRecursive(fc.path);
        if (overThreshold) {
            sendVirtualMessage(buildMessage("Flash Auto-cleanup: deleted %s (usage >%d%%)",
                                              fc.path.c_str(), threshold), INFO);
        } else {
            sendVirtualMessage(buildMessage("Flash Auto-cleanup: deleted %s (older than %dd)",
                                              fc.path.c_str(), keepDays), INFO);
        }
        break; // delete one entry per call, re-check on next invocation
    }
    lastFlashCleanupMs = nowMs;
    lfsMutexUnlock();
}

#else // ENABLE_FILESYSTEM!=1

static inline void flashAutoCleanupIfNeeded() {}

#endif // ENABLE_FILESYSTEM==1
