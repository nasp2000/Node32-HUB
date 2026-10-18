// Handlers for OTA enable and firmware upload APIs.

#include "ota_admin_handler.h"

#include <Arduino.h>
#include "../../languages/lang_active.h"
#include <Update.h>

#include "../../commands.h"
#include "../../direct_log.h"
#include "../../debug_writer.h"
#include "../../feature_flags.h"
#include "../../ota_security.h"

extern unsigned long otaEnabledUntil;

void registerOtaAdminRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_OTA==1
    server.on(
        "/api/firmware-upload",
        HTTP_POST,
        [&context](AsyncWebServerRequest* request) {
            if (!context.ensureBasicAuth(request)) return;
        },
        [&context](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
            if (!context.ensureBasicAuth(request)) return;

            if (otaEnabledUntil == 0 || (long)(otaEnabledUntil - millis()) <= 0) {
                String otaMsg = String(L(activeLang->apiOtaNotEnabled));
                otaMsg.replace("{btn}", L(activeLang->ethBtnRefresh));
                request->send(403, "application/json", "{\"success\":false,\"message\":\"" + otaMsg + "the OTA page to enable OTA before uploading.\"}");
                return;
            }

            static bool updating = false;
            static bool hasError = false;
            static String errorMsg;
            static size_t written = 0;
            static size_t totalSize = 0;
            static size_t fwSize = 0;

            if (index == 0) {
                if (Update.isRunning()) Update.abort();
                updating = false;
                hasError = false;
                errorMsg = "";
                written = 0;
                totalSize = 0;
                fwSize = 0;

                if (!filename.endsWith(".bin")) {
                    hasError = true;
                    errorMsg = L(activeLang->apiOtaErrFileType);
                    directLogPrintln(F("❌ OTA: invalid file (not .bin)"));
                } else {
                    directLogPrintln(String(F("Starting OTA update via web: ")) + filename);
                    if (request->hasParam("filesize", true)) {
                        String sizeValue;
                        const AsyncWebParameter* parameter = request->getParam("filesize", true);
                        if (parameter != nullptr) sizeValue = parameter->value();
                        totalSize = (size_t)strtoul(sizeValue.c_str(), nullptr, 10);
                    }

                    if (totalSize > 0 && totalSize > (size_t)ESP.getFlashChipSize()) {
                        hasError = true;
                        errorMsg = L(activeLang->apiOtaErrTooLarge);
                        directLogPrintln(F("❌ OTA: firmware maior que flash"));
                    } else if (totalSize > 0 && totalSize < 260) {
                        hasError = true;
                        errorMsg = "Firmware demasiado pequeno";
                        directLogPrintln(F("❌ OTA: firmware too small"));
                    } else {
                        // Optional MD5 verification: client sends 'expected_md5' (32-char hex string).
                        String md5Param;
                        if (request->hasParam("expected_md5", true)) {
                            const AsyncWebParameter* md5p = request->getParam("expected_md5", true);
                            if (md5p) md5Param = md5p->value();
                        }
                        if (md5Param.length() == 32) {
                            Update.setMD5(md5Param.c_str());
                        }
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
                        // TouchMeteo-7: stop RGB DMA+ISR before firmware flash
                        // writes (minutes of SPI1 erase/write would panic on
                        // any concurrent PSRAM access). Reboot follows OTA.
                        extern void touch7OtaBegin();
                        touch7OtaBegin();
#endif

                        if (totalSize > 0) {
                            // Signed firmware: strip 260 bytes (4 magic + 256 signature)
                            fwSize = totalSize - 260;
                            otaVerifyInit(totalSize);
                            bool begun = Update.begin(fwSize);
                            if (!begun) {
                                hasError = true;
                                errorMsg = L(activeLang->apiOtaErrOta);
                                directLogPrintln(String(F("❌ OTA: Update.begin failed: err ")) + String(Update.getError()));
                            } else {
                                updating = true;
                                written = 0;
                            }
                        } else {
                            // Legacy unsigned firmware (no filesize): write everything
                            bool begun = Update.begin();
                            if (!begun) {
                                hasError = true;
                                errorMsg = L(activeLang->apiOtaErrOta);
                                directLogPrintln(String(F("❌ OTA: Update.begin failed: err ")) + String(Update.getError()));
                            } else {
                                updating = true;
                                written = 0;
                            }
                        }
                    }
                }
            }

            if (len > 0 && updating && !hasError) {
                // Feed ALL bytes to verification (hashes fw + magic, buffers magic + sig)
                otaVerifyFeed(data, len);

                // Write only firmware bytes (strip last 260 from flash)
                size_t toWrite = len;
                if (fwSize > 0) {
                    size_t remaining = (written < fwSize) ? fwSize - written : 0;
                    toWrite = (len < remaining) ? len : remaining;
                }
                if (toWrite > 0) {
                    size_t writeCount = Update.write(data, toWrite);
                    written += writeCount;
                    if (writeCount != toWrite) {
                        hasError = true;
                        errorMsg = L(activeLang->apiOtaWriteFail);
                        directLogPrintln(String(F("❌ OTA: escrita parcial: err ")) + String(Update.getError()));
                    }
                }
            }

            if (final) {
                bool ok = false;
                if (!hasError && updating) {
                    // Verify signature (if we have a totalSize)
                    if (totalSize > 0) {
                        bool sigValid = otaVerifyEnd();
                        if (!sigValid) {
                            hasError = true;
                            errorMsg = L(activeLang->apiOtaErrSignature);
                            directLogPrintln(L(activeLang->apiOtaErrSignatureAlert));
                            Update.abort();
                        }
                    }
                }

                if (!hasError && updating) {
                    if (Update.end(true)) {
                        directLogPrintln(String(F("OTA: escrito ")) + String((unsigned int)written) + String(F(" bytes.")));
                        ok = true;
                    } else {
                        hasError = true;
                        errorMsg = "Update.end falhou";
                        directLogPrintln(String(F("❌ OTA: Update.end failed: err ")) + String(Update.getError()));
                    }
                }

                if (ok) {
                    request->send(200, "application/json", String("{\"success\":true,\"message\":\"") + L(activeLang->apiOtaDoneRestarting) + "\"}");
                    delay(150);
#if ENABLE_SDCARD==1
                    debugWriterFlush();
                    delay(100);
                    flushSdcardNowBlocking("OTA_REBOOT", 5000);
#endif
                    safeReboot("OTA Success");
                } else {
                    String errorDetails = errorMsg;
                    int updateError = Update.getError();
                    if (updateError != 0) {
                        errorDetails += String(" | Update.error=") + String(updateError);
                    }
                    String json = String("{\"success\":false,\"message\":\"") + (errorDetails.length() ? errorDetails : L(activeLang->apiOtaUpdateFail)) + String("\"}");
                    request->send(400, "application/json", json);
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
                    // OTA failed: the panel was stopped by touch7OtaBegin();
                    // rebuild it so the device stays usable (success reboots).
                    extern void touch7OtaAbort();
                    touch7OtaAbort();
#endif
                }

                updating = false;
                hasError = false;
                errorMsg = "";
                written = 0;
                totalSize = 0;
                fwSize = 0;
            }
        },
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            (void)request;
            (void)data;
            (void)len;
            (void)index;
            (void)total;
        });
#else
    (void)server;
    (void)context;
#endif
}