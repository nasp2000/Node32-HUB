#if defined(ARDUINO)
#include "feature_flags.h"
#include "log.h"  // WARN/INFO/ERROR enum (un-breaks the /config guard)
#if ENABLE_FTP_CLIENT==1

#include "ftp_client.h"
#include "config_manager_runtime.h"
#include "runtime_environment.h" // hasNetworkConnectivity() for the monitor tick gate
#include "sdcard.h"
#include "psram_vfs.h"
#include "psram_helpers.h"
#include "littlefs_manager.h"
#include <WiFiClient.h>
#if ENABLE_SDCARD==1
#include <SD_MMC.h>
#endif
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <time.h>
#include "rtc_state_util.h"

// All user-facing messages go through sendVirtualMessage so they reach Serial + Web Monitor.
// NOTE: no default args here — they are declared once in ConfigManager.h/commands.h;
// repeating them is a hard error (-fpermissive) on newer GCC.
extern void sendVirtualMessage(const String& msg, int msgType, bool alreadyFormatted, bool bypassDedup);

// --- Pure helpers (mirrored in native test) ---
bool ftpReplyIs(const char* resp, int code) {
    if (!resp) return false;
    while (*resp == ' ' || *resp == '\r' || *resp == '\n') resp++;
    int v = (resp[0] - '0') * 100 + (resp[1] - '0') * 10 + (resp[2] - '0');
    return v == code;
}

bool ftpParsePasvResponse(const char* resp, char* outIp, size_t ipSize, uint16_t* outPort) {
    if (!resp || !outIp || !outPort || ipSize < 16) return false;
    const char* p = nullptr;
    for (const char* q = resp; *q; q++) if (*q == '(') p = q;
    if (!p) return false;
    int h1, h2, h3, h4, p1, p2;
    if (sscanf(p, "(%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2) != 6) return false;
    if (h1 < 0 || h1 > 255 || h2 < 0 || h2 > 255 || h3 < 0 || h3 > 255 || h4 < 0 || h4 > 255) return false;
    if (p1 < 0 || p1 > 255 || p2 < 0 || p2 > 255) return false;
    snprintf(outIp, ipSize, "%d.%d.%d.%d", h1, h2, h3, h4);
    *outPort = (uint16_t)(p1 * 256 + p2);
    return true;
}

int ftpClampPort(int p) { if (p < 1) return 1; if (p > 65535) return 65535; return p; }
int ftpClampIntervalMin(int m) { if (m < 300) return 300; if (m > 2160) return 2160; return m; }
int ftpClampMaxFiles(int m) { if (m < 1) return 1; if (m > FTP_MAX_FILES_PER_CYCLE) return FTP_MAX_FILES_PER_CYCLE; return m; }

void ftpNormalizeRemotePath(const char* in, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    if (!in || !*in) { strncpy(out, "/", outSize); out[outSize - 1] = 0; return; }
    size_t j = 0;
    if (in[0] != '/') { out[j++] = '/'; }
    for (size_t i = 0; in[i] && j + 1 < outSize; i++) out[j++] = in[i];
    out[j] = 0;
    while (j > 1 && out[j - 1] == '/') out[--j] = 0;
}

// Remote base folder = board system name (same rule on ALL packs).
// Sanitized for FTP paths; falls back to "Node32" when empty.
static String ftpRemoteBase() {
    String n = configManager.getSystemName();
    String o;
    for (size_t i = 0; i < n.length() && o.length() < 48; i++) {
        char c = n[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') o += c;
        else if (o.length() && !o.endsWith("-")) o += '-';
    }
    while (o.endsWith("-")) o.remove(o.length() - 1);
    if (!o.length()) o = "Node32";
    return String("/") + o;
}

int ftpRemoteDirList(const char* remotePath, char out[][64], int maxOut) {
    if (!out || maxOut <= 0) return 0;
    char norm[128];
    ftpNormalizeRemotePath(remotePath, norm, sizeof(norm));
    if (strcmp(norm, "/") == 0) return 0;   // root/home already exists
    int count = 0;
    char prefix[64];
    size_t pj = 0;
    prefix[0] = 0;
    const char* s = norm;
    while (*s) {
        while (*s == '/') s++;
        if (!*s) break;
        const char* start = s;
        while (*s && *s != '/') s++;
        size_t seg = (size_t)(s - start);
        if (pj + 1 + seg >= sizeof(prefix)) break;   // too long — stop cleanly
        prefix[pj++] = '/';
        memcpy(prefix + pj, start, seg);
        pj += seg;
        prefix[pj] = 0;
        if (count < maxOut) {
            strncpy(out[count], prefix, 63);
            out[count][63] = 0;
            count++;
        }
    }
    return count;
}

static bool matchExt(const char* filename, const char* ext) {
    size_t fl = strlen(filename), el = strlen(ext);
    if (el == 0 || fl < el) return false;
    const char* tail = filename + fl - el;
    for (size_t i = 0; i < el; i++) {
        char a = tail[i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
    }
    return true;
}

bool ftpMatchFilter(const char* filename, const char* filter) {
    if (!filename || !*filename) return false;
    if (!filter || !*filter) return true;
    const char* base = filename;
    for (const char* q = filename; *q; q++) if (*q == '/' || *q == '\\') base = q + 1;
    char tmp[96];
    size_t fi = 0;
    // Iterate ;-separated patterns like "*.csv;*.log"
    for (size_t i = 0; ; i++) {
        char c = filter[i];
        if (c == ';' || c == ',' || c == 0) {
            tmp[fi] = 0;
            // trim spaces
            char* s = tmp;
            while (*s == ' ') s++;
            size_t L = strlen(s);
            while (L && s[L - 1] == ' ') s[--L] = 0;
            if (strchr(s, '*')) {
                // Glob pattern: match by the extension after the last dot, so
                // "*.csv", "*.*.csv" and "*.*.*.json" all resolve to ".csv"/".json".
                // A bare "*" (no dot) matches everything.
                const char* dot = strrchr(s, '.');
                if (dot && dot[1]) { if (matchExt(base, dot)) return true; }
                else return true;
            }
            else if (*s && strcmp(s, base) == 0) return true;
            fi = 0;
            if (c == 0) break;
        } else if (fi + 1 < sizeof(tmp)) tmp[fi++] = c;
    }
    return false;
}

bool ftpShouldUpload(const char* filename, size_t fileSize, const char* filter,
                     bool onlyNew, const char* sentList) {
    if (!filename || !*filename || fileSize == 0) return false;
    if (fileSize >= FTP_MAX_UPLOAD_BYTES) return false;
    if (!ftpMatchFilter(filename, filter)) return false;
    if (!onlyNew) return true;
    if (!sentList || !*sentList) return true;
    // sentList holds "name:size" lines; match the exact line with a two-step
    // compare (no fixed-size needle buffer — deep relPaths exceed any stack
    // buffer and then never match, re-uploading every cycle).
    size_t fnLen = strlen(filename);
    const char* p = sentList;
    while (*p) {
        while (*p == '\r' || *p == '\n' || *p == ' ') p++;
        if (!*p) break;
        if (strncmp(p, filename, fnLen) == 0 && p[fnLen] == ':') {
            const char* num = p + fnLen + 1;
            char* end = nullptr;
            unsigned long v = strtoul(num, &end, 10);
            if (end != num && v == (unsigned long)fileSize &&
                (*end == '\r' || *end == '\n' || *end == 0)) return false;
        }
        while (*p && *p != '\n') p++;
    }
    return true;
}

size_t ftpSentKeepOffset(const char* buf, size_t len, size_t keepMax) {
    if (!buf || len <= keepMax) return 0;
    size_t cut = len - keepMax;
    if (cut > 0 && buf[cut - 1] == '\n') return cut;  // already line-aligned
    size_t i = cut;
    while (i < len && buf[i] != '\n') i++;
    if (i + 1 < len) return i + 1;  // first whole line inside the window
    return 0;                       // only a partial tail fits: keep all
}

void ftpSentTailWindow(size_t fileSize, size_t want, size_t slack,
                       size_t* seekOut, size_t* lenOut) {
    size_t cap = want + slack;
    size_t len = (fileSize > cap) ? cap : fileSize;
    if (seekOut) *seekOut = fileSize - len;
    if (lenOut) *lenOut = len;
}

bool ftpIsConnectFailure(const char* detail) {
    if (!detail || !*detail) return false;
    // Exact tokens produced by ftpLogin()/connect above. Per-file failures
    // ("TYPE/PASV/STOR/SIZE/data/open-read") must NOT match here.
    static const char* const kConnect[] = {
        "control connect failed",
        "greeting:",
        "USER timeout",
        "USER rejected:",
        "PASS timeout",
        "login failed:",
    };
    for (size_t i = 0; i < sizeof(kConnect) / sizeof(kConnect[0]); i++) {
        const char* t = kConnect[i];
        size_t n = strlen(t);
        if (strncmp(detail, t, n) == 0) return true;
    }
    return false;
}

FtpLocalFs ftpParseLocalRef(const char* ref, const char*& outPath) {
    if (ref && strncmp(ref, "SD:", 3) == 0) { outPath = ref[3] ? ref + 3 : "/"; return FTP_FS_SD; }
    if (ref && strncmp(ref, "PSRAM:", 6) == 0) { outPath = ref[6] ? ref + 6 : "/"; return FTP_FS_PSRAM; }
    if (ref && strncmp(ref, "LFS:", 4) == 0) { outPath = ref[4] ? ref + 4 : "/"; return FTP_FS_LFS; }
    outPath = (ref && *ref) ? ref : "/";
    return FTP_FS_AUTO;
}

FtpLocalFs ftpResolveLocalFs(FtpLocalFs fs, bool sdActive) {
    if (fs == FTP_FS_AUTO) return sdActive ? FTP_FS_SD : FTP_FS_PSRAM;
    return fs;
}

// /config is never a valid upload source (any backend, any prefix).
bool ftpIsProtectedUploadPath(const char* path) {
    if (!path || !*path) return false;
    // Normalize: skip a single "SD:"/"LFS:"/"PSRAM:" prefix for the check so
    // "LFS:/config/..." can not smuggle the protected tree in.
    const char* p = path;
    if (strncmp(p, "SD:", 3) == 0) p += 3;
    else if (strncmp(p, "LFS:", 4) == 0) p += 4;
    else if (strncmp(p, "PSRAM:", 6) == 0) p += 6;
    if (*p == '\0') return false;
    // Exact "/config" or anything underneath it (trailing slashes tolerated).
    if (strcmp(p, "/config") == 0) return true;
    if (strncmp(p, "/config/", 8) == 0) return true;
    size_t len = strlen(p);
    while (len > 8 && p[len - 1] == '/') len--;
    if (len == 7 && strncmp(p, "/config", 7) == 0) return true;
    return false;
}

// --- Runtime state (DRAM: ~150 bytes) ---
static FtpStatus s_status;
static SemaphoreHandle_t s_mutex = nullptr;
static TaskHandle_t s_monitorTask = nullptr;
static unsigned long s_lastCycle = 0;

// --- RTC RAM: auto-upload schedule phase (FTPC) --------------------
// RTC_SLOW_MEM (SRAM, zero flash wear). `lastCyEpoch` mirrors the wall-clock
// epoch of the last completed cycle so the every-`interval_min` phase survives
// a warm reboot/OTA; a power cut clears it and the post-boot countdown restarts
// from zero (the millis fallback below).
#define FTP_RTC_MAGIC    0x46545043UL // "FTPC"
#define FTP_RTC_VERSION  1U
RTC_NOINIT_ATTR static struct {
    uint32_t magic;
    uint32_t version;
    uint32_t lastCyEpoch; // NTP epoch seconds of the last completed cycle (0 = none)
} s_ftpRtc;
static bool s_ftpRtcValid = false;

static void _ftpRtcRead() {
    s_ftpRtcValid = (s_ftpRtc.magic == FTP_RTC_MAGIC && s_ftpRtc.version == FTP_RTC_VERSION);
}

static void _ftpRtcSaveEpoch() {
    time_t raw = time(nullptr);
    if (raw <= 1000) return; // not NTP-synced yet: keep the previous anchor
    s_ftpRtc.lastCyEpoch = (uint32_t)raw;
    s_ftpRtc.version     = FTP_RTC_VERSION;
    s_ftpRtc.magic       = FTP_RTC_MAGIC; // written last: guards a partial mirror
    s_ftpRtcValid = true;
}


static void ftpLock() {
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (s_mutex) xSemaphoreTake(s_mutex, pdMS_TO_TICKS(2000));
}
static void ftpUnlock() { if (s_mutex) xSemaphoreGive(s_mutex); }

static bool readRespLine(WiFiClient& c, char* out, size_t outSize, uint32_t timeoutMs) {
    size_t n = 0;
    unsigned long t0 = millis();
    while (millis() - t0 < timeoutMs) {
        while (c.available() && n + 1 < outSize) {
            char ch = (char)c.read();
            if (ch == '\n') { out[n] = 0; return n > 0; }
            if (ch != '\r') out[n++] = ch;
        }
        if (!c.connected()) { out[n] = 0; return n > 0; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    out[n] = 0;
    return n > 0;
}

static bool sendCmd(WiFiClient& c, const String& cmd, char* resp, size_t respSize, int expectCode) {
    c.print(cmd + "\r\n");
    if (!readRespLine(c, resp, respSize, 8000)) return false;
    return expectCode < 0 || ftpReplyIs(resp, expectCode);
}

void ftpClientInit();
// The monitor task executes the ENTIRE FTP cycle inline: recursive SD scan
// (deep FAT/VFS path, e.g. f_stat → sdmmc_read_sectors_dma → wait on the DMA
// interrupt) + WiFi client + FTP streaming. 4096 B was far too small for that
// path — the first auto cycle crashed inside sdmmc_host_wait_for_event
// (LoadStoreError Addr=0x0, Task=ftpMon) while manual uploads, which run in
// the larger web task, never crashed. Keep the headroom measured in the
// monitor (s_status.monitorStackFree, re-surfaced in the web monitor + API).
#ifndef FTP_MONITOR_STACK_BYTES
#define FTP_MONITOR_STACK_BYTES 16384
#endif
static void ftpMonitorTask(void* p);
// REVERTED 2026-10-06 (OEE-69b): ftpMon stack back to heap (xTaskCreate).
// The PSRAM-stack experiment caused production crash-loops (see HISTORIC
// OEE-69). Heap-snapshot later proved TRUE peak use ~29KB (HWM), so the
// tuned 32KB was unsafe too: keep the historic 64KB headroom.
// NOTE: depth param is in WORDS (16384 words = 64KB).
void ftpClientInit() {
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_monitorTask) xTaskCreate(ftpMonitorTask, "ftpMon", FTP_MONITOR_STACK_BYTES, nullptr, 1, &s_monitorTask);
    // First automatic cycle waits a full interval after boot: the first few
    // minutes of uptime are a risky window for SD/PSRAM (FatFS cache in PSRAM +
    // SDMMC DMA can corrupt heap — see sd_status_impl.h). With interval=60min
    // the first run happens at ~60min uptime, not 30s.
    s_lastCycle = millis();
    _ftpRtcRead();
}

bool ftpIsAvailable() {
    String host = configManager.getFtpHost();
    if (host.length() == 0) return false;
    if (configManager.getFtpAnonymous()) return true;
    return configManager.getFtpUser().length() > 0;
}

void ftpGetStatus(FtpStatus& out) {
    ftpLock();
    out = s_status;
    out.enabled = configManager.getFtpEnabled();
    out.autoUpload = configManager.getFtpAutoUpload();
    ftpUnlock();
}

bool ftpIsBusy() {
    if (!s_mutex) return s_status.busy;
    if (xSemaphoreTake(s_mutex, 0) != pdTRUE) return true; // writer active -> treat as busy
    bool b = s_status.busy;
    xSemaphoreGive(s_mutex);
    return b;
}

static bool ftpLogin(WiFiClient& ctl, char* resp, size_t rs, String& detail) {
    if (!readRespLine(ctl, resp, rs, 8000) || !ftpReplyIs(resp, 220)) { detail = String("greeting: ") + resp; return false; }
    String user = configManager.getFtpAnonymous() ? String("anonymous") : configManager.getFtpUser();
    String pass = configManager.getFtpAnonymous() ? String("node32@hub") : configManager.getFtpPassword();
    ctl.print(String("USER ") + user + "\r\n");
    if (!readRespLine(ctl, resp, rs, 8000)) { detail = "USER timeout"; return false; }
    if (ftpReplyIs(resp, 230)) return true;
    if (!ftpReplyIs(resp, 331)) { detail = String("USER rejected: ") + resp; return false; }
    ctl.print(String("PASS ") + pass + "\r\n");
    if (!readRespLine(ctl, resp, rs, 8000)) { detail = "PASS timeout"; return false; }
    if (!ftpReplyIs(resp, 230)) { detail = String("login failed: ") + resp; return false; }
    return true;
}

// Chunked source reader for ftpStoreStream: copies the next bytes of the file
// into buf (at most maxLen); returns bytes delivered, 0 on EOF/error.
typedef size_t (*FtpChunkReader)(void* ctx, uint8_t* buf, size_t maxLen);

// Arduino File-backed source (SD_MMC and LittleFS share the fs::File API).
struct FtpFileReaderCtx { File* f; size_t remaining; };
static size_t ftpFileReadChunk(void* ctx, uint8_t* buf, size_t maxLen) {
    FtpFileReaderCtx* r = (FtpFileReaderCtx*)ctx;
    if (!r || !r->f || !r->remaining) return 0;
    size_t n = r->remaining < maxLen ? r->remaining : maxLen;
    int got = r->f->read(buf, n);
    if (got <= 0) return 0;
    r->remaining -= (size_t)got;
    return (size_t)got;
}

// PSRAM VFS source (already chunk-addressable; just advance the offset).
struct FtpVfsReaderCtx { String folder; String fname; size_t offset; size_t remaining; };
static size_t ftpVfsReadChunk(void* ctx, uint8_t* buf, size_t maxLen) {
    FtpVfsReaderCtx* r = (FtpVfsReaderCtx*)ctx;
    if (!r || !r->remaining) return 0;
    size_t n = r->remaining < maxLen ? r->remaining : maxLen;
    size_t got = psramVFSReadChunk(r->folder, r->fname, r->offset, buf, n);
    if (!got) return 0;
    r->offset += got;
    r->remaining -= got;
    return got;
}

// STOR with an internal-RAM streaming buffer. Source bytes are pulled through
// `reader` in FTP_STREAM_CHUNK pieces and written to the data socket in
// ~1460 B segments. Returns true on full 226 completion; sentBytes = file size.
//
// WHY STREAMING (never stage whole files in PSRAM): SDMMC DMA cannot reliably
// target PSRAM on ESP32-S3 — staging a file in PSRAM then f.read()ing it there
// corrupted heap-adjacent SDMMC state and ended in a deterministic ftpMon panic
// inside xQueueReceive (Addr=0x0) on the next SD op (proven 2026-09-25 via
// core-dump BT decode: vfs_fat_stat -> ... -> sdmmc_read_sectors_dma ->
// xQueueReceive). The bounce buffer below is therefore MALLOC_CAP_INTERNAL
// (+DMA) so every SDMMC sector read lands in DMA-safe RAM.
// See HISTORIC/Waveshare/HISTORICO.md entry 56.
#define FTP_STREAM_CHUNK 8192
static bool ftpStoreStream(WiFiClient& ctl, size_t totalLen, const String& remotePath,
                           char* resp, size_t rs, String& detail, size_t& sentBytes,
                           FtpChunkReader reader, void* ctx) {
    ctl.print("TYPE I\r\n");
    if (!readRespLine(ctl, resp, rs, 8000) || !ftpReplyIs(resp, 200)) { detail = String("TYPE failed: ") + resp; return false; }
    ctl.print("PASV\r\n");
    if (!readRespLine(ctl, resp, rs, 8000) || !ftpReplyIs(resp, 227)) { detail = String("PASV failed: ") + resp; return false; }
    char ip[24]; uint16_t port = 0;
    if (!ftpParsePasvResponse(resp, ip, sizeof(ip), &port)) { detail = String("PASV parse failed: ") + resp; return false; }
    WiFiClient ds;
    ds.setTimeout(8000);
    if (!ds.connect(ip, port)) { detail = String("data connect failed: ") + ip; return false; }
    ctl.print(String("STOR ") + remotePath + "\r\n");
    if (!readRespLine(ctl, resp, rs, 10000) || (!ftpReplyIs(resp, 150) && !ftpReplyIs(resp, 125))) {
        detail = String("STOR rejected: ") + resp; ds.stop(); return false;
    }
    // DMA-safe bounce buffer (internal RAM only) with graceful shrink fallback.
    size_t chunkCap = FTP_STREAM_CHUNK;
    uint8_t* chunk = (uint8_t*)heap_caps_malloc(chunkCap, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!chunk) { chunkCap = 4096; chunk = (uint8_t*)heap_caps_malloc(chunkCap, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT); }
    if (!chunk) { chunkCap = 1460; chunk = (uint8_t*)heap_caps_malloc(chunkCap, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT); }
    if (!chunk) { detail = "no internal RAM for stream buffer"; ds.stop(); return false; }
    size_t off = 0;
    bool ok = true;
    while (off < totalLen) {
        size_t got = reader(ctx, chunk, chunkCap);
        if (!got) { detail = "source read failed (file changed mid-upload?)"; ok = false; break; }
        size_t woff = 0;
        while (woff < got) {
            size_t n = got - woff;
            if (n > 1460) n = 1460;
            size_t w = ds.write(chunk + woff, n);
            if (w == 0) { detail = "data write failed"; ok = false; break; }
            woff += w;
        }
        if (!ok) break;
        off += got;
    }
    heap_caps_free(chunk);
    if (!ok) { ds.stop(); return false; }
    ds.stop();
    if (!readRespLine(ctl, resp, rs, 15000) || !ftpReplyIs(resp, 226)) { detail = String("STOR incomplete: ") + resp; return false; }
    sentBytes = totalLen;
    return true;
}

bool ftpTestConnection(String& detail) {
    ftpLock();
    if (s_status.busy) { ftpUnlock(); detail = "busy"; return false; }
    s_status.busy = true;
    ftpUnlock();
    bool ok = false;
    WiFiClient ctl;
    ctl.setTimeout(8000);
    char resp[192];
    String host = configManager.getFtpHost();
    int port = ftpClampPort(configManager.getFtpPort());
    if (!ctl.connect(host.c_str(), (uint16_t)port)) {
        detail = "control connect failed";
    } else if (!ftpLogin(ctl, resp, sizeof(resp), detail)) {
        ok = false;
    } else {
        // PWD + PASV probe (no file written)
        ctl.print("PWD\r\n");
        if (!readRespLine(ctl, resp, sizeof(resp), 8000) || !ftpReplyIs(resp, 257)) {
            detail = String("PWD failed: ") + resp;
        } else {
            ctl.print("PASV\r\n");
            if (!readRespLine(ctl, resp, sizeof(resp), 8000) || !ftpReplyIs(resp, 227)) {
                detail = String("PASV failed: ") + resp;
            } else {
                char ip[24]; uint16_t dp = 0;
                if (!ftpParsePasvResponse(resp, ip, sizeof(ip), &dp)) detail = String("PASV parse failed: ") + resp;
                else {
                    detail = String("OK control+login+PASV ") + ip + ":" + dp;
                    ok = true;
                    // Report whether the system-name remote folder exists. Missing
                    // is not fatal (uploader MKDs it) but a server that then
                    // returns 553 on STOR usually cannot create it (permissions).
                    String rp = ftpRemoteBase();
                    if (rp != "/") {
                        ctl.print(String("CWD ") + rp + "\r\n");
                        if (readRespLine(ctl, resp, sizeof(resp), 8000) && ftpReplyIs(resp, 250)) detail += String(" | path ") + rp + " OK";
                        else detail += String(" | path ") + rp + " missing (will be created on upload)";
                    }
                }
            }
        }
        ctl.print("QUIT\r\n");
    }
    ctl.stop();
    ftpLock();
    s_status.busy = false;
    s_status.lastCode = ok ? 200 : 500;
    strncpy(s_status.lastResult, detail.c_str(), sizeof(s_status.lastResult) - 1);
    s_status.lastTime = millis();
    if (ok) s_status.okCount++; else s_status.failCount++;
    ftpUnlock();
    sendVirtualMessage(String(F("FTP test: ")) + detail, ok ? 0 : 1, true, true);
    return ok;
}

// List candidate files from active source into PSRAM vector.
struct FtpCandidate {
    FtpLocalFs fs;    // where the file physically lives
    String relPath;   // path relative to the chosen local root (remote mirror target)
    String readPath;  // full local read path (absolute for SD/LFS, "folder/file" for PSRAM)
    size_t size;
};
// Defensive basename: both the old and the recursive walk reuse this so it works
// whether File::name() returns a bare name or a path.
static String ftpBaseName(const String& full) {
    int sl = full.lastIndexOf('/');
    return sl >= 0 ? full.substring(sl + 1) : full;
}

// Runs under the sdMutex held by ftpRunCycle for the WHOLE scan+upload cycle,
// so FTP is the only SD user while it walks the tree (fix for the crash-loop,
// HISTORIC #57: the old per-entry unlock+vTaskDelay let concurrent writers /
// web pages interleave with the scan and wedge the SDMMC host). No yields, no
// re-lock: any other writer waits its short timeout and skips, and page
// handlers answer "sd_busy".
static void ftpSdScan(const String& dirPath, const String& relPrefix, const char* filter,
                       std::vector<FtpCandidate, PsramAllocatorStd<FtpCandidate>>& out) {
    if (out.size() >= FTP_MAX_PENDING) return;
#if ENABLE_SDCARD==1
    std::vector<String, PsramAllocatorStd<String>> subDirs;
    File d = SD_MMC.open(dirPath);
    if (d && d.isDirectory()) {
        File f = d.openNextFile();
        while (f) {
            if (f.isDirectory()) {
                String sub = dirPath;
                if (!sub.endsWith("/")) sub += "/";
                sub += ftpBaseName(String(f.name()));  // full child path, not bare entry name
                // Belt: never descend into /config (also enforced at listing entry).
                if (ftpIsProtectedUploadPath(sub.c_str())) { f.close(); f = d.openNextFile(); continue; }
                subDirs.push_back(sub);
            } else {
                String full = String(f.name());
                // Belt: never upload /config entries (also enforced at listing entry).
                if (ftpIsProtectedUploadPath(full.c_str())) { f.close(); f = d.openNextFile(); continue; }
                String base = ftpBaseName(full);
                size_t sz = f.size();
                if (sz > 0 && ftpMatchFilter(full.c_str(), filter)) {
                    FtpCandidate c;
                    c.fs = FTP_FS_SD;
                    c.relPath = relPrefix.isEmpty() ? base : relPrefix + "/" + base;
                    c.readPath = dirPath.endsWith("/") ? dirPath + base : dirPath + "/" + base;
                    c.size = sz;
                    out.push_back(c);
                }
            }
            f.close();
            f = d.openNextFile();
        }
    }
    if (d) d.close();
    for (size_t i = 0; i < subDirs.size() && out.size() < FTP_MAX_PENDING; i++) {
        String base = ftpBaseName(subDirs[i]);
        String subRel = relPrefix.isEmpty() ? base : relPrefix + "/" + base;
        ftpSdScan(subDirs[i], subRel, filter, out);
    }
#else
    (void)dirPath; (void)relPrefix; (void)filter;
#endif
}

static void ftpLfsScan(const String& dirPath, const String& relPrefix, const char* filter,
                       std::vector<FtpCandidate, PsramAllocatorStd<FtpCandidate>>& out) {
    if (out.size() >= FTP_MAX_PENDING) return;
#if ENABLE_FILESYSTEM==1
    if (!littlefsBegin()) return;
    std::vector<String, PsramAllocatorStd<String>> subDirs;
    fs::FS& lfs = getLittleFS();
    File d = lfs.open(dirPath);
    if (d && d.isDirectory()) {
        File f = d.openNextFile();
        while (f) {
            if (f.isDirectory()) {
                String sub = dirPath;
                if (!sub.endsWith("/")) sub += "/";
                sub += ftpBaseName(String(f.name()));  // full child path, not bare entry name
                // Belt: never descend into /config (also enforced at listing entry).
                if (ftpIsProtectedUploadPath(sub.c_str())) { f.close(); f = d.openNextFile(); continue; }
                subDirs.push_back(sub);
            } else {
                String full = String(f.name());
                // Belt: never upload /config entries (also enforced at listing entry).
                if (ftpIsProtectedUploadPath(full.c_str())) { f.close(); f = d.openNextFile(); continue; }
                String base = ftpBaseName(full);
                size_t sz = f.size();
                if (sz > 0 && ftpMatchFilter(full.c_str(), filter)) {
                    FtpCandidate c;
                    c.fs = FTP_FS_LFS;
                    c.relPath = relPrefix.isEmpty() ? base : relPrefix + "/" + base;
                    c.readPath = dirPath.endsWith("/") ? dirPath + base : dirPath + "/" + base;
                    c.size = sz;
                    out.push_back(c);
                }
            }
            f.close();
            f = d.openNextFile();
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    if (d) d.close();
    for (size_t i = 0; i < subDirs.size() && out.size() < FTP_MAX_PENDING; i++) {
        String base = ftpBaseName(subDirs[i]);
        String subRel = relPrefix.isEmpty() ? base : relPrefix + "/" + base;
        ftpLfsScan(subDirs[i], subRel, filter, out);
    }
#else
    (void)dirPath; (void)relPrefix; (void)filter;
#endif
}

static void ftpPsramScan(const String& folder, const char* filter,
                         std::vector<FtpCandidate, PsramAllocatorStd<FtpCandidate>>& out) {
    if (!psramVFSIsEnabled()) return;
    std::vector<String> folders = psramVFSListFolders();
    for (size_t i = 0; i < folders.size() && out.size() < FTP_MAX_PENDING; i++) {
        // folder == "/" means "all" (root); otherwise restrict to that one.
        if (folder != "/" && folder != folders[i]) continue;
        std::vector<String> files = psramVFSListFiles(folders[i]);
        for (size_t j = 0; j < files.size() && out.size() < FTP_MAX_PENDING; j++) {
            if (ftpMatchFilter(files[j].c_str(), filter)) {
                size_t sz = psramVFSGetFileSize(folders[i], files[j]);
                if (sz > 0) {
                    FtpCandidate c;
                    c.fs = FTP_FS_PSRAM;
                    c.relPath = (folder == folders[i]) ? files[j] : folders[i] + "/" + files[j];
                    c.readPath = folders[i] + "/" + files[j];
                    c.size = sz;
                    out.push_back(c);
                }
            }
        }
    }
}

static void ftpListCandidates(std::vector<FtpCandidate, PsramAllocatorStd<FtpCandidate>>& out,
                              const String& localRef, const char* filter) {
    out.clear();
    const char* pathP = nullptr;
    FtpLocalFs asked = ftpParseLocalRef(localRef.c_str(), pathP);
    String path = String(pathP);
    if (path.isEmpty() || path == "/") path = "/";
    // /config is never uploaded (any backend, any prefix) — refuse loudly.
    if (ftpIsProtectedUploadPath(path.c_str())) {
        sendVirtualMessage(String(F("FTP: refusing protected path (never uploaded): ")) + path, WARN, false, true);
        return;
    }
    FtpLocalFs fs = ftpResolveLocalFs(asked, sdcardShouldUseSD());
    if (fs == FTP_FS_SD) ftpSdScan(path, "", filter, out);
    else if (asked == FTP_FS_AUTO && !sdcardShouldUseSD()) {
        // Parity without SD: LittleFS mirrors (durable tier — the same content
        // SD would hold) PLUS the PSRAM hot tier (unflushed windows, blobs),
        // deduped by relPath. Either tier alone would miss files; together the
        // no-SD upload covers what the SD upload covers.
        ftpLfsScan(path, "", filter, out);
        std::vector<FtpCandidate, PsramAllocatorStd<FtpCandidate>> hot;
        String folder = (path == "/") ? "/" : ftpBaseName(path);
        ftpPsramScan(folder, filter, hot);
        for (size_t i = 0; i < hot.size() && out.size() < FTP_MAX_PENDING; i++) {
            bool dup = false;
            for (size_t j = 0; j < out.size(); j++) {
                if (out[j].relPath == hot[i].relPath) { dup = true; break; }
            }
            if (!dup) out.push_back(hot[i]);
        }
    }
    else if (fs == FTP_FS_PSRAM) {
        String folder = (path == "/") ? "/" : ftpBaseName(path);
        ftpPsramScan(folder, filter, out);
    }
#if ENABLE_FILESYSTEM==1
    else if (fs == FTP_FS_LFS) ftpLfsScan(path, "", filter, out);
#endif
    else ftpPsramScan("/", filter, out);  // AUTO + SD inactive → PSRAM fallback
}

static String ftpLoadSentList() {
    String s;
    if (!littlefsBegin()) return s;
    File f = LittleFS.open("/config/ftp_sent.log", "r");
    if (!f) return s;
    // Read the NEWEST 8 KB (tail), line-aligned: the trim keeps the tail, so
    // reading the head would silently forget recent entries and re-upload
    // them every cycle once the journal passes 8 KB.
    size_t seek = 0, len = 0;
    ftpSentTailWindow((size_t)f.size(), 8192, 512, &seek, &len);
    if (len == 0) { f.close(); return s; }
    if (seek > 0 && !f.seek(seek)) { f.close(); return s; }
    s.reserve(len + 1);
    while (f.available() && s.length() < len) s += (char)f.read();
    f.close();
    size_t off = ftpSentKeepOffset(s.c_str(), s.length(), 8192);
    if (off > 0) s = s.substring(off);
    return s;
}

static void ftpTrimSentLog() {
    // Cap the only-new journal: past ~16 KB the 8 KB read window silently
    // forgets old entries and they re-upload every cycle. Keep the newest
    // ~8 KB from a line start (atomic .tmp rewrite). Rare path (once per ~100
    // files); a power cut mid-rewrite loses at most the journal (re-uploads).
    if (!littlefsBegin()) return;
    File f = LittleFS.open("/config/ftp_sent.log", "r");
    if (!f) return;
    size_t sz = (size_t)f.size();
    const size_t cap = 16384, keep = 8192;
    if (sz <= cap) { f.close(); return; }
    size_t tail = (sz > keep + 512) ? keep + 512 : sz;
    if (!f.seek(sz - tail)) { f.close(); return; }
    String chunk;
    chunk.reserve(tail + 1);
    while (f.available()) chunk += (char)f.read();
    f.close();
    size_t off = ftpSentKeepOffset(chunk.c_str(), chunk.length(), keep);
    if (off == 0) return;   // untrimmable shape: keep all, retry on next mark
    File w = LittleFS.open("/config/ftp_sent.log.tmp", "w");
    if (!w) return;
    w.print(chunk.substring(off));
    w.close();
    LittleFS.remove("/config/ftp_sent.log");
    LittleFS.rename("/config/ftp_sent.log.tmp", "/config/ftp_sent.log");
}

static void ftpMarkSent(const String& base, size_t size) {
    if (!littlefsBegin()) return;
    File f = LittleFS.open("/config/ftp_sent.log", "a");
    if (!f) return;
    size_t w = f.print(base + ":" + String((unsigned)size) + "\n");
    f.close();
    storNoteLfsWrite(w);
    ftpTrimSentLog();
}

// Best-effort creation of the configured remote base folder and its parents
// ("/Equip15" -> MKD /Equip15; "/a/b" -> MKD /a, MKD /a/b). A missing target
// folder is the usual cause of STOR "553 Could not create file". "Already
// exists" / permission replies are ignored; STOR decides success. Root = no-op.
static void ftpEnsureRemoteBase(WiFiClient& ctl, char* resp, size_t rs, const String& remoteBase) {
    char dirs[8][64];
    int n = ftpRemoteDirList(remoteBase.c_str(), dirs, 8);
    for (int i = 0; i < n; i++) {
        ctl.print(String("MKD ") + dirs[i] + "\r\n");
        readRespLine(ctl, resp, rs, 8000);
    }
}

// Best-effort creation of remote subfolders for a recursive relPath. When the
// session is already inside remoteBase (cwdOk) subfolders are created relative
// to it; otherwise remoteBase is prefixed (absolute path).
// ("logs/2026-09-19/file.csv" -> [<base>/]logs, [<base>/]logs/2026-09-19).
// "Already exists" replies are ignored; STOR decides success.
static void ftpEnsureRemoteDirs(WiFiClient& ctl, char* resp, size_t rs,
                                const String& remoteBase, const String& remoteName) {
    if (remoteName.indexOf('/') < 0) return;
    String prefix = remoteBase;
    int start = 0;
    while (true) {
        int idx = remoteName.indexOf('/', (unsigned)start);
        if (idx < 0) break;
        String seg = remoteName.substring(start, idx);
        start = idx + 1;
        if (seg.isEmpty()) continue;
        if (prefix.length() && !prefix.endsWith("/")) prefix += "/";
        prefix += seg;
        ctl.print(String("MKD ") + prefix + "\r\n");
        readRespLine(ctl, resp, rs, 8000);
    }
}

// Best-effort CWD into the remote base. Returns true when the folder exists
// (or the base is the FTP root/home, where no CWD is needed). Using CWD +
// relative STOR is far more compatible than STOR with an absolute path.
static bool ftpCwdTo(WiFiClient& ctl, char* resp, size_t rs, const String& remoteBase) {
    if (remoteBase.isEmpty() || remoteBase == "/") return true;
    ctl.print(String("CWD ") + remoteBase + "\r\n");
    return readRespLine(ctl, resp, rs, 8000) && ftpReplyIs(resp, 250);
}

bool ftpUploadFileImpl(FtpLocalFs fs, const String& localPath, const String& remoteName,
                       String& detail, bool deleteAfter) {
    bool useSd = (fs == FTP_FS_SD);
    bool usePsram = (fs == FTP_FS_PSRAM);
    // Resolve size first (cheap stat, no bulk DMA), then STREAM the file in
    // internal-RAM chunks via ftpStoreStream — never stage whole files in
    // PSRAM (SDMMC DMA into PSRAM corrupts memory on ESP32-S3; see above).
    size_t fileSize = 0;
    File f;                    // SD / LFS handle, kept open for the stream
    bool sdLocked = false;
    String vfsFolder, vfsFname;  // PSRAM-VFS source identity
#if ENABLE_SDCARD==1
    if (useSd) {
        // Held for the whole stream: the open File must not see concurrent
        // create/delete/rename (log rotation, cleanup) mid-upload.
        if (!sdMutexLock(pdMS_TO_TICKS(5000))) { detail = "SD busy"; return false; }
        sdLocked = true;
        f = SD_MMC.open(localPath, FILE_READ);
        if (f && !f.isDirectory()) fileSize = f.size();
    }
#endif
#if ENABLE_FILESYSTEM==1
    if (!useSd && !usePsram && fs == FTP_FS_LFS) {
        // Covered by the cycle-wide lfsMutex hold above (same as sdLocked).
        if (littlefsBegin()) {
            fs::FS& lfs = getLittleFS();
            f = lfs.open(localPath, FILE_READ);
            if (f && !f.isDirectory()) fileSize = f.size();
        }
    }
#endif
    if (usePsram) {
        // localPath is "folder/filename" (rel to VFS root)
        int sl = localPath.lastIndexOf('/');
        vfsFolder = sl >= 0 ? localPath.substring(0, sl) : String("?");
        vfsFname = sl >= 0 ? localPath.substring(sl + 1) : localPath;
        size_t sz = psramVFSGetFileSize(vfsFolder, vfsFname);
        if (sz > 0 && sz < FTP_MAX_UPLOAD_BYTES) fileSize = sz;
    }
    if (!(fileSize > 0 && fileSize < FTP_MAX_UPLOAD_BYTES)) {
        if (f) f.close();
        if (sdLocked) sdMutexUnlock();
        detail = "open/read failed or empty";
        return false;
    }
    WiFiClient ctl;
    ctl.setTimeout(8000);
    char resp[192];
    String host = configManager.getFtpHost();
    int port = ftpClampPort(configManager.getFtpPort());
    bool ok = false;
    if (!ctl.connect(host.c_str(), (uint16_t)port)) detail = "control connect failed";
    else if (!ftpLogin(ctl, resp, sizeof(resp), detail)) ok = false;
    else {
        String remoteBase = ftpRemoteBase();
        // Prefer CWD + relative STOR: some servers reject STOR with an absolute
        // path, and a target folder that doesn't exist returns 553. Create the
        // base folder first when it's missing, then retry the CWD.
        bool cwdOk = ftpCwdTo(ctl, resp, sizeof(resp), remoteBase);
        if (!cwdOk && remoteBase != "/") {
            ftpEnsureRemoteBase(ctl, resp, sizeof(resp), remoteBase);
            cwdOk = ftpCwdTo(ctl, resp, sizeof(resp), remoteBase);
        }
        ftpEnsureRemoteDirs(ctl, resp, sizeof(resp), cwdOk ? String("") : remoteBase, remoteName);
        String remote = cwdOk ? remoteName : (remoteBase + "/" + remoteName);
        size_t sent = 0;
        FtpFileReaderCtx fctx{ &f, fileSize };
        FtpVfsReaderCtx vctx{ vfsFolder, vfsFname, 0, fileSize };
        bool stored;
        if (usePsram) stored = ftpStoreStream(ctl, fileSize, remote, resp, sizeof(resp), detail, sent, ftpVfsReadChunk, &vctx);
        else stored = ftpStoreStream(ctl, fileSize, remote, resp, sizeof(resp), detail, sent, ftpFileReadChunk, &fctx);
        if (f) f.close();
        if (sdLocked) sdMutexUnlock();
        if (stored) {
            ok = true;
            detail = String("OK ") + remoteName + " (" + String((unsigned)sent) + "B)";
            ftpMarkSent(remoteName, fileSize);
            if (deleteAfter) {
                // SIZE verify before local delete
                ctl.print(String("SIZE ") + remote + "\r\n");
                char sr[96];
                if (readRespLine(ctl, sr, sizeof(sr), 8000) && ftpReplyIs(sr, 213)) {
                    long rsz = atol(sr + 4);
                    if (rsz == (long)fileSize) {
                        if (useSd) {
#if ENABLE_SDCARD==1
                            if (sdMutexLock(pdMS_TO_TICKS(2000))) { SD_MMC.remove(localPath); sdMutexUnlock(); }
#endif
                        } else if (fs == FTP_FS_LFS) {
#if ENABLE_FILESYSTEM==1
                            if (littlefsBegin()) getLittleFS().remove(localPath);
#endif
                        }
                        // PSRAM VFS has per-file remove only via folder cleanup; kept local.
                    } else detail += " (size mismatch, kept local)";
                } else detail += " (SIZE verify failed, kept local)";
            }
        }
        ctl.print("QUIT\r\n");
    }
    ctl.stop();
    ftpLock();
    // NOTE: busy stays set — the monitor cycle owns it and clears it at the end.
    strncpy(s_status.lastFile, remoteName.c_str(), sizeof(s_status.lastFile) - 1);
    strncpy(s_status.lastResult, detail.c_str(), sizeof(s_status.lastResult) - 1);
    s_status.lastTime = millis();
    s_status.lastCode = ok ? 226 : 500;
    if (ok) s_status.okCount++; else s_status.failCount++;
    strncpy(s_status.activeSource, useSd ? "SD" : (usePsram ? "PSRAM" : "LFS"), sizeof(s_status.activeSource) - 1);
    ftpUnlock();
    sendVirtualMessage(String(F("FTP upload ")) + detail, ok ? 0 : 1, true, true);
    return ok;
}

static int ftpRunCycle(String& detail) {
    if (!configManager.getFtpEnabled() || !ftpIsAvailable()) { detail = "disabled/incomplete"; return 0; }
    ftpLock(); if (s_status.busy) { ftpUnlock(); detail = "busy"; return 0; } s_status.busy = true; ftpUnlock();
    String localRef = configManager.getFtpLocalDir();
    String filter = configManager.getFtpFilter();
    bool onlyNew = configManager.getFtpOnlyNew();
    bool deleteAfter = configManager.getFtpDeleteAfter();
    int maxFiles = ftpClampMaxFiles(configManager.getFtpMaxFiles());
#if ENABLE_SDCARD==1
    // Exclusive SD for the WHOLE scan+upload cycle: FTP must be the only SD
    // user while it runs. The old per-entry yield let concurrent writers
    // (proximity CSV appends, web pages) interleave with the scan and wedge
    // the SDMMC host (crash-loop, HISTORIC #57). While held here, writers wait
    // their short timeout and skip; page handlers answer "sd_busy". The mutex
    // is recursive, so the per-upload sdMutexLock below just nests.
    const char* pathP = nullptr;
    FtpLocalFs fs = ftpParseLocalRef(localRef.c_str(), pathP);
    fs = ftpResolveLocalFs(fs, sdcardShouldUseSD());
    bool sdLocked = false;
    if (fs == FTP_FS_SD) {
        sdLocked = sdMutexLock(pdMS_TO_TICKS(8000));
        if (!sdLocked) {
            ftpLock(); s_status.busy = false; ftpUnlock();
            detail = "SD busy"; return 0;
        }
    }
#endif
#if ENABLE_FILESYSTEM==1
    // Exclusive LittleFS for the WHOLE scan+upload cycle when the cycle can
    // touch flash (explicit LFS ref, or legacy AUTO without SD which merges
    // the LittleFS mirrors). Same discipline as sdLocked above: while held,
    // writers wait their short timeout and skip. Recursive: per-upload locks
    // below just nest. (Crash guard: emailQueueProc LittleFS stat vs loop
    // writers, .24 2026-10-14.)
    bool lfsLocked = false;
    {
        const char* lpP = nullptr;
        FtpLocalFs askedFs = ftpParseLocalRef(localRef.c_str(), lpP);
        bool touchesFlash = (fs == FTP_FS_LFS) ||
                            (askedFs == FTP_FS_AUTO && !sdcardShouldUseSD());
        if (touchesFlash) {
            extern bool lfsMutexLock(TickType_t timeoutTicks);
            lfsLocked = lfsMutexLock(pdMS_TO_TICKS(8000));
            if (!lfsLocked) {
                ftpLock(); s_status.busy = false; ftpUnlock();
#if ENABLE_SDCARD==1
                if (sdLocked) sdMutexUnlock();
#endif
                detail = "LittleFS busy"; return 0;
            }
        }
    }
#endif
    std::vector<FtpCandidate, PsramAllocatorStd<FtpCandidate>> cands;
    ftpListCandidates(cands, localRef, filter.c_str());
    String sent = onlyNew ? ftpLoadSentList() : String("");
    // Each successful file appends "relPath:size\n"; reserve the worst case up
    // front so the loop does not realloc the growing String (fragmentation).
    if (onlyNew) sent.reserve(sent.length() + (size_t)maxFiles * 96 + 1);
    int done = 0;
    for (size_t i = 0; i < cands.size() && done < maxFiles; i++) {
        // Oversized files can never upload (4 MB cap): skip loudly instead of
        // failing every cycle and wedging the queue behind them (the old
        // `break` retried them forever).
        if (cands[i].size >= FTP_MAX_UPLOAD_BYTES) {
            sendVirtualMessage(String(F("FTP skip (over 4MB cap): ")) + cands[i].relPath, 0, true, false);
            continue;
        }
        if (!ftpShouldUpload(cands[i].relPath.c_str(), cands[i].size, filter.c_str(), onlyNew, sent.c_str())) continue;
        String d;
        if (ftpUploadFileImpl(cands[i].fs, cands[i].readPath, cands[i].relPath, d, deleteAfter)) {
            done++; sent += cands[i].relPath + ":" + String((unsigned)cands[i].size) + "\n";
        }
        else if (ftpIsConnectFailure(d.c_str())) break;  // server/session down: retry next interval
        else {
            // Per-file failure (rejected STOR, bad data socket, unreadable
            // file): skip loudly and continue with the rest of the queue
            // instead of wedging behind one bad file every cycle.
            sendVirtualMessage(String(F("FTP skip (file failed, continuing): ")) + cands[i].relPath + " | " + d, 1, true, false);
        }
    }
#if ENABLE_SDCARD==1
    if (sdLocked) sdMutexUnlock();
#endif
#if ENABLE_FILESYSTEM==1
    extern void lfsMutexUnlock();
    if (lfsLocked) lfsMutexUnlock();
#endif
    ftpLock(); s_status.busy = false; ftpUnlock();
    s_lastCycle = millis();
    _ftpRtcSaveEpoch();
    detail = String("cycle done: ") + done + " file(s)";
    return done;
}

int ftpUploadNow(String& detail) { return ftpRunCycle(detail); }

void ftpMonitorTick() {
    if (!configManager.getFtpEnabled() || !configManager.getFtpAutoUpload()) return;
    // No network yet (early boot, WiFi reconnecting): skip without touching
    // s_lastCycle so the next 30 s tick retries — never burn a cycle/fail.
    if (!hasNetworkConnectivity()) return;
    int iv = ftpClampIntervalMin(configManager.getFtpIntervalMin());
    if (s_lastCycle && millis() - s_lastCycle < (unsigned long)iv * 60000UL) {
        // Countdown quiet. After a warm reboot/OTA the countdown restarted at
        // boot; if the RTC anchor shows the next cycle is already due, break
        // the wait so the every-interval phase survives the reboot. A 2 min
        // floor keeps the post-boot SD/PSRAM settle window from the ftpMon
        // crash history (first auto cycle used to crash right after boot).
        uint32_t nowEpoch = (time(nullptr) > 1000) ? (uint32_t)time(nullptr) : 0;
        if (!s_ftpRtcValid || !nowEpoch ||
            !ftpCycleDueEpoch(s_ftpRtc.lastCyEpoch, nowEpoch, (uint32_t)iv * 60u) ||
            millis() < 120000UL) return;
    }
    String d;
    ftpRunCycle(d);
    // Record the cycle's peak stack use here (after the deepest call): reads
    // into the monitor + /api/ftp-status so a regression back toward overflow
    // is visible before it turns into another ftpMon crash. Skipped early-outs
    // (disabled/incomplete, busy) do not count as cycles and are not logged.
    if (!d.startsWith("cycle done")) return;
    uint32_t freeB = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    ftpLock();
    s_status.monitorStackFree = (uint16_t)(freeB > 0xFFFF ? 0xFFFF : freeB);
    ftpUnlock();
    sendVirtualMessage(String(F("FTP cycle: ")) + d + F(" (monitor stack free ") + String((unsigned)freeB) + F("B)"), 0, true, true);
}

static void ftpMonitorTask(void* p) {
    (void)p;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        ftpMonitorTick();
    }
}

#endif
#endif
