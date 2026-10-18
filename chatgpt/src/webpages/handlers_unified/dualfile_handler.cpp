#include "dualfile_handler.h"

#include "../../feature_flags.h"

#if ENABLE_DUALFILE==1

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>

#include "../../arduinojson_fix.h"
#include <ArduinoJson.h>

#include "../../ConfigManager.h"
#include "../../littlefs_manager.h"
#include "../../psram_vfs.h"
#include "../../sd_security.h"
#include "../../sdcard.h"
#include "../../structures.h"

extern ConfigManager configManager;
extern volatile StorageMode currentStorageMode;
extern bool acquireSDAccess(const char* owner, uint32_t timeoutMs);
extern void releaseSDAccess();

// =============================================================================
// Local helpers (duplicated from files_handler.cpp for encapsulation)
// =============================================================================

namespace {

static unsigned long lastSdOperationMs = 0;
static const unsigned long SD_OPERATION_DELAY_MS = 1000;

bool isSdOperationAllowed() {
    unsigned long now = millis();
    unsigned long elapsed = now - lastSdOperationMs;
    if (now < lastSdOperationMs) elapsed = now;
    return elapsed >= SD_OPERATION_DELAY_MS;
}

void updateSdOperationTime() {
    lastSdOperationMs = millis();
}

String getFormValue(const String& src, const char* key) {
    String search = String(key) + "=";
    int idx = src.indexOf(search);
    if (idx < 0) return String();
    int start = idx + search.length();
    int end = src.indexOf('&', start);
    return end < 0 ? src.substring(start) : src.substring(start, end);
}

String resolveStorageType(const HttpRequest& req) {
    const String& q = req.query.length() ? req.query : req.body;
    String storageType = getFormValue(q, "storage");
    storageType.toLowerCase();
    bool supported = false;
#if ENABLE_SDCARD==1
    if (storageType == "sd") supported = true;
#endif
#if ENABLE_FILESYSTEM==1
    if (storageType == "flash") supported = true;
#endif
    if (storageType == "psram" && (psramFound() || psramVFSIsEnabled())) supported = true;
    if (supported) return storageType;
#if ENABLE_SDCARD==1
    if (sdcardIsMounted()) return "sd";
#endif
    if (psramVFSIsEnabled()) return "psram";
#if ENABLE_FILESYSTEM==1
    return "flash";
#elif ENABLE_SDCARD==1
    return "sd";
#else
    return "psram";
#endif
}

struct FsResult {
    fs::FS* fs = nullptr;
    bool usesFlash = false;
    bool mounted = false;
};

FsResult getFileSystem(const HttpRequest& req) {
    FsResult r;
    String storageType = resolveStorageType(req);
    r.usesFlash = (storageType == "flash");
    if (storageType == "psram") {
        r.mounted = psramVFSIsEnabled();
        return r;
    }
    if (r.usesFlash) {
#if ENABLE_FILESYSTEM==1
        r.mounted = littlefsMounted();
        if (r.mounted) r.fs = &getLittleFS();
#else
        r.mounted = false;
#endif
    } else {
#if ENABLE_SDCARD==1
        if (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled()) {
            r.mounted = false;
            return r;
        }
        r.mounted = sdcardIsMounted();
        if (r.mounted) r.fs = &SD_MMC;
#else
        r.mounted = false;
#endif
    }
    return r;
}

String getParam(const HttpRequest& req, const char* key) {
    String v = getFormValue(req.body, key);
    if (v.length()) return v;
    return getFormValue(req.query, key);
}

} // namespace

// =============================================================================
// GET /api/dualfile/list?path=/&storage=sd
// =============================================================================

void handleDualFileListGet(HttpRequest& req, HttpResponse& res) {
    String path = getParam(req, "path");
    if (!path.length()) path = "/";
    path = SDSecurity::normalizePath(path);
    if (!SDSecurity::isPathSafe(path)) {
        res.send(400, "application/json", F("{\"success\":false,\"error\":\"Path traversal blocked\"}"));
        return;
    }

    String st = resolveStorageType(req);
    FsResult fsr = getFileSystem(req);
    if (st == "psram" && psramVFSIsEnabled()) {
        // PSRAM VFS listing not supported in Phase 1
        StaticJsonDocument<256> doc;
        doc["success"] = false;
        doc["error"] = F("PSRAM VFS listing not supported");
        doc["path"] = path;
        doc["storage"] = "psram";
        String out;
        serializeJson(doc, out);
        res.sendJson(out);
        return;
    }

#if ENABLE_SDCARD==1
    if (st == "sd") {
        if (!acquireSDAccess("dualfile-list", 8000)) {
            res.send(503, "application/json", F("{\"success\":false,\"error\":\"SD busy\"}"));
            return;
        }
        if (!sdcardIsMounted()) {
            releaseSDAccess();
            StaticJsonDocument<256> doc;
            doc["success"] = false;
            doc["error"] = F("SD not mounted");
            doc["path"] = path;
            doc["storage"] = "sd";
            String out;
            serializeJson(doc, out);
            res.sendJson(out);
            return;
        }
    }
#endif
    if (!fsr.fs || !fsr.mounted) {
#if ENABLE_SDCARD==1
        if (st == "sd") releaseSDAccess();
#endif
        StaticJsonDocument<256> doc;
        doc["success"] = false;
        doc["error"] = F("Storage not mounted");
        doc["path"] = path;
        doc["storage"] = st;
        String out;
        serializeJson(doc, out);
        res.sendJson(out);
        return;
    }

    if (!isSdOperationAllowed()) {
#if ENABLE_SDCARD==1
        if (st == "sd") releaseSDAccess();
#endif
        res.send(429, "application/json", F("{\"success\":false,\"error\":\"Rate limit\"}"));
        return;
    }

    updateSdOperationTime();

    File dir = fsr.fs->open(path.c_str());
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
#if ENABLE_SDCARD==1
        if (st == "sd") releaseSDAccess();
#endif
        StaticJsonDocument<256> doc;
        doc["success"] = false;
        doc["error"] = F("ENOENT");
        doc["path"] = path;
        doc["storage"] = st;
        String out;
        serializeJson(doc, out);
        res.sendJson(out);
        return;
    }

    DynamicJsonDocument doc(8192);
    doc["success"] = true;
    doc["path"] = path;
    doc["separator"] = "/";

    if (path == "/") {
        doc["parent"] = nullptr;
    } else {
        String parent = path;
        if (parent.endsWith("/")) parent.remove(parent.length() - 1);
        int slash = parent.lastIndexOf('/');
        parent = (slash <= 0) ? "/" : parent.substring(0, slash);
        doc["parent"] = parent;
    }

    JsonArray entries = doc.createNestedArray("entries");

    // Parent directory entry
    if (path != "/") {
        JsonObject up = entries.createNestedObject();
        up["n"] = "..";
        up["d"] = 1;
        up["s"] = 0;
        up["t"] = 0;
    }

    File entry = dir.openNextFile();
    while (entry) {
        JsonObject e = entries.createNestedObject();
        e["n"] = String(entry.name());
        e["d"] = entry.isDirectory() ? 1 : 0;
        e["s"] = entry.size();
        e["t"] = entry.getLastWrite();
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    doc["storage"] = st;

    // Storage info (total/used/free) - omit if unavailable
    if (fsr.usesFlash) {
#if ENABLE_FILESYSTEM==1
        size_t total = LittleFS.totalBytes();
        size_t used  = LittleFS.usedBytes();
        doc["total_kb"] = total / 1024;
        doc["used_kb"]  = used / 1024;
        doc["free_kb"]  = (total - used) / 1024;
#endif
    }
#if ENABLE_SDCARD==1
    else if (st == "sd") {
        uint64_t total = SD_MMC.totalBytes();
        uint64_t used  = SD_MMC.usedBytes();
        doc["total_kb"] = (unsigned long)(total / 1024);
        doc["used_kb"]  = (unsigned long)(used / 1024);
        doc["free_kb"]  = (unsigned long)((total - used) / 1024);
    }
#endif
    else if (st == "flash") {
#if ENABLE_FILESYSTEM==1
        size_t total = LittleFS.totalBytes();
        size_t used  = LittleFS.usedBytes();
        doc["total_kb"] = total / 1024;
        doc["used_kb"]  = used / 1024;
        doc["free_kb"]  = (total - used) / 1024;
#endif
    }

    String out;
    serializeJson(doc, out);
    res.sendJson(out);

#if ENABLE_SDCARD==1
    if (st == "sd") releaseSDAccess();
#endif
}

// =============================================================================
// POST /api/dualfile/copy
//   body: src=<path>&src_storage=<sd|flash>&dst=<path>&dst_storage=<sd|flash>
// =============================================================================

void handleDualFileCopyPost(HttpRequest& req, HttpResponse& res) {
    String src   = getParam(req, "src");
    String dst   = getParam(req, "dst");
    String srcSt = getParam(req, "src_storage");
    String dstSt = getParam(req, "dst_storage");

    if (!src.length() || !dst.length()) {
        res.send(400, "application/json", F("{\"success\":false,\"error\":\"src and dst required\"}"));
        return;
    }
    if (!srcSt.length()) srcSt = resolveStorageType(req);
    if (!dstSt.length()) dstSt = srcSt;

    src = SDSecurity::normalizePath(src);
    dst = SDSecurity::normalizePath(dst);
    if (!SDSecurity::isPathSafe(src) || !SDSecurity::isPathSafe(dst)) {
        res.send(400, "application/json", F("{\"success\":false,\"error\":\"Path traversal blocked\"}"));
        return;
    }

    // Resolve source FS
    HttpRequest srcReq;
    srcReq.query = String("storage=") + srcSt;
    FsResult srcFs = getFileSystem(srcReq);
    if (!srcFs.fs || !srcFs.mounted) {
        res.send(503, "application/json", F("{\"success\":false,\"error\":\"Source storage not mounted\"}"));
        return;
    }

    // Resolve dest FS
    HttpRequest dstReq;
    dstReq.query = String("storage=") + dstSt;
    FsResult dstFs = getFileSystem(dstReq);
    if (!dstFs.fs || !dstFs.mounted) {
        res.send(503, "application/json", F("{\"success\":false,\"error\":\"Dest storage not mounted\"}"));
        return;
    }

    bool needSdLock = (srcSt == "sd" || dstSt == "sd");
    if (needSdLock) {
        if (!acquireSDAccess("dualfile-copy", 8000)) {
            res.send(503, "application/json", F("{\"success\":false,\"error\":\"SD busy\"}"));
            return;
        }
    }

    if (!isSdOperationAllowed()) {
        if (needSdLock) releaseSDAccess();
        res.send(429, "application/json", F("{\"success\":false,\"error\":\"Rate limit\"}"));
        return;
    }

    updateSdOperationTime();

    File srcFile = srcFs.fs->open(src.c_str(), FILE_READ);
    if (!srcFile) {
        if (needSdLock) releaseSDAccess();
        StaticJsonDocument<256> doc;
        doc["success"] = false;
        doc["error"] = F("ENOENT");
        doc["src"] = src;
        String out;
        serializeJson(doc, out);
        res.sendJson(out);
        return;
    }

    size_t totalSize = srcFile.size();
    if (totalSize > 8 * 1024 * 1024) {
        srcFile.close();
        if (needSdLock) releaseSDAccess();
        StaticJsonDocument<256> doc;
        doc["success"] = false;
        doc["error"] = F("File too large (>8MB)");
        doc["src"] = src;
        String out;
        serializeJson(doc, out);
        res.sendJson(out);
        return;
    }

    File dstFile = dstFs.fs->open(dst.c_str(), FILE_WRITE);
    if (!dstFile) {
        srcFile.close();
        if (needSdLock) releaseSDAccess();
        StaticJsonDocument<256> doc;
        doc["success"] = false;
        doc["error"] = F("Cannot create destination");
        doc["dst"] = dst;
        String out;
        serializeJson(doc, out);
        res.sendJson(out);
        return;
    }

    uint8_t buf[4096];
    size_t copied = 0;
    while (copied < totalSize) {
        vTaskDelay(pdMS_TO_TICKS(10));  // yield for watchdog; no rate limit on copy stream
        size_t toRead = (totalSize - copied > sizeof(buf)) ? sizeof(buf) : (totalSize - copied);
        size_t rd = srcFile.read(buf, toRead);
        if (rd == 0) break;
        dstFile.write(buf, rd);
        copied += rd;
        updateSdOperationTime();
    }

    srcFile.close();
    dstFile.close();

    if (dstFs.usesFlash) flashIncrementWriteCount(copied);
    if (needSdLock) releaseSDAccess();

    StaticJsonDocument<256> doc;
    doc["success"] = true;
    doc["copied"] = (unsigned long)copied;
    doc["path"] = dst;
    String out;
    serializeJson(doc, out);
    res.sendJson(out);
}

// =============================================================================
// GET /api/dualfile/view?path=/x.txt&storage=sd&mode=text&offset=0&limit=4096
// =============================================================================

void handleDualFileViewGet(HttpRequest& req, HttpResponse& res) {
    String path = getParam(req, "path");
    if (!path.length()) {
        res.send(400, "application/json", F("{\"success\":false,\"error\":\"path required\"}"));
        return;
    }
    path = SDSecurity::normalizePath(path);
    if (!SDSecurity::isPathSafe(path)) {
        res.send(400, "application/json", F("{\"success\":false,\"error\":\"Path traversal blocked\"}"));
        return;
    }

    long offsetLong = getParam(req, "offset").toInt();
    if (offsetLong < 0) offsetLong = 0;
    size_t offset = (size_t)offsetLong;
    long limitLong = getParam(req, "limit").toInt();
    if (limitLong < 0) limitLong = 0;
    size_t limit = (size_t)limitLong;
    if (limit == 0 || limit > 65536) limit = 4096;
    String mode = getParam(req, "mode");
    if (mode.length() == 0) mode = "text";

    FsResult fsr = getFileSystem(req);
    if (!fsr.fs || !fsr.mounted) {
        res.send(503, "application/json", F("{\"success\":false,\"error\":\"Storage not mounted\"}"));
        return;
    }

    File f = fsr.fs->open(path.c_str(), FILE_READ);
    if (!f) {
        StaticJsonDocument<256> doc;
        doc["success"] = false;
        doc["error"] = F("ENOENT");
        String out;
        serializeJson(doc, out);
        res.sendJson(out);
        return;
    }

    if (f.isDirectory()) {
        f.close();
        StaticJsonDocument<256> doc;
        doc["success"] = false;
        doc["error"] = F("EISDIR");
        String out;
        serializeJson(doc, out);
        res.sendJson(out);
        return;
    }

    size_t totalSize = f.size();
    if (offset >= totalSize) {
        f.close();
        StaticJsonDocument<256> doc;
        doc["success"] = true;
        doc["total_size"] = (unsigned long)totalSize;
        doc["truncated"] = false;
        doc["content"] = "";
        String out;
        serializeJson(doc, out);
        res.sendJson(out);
        return;
    }
    bool truncated = (totalSize > offset + limit);

    if (offset > 0) f.seek(offset, SeekSet);

    size_t toRead = limit;
    if (offset + toRead > totalSize) toRead = totalSize - offset;

    DynamicJsonDocument doc(8192);
    doc["success"] = true;
    doc["total_size"] = (unsigned long)totalSize;
    doc["truncated"] = truncated;

    if (mode == "hex") {
        // Hex mode: read as bytes, build hex + ascii strings
        uint8_t* buf = (uint8_t*)malloc(toRead > 0 ? toRead : 1);
        if (!buf) {
            f.close();
            res.send(500, "application/json", F("{\"success\":false,\"error\":\"OOM\"}"));
            return;
        }
        size_t rd = f.read(buf, toRead);
        f.close();

        String hexStr;
        hexStr.reserve(rd * 3 + 1);
        String asciiStr;
        asciiStr.reserve(rd + 1);
        char hexBuf[4];
        for (size_t i = 0; i < rd; i++) {
            if (i > 0) hexStr += " ";
            snprintf(hexBuf, sizeof(hexBuf), "%02X", (unsigned int)buf[i]);
            hexStr += hexBuf;
            char c = (char)buf[i];
            asciiStr += (c >= 32 && c < 127) ? c : '.';
        }
        free(buf);
        doc["hex"]   = hexStr;
        doc["ascii"] = asciiStr;
    } else {
        // Text mode
        String content;
        if (toRead > 0) {
            uint8_t* buf = (uint8_t*)malloc(toRead + 1);
            if (!buf) {
                f.close();
                res.send(500, "application/json", F("{\"success\":false,\"error\":\"OOM\"}"));
                return;
            }
            size_t rd = f.read(buf, toRead);
            buf[rd] = 0;
            content = String((char*)buf);
            free(buf);
        }
        f.close();
        doc["content"] = content;
    }

    String out;
    serializeJson(doc, out);
    res.sendJson(out);
}

// =============================================================================
// POST /api/dualfile/write
//   body: path=/x.txt&storage=sd&content=...
// =============================================================================

void handleDualFileWritePost(HttpRequest& req, HttpResponse& res) {
    String path = getParam(req, "path");
    String content = getParam(req, "content");

    if (!path.length()) {
        res.send(400, "application/json", F("{\"success\":false,\"error\":\"path required\"}"));
        return;
    }
    path = SDSecurity::normalizePath(path);
    if (!SDSecurity::isPathSafe(path)) {
        res.send(400, "application/json", F("{\"success\":false,\"error\":\"Path traversal blocked\"}"));
        return;
    }

    if (content.length() > 65536) {
        res.send(413, "application/json", F("{\"success\":false,\"error\":\"Content too large (>64KB)\"}"));
        return;
    }

    FsResult fsr = getFileSystem(req);
    if (!fsr.fs || !fsr.mounted) {
        res.send(503, "application/json", F("{\"success\":false,\"error\":\"Storage not mounted\"}"));
        return;
    }

    bool needSdLock = !fsr.usesFlash;
    if (needSdLock) {
        if (!acquireSDAccess("dualfile-write", 8000)) {
            res.send(503, "application/json", F("{\"success\":false,\"error\":\"SD busy\"}"));
            return;
        }
    }

    if (!isSdOperationAllowed()) {
        if (needSdLock) releaseSDAccess();
        res.send(429, "application/json", F("{\"success\":false,\"error\":\"Rate limit\"}"));
        return;
    }

    updateSdOperationTime();

    File f = fsr.fs->open(path.c_str(), FILE_WRITE);
    if (!f) {
        if (needSdLock) releaseSDAccess();
        StaticJsonDocument<256> doc;
        doc["success"] = false;
        doc["error"] = F("Cannot open file for writing");
        String out;
        serializeJson(doc, out);
        res.sendJson(out);
        return;
    }

    size_t written = f.print(content);
    f.close();

    if (fsr.usesFlash) flashIncrementWriteCount(written);
    if (needSdLock) releaseSDAccess();

    StaticJsonDocument<256> doc;
    doc["success"] = true;
    doc["written"] = (unsigned long)written;
    doc["path"] = path;
    String out;
    serializeJson(doc, out);
    res.sendJson(out);
}

// =============================================================================
// POST /api/dualfile/upload (body accumulator, manual route)
// =============================================================================

struct DualFileUploadCtx {
    fs::File file;
    String path;
    String storage;
    bool failed = false;
    bool usesFlash = false;
    bool sdLockAcquired = false;
    size_t written = 0;
    size_t totalSize = 0;
};

void handleDualFileUploadBody(AsyncWebServerRequest* request, uint8_t* data,
    size_t len, size_t index, size_t total) {
    DualFileUploadCtx* ctx = (DualFileUploadCtx*)request->_tempObject;

    if (index == 0) {
        ctx = new (std::nothrow) DualFileUploadCtx();
        if (!ctx) { request->send(500, "application/json", F("{\"success\":false,\"error\":\"OOM\"}")); return; }
        request->_tempObject = ctx;
        ctx->totalSize = total;

        const AsyncWebParameter* pPath = request->getParam("path");
        const AsyncWebParameter* pStor = request->getParam("storage");
        if (!pPath) { ctx->failed = true; return; }
        ctx->path = SDSecurity::normalizePath(pPath->value());
        if (!SDSecurity::isPathSafe(ctx->path)) { ctx->failed = true; return; }
        ctx->storage = pStor ? pStor->value() : String("sd");
        ctx->storage.toLowerCase();

        fs::FS* fs = nullptr;
        if (ctx->storage == "sd") {
#if ENABLE_SDCARD==1
            if (!acquireSDAccess("dualfile-upload", 8000)) { ctx->failed = true; return; }
            ctx->sdLockAcquired = true;
            if (!sdcardIsMounted()) { ctx->failed = true; return; }
            fs = &SD_MMC;
#endif
        } else if (ctx->storage == "flash") {
#if ENABLE_FILESYSTEM==1
            if (!littlefsMounted()) { ctx->failed = true; return; }
            fs = &getLittleFS();
            ctx->usesFlash = true;
#endif
        }
        if (!fs) { ctx->failed = true; return; }

        ctx->file = fs->open(ctx->path.c_str(), FILE_WRITE);
        if (!ctx->file) { ctx->failed = true; return; }
    }

    if (ctx->failed) return;

    if (len > 0) {
        size_t w = ctx->file.write(data, len);
        ctx->written += w;
    }

    if (total > 0 && index + len >= total) {
        ctx->file.close();
        ctx->totalSize = total;
    }
}

void handleDualFileUploadPost(AsyncWebServerRequest* request) {
    DualFileUploadCtx* ctx = (DualFileUploadCtx*)request->_tempObject;
    if (!ctx) {
        request->send(400, "application/json", F("{\"success\":false,\"error\":\"No upload data\"}"));
        return;
    }
    if (ctx->file) ctx->file.close();
    if (ctx->sdLockAcquired) releaseSDAccess();
    if (ctx->usesFlash) flashIncrementWriteCount(ctx->written);

    DynamicJsonDocument doc(256);
    if (ctx->failed) {
        doc["success"] = false;
        doc["error"] = F("Upload failed");
    } else {
        doc["success"] = true;
        doc["written"] = (unsigned long)ctx->written;
        doc["path"] = ctx->path;
    }
    String out;
    serializeJson(doc, out);
    request->send(ctx->failed ? 500 : 200, "application/json", out);
    delete ctx;
    request->_tempObject = nullptr;
}

#else // ENABLE_DUALFILE==0

void handleDualFileListGet(HttpRequest& req, HttpResponse& res)  { (void)req; res.send(404, "application/json", F("{\"error\":\"DualFile not enabled\"}")); }
void handleDualFileCopyPost(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"DualFile not enabled\"}")); }
void handleDualFileViewGet(HttpRequest& req, HttpResponse& res)  { (void)req; res.send(404, "application/json", F("{\"error\":\"DualFile not enabled\"}")); }
void handleDualFileWritePost(HttpRequest& req, HttpResponse& res){ (void)req; res.send(404, "application/json", F("{\"error\":\"DualFile not enabled\"}")); }
void handleDualFileUploadPost(AsyncWebServerRequest* request)    { (void)request; request->send(404, "application/json", F("{\"error\":\"DualFile not enabled\"}")); }
void handleDualFileUploadBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) { (void)request; (void)data; (void)len; (void)index; (void)total; }

#endif // ENABLE_DUALFILE
