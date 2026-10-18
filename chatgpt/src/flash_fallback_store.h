// flash_fallback_store.h — LittleFS fallback writers for no-SD operation (header-only).
//
// Architecture: PSRAM accumulates data (hot), and unloads to SD or LittleFS
// at flush_interval (minimum 10m, default 30m — all packs) or earlier when the
// capture buffer fills, plus always before reboot/OTA (same guarantee as SD).
// The Flash unload below follows the configured interval per source, or
// earlier when 64KB coalesced (buffer-full emergency) — so small interval
// flushes never wear the flash.
// Wear-safe by construction:
//   - Coalesced bulk appends only, 256KB per file rotation.
//   - OEE aggregates are sporadic (shift-end / ticks) and appended directly.
//   - Every write is skipped when free flash drops below 1MB (/config reserve).
//   - Pre-NTP data stays buffered in PSRAM (no PENDING debris on flash).
//   - Readers and cleanup understand these layouts (see flash_fallback_cleanup.h).
//
// Layouts:
//   /data_fallback/<sys>/<SOURCE>_<DD-MM-YYYY>[_<idx>].<ext>   (RS232/LPT data)
//   /oee/PROXIMITY_<DD-MM-YYYY>.csv                            (OEE daily mirror)
//   /oee/wk<NN>-oee.csv                                        (OEE weekly mirror)
//   /logs/...                                                  (log mirror, see log.cpp)
//
// Header-only (no new build units), like sd_layout.h. All helpers are
// stateless (rotation resolved by directory scan, reboot-safe).
#pragma once

#include <Arduino.h>
#include <vector>
#include <utility>
#include <cstring>
#include "feature_flags.h"
#include "structures.h"
#include "email_flash_rpc.h" // read helpers below go through the stack-safe flash API

#if ENABLE_FILESYSTEM==1

#include <LittleFS.h>
#include "littlefs_manager.h"
#include "ConfigManager.h"
#include "psram_helpers.h"
#include "string_utils.h"
#include "log.h"
#include "sdcard.h"
#include "sd_layout.h"

extern ConfigManager configManager;

#define FLASH_FB_ROOT "/data_fallback"
#define FLASH_FB_OEE_ROOT "/oee"
#define FLASH_FB_FILE_MAX_BYTES (256UL * 1024UL)   // 256KB per fallback file
#define FLASH_FB_RESERVE_BYTES (1024ULL * 1024UL)  // keep 1MB free for /config
#define FLASH_FB_BURST_BYTES (64UL * 1024UL)       // ...or earlier when 64KB coalesced
#define FLASH_FB_BUF_CAP (256UL * 1024UL)          // per-source PSRAM cap (shed beyond)
#define FLASH_FB_MAX_SLOTS 4

// Unload window follows the configured buffer-write interval (default 30m,
// minimum 10m enforced at the config layer) — same rule for every pack.
// Cached (page saves apply within ~30s); the hot path stays allocation-free.
static unsigned long flashFbWindowMs() {
    static unsigned long cached = 0;
    static unsigned long lastMs = 0;
    unsigned long now = millis();
    if (cached == 0 || (now - lastMs) > 30000UL) {
        extern ConfigManager configManager;
        String v = configManager.getSdcardFlushInterval();
        cached = (v == "10m") ? 600000UL : (v == "60m") ? 3600000UL : 1800000UL;
        lastMs = now;
    }
    return cached;
}

// NEVER to LittleFS: G-code, AVI/video, photos and other large blobs stay in
// PSRAM (VFS budget) or are refused without SD (camera recording). This store
// only handles small persistent data: logs, RS232/LPT text and OEE aggregates.

static bool flashFbStorageReady() {
    // Cheap per-call gate first; the FS/reserve probe is cached (changes slowly).
    if (sdcardShouldUseSD()) return false; // SD owns persistence
    static unsigned long lastProbeMs = 0;
    static bool lastProbeOk = false;
    unsigned long now = millis();
    if (lastProbeOk && (now - lastProbeMs) < 5000UL) return true;
    if (!littlefsBegin() || !littlefsMounted()) { lastProbeOk = false; return false; }
    if (littlefsFreeBytes() < FLASH_FB_RESERVE_BYTES) {
        lastProbeOk = false;
        lastProbeMs = now;
        return false;
    }
    lastProbeOk = true;
    lastProbeMs = now;
    return true;
}

// Configured data format, cached (page saves apply within ~30s; the slot
// keeps its creation ext, same staleness the SD rotation has).
static String flashFbFormatCached() {
    static String cached;
    static unsigned long lastMs = 0;
    unsigned long now = millis();
    if (cached.isEmpty() || (now - lastMs) > 30000UL) {
        extern ConfigManager configManager;
        String f = configManager.getSdcardFormat();
        if (f.isEmpty() || (f != "txt" && f != "csv" && f != "bin")) f = "csv";
        cached = f;
        lastMs = now;
    }
    return cached;
}

static void flashFbMkdirParents(const String& filePath) {
    int slash = filePath.lastIndexOf('/');
    if (slash <= 0) return;
    String dir = filePath.substring(0, slash);
    int second = dir.indexOf('/', 1);
    if (second > 0) {
        String lvl1 = dir.substring(0, second);
        if (!LittleFS.exists(lvl1.c_str())) LittleFS.mkdir(lvl1.c_str());
    }
    if (!LittleFS.exists(dir.c_str())) LittleFS.mkdir(dir.c_str());
}

// Only small files ever land on LittleFS: .csv, .txt, .log, .chart.
// Everything else (.bin, .avi, .jpg, .gcode, ...) stays in PSRAM or on SD.
static bool flashFbExtAllowed(const char* ext) {
    if (!ext || !ext[0]) return false;
    return strcmp(ext, "csv") == 0 || strcmp(ext, "txt") == 0 ||
           strcmp(ext, "log") == 0 || strcmp(ext, "chart") == 0;
}

// Current date suffix for fallback files. Pre-NTP returns false (caller
// keeps data in PSRAM VFS — no PENDING debris is ever written to flash).
static bool flashFbDateSuffix(char out[16]) {
    time_t now = time(nullptr);
    if (!sd_layout::clockReady(now)) return false;
    struct tm ti;
    localtime_r(&now, &ti);
    snprintf(out, 16, "%02d-%02d-%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    return true;
}

static String flashFbDataPrefix(const String& sys, const String& sourceUpper,
                                const String& dateSuf) {
    return String(FLASH_FB_ROOT) + "/" + sys + "/" + sourceUpper + "_" + dateSuf;
}

// Coalescing slot: PSRAM buffer unloaded to LittleFS at most every
// configured window per source, or earlier on 64KB (buffer-full emergency).
struct FlashFbSlot {
    String source;
    String ext;
    std::vector<char, PsramAllocatorStd<char>> buf;
    unsigned long lastFlushMs;
    int fileIdx;
    char curDate[16];
    FlashFbSlot() : lastFlushMs(0), fileIdx(0) { curDate[0] = '\0'; }
};

static std::vector<FlashFbSlot>& flashFbSlots() {
    static std::vector<FlashFbSlot> s;
    return s;
}

// Flush one slot (rotation at 256KB per file; current size re-checked so a
// reboot never overfills).
static bool flashFbFlushSlot(FlashFbSlot& slot) {
    if (slot.buf.empty()) return true;
    if (!flashFbStorageReady()) return false;
#if ENABLE_FILESYSTEM==1
    // Serialized under lfsMutex (same crash guard as the email sender); on
    // contention the buffer stays queued for the next window.
    extern bool lfsMutexLock(TickType_t timeoutTicks);
    extern void lfsMutexUnlock();
    if (!lfsMutexLock(pdMS_TO_TICKS(5000))) return false;
#endif
    extern ConfigManager configManager;
    String sys = configManager.getSdcardName();
    if (sys.isEmpty()) sys = "SD_LOG";
    char dateSuf[16];
    if (!flashFbDateSuffix(dateSuf)) {
#if ENABLE_FILESYSTEM==1
        lfsMutexUnlock();
#endif
        return false; // pre-NTP: keep buffering
    }
    if (strncmp(slot.curDate, dateSuf, sizeof(slot.curDate)) != 0) {
        strncpy(slot.curDate, dateSuf, sizeof(slot.curDate) - 1);
        slot.curDate[sizeof(slot.curDate) - 1] = '\0';
        slot.fileIdx = 0;
    }
    String src = slot.source;
    src.toUpperCase();
    String ext = slot.ext.length() ? slot.ext : "csv";
    if (!flashFbExtAllowed(ext.c_str())) {
#if ENABLE_FILESYSTEM==1
        lfsMutexUnlock();
#endif
        return false; // e.g. switched to .bin: hold (VFS has it)
    }
    String prefix = flashFbDataPrefix(sys, src, String(dateSuf));
    for (int guard = 0; guard < 64; guard++) {
        String path = prefix;
        if (slot.fileIdx > 0) { path += "_"; path += String(slot.fileIdx); }
        path += ".";
        path += ext;
        size_t curSize = 0;
        if (LittleFS.exists(path.c_str())) {
            File rf = LittleFS.open(path.c_str(), FILE_READ);
            if (rf) { curSize = rf.size(); rf.close(); }
        }
        if (curSize < FLASH_FB_FILE_MAX_BYTES) {
            flashFbMkdirParents(path);
            File f = LittleFS.open(path.c_str(), FILE_APPEND);
            if (!f) {
#if ENABLE_FILESYSTEM==1
                lfsMutexUnlock();
#endif
                return false;
            }
            size_t w = f.write((const uint8_t*)slot.buf.data(), slot.buf.size());
            f.close();
            if (w != slot.buf.size()) {
#if ENABLE_FILESYSTEM==1
                lfsMutexUnlock();
#endif
                return false;
            }
            storNoteLfsWrite(w);
            slot.buf.clear();
            slot.buf.shrink_to_fit(); // give PSRAM back between 30min windows
            slot.lastFlushMs = millis();
#if ENABLE_FILESYSTEM==1
            lfsMutexUnlock();
#endif
            return true;
        }
        slot.fileIdx++;
    }
#if ENABLE_FILESYSTEM==1
    lfsMutexUnlock();
#endif
    return false;
}

// Unload entry point. Best effort: failures/shed never fail the caller (the
// PSRAM VFS already holds the data). Returns true when buffered OR flushed.
// ext==nullptr (default) uses the cached configured format.
static bool flashFbWrite(const char* source, const char* data, size_t len,
                         const char* ext = nullptr) {
    if (!source || !source[0]) return false;
    if (!data || len == 0) return true;
    if (!flashFbStorageReady()) return false;
    std::vector<FlashFbSlot>& slots = flashFbSlots();
    FlashFbSlot* slot = nullptr;
    for (auto& s : slots) {
        if (s.source.equals(source)) { slot = &s; break; }
    }
    if (!slot) {
        if (slots.size() >= FLASH_FB_MAX_SLOTS) return false; // shed, VFS has it
        String e = (ext && ext[0]) ? String(ext) : flashFbFormatCached();
        if (!flashFbExtAllowed(e.c_str())) return false; // .bin and blobs stay in PSRAM
        slots.emplace_back();
        slot = &slots.back();
        slot->source = source;
        slot->ext = e;
        slot->lastFlushMs = millis();
    }
    if (slot->buf.size() + len > FLASH_FB_BUF_CAP) return false; // shed, VFS has it
    try {
        slot->buf.insert(slot->buf.end(), data, data + len);
    } catch (...) {
        return false;
    }
    if (slot->buf.size() >= FLASH_FB_BURST_BYTES ||
        (millis() - slot->lastFlushMs) >= flashFbWindowMs()) {
        slot->ext = flashFbFormatCached(); // refresh on flush only (format switch safe)
        return flashFbFlushSlot(*slot);
    }
    return true;
}

// String convenience overload (no extra allocation beyond the caller's own).
static inline bool flashFbWrite(const String& source, const char* data, size_t len,
                               const char* ext = nullptr) {
    return flashFbWrite(source.c_str(), data, len, ext);
}

// Flush every non-empty slot regardless of window (pre-reboot / pre-OTA:
// same guarantee the SD path has via logFlushNow + sensor flush in safeReboot).
static void flashFbFlushAllSlots() {
    std::vector<FlashFbSlot>& slots = flashFbSlots();
    for (auto& s : slots) {
        if (!s.buf.empty()) flashFbFlushSlot(s);
    }
}


// Direct OEE append (sporadic rows): header written when the file is new,
// per-file cap 256KB, 1MB reserve always enforced. Serialized under lfsMutex
// (same crash guard as the email sender); on contention the row is skipped.
static bool flashOeeAppend(const String& fileName, const char* headerLine,
                           const char* rowLine) {
    if (!flashFbStorageReady()) return false;
#if ENABLE_FILESYSTEM==1
    extern bool lfsMutexLock(TickType_t timeoutTicks);
    extern void lfsMutexUnlock();
    if (!lfsMutexLock(pdMS_TO_TICKS(5000))) return false;
#endif
    String path = String(FLASH_FB_OEE_ROOT) + "/" + fileName;
    if (!LittleFS.exists(FLASH_FB_OEE_ROOT)) LittleFS.mkdir(FLASH_FB_OEE_ROOT);
    bool isNew = !LittleFS.exists(path.c_str());
    if (!isNew) {
        File rf = LittleFS.open(path.c_str(), FILE_READ);
        if (rf) {
            bool full = ((size_t)rf.size() >= FLASH_FB_FILE_MAX_BYTES);
            rf.close();
            if (full) {
#if ENABLE_FILESYSTEM==1
                lfsMutexUnlock();
#endif
                return false;
            }
        }
    }
    File f = LittleFS.open(path.c_str(), FILE_APPEND);
    if (!f) {
#if ENABLE_FILESYSTEM==1
        lfsMutexUnlock();
#endif
        return false;
    }
    size_t oeeStart = f.size();
    if (isNew && headerLine && headerLine[0]) {
        f.println(headerLine);
    }
    if (rowLine && rowLine[0]) {
        f.println(rowLine);
    }
    size_t oeeEnd = f.size();
    f.close();
#if ENABLE_FILESYSTEM==1
    lfsMutexUnlock();
#endif
    storNoteLfsWrite(oeeEnd > oeeStart ? oeeEnd - oeeStart : 0);
    return true;
}

// Attachment filter shared with the day-email senders. .chart binaries NEVER
// ride emails (graphs/OEE charts only) — same rule as the senders'
// isAllowedEmailDayAttachment (live .24 2026-10-14).
static void flashFbListDir(const String& dirPath,
                           std::vector<std::pair<String, size_t>>& out);
static bool flashFbAttachAllowed(const String& fileName) {
    String l = fileName;
    l.toLowerCase();
    return l.endsWith(".txt") || l.endsWith(".csv") || l.endsWith(".bin") ||
           l.endsWith(".log");
}

// YYYY-MM-DD -> DD-MM-YYYY (OEE / SD-layout names).
static String flashFbIsoToBr(const String& iso) {
    if (iso.length() >= 10 && iso.charAt(4) == '-' && iso.charAt(7) == '-') {
        return iso.substring(8, 10) + "-" + iso.substring(5, 7) + "-" + iso.substring(0, 4);
    }
    return iso;
}

static void flashFbCollectFromDir(const String& dir, const String& mustContain,
                                  std::vector<StorageFileInfo>& out) {
    std::vector<std::pair<String, size_t>> entries;
    flashFbListDir(dir, entries);
    for (auto& e : entries) {
        if (!mustContain.isEmpty() && e.first.indexOf(mustContain) < 0) continue;
        if (!flashFbAttachAllowed(e.first)) continue;
        StorageFileInfo info;
        info.name = e.first;
        info.size = e.second;
        info.fullPath = dir + "/" + e.first;
        info.backend = STORAGE_BACKEND_FLASH;
        out.push_back(info);
    }
}

// Collect LittleFS files relevant to a day-email folder request.
// folderPath/dateFilter follow the senders' convention (actualFolderPath +
// dateSuffixFilter, "@" already stripped by the caller).
//   /logs/...              -> same path (log mirror, identical layout)
//   /sensor_history/<date> -> same path (.csv/.chart) + /oee DD-MM-YYYY daily
//   /data_fallback/...     -> same path
//   /oee/...               -> same path
//   /YYYY-MM-DD (VFS today)-> today's /data_fallback/<sys> + /logs/log_<DD-MM-YYYY>
//   /<sys> (EMAILDAY layout)-> /data_fallback/<sys> filtered by dateFilter
// includeBulk gates overlapping bulk data (/data_fallback, /logs): pass false
// when the hot tier (VFS) already yielded files, so the same content is never
// attached twice. /oee + /sensor_history never overlap VFS and are always
// included.
static void flashFbCollectEmailFiles(const String& folderPath, const String& dateFilter,
                                     std::vector<StorageFileInfo>& out, bool includeBulk = true) {
    // No littlefsBegin() gate here: this helper is reachable from the PSRAM-
    // stacked email tasks, and an unmounted FS would turn that call into a
    // flash op on an unsafe stack. The listing below goes through
    // emailFlashListDir(), whose executor (loop or internal caller) does the
    // begin/mount on a safe stack; failure simply yields an empty listing.
    String fp = folderPath;
    String filter = dateFilter;
    // Callers sometimes pass the raw "/base@DD-MM-YYYY" form: split it here too.
    int atPos = fp.indexOf('@');
    if (atPos >= 0) {
        if (filter.isEmpty()) filter = fp.substring(atPos + 1);
        fp = fp.substring(0, atPos);
    }
    while (fp.endsWith("/") && fp.length() > 1) fp = fp.substring(0, fp.length() - 1);
    if (fp.startsWith("/logs")) {
        if (includeBulk) flashFbCollectFromDir(fp, filter, out);
        return;
    }
    if (fp.startsWith(String(FLASH_FB_ROOT)) || fp.startsWith(String(FLASH_FB_OEE_ROOT))) {
        if (fp.startsWith(String(FLASH_FB_ROOT))) {
            if (includeBulk) flashFbCollectFromDir(fp, filter, out);
        } else {
            flashFbCollectFromDir(fp, filter, out);
        }
        return;
    }
    if (fp.startsWith("/sensor_history")) {
        flashFbCollectFromDir(fp, "", out);
        String tail = fp.substring(String("/sensor_history").length());
        if (tail.startsWith("/")) tail = tail.substring(1);
        int sl = tail.indexOf('/');
        String dayIso = (sl >= 0) ? tail.substring(0, sl) : tail;
        if (dayIso.length() >= 10) {
            flashFbCollectFromDir(String(FLASH_FB_OEE_ROOT), flashFbIsoToBr(dayIso), out);
        }
        return;
    }
    if (fp.length() == 11 && fp.charAt(0) == '/' && fp.charAt(5) == '-' && fp.charAt(8) == '-') {
        if (!includeBulk) return;
        extern ConfigManager configManager;
        String sys = configManager.getSdcardName();
        if (sys.isEmpty()) sys = "SD_LOG";
        String br = flashFbIsoToBr(fp.substring(1));
        flashFbCollectFromDir(String(FLASH_FB_ROOT) + "/" + sys, filter.length() ? filter : br, out);
        flashFbCollectFromDir(String("/logs/log_") + br, "", out);
        return;
    }
    // SD-layout sys folder (EMAILDAY): map to the flash fallback mirror.
    if (!includeBulk) return;
    String base = fp;
    if (base.startsWith("/")) base = base.substring(1);
    int sl = base.indexOf('/');
    if (sl > 0) base = base.substring(0, sl);
    if (base.length() > 0) {
        flashFbCollectFromDir(String(FLASH_FB_ROOT) + "/" + base, filter, out);
    }
}

// True when the flash mirror holds data for an SD-layout sys folder + date.
static bool flashFbHasDataForDate(const String& sysBase, const String& dateDDMMYYYY) {
    if (!littlefsBegin() || !littlefsMounted()) return false;
    String base = sysBase;
    if (base.startsWith("/")) base = base.substring(1);
    int sl = base.indexOf('/');
    if (sl > 0) base = base.substring(0, sl);
    if (base.isEmpty() || dateDDMMYYYY.isEmpty()) return false;
    std::vector<std::pair<String, size_t>> entries;
    flashFbListDir(String(FLASH_FB_ROOT) + "/" + base, entries);
    for (auto& e : entries) {
        if (e.first.indexOf(dateDDMMYYYY) >= 0 && e.second > 0) return true;
    }
    return false;
}

// Locate an SD-path file on LittleFS: same path first (log mirror), then the
// /oee basename fallback (OEE daily/weekly mirrors). Returns true + out path.
// Reads go through emailFlash*(): locking + direct/RPC dispatch are handled
// inside (this helper is reachable from the PSRAM-stacked email tasks via the
// no-SD flash fallback, HISTORIC/HomeE32 (8)).
static bool flashFbFindFile(const String& sdPath, String& outFlashPath) {
    size_t sz = 0;
    if (emailFlashProbeSize(sdPath, sz) && sz > 0) {
        outFlashPath = sdPath;
        return true;
    }
    int slash = sdPath.lastIndexOf('/');
    String base = (slash >= 0) ? sdPath.substring(slash + 1) : sdPath;
    if (base.length() == 0) return false;
    String oeePath = String(FLASH_FB_OEE_ROOT) + "/" + base;
    sz = 0;
    if (emailFlashProbeSize(oeePath, sz) && sz > 0) {
        outFlashPath = oeePath;
        return true;
    }
    return false;
}

// List a LittleFS directory as (name, size) pairs for storageListFiles().
// Same emailFlash*() rule as flashFbFindFile (empty list when unmounted/busy).
static void flashFbListDir(const String& dirPath,
                           std::vector<std::pair<String, size_t>>& out) {
    emailFlashListDir(dirPath, out);
}

#else // ENABLE_FILESYSTEM!=1

static inline bool flashFbWrite(const char*, const char*, size_t, const char* = "csv") { return false; }
static inline bool flashFbWrite(const String&, const char*, size_t, const char* = "csv") { return false; }
static inline bool flashOeeAppend(const String&, const char*, const char*) { return false; }
static inline bool flashFbFindFile(const String&, String&) { return false; }
static inline void flashFbCollectEmailFiles(const String&, const String&, std::vector<StorageFileInfo>&, bool = true) {}
static inline bool flashFbHasDataForDate(const String&, const String&) { return false; }

#endif // ENABLE_FILESYSTEM==1
