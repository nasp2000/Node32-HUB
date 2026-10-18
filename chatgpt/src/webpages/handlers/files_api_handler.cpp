// Handlers for the /api/sd/* routes used by the /files page.
// Preserves the original storage behavior while moving it out of webpage_impl.h.

#include "files_api_handler.h"

#include <Arduino.h>
#include "../../languages/lang_active.h"
#include <FS.h>
#include <esp_task_wdt.h>

#include <atomic>
#include <functional>
#include <memory>

#include "../../ConfigManager.h"
#include "../../feature_flags.h"
#include "../../littlefs_manager.h"
#include "../../priority_manager.h"
#include "../../psram_vfs.h"
#include "../../sd_security.h"
#include "../../sdcard.h"
#include "../../structures.h"
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
#include "../../touch7_display.h" // flash-hold for LittleFS download chunks
#endif

extern volatile StorageMode currentStorageMode;
extern bool acquireSDAccess(const char* owner, uint32_t timeoutMs);
extern void releaseSDAccess();

namespace {

static unsigned long lastSdOperationMs = 0;
static const unsigned long SD_OPERATION_DELAY_MS = 1000;

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
struct ConditionalTouch7PanelHold {
    explicit ConditionalTouch7PanelHold(bool enable) : held(enable) {
        if (held) touch7PanelSuspend();
    }
    ~ConditionalTouch7PanelHold() {
        if (held) (void)touch7PanelResume();
    }
    bool held;
};
#endif

bool isSdOperationAllowed() {
    unsigned long now = millis();
    unsigned long elapsed = now - lastSdOperationMs;
    if (now < lastSdOperationMs) elapsed = now;
    return elapsed >= SD_OPERATION_DELAY_MS;
}

void updateSdOperationTime() {
    lastSdOperationMs = millis();
}

String resolveStorageType(AsyncWebServerRequest* request) {
    String storageType;
    if (request->hasParam("storage", true)) {
        storageType = request->getParam("storage", true)->value();
    } else if (request->hasParam("storage")) {
        storageType = request->getParam("storage")->value();
    }

    storageType.toLowerCase();
    bool storageSupported = false;
#if ENABLE_SDCARD==1
    if (storageType == "sd") {
        storageSupported = true;
    }
#endif
#if ENABLE_FILESYSTEM==1
    if (storageType == "flash") {
        storageSupported = true;
    }
#endif
    if (storageType == "psram" && (psramFound() || psramVFSIsEnabled())) {
        storageSupported = true;
    }
    if (storageSupported) {
        return storageType;
    }

#if ENABLE_SDCARD==1
    if (sdcardIsMounted()) {
        return "sd";
    }
#endif
    if (psramVFSIsEnabled()) {
        return "psram";
    }
#if ENABLE_FILESYSTEM==1
    return "flash";
#elif ENABLE_SDCARD==1
    return "sd";
#else
    return "psram";
#endif
}

fs::FS* getFileSystem(AsyncWebServerRequest* request, bool& usesFlash, bool& mounted) {
    String storageType = resolveStorageType(request);

    usesFlash = (storageType == "flash");

    if (storageType == "psram") {
        mounted = psramVFSIsEnabled();
        return nullptr;
    }

    if (usesFlash) {
#if ENABLE_FILESYSTEM==1
        mounted = littlefsMounted();
        if (mounted) return &getLittleFS();
#else
        mounted = false;
#endif
    } else {
#if ENABLE_SDCARD==1
        if (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled()) {
            mounted = false;
            return nullptr;
        }
        mounted = sdcardIsMounted();
        if (mounted) return &SD_MMC;
#else
        mounted = false;
#endif
    }

    return nullptr;
}

} // namespace

void registerFilesApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    server.on("/api/sd/download", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;

        String storageType = resolveStorageType(request);

        if (storageType == "psram") {
            if (!psramVFSIsEnabled()) {
                request->send(404, "text/plain; charset=UTF-8", "storage_nao_montado");
                return;
            }

            if (!request->hasParam("path")) {
                request->send(400, "text/plain", "faltapath");
                return;
            }

            String path = request->getParam("path")->value();
            if (path.length() == 0) {
                request->send(400, "text/plain", "vazio");
                return;
            }

            String psramFolder;
            String psramFilename;
            int lastSlash = path.lastIndexOf('/');
            if (lastSlash < 0) {
                request->send(400, "text/plain", "caminho_invalido");
                return;
            }
            psramFolder = path.substring(0, lastSlash);
            psramFilename = path.substring(lastSlash + 1);

            size_t fileSize = psramVFSGetFileSize(psramFolder, psramFilename);
            if (fileSize == 0) {
                request->send(404, "text/plain", "nao_existe");
                return;
            }

            auto folderPtr = std::make_shared<String>(psramFolder);
            auto filenamePtr = std::make_shared<String>(psramFilename);
            auto currentOffset = std::make_shared<size_t>(0);

            String contentType = SDSecurity::getContentType(psramFilename);

            AsyncWebServerResponse* response = request->beginResponse(contentType.c_str(), fileSize,
                [folderPtr, filenamePtr, currentOffset, fileSize](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
                    esp_task_wdt_reset();

                    if (*currentOffset >= fileSize) {
                        return 0;
                    }

                    size_t remaining = fileSize - *currentOffset;
                    size_t toRead = min(maxLen, min(remaining, static_cast<size_t>(8 * 1024)));

                    size_t bytesRead = psramVFSReadChunk(*folderPtr, *filenamePtr, *currentOffset, buffer, toRead);
                    *currentOffset += bytesRead;

                    if (index > 0 && (index % (32 * 1024)) == 0) {
                        vTaskDelay(pdMS_TO_TICKS(10));
                    }

                    return bytesRead;
                }
            );

            response->addHeader("Content-Disposition", "attachment; filename=\"" + psramFilename + "\"");
            request->send(response);
            return;
        }

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
        // Suspend before even asking LittleFS whether the file exists: exists(),
        // open(), and size() all read flash metadata. Keep the panel stopped
        // for the async stream and release it on every error/completion path.
        auto dlHeld = std::make_shared<bool>(false);
        if (storageType == "flash") {
            touch7PanelSuspend();
            *dlHeld = true;
        }
        auto dlRelease = [dlHeld]() {
            if (*dlHeld) {
                *dlHeld = false;
                touch7PanelResume();
            }
        };
#endif

        bool usesFlash, mounted;
        fs::FS* activeFS = getFileSystem(request, usesFlash, mounted);
        if (!activeFS || !mounted) {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
            dlRelease();
#endif
            request->send(404, "text/plain; charset=UTF-8", "storage_nao_montado");
            return;
        }

        bool mutexAcquired = false;
        if (!usesFlash) {
            taskYIELD();
            vTaskDelay(pdMS_TO_TICKS(5));

            if (!acquireSDAccess("download", 75000)) {
                request->send(503, "application/json; charset=UTF-8", String("{\"error\":\"sd_ocupado\",\"message\":\"") + L(activeLang->apiSdBusy) + "\"}");
                return;
            }
            mutexAcquired = true;
        }

        auto releaseMutexIfNeeded = [mutexAcquired]() {
            if (mutexAcquired) {
                releaseSDAccess();
            }
        };

        if (!request->hasParam("path")) {
            releaseMutexIfNeeded();
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
            dlRelease();
#endif
            request->send(400, "text/plain", "faltapath");
            return;
        }

        String path = request->getParam("path")->value();
        if (path.length() == 0) {
            releaseMutexIfNeeded();
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
            dlRelease();
#endif
            request->send(400, "text/plain", "vazio");
            return;
        }
        if (path[0] != '/') path = String("/") + path;

        path = SDSecurity::normalizePath(path);
        if (!SDSecurity::isPathSafe(path)) {
            releaseMutexIfNeeded();
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
            dlRelease();
#endif
            request->send(400, "text/plain", "Caminho invalido ou inseguro (path traversal bloqueado)");
            return;
        }

        if (!activeFS->exists(path.c_str())) {
            releaseMutexIfNeeded();
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
            dlRelease();
#endif
            request->send(404, "text/plain", "nao_existe");
            return;
        }

        File* filePtr = new File();
        *filePtr = activeFS->open(path.c_str());
        if (!*filePtr) {
            delete filePtr;
            releaseMutexIfNeeded();
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
            dlRelease();
#endif
            request->send(500, "text/plain", "erro_abrir");
            return;
        }

        size_t fileSize = filePtr->size();
        String fileName = path.substring(path.lastIndexOf('/') + 1);
        String contentType = SDSecurity::getContentType(path);

        request->onDisconnect([filePtr, mutexAcquired
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
            , dlRelease
#endif
        ]() {
            taskYIELD();
            vTaskDelay(pdMS_TO_TICKS(1));

            if (filePtr && *filePtr) {
                filePtr->close();
            }
            delete filePtr;

            if (mutexAcquired) {
                releaseSDAccess();
            }
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
            dlRelease();
#endif
        });

        auto lastActivity = std::make_shared<std::atomic<unsigned long>>(millis());
        auto bytesServed = std::make_shared<std::atomic<size_t>>(0);

            AsyncWebServerResponse* response = request->beginResponse(contentType.c_str(), fileSize,
                [filePtr, lastActivity, bytesServed, fileSize
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
                , dlRelease
#endif
                ](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
                    esp_task_wdt_reset();

                    if (!filePtr || !*filePtr) {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
                        dlRelease();
#endif
                        return 0;
                    }

                if (index > 0 && (index % (32 * 1024)) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }

                size_t readSize = min(maxLen, static_cast<size_t>(8 * 1024));
                size_t bytesRead = filePtr->read(buffer, readSize);

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
                // The final bytes are already copied into the HTTP buffer; no
                // more flash reads are needed, so restore the panel now rather
                // than depending on the later disconnect callback.
                if (bytesRead == 0 || index + bytesRead >= fileSize) {
                    filePtr->close();
                    dlRelease();
                }
#endif

                lastActivity->store(millis());
                bytesServed->fetch_add(bytesRead);
                priorityManager().updateDownloadProgress(bytesRead);

                return bytesRead;
            }
        );

        response->addHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
        request->send(response);
    });

    server.on("/api/sd/delete-multiple", HTTP_POST,
        [](AsyncWebServerRequest* request) { },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            (void)index;
            (void)total;
            if (!context.ensureBasicAuth(request)) return;

            if (resolveStorageType(request) == "psram") {
                request->send(501, "application/json", String("{\"success\":false,\"error\":\"") + L(activeLang->apiErrPsramUnsupported) + "\"}");
                return;
            }

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
            // This route is registered directly (outside registerApiRoute's
            // mutation guard). Stop RGB before getFileSystem/exists/open/remove
            // can touch LittleFS metadata or contents.
            ConditionalTouch7PanelHold t7PanelHold(resolveStorageType(request) == "flash");
#endif

            bool usesFlash, mounted;
            fs::FS* activeFS = getFileSystem(request, usesFlash, mounted);
            if (!activeFS || !mounted) {
                request->send(400, "application/json", "{\"success\":false,\"error\":\"storage_nao_montado\"}");
                return;
            }

            if (!isSdOperationAllowed()) {
                request->send(429, "application/json", "{\"success\":false,\"error\":\"aguarde_1_segundo\"}");
                return;
            }
            updateSdOperationTime();

            // Serialise SD access with the loop-task scan and other SD consumers
            // (same mutex). Flash/LittleFS needs no SD lock.
            bool sdLocked = false;
            if (!usesFlash) {
                if (!acquireSDAccess("delete-multiple", 8000UL)) {
                    request->send(503, "application/json", "{\"success\":false,\"error\":\"sd_busy\"}");
                    return;
                }
                sdLocked = true;
            }

            std::function<bool(const String&)> deleteFolderRecursive;
            deleteFolderRecursive = [activeFS, &deleteFolderRecursive](const String& folderPath) -> bool {
                if (folderPath.length() == 0) return false;
                if (folderPath.indexOf("..") >= 0) return false;

                File dir = activeFS->open(folderPath.c_str());
                if (!dir || !dir.isDirectory()) {
                    if (dir) dir.close();
                    return false;
                }

                File entry = dir.openNextFile();
                while (entry) {
                    String entryName = String(entry.name());
                    String entryPath = String(folderPath);
                    if (!entryPath.endsWith("/")) entryPath += "/";
                    entryPath += entryName;

                    if (entry.isDirectory()) {
                        entry.close();
                        if (!deleteFolderRecursive(entryPath)) {
                            dir.close();
                            return false;
                        }
                    } else {
                        entry.close();
                        if (!activeFS->remove(entryPath.c_str())) {
                            dir.close();
                            return false;
                        }
                    }
                    entry = dir.openNextFile();
                }
                dir.close();

                return activeFS->rmdir(folderPath.c_str());
            };

            String body(reinterpret_cast<char*>(data), len);
            int deleted = 0;
            int errors = 0;
            int processedCount = 0;
            const int MAX_DELETE_BATCH = 50;
            String errorPaths = "";
            const char* p = body.c_str();
            const char* start = strchr(p, '[');
            if (!start) {
                if (sdLocked) releaseSDAccess();
                request->send(400, "application/json", "{\"success\":false,\"error\":\"json_invalido\"}");
                return;
            }

            start++;
            while (*start && *start != ']') {
                if (*start == '"') {
                    start++;
                    const char* end = strchr(start, '"');
                    if (end) {
                        if (++processedCount > MAX_DELETE_BATCH) {
                            errors++;
                            errorPaths += "[BATCH_LIMIT_EXCEEDED] ";
                            break;
                        }

                        if (processedCount % 10 == 0) {
                            esp_task_wdt_reset();
                            vTaskDelay(pdMS_TO_TICKS(5));
                        }

                        String path(start, end - start);
                        path = SDSecurity::normalizePath(path);
                        if (!SDSecurity::isPathSafe(path)) {
                            errors++;
                            errorPaths += "[INVALID_PATH:" + path + "] ";
                            start = end + 1;
                            continue;
                        }

                        if (path.length() > 0 && path[0] == '/') {
                            if (!activeFS->exists(path.c_str())) {
                                errors++;
                                errorPaths += "[NOT_EXIST:" + path + "] ";
                            } else {
                                File file = activeFS->open(path.c_str());
                                if (file) {
                                    bool isDir = file.isDirectory();
                                    file.close();
                                    bool ok = false;
                                    if (isDir) {
                                        ok = deleteFolderRecursive(path);
                                    } else {
                                        ok = activeFS->remove(path.c_str());
                                    }
                                    if (ok) {
                                        deleted++;
                                    } else {
                                        errors++;
                                        errorPaths += "[DEL_FAIL:" + path + "] ";
                                    }
                                } else {
                                    errors++;
                                    errorPaths += "[OPEN_FAIL:" + path + "] ";
                                }
                            }
                        } else {
                            errors++;
                            errorPaths += "[INVALID:" + path + "] ";
                        }
                        start = end + 1;
                    } else {
                        break;
                    }
                } else {
                    start++;
                }
            }

            delay(100);
            String response = "{\"success\":";
            response += (errors == 0) ? "true" : "false";
            response += ",\"deleted\":";
            response += deleted;
            response += ",\"errors\":";
            response += errors;
            if (errors > 0 && errorPaths.length() > 0) {
                errorPaths.replace("\"", "\\\"");
                response += ",\"error_detail\":\"";
                response += errorPaths;
                response += "\"";
            }
            response += "}";
            if (sdLocked) releaseSDAccess();
            request->send(200, "application/json", response);
        }
    );
}
