#include "ftp_handler.h"
#include "../../feature_flags.h"

#if ENABLE_FTP_CLIENT==1

#include "../../ftp_client.h"
#include "../../config_manager_runtime.h"
#include "../../psram_helpers.h"
#include "../../sdcard.h"
#include "../../structures.h"
#include "../../psram_vfs.h"
#include "../../littlefs_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void handleFtpFoldersGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    SpiRamJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();
    // Each filesystem contributes its root + primary (1-level) folders, prefixed
    // so the UI can group by source and the engine can resolve the filesystem.
#if ENABLE_SDCARD==1
    if (sdcardIsMounted()) {
        arr.add("SD:/");
        if (sdMutexLock(pdMS_TO_TICKS(2000))) {
            File root = SD_MMC.open("/");
            if (root && root.isDirectory()) {
                File entry = root.openNextFile();
                while (entry) {
                    if (entry.isDirectory()) {
                        String nm = String(entry.name());
                        if (!nm.startsWith("/")) nm = "/" + nm;
                        arr.add("SD:" + nm);
                    }
                    entry.close();
                    entry = root.openNextFile();
                    sdMutexUnlock();
                    vTaskDelay(pdMS_TO_TICKS(1));
                    if (!sdMutexLock(pdMS_TO_TICKS(2000))) break;
                }
            }
            if (root) root.close();
            sdMutexUnlock();
        }
    }
#endif
    if (psramVFSIsEnabled()) {
        arr.add("PSRAM:/");
        std::vector<String> folders = psramVFSListFolders();
        for (size_t i = 0; i < folders.size() && i < 32; i++) {
            arr.add(String("PSRAM:/") + folders[i]);
        }
    }
#if ENABLE_FILESYSTEM==1
    if (littlefsBegin()) {
        arr.add("LFS:/");
        fs::FS& lfs = getLittleFS();
        File d = lfs.open("/");
        if (d && d.isDirectory()) {
            File entry = d.openNextFile();
            while (entry) {
                if (entry.isDirectory()) {
                    String nm = String(entry.name());
                    if (!nm.startsWith("/")) nm = "/" + nm;
                    arr.add("LFS:" + nm);
                }
                entry.close();
                entry = d.openNextFile();
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
        if (d) d.close();
    }
#endif
    String out;
    out.reserve(4096);
    serializeJson(doc, out);
    res.send(200, F("application/json"), out);
}

void handleFtpStatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    ftpClientInit();
    FtpStatus st;
    ftpGetStatus(st);
    // JSON in PSRAM (SpiRamJsonDocument), response <4KB.
    SpiRamJsonDocument doc(2048);
    doc["enabled"] = st.enabled;
    doc["auto_upload"] = st.autoUpload;
    doc["host"] = configManager.getFtpHost();
    doc["port"] = configManager.getFtpPort();
    doc["user"] = configManager.getFtpUser();
    doc["anonymous"] = configManager.getFtpAnonymous();
    doc["remote_path"] = configManager.getFtpRemotePath();
    // Default (unset or root) → show "/<system name>" as the suggested remote folder.
    if (doc["remote_path"].as<String>().isEmpty() || doc["remote_path"] == "/") {
        String sysName = configManager.getSystemName();
        if (sysName.length() == 0) sysName = "SD_LOG";
        doc["remote_path"] = "/" + sysName;
    }
    doc["local_dir"] = configManager.getFtpLocalDir();
    doc["interval_min"] = configManager.getFtpIntervalMin();
    doc["only_new"] = configManager.getFtpOnlyNew();
    doc["delete_after"] = configManager.getFtpDeleteAfter();
    doc["max_files"] = configManager.getFtpMaxFiles();
    doc["filter"] = configManager.getFtpFilter();
    doc["password"] = "********";
    doc["last_file"] = st.lastFile;
    doc["last_result"] = st.lastResult;
    doc["last_code"] = st.lastCode;
    doc["last_time"] = (unsigned long)st.lastTime;
    doc["ok_count"] = (unsigned long)st.okCount;
    doc["fail_count"] = (unsigned long)st.failCount;
    doc["busy"] = st.busy;
    doc["source"] = st.activeSource;
    doc["monitor_stack_free"] = (unsigned long)st.monitorStackFree;
    String out;
    serializeJson(doc, out);
    res.send(200, F("application/json"), out);
}

void handleFtpTestPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    ftpClientInit();
    static unsigned long lastAttempt = 0;
    unsigned long now = millis();
    if (lastAttempt && now - lastAttempt < 10000) {
        unsigned long s = (10000 - (now - lastAttempt)) / 1000;
        res.send(429, F("application/json"), String("{\"success\":false,\"error\":\"throttle\",\"message\":\"Wait ") + String(s) + "s\"}");
        return;
    }
    lastAttempt = now;
    if (!ftpIsAvailable()) {
        res.send(400, F("application/json"), F("{\"success\":false,\"error\":\"config_incomplete\",\"message\":\"FTP host/user missing\"}"));
        return;
    }
    String detail;
    bool ok = ftpTestConnection(detail);
    detail.replace("\"", "'");
    res.send(ok ? 200 : 500, F("application/json"),
        String("{\"success\":") + (ok ? "true" : "false") + ",\"message\":\"" + detail + "\"}");
}

void handleFtpUploadNowPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    ftpClientInit();
    if (!ftpIsAvailable()) {
        res.send(400, F("application/json"), F("{\"success\":false,\"error\":\"config_incomplete\",\"message\":\"FTP host/user missing\"}"));
        return;
    }
    String detail;
    int n = ftpUploadNow(detail);
    detail.replace("\"", "'");
    // "busy"/"disabled/incomplete" mean nothing ran: report failure so the UI
    // does not show a green success for a cycle that never happened.
    bool ok = (n > 0) || (detail.startsWith("cycle done"));
    res.send(ok ? 200 : 500, F("application/json"),
        String("{\"success\":") + (ok ? "true" : "false") + ",\"uploaded\":" + n + ",\"message\":\"" + detail + "\"}");
}

#else

void handleFtpStatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.send(503, F("application/json"), F("{\"success\":false,\"error\":\"ftp_disabled\"}"));
}
void handleFtpTestPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.send(503, F("application/json"), F("{\"success\":false,\"error\":\"ftp_disabled\"}"));
}
void handleFtpUploadNowPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.send(503, F("application/json"), F("{\"success\":false,\"error\":\"ftp_disabled\"}"));
}
void handleFtpFoldersGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.send(503, F("application/json"), F("{\"success\":false,\"error\":\"ftp_disabled\"}"));
}

#endif
