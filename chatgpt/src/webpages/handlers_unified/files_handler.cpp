#include "files_handler.h"
#include "../../feature_flags.h"

#if ENABLE_SDCARD == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
#include "../../touch7_display.h" // Touch7FlashGuard (mutating ops hit flash)
#endif

#include <Arduino.h>
#include "../../languages/lang_active.h"
#include <FS.h>
#include <esp_task_wdt.h>
#include "../../ConfigManager.h"
#include "../../littlefs_manager.h"
#include "../../structures.h"
#include "../../psram_vfs.h"
#include "../../sd_security.h"
#include "../../sdcard.h"

extern ConfigManager configManager;
extern volatile StorageMode currentStorageMode;
extern bool acquireSDAccess(const char* owner, uint32_t timeoutMs);
extern void releaseSDAccess();

static unsigned long lastSdOperationMs = 0;
static const unsigned long SD_OPERATION_DELAY_MS = 1000;

namespace {

String getFormValue(const String& src, const char* key) {
    String search = String(key) + "=";
    int idx = src.indexOf(search);
    if (idx < 0) return String();
    int start = idx + search.length();
    int end = src.indexOf('&', start);
    return end < 0 ? src.substring(start) : src.substring(start, end);
}

bool isSdOperationAllowed() {
    unsigned long now = millis();
    unsigned long elapsed = now - lastSdOperationMs;
    if (now < lastSdOperationMs) elapsed = now;
    return elapsed >= SD_OPERATION_DELAY_MS;
}

void updateSdOperationTime() {
    lastSdOperationMs = millis();
}

String resolveStorageType(const HttpRequest& req) {
    const String& q = req.query.length() ? req.query : req.body;
    String storageType = getFormValue(q, "storage");
    storageType.toLowerCase();
    bool storageSupported = false;
#if ENABLE_SDCARD==1
    if (storageType == "sd") storageSupported = true;
#endif
#if ENABLE_FILESYSTEM==1
    if (storageType == "flash") storageSupported = true;
#endif
    if (storageType == "psram" && (psramFound() || psramVFSIsEnabled())) storageSupported = true;
    if (storageSupported) return storageType;
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

bool hasParam(const HttpRequest& req, const char* key) {
    return getParam(req, key).length() > 0;
}

bool wantsJson(const HttpRequest& req) {
    String v = getFormValue(req.query, "json");
    if (v.length() == 0) v = getFormValue(req.body, "json");
    return v == "1";
}

} // namespace

void handleSdMountPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    sdcardBeginAsync();
    res.redirect("/files");
}

void handleSdUnmountPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    sdcardUnmount();
    res.redirect("/files");
}

void handleSdCreateFilePost(HttpRequest& req, HttpResponse& res) {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    Touch7PanelHold _t7g;
#endif
    String st = resolveStorageType(req);
    if (st == "psram") {
        res.send(501, "application/json", (String(F("{\"success\":false,\"error\":\"")) + L(activeLang->apiErrPsramUnsupported) + "\"}"));
        return;
    }

    FsResult fsr = getFileSystem(req);
    if (!fsr.fs || !fsr.mounted) {
        res.send(503, "application/json", F("{\"success\":false,\"error\":\"Filesystem not mounted\"}"));
        return;
    }

    if (!isSdOperationAllowed()) {
        res.send(429, "text/plain", F("Aguarde 1 segundo entre operacoes"));
        return;
    }

    String name = getParam(req, "name");
    if (!name.length()) {
        String url = String("/files?path=") + getParam(req, "cwd") + "&storage=" + st;
        res.redirect(url);
        return;
    }

    String cwd = getParam(req, "cwd");
    if (!cwd.length()) cwd = "/";
    if (cwd[0] != '/') cwd = "/" + cwd;

    String full = cwd;
    if (full != "/") full += "/";
    full += name;
    full.replace("//", "/");

    updateSdOperationTime();
    File file = fsr.fs->open(full.c_str(), FILE_WRITE);
    bool created = (bool)file;
    if (file) {
        file.close();
        if (fsr.usesFlash) flashIncrementWriteCount(0);
    }

    delay(50);
    if (wantsJson(req)) {
        if (created) {
            res.sendJson(F("{\"success\":true}"));
        } else {
            res.send(400, "application/json", F("{\"status\":\"error\",\"msg\":\"create_falhou\"}"));
        }
        return;
    }
    String url = String("/files?path=") + cwd + "&storage=" + (fsr.usesFlash ? "flash" : "sd");
    res.redirect(url);
}

void handleSdCreateDirPost(HttpRequest& req, HttpResponse& res) {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    Touch7PanelHold _t7g;
#endif
    String st = resolveStorageType(req);
    if (st == "psram") {
        res.send(501, "application/json", (String(F("{\"success\":false,\"error\":\"")) + L(activeLang->apiErrPsramUnsupported) + "\"}"));
        return;
    }

    FsResult fsr = getFileSystem(req);
    if (!fsr.fs || !fsr.mounted) {
        res.send(503, "application/json", F("{\"success\":false,\"error\":\"Filesystem not mounted\"}"));
        return;
    }

    if (!isSdOperationAllowed()) {
        res.send(429, "text/plain", F("Aguarde 1 segundo entre operacoes"));
        return;
    }

    String name = getParam(req, "name");
    if (!name.length()) {
        res.send(400, "application/json", F("{\"status\":\"error\",\"msg\":\"name_obrigatorio\"}"));
        return;
    }

    String cwd = getParam(req, "cwd");
    if (!cwd.length()) cwd = "/";
    if (cwd[0] != '/') cwd = "/" + cwd;

    String full = cwd;
    if (full != "/") full += "/";
    full += name;
    full.replace("//", "/");

    updateSdOperationTime();
    bool ok = fsr.fs->mkdir(full.c_str());

    if (ok) {
        res.sendJson(F("{\"status\":\"ok\",\"msg\":\"criado\"}"));
    } else {
        res.send(400, "application/json", F("{\"status\":\"error\",\"msg\":\"mkdir_falhou\"}"));
    }
}

void handleSdRenamePost(HttpRequest& req, HttpResponse& res) {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    Touch7PanelHold _t7g;
#endif
    String st = resolveStorageType(req);
    if (st == "psram") {
        res.send(501, "application/json", (String(F("{\"success\":false,\"error\":\"")) + L(activeLang->apiErrPsramUnsupported) + "\"}"));
        return;
    }

    FsResult fsr = getFileSystem(req);
    if (!fsr.fs || !fsr.mounted) {
        res.redirect("/files");
        return;
    }

    if (!isSdOperationAllowed()) {
        res.send(429, "text/plain", F("Aguarde 1 segundo entre operacoes"));
        return;
    }

    String src = getParam(req, "src");
    String dst = getParam(req, "dst");
    if (!src.length() || !dst.length()) {
        res.redirect("/files");
        return;
    }

    String cwd = getParam(req, "cwd");
    if (!cwd.length()) cwd = "/";
    if (cwd[0] != '/') cwd = "/" + cwd;

    String fullSource = cwd;
    if (fullSource != "/") fullSource += "/";
    fullSource += src;

    String fullDest = cwd;
    if (fullDest != "/") fullDest += "/";
    fullDest += dst;

    fullSource = SDSecurity::normalizePath(fullSource);
    fullDest = SDSecurity::normalizePath(fullDest);

    if (!SDSecurity::isPathSafe(fullSource) || !SDSecurity::isPathSafe(fullDest)) {
        res.send(400, "text/plain", F("Caminho invalido ou inseguro (path traversal bloqueado)"));
        return;
    }

    updateSdOperationTime();
    fsr.fs->rename(fullSource.c_str(), fullDest.c_str());

    delay(50);
    if (wantsJson(req)) {
        res.sendJson(F("{\"success\":true}"));
        return;
    }
    String url = String("/files?path=") + cwd + "&storage=" + (fsr.usesFlash ? "flash" : "sd");
    res.redirect(url);
}

void handleSdDeletePost(HttpRequest& req, HttpResponse& res) {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    Touch7PanelHold _t7g;
#endif
    String st = resolveStorageType(req);
    if (st == "psram") {
        res.send(501, "application/json", (String(F("{\"success\":false,\"error\":\"")) + L(activeLang->apiErrPsramUnsupported) + "\"}"));
        return;
    }

    FsResult fsr = getFileSystem(req);
    if (!fsr.fs || !fsr.mounted) {
        res.send(503, "application/json", F("{\"status\":\"error\",\"msg\":\"storage_nao_montado\"}"));
        return;
    }

    if (!isSdOperationAllowed()) {
        res.send(429, "text/plain", F("Aguarde 1 segundo entre operacoes"));
        return;
    }

    String path = getParam(req, "path");
    if (!path.length()) {
        res.send(400, "application/json", F("{\"status\":\"error\",\"msg\":\"path_obrigatorio\"}"));
        return;
    }

    String cwd = getParam(req, "cwd");
    if (!cwd.length()) cwd = "/";
    if (cwd[0] != '/') cwd = "/" + cwd;

    String full = cwd;
    if (full != "/") full += "/";
    full += path;

    full = SDSecurity::normalizePath(full);
    if (!SDSecurity::isPathSafe(full)) {
        res.send(400, "application/json", F("{\"status\":\"error\",\"msg\":\"caminho_invalido\"}"));
        return;
    }

    updateSdOperationTime();
    bool ok = false;
    File test = fsr.fs->open(full.c_str());
    if (test && test.isDirectory()) {
        test.close();
        ok = fsr.fs->rmdir(full.c_str());
    } else {
        if (test) test.close();
        ok = fsr.fs->remove(full.c_str());
    }

    if (ok) {
        res.sendJson(F("{\"status\":\"ok\"}"));
    } else {
        res.send(404, "application/json", F("{\"status\":\"error\",\"msg\":\"remocao_falhou\"}"));
    }
}

#else
void handleSdMountPost(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"SD not enabled\"}")); }
void handleSdUnmountPost(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"SD not enabled\"}")); }
void handleSdCreateFilePost(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"SD not enabled\"}")); }
void handleSdCreateDirPost(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"SD not enabled\"}")); }
void handleSdRenamePost(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"SD not enabled\"}")); }
void handleSdDeletePost(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"SD not enabled\"}")); }
#endif
