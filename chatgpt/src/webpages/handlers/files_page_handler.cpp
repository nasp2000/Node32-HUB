// Handler for the /files GET route.
// Serves a dynamic file browser for SD card, Flash (LittleFS), and PSRAM virtual storage.
// Session tracking prevents heap overflow from too many concurrent listings.

#include "files_page_handler.h"

#include <atomic>
#include <vector>
#include <algorithm>

#include <Arduino.h>
#include <FS.h>

#include "../../commands.h"
#include "../../feature_flags.h"
#include "../../structures.h"
#include "../../sd_security.h"
#include "../../sdcard.h"
#include "../../psram_vfs.h"
#include "../../littlefs_manager.h"
#include "../../ConfigManager.h"
#include "../../config_manager_runtime.h"
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
#include "../../touch7_display.h" // Touch7FlashGuard (listing reads LittleFS)
#endif

extern const char* FIRMWARE_VERSION;
extern const char* FIRMWARE_BUILD;
extern volatile StorageMode currentStorageMode;
extern bool acquireSDAccess(const char* owner, uint32_t timeoutMs);
extern void releaseSDAccess();

// Maximum number of simultaneous browser file-listing sessions.
static const int MAX_BROWSER_SESSIONS = 3;
// Sessions idle longer than this are evicted automatically.
static const unsigned long SESSION_TIMEOUT_MS = 2UL * 60UL * 1000UL; // 2 minutes
// Hard cap for scanned directory entries per request to keep response latency bounded.
static const size_t MAX_FILES_PAGE_SCAN_ENTRIES = 1200;

struct BrowserSession {
    unsigned long lastActivity;
    IPAddress ip;
    bool active;
};

static std::atomic<int> activeBrowserSessions(0);
static BrowserSession browserSessions[MAX_BROWSER_SESSIONS] = {};

// Evict expired sessions and return true if at least one slot is free.
static bool cleanupExpiredSessions() {
    unsigned long now = millis();
    int activeCount = 0;
    // "Local processing pages never expire": with the option on, file-browser
    // sessions are kept alive instead of being dropped after 2 minutes idle.
    bool noKick = configManager.getChartsExemptSessions();
    for (int i = 0; i < MAX_BROWSER_SESSIONS; i++) {
        if (browserSessions[i].active) {
            if (!noKick && (now - browserSessions[i].lastActivity) >= SESSION_TIMEOUT_MS) {
                browserSessions[i].active = false;
                browserSessions[i].ip = (uint32_t)0;
                if (activeBrowserSessions.load() > 0) {
                    activeBrowserSessions.fetch_sub(1);
                }
                sendVirtualMessage(String(F("Files: Session expired (slot ")) + String(i) + F(")"), INFO, false, false);
            } else {
                activeCount++;
            }
        }
    }
    return activeCount < MAX_BROWSER_SESSIONS;
}

// Register or refresh the session for a given client IP.
// Returns true if the client is allowed to proceed.
static bool registerSession(IPAddress ip) {
    unsigned long now = millis();
    // Refresh an existing session for this IP.
    for (int i = 0; i < MAX_BROWSER_SESSIONS; i++) {
        if (browserSessions[i].active && browserSessions[i].ip == ip) {
            browserSessions[i].lastActivity = now;
            return true;
        }
    }
    // Claim the first free slot.
    for (int i = 0; i < MAX_BROWSER_SESSIONS; i++) {
        if (!browserSessions[i].active) {
            browserSessions[i].active = true;
            browserSessions[i].ip = ip;
            browserSessions[i].lastActivity = now;
            activeBrowserSessions.fetch_add(1);
            String sessionMsg = String(F("Files: Session registered (slot ")) + String(i) + F(", IP=") + ip.toString() + F(")");
            sendVirtualMessage(sessionMsg, INFO, false, false);
            return true;
        }
    }
    return false; // All slots occupied.
}

void registerFilesPageRoute(AsyncWebServer& server, const WebRouteContext& context) {
    // File browser - SD, Flash (LittleFS), and PSRAM virtual filesystem.
    server.on("/files", HTTP_GET, [context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;

        // Evict idle sessions before checking the limit.
        cleanupExpiredSessions();

        IPAddress clientIP = request->client()->remoteIP();

        // Block clients when all session slots are occupied.
        if (!registerSession(clientIP)) {
            sendVirtualMessage(String(F("Files: Blocked - activeBrowserSessions=")) + String(activeBrowserSessions.load()), WARN, false, false);
request->send(503, "text/html; charset=UTF-8",
                "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>&#128193; Explorer Busy</title><link rel='stylesheet' href='/core.css'></head><body>"
                "<main class='page'><div class='wrap'>"
                "<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome'>&#127968; Home</a></div>"
                "<h1>&#128193; Explorer Busy</h1>"
                "<div style='border:2px solid #f59e0b;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>"
                "<div style='background:#78350f;border-left:4px solid #f59e0b;padding:16px;margin:12px 0;font-size:14px;line-height:1.8;color:#fef3c7;border-radius:4px'>"
                "&#9888;&#65039; <strong>Too many active sessions</strong><br><br>"
                "The file explorer already has 3 open sessions (maximum limit).<br>"
                "Wait a few seconds and try again, or close other open pages.<br><br>"
                "&#128161; <strong>Note:</strong> Sessions inactive for 2 minutes are automatically disconnected."
                "</div></div>"
                "<div style='text-align:center;margin-top:24px'>"
                "<button onclick='location.reload()' class='btn' style='padding:10px 24px'>&#128260; Try Again</button>"
                "</div></div></main>"
                "</body></html>");
            return;
        }

        if (!context.allowHtmlClient(request)) return;

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
        // TouchMeteo-7: directory scan reads LittleFS (SPI1 flash) while the
        // RGB DMA streams — quiesce for the whole page render (T7-20).
        Touch7FlashGuard _t7filesGuard;
#endif

        // Determine which storage to list based on the requested value and available backends.
        // Order everywhere (default + selector): SD card, Flash (LittleFS), PSRAM.
        auto resolveDefaultStorageType = []() -> String {
#if ENABLE_SDCARD==1
            if (sdcardIsMounted()) {
                return "sd";
            }
#endif
#if ENABLE_FILESYSTEM==1
            littlefsBegin();
            if (littlefsMounted()) {
                return "flash";
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
        };

        String storageType = request->hasParam("storage") ? request->getParam("storage")->value() : resolveDefaultStorageType();
        storageType.toLowerCase();
        bool storageSupported = false;
    #if ENABLE_SDCARD==1
        if (storageType == "sd") storageSupported = true;
    #endif
    #if ENABLE_FILESYSTEM==1
        if (storageType == "flash") storageSupported = true;
    #endif
        if (storageType == "psram" && (psramFound() || psramVFSIsEnabled())) storageSupported = true;
        if (!storageSupported) {
            storageType = resolveDefaultStorageType();
        }
        bool usesFlashForListing = (storageType == "flash");
        bool usesPsramForListing = (storageType == "psram");

        // Acquire the SD mutex only when listing from the SD card.
#if ENABLE_SDCARD==1
        if (!usesFlashForListing && !usesPsramForListing) {
            // Yield before the mutex to let async_tcp + mutex interaction settle.
            taskYIELD();
            vTaskDelay(pdMS_TO_TICKS(5));
            // Fail fast to keep navigation responsive when SD is busy.
            if (!acquireSDAccess("file-list", 8000)) {
                request->send(503, "text/html; charset=UTF-8",
                    "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "<title>&#9888;&#65039; SD Busy</title><link rel='stylesheet' href='/core.css'></head><body>"
                    "<main class='page'><div class='wrap'>"
                    "<div class='actions nav-inicio'><a href='/files' class='btn'>&#11013;&#65039; Back</a></div>"
                    "<h1>&#9888;&#65039; SD Card Busy</h1>"
                    "<div style='border:2px solid #f59e0b;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>"
                    "<div style='background:#78350f;border-left:4px solid #f59e0b;padding:16px;margin:12px 0;font-size:14px;line-height:1.8;color:#fef3c7;border-radius:4px'>"
                    "&#128190; <strong>SD card busy</strong><br><br>"
                    "The SD card is processing a very long operation (probably a large buffer flush with RS232 data). "
                    "Wait for the current write to finish or reduce the incoming data volume.<br><br>"
                    "&#128161; <strong>Tip:</strong> Check the logs via Serial/Telnet to see the Retry-PSRAM buffer status."
                    "</div></div>"
                    "<div style='text-align:center;margin-top:24px'>"
                    "<button onclick='location.reload()' class='btn' style='padding:10px 24px'>&#128260; Reload</button>"
                    "</div></div></main></body></html>");
                return;
            }
        }
#endif

        String currentPath = request->hasParam("path") ? request->getParam("path")->value() : "/";
        if (currentPath.length() == 0) currentPath = "/";
        currentPath.replace('\\', '/');
        if (!currentPath.startsWith("/")) currentPath = String("/") + currentPath;
        currentPath = SDSecurity::normalizePath(currentPath);
        if (!SDSecurity::isPathSafe(currentPath) || currentPath.length() == 0) currentPath = "/";

        // Resolve parent path for the back button.
        String parentPath = "/";
        if (currentPath != "/") {
            int slash = currentPath.lastIndexOf('/');
            if (slash > 0) parentPath = currentPath.substring(0, slash); else parentPath = "/";
        }

    int page = 0; if(request->hasParam("p")) page = request->getParam("p")->value().toInt(); if(page<0) page=0;
    String sortMode = request->hasParam("sort") ? request->getParam("sort")->value() : String("date");
    const int PAGE_SIZE = 10; int startIndex = page * PAGE_SIZE; int endIndex = startIndex + PAGE_SIZE;
    auto escapeHtmlAttr = [](const String &value)->String {
        String escaped;
        escaped.reserve(value.length());
        for(size_t i=0;i<value.length();++i){
            char c = value[i];
            switch(c){
                case '&': escaped += "&amp;"; break;
                case '<': escaped += "&lt;"; break;
                case '>': escaped += "&gt;"; break;
                case '"': escaped += "&quot;"; break;
                case '\'': escaped += "&#39;"; break;
                default: escaped += c; break;
            }
        }
        return escaped;
    };
    auto urlEncode = [](const String &value)->String {
        String encoded;
        encoded.reserve(value.length() * 3);
        const char *hexDigits = "0123456789ABCDEF";
        for(size_t i=0;i<value.length();++i){
            uint8_t c = (uint8_t)value[i];
            if((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c=='-' || c=='_' || c=='.' || c=='~'){
                encoded += (char)c;
            } else if(c == ' '){
                encoded += "%20";
            } else {
                encoded += '%';
                encoded += hexDigits[c >> 4];
                encoded += hexDigits[c & 0x0F];
            }
        }
        return encoded;
    };
    const String escapedCwdAttr = escapeHtmlAttr(currentPath);
        AsyncResponseStream *res = request->beginResponseStream("text/html; charset=utf-8"); if(!res){ request->send(500,"text/plain","Error" ); return; }
        // Keep freshness checks but allow browser back-forward cache optimizations.
        res->addHeader("Cache-Control", "no-cache, must-revalidate");
        res->addHeader("Pragma", "no-cache");
        res->addHeader("Expires", "0");
        res->addHeader("Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'");
        // Determine the active filesystem and read storage stats.
        bool usesFlash = (storageType == "flash");
        bool usesPsram = (storageType == "psram");
        bool mounted = false;
        uint64_t tot = 0ULL;
        uint64_t used = 0ULL;
        uint64_t free = 0ULL;
        fs::FS* activeFS = nullptr;

        if(usesPsram){
            mounted = psramVFSIsEnabled();
            if(mounted){
                tot = PSRAM_VFS_BUFFER_SIZE;
                used = psramVFSGetUsedBytes();
                free = psramVFSGetFreeBytes();
                activeFS = nullptr;
            }
        } else if(usesFlash){
#if ENABLE_FILESYSTEM==1
            mounted = littlefsBegin() && littlefsMounted();
            if(mounted){
                tot = littlefsTotalBytes();
                used = littlefsUsedBytes();
                free = (tot>used)? (tot-used):0ULL;
                activeFS = &getLittleFS();
            }
#else
            mounted = false;
#endif
        } else {
#if ENABLE_SDCARD==1
            mounted = sdcardIsMounted();
            if(mounted){
                tot = sdcardTotalBytes();
                used = sdcardUsedBytes();
                free = (tot>used)? (tot-used):0ULL;
                activeFS = &SD_MMC;
            }
#endif
        }
        auto printBytes = [&res](uint64_t v)->void{
            if(v < 1024ULL){ res->print(v); res->print(F(" B")); return; }
            double kb = (double)v / 1024.0;
            if(kb < 1024.0){ char b[24]; snprintf(b,sizeof(b),"%.2f KB", kb); res->print(b); return; }
            double mb = kb / 1024.0;
            char b[24]; snprintf(b,sizeof(b),"%.2f MB", mb); res->print(b);
        };
    res->print(F("<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta http-equiv='Cache-Control' content='no-cache, must-revalidate'><meta http-equiv='Pragma' content='no-cache'><meta http-equiv='Expires' content='0'><title>MicroSD</title><link rel='stylesheet' href='/core.css'></head><body><div id='loadingOverlay' style='display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(10,25,41,0.95);z-index:99999;align-items:center;justify-content:center'><div style='text-align:center'><div style='border:4px solid #1e40af;border-top:4px solid #3b82f6;border-radius:50%;width:50px;height:50px;animation:spin 1s linear infinite;margin:0 auto'></div><p style='color:#e6f1ff;margin-top:15px;font-size:14px' data-i18n='loading'>Processing...</p></div></div><style>@keyframes spin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}</style><main class='page'><div class='wrap'>"));
    res->print(F("<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome'>&#127968; Home</a></div>"));
    res->print(F("<h1 data-i18n='navFiles'>&#128193; File Explorer</h1>"));
    res->print(F("<div style='background:#064e3b;border-left:4px solid #10b981;border-radius:4px;padding:8px 12px;font-size:12px;color:#d1fae5;margin:12px 0' data-i18n-html='filesShortcutHint'>&#128161; <strong>Right-click:</strong> Context menu | &#9000;&#65039; <strong>Ctrl+A:</strong> Select all | <strong>Delete:</strong> Delete | <strong>Esc:</strong> Clear</div>"));

    res->print(F("<div style='border:2px solid #3b82f6;border-radius:8px;padding:16px;background:#0f172a;margin-top:16px'>"));
    if(usesPsram){
        res->print(F("<h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='filesSpacePsram'>&#129504; PSRAM Virtual Space</h2>"));
    } else if(usesFlash){
        res->print(F("<h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='filesSpaceFlash'>&#9889; Internal Flash Space</h2>"));
    } else {
        res->print(F("<h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='filesSpaceSD'>&#128190; SD Card Space</h2>"));
    }
        if(!mounted){
            if(usesFlash){
#if ENABLE_FILESYSTEM==1
                res->print(F("<div style='padding:12px;background:#7f1d1d;border-left:4px solid #dc2626;border-radius:4px;color:#fecaca' data-i18n='filesFlashNotMounted'>&#9888;&#65039; Flash not mounted</div>"));
#else
                res->print(F("<div style='padding:12px;background:#7f1d1d;border-left:4px solid #dc2626;border-radius:4px;color:#fecaca' data-i18n='filesFlashDisabled'>&#128683; Filesystem disabled (ENABLE_FILESYSTEM=0)</div>"));
#endif
            } else {
                if(sdcardMountInProgress()){
                    res->print(F("<div data-i18n='filesSDMounting'>&#9203; SD mount: in progress...</div>"));
                } else {
                    sdcardBeginAsync();
                    res->print(F("<div data-i18n='filesSDMountingBg'>&#9203; SD mount: started in background...</div>"));
                }
                mounted = sdcardIsMounted();
#if ENABLE_SDCARD==1
                if(mounted){ tot = sdcardTotalBytes(); used = sdcardUsedBytes(); free = (tot>used)? (tot-used):0ULL; activeFS = &SD_MMC; }
#endif
            }
        }
        if(mounted){
            float pct = (tot>0)? ((float)used*100.0f/(float)tot) : 0.0f;
            if(pct>100.0f) pct=100.0f;
            unsigned int pctInt = (unsigned int)(pct + 0.5f);
            res->print(F("<div style='display:grid;grid-template-columns:auto auto auto;gap:16px;font-size:13px;margin-bottom:12px'>"));
            res->print(F("<div><strong style='color:#60a5fa'><span data-i18n='filesTotalLabel'>&#128190; Total:</span></strong> ")); printBytes(tot); res->print(F("</div>"));
            res->print(F("<div><strong style='color:#60a5fa'><span data-i18n='filesUsedLabel'>&#128202; Used:</span></strong> ")); printBytes(used); res->print(F("</div>"));
            res->print(F("<div><strong style='color:#60a5fa'><span data-i18n='filesFreeLabel'>&#128154; Free:</span></strong> ")); printBytes(free); res->print(F("</div>"));
            res->print(F("</div>"));
            res->print(F("<div style='width:100%;height:14px;border:1px solid #3b82f6;border-radius:6px;overflow:hidden;background:#0a1929'>"));
            res->print(F("<div style='height:100%;width:")); res->print(pctInt); res->print(F("%;background:linear-gradient(90deg,#2563eb,#3b82f6);transition:width 0.3s'></div></div>"));
            res->print(F("<div style='font-size:11px;color:#94a3b8;margin-top:4px'>"));
            if(pct < 0.01f && used > 0){ res->print(F("< 0.01% used")); }
            else { res->print(pct, 2); res->print(F("% used")); }
            res->print(F("</div>"));
        } else {
            res->print(F("<div style='padding:12px;background:#7f1d1d;border-left:4px solid #dc2626;border-radius:4px;color:#fecaca' data-i18n='filesSDNotMounted'>&#10060; Card not mounted</div>"));
        }
        res->print(F("</div>")); // close storage stats section

    res->print(F("<script>"));
    res->print(F("window._ft=function(k,fb){try{if(typeof window._t==='function')return window._t(k,fb);var raw=localStorage.getItem('ui_lang_cache');if(raw){var p=JSON.parse(raw);if(p&&p[k]!==undefined)return p[k];}}catch(e){}return fb!==undefined?fb:k;};"));
    res->print(F("if(!window._customDialogsLoaded){window._customDialogsLoaded=true;"));
    res->print(F("function createOverlay(){var o=document.createElement('div');o.style.cssText='position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,20,40,0.85);z-index:99998;display:flex;align-items:center;justify-content:center;backdrop-filter:blur(4px)';return o;}"));
    res->print(F("function createBox(){var b=document.createElement('div');b.style.cssText='background:linear-gradient(135deg,#0a2742 0%,#083050 100%);border:2px solid #1e66c7;border-radius:12px;padding:24px;min-width:320px;max-width:500px;box-shadow:0 12px 40px rgba(0,0,0,0.5);color:#e6f1ff;font-family:system-ui,-apple-system,sans-serif';return b;}"));
    res->print(F("window.customAlert=function(msg){var o=createOverlay(),b=createBox();b.innerHTML='<div style=\"font-size:15px;margin-bottom:20px;line-height:1.5\">'+msg+'</div>';var btn=document.createElement('button');btn.textContent='OK';btn.style.cssText='background:#1e66c7;color:#fff;border:none;padding:10px 24px;border-radius:6px;cursor:pointer;font-size:14px;font-weight:600;width:100%';btn.onmouseenter=function(){btn.style.background='#2374d9';};btn.onmouseleave=function(){btn.style.background='#1e66c7';};btn.onclick=function(){document.body.removeChild(o);};b.appendChild(btn);o.appendChild(b);document.body.appendChild(o);btn.focus();};"));
    res->print(F("window.customConfirm=function(msg,cb){var o=createOverlay(),b=createBox();b.innerHTML='<div style=\"font-size:15px;margin-bottom:20px;line-height:1.5\">'+msg+'</div>';var btns=document.createElement('div');btns.style.cssText='display:flex;gap:10px';var no=document.createElement('button'),yes=document.createElement('button');no.textContent='Cancelar';yes.textContent='Confirmar';no.style.cssText='flex:1;background:#444;color:#fff;border:none;padding:10px;border-radius:6px;cursor:pointer;font-size:14px';yes.style.cssText='flex:1;background:#1e66c7;color:#fff;border:none;padding:10px;border-radius:6px;cursor:pointer;font-size:14px;font-weight:600';no.onmouseenter=function(){no.style.background='#555';};no.onmouseleave=function(){no.style.background='#444';};yes.onmouseenter=function(){yes.style.background='#2374d9';};yes.onmouseleave=function(){yes.style.background='#1e66c7';};no.onclick=function(){document.body.removeChild(o);if(cb)cb(false);};yes.onclick=function(){document.body.removeChild(o);if(cb)cb(true);};btns.appendChild(no);btns.appendChild(yes);b.appendChild(btns);o.appendChild(b);document.body.appendChild(o);yes.focus();};"));
    res->print(F("window.customPrompt=function(msg,def,cb){var o=createOverlay(),b=createBox();b.innerHTML='<div style=\"font-size:15px;margin-bottom:12px;line-height:1.5\">'+msg+'</div>';var inp=document.createElement('input');inp.type='text';inp.value=def||'';inp.style.cssText='width:100%;padding:10px;border:1px solid #1e66c7;border-radius:6px;background:#0a1929;color:#e6f1ff;font-size:14px;margin-bottom:16px;box-sizing:border-box';b.appendChild(inp);var btns=document.createElement('div');btns.style.cssText='display:flex;gap:10px';var no=document.createElement('button'),yes=document.createElement('button');no.textContent='Cancelar';yes.textContent='OK';no.style.cssText='flex:1;background:#444;color:#fff;border:none;padding:10px;border-radius:6px;cursor:pointer;font-size:14px';yes.style.cssText='flex:1;background:#1e66c7;color:#fff;border:none;padding:10px;border-radius:6px;cursor:pointer;font-size:14px;font-weight:600';no.onmouseenter=function(){no.style.background='#555';};no.onmouseleave=function(){no.style.background='#444';};yes.onmouseenter=function(){yes.style.background='#2374d9';};yes.onmouseleave=function(){yes.style.background='#1e66c7';};no.onclick=function(){document.body.removeChild(o);if(cb)cb(null);};yes.onclick=function(){var v=inp.value;document.body.removeChild(o);if(cb)cb(v);};inp.onkeydown=function(e){if(e.key==='Enter'){yes.click();}else if(e.key==='Escape'){no.click();}};btns.appendChild(no);btns.appendChild(yes);b.appendChild(btns);o.appendChild(b);document.body.appendChild(o);inp.focus();inp.select();}}"));
    res->print(F("if(!window.decodeHtml){window.decodeHtml=function(str){var txt=document.createElement('textarea');txt.innerHTML=str;return txt.value;};}"));
    res->print(F("if(!window.selectedFiles)window.selectedFiles=[];"));

    res->print(F("window.showLoading=function(){var o=document.getElementById('loadingOverlay');if(o)o.style.display='flex';};"));
    res->print(F("window.hideLoading=function(){var o=document.getElementById('loadingOverlay');if(o)o.style.display='none';};"));
    res->print(F("window.toggleFile=function(cb,path){if(cb.checked){if(window.selectedFiles.indexOf(path)<0){window.selectedFiles.push(path);}}else{window.selectedFiles=window.selectedFiles.filter(function(p){return p!==path;});}updateRowHighlight(cb);};")); 
    res->print(F("window.toggleSelectAll=function(cb){document.querySelectorAll('.file-checkbox').forEach(function(c){c.checked=cb.checked;toggleFile(c,c.value);});updateSelectAllState();};")); 
    res->print(F("window.selectAllFiles=function(){var cb=document.getElementById('selectAll');if(cb){cb.checked=true;toggleSelectAll(cb);}};"));
    res->print(F("window.clearAllFiles=function(){var cb=document.getElementById('selectAll');if(cb){cb.checked=false;toggleSelectAll(cb);}};"));
    res->print(F("window.updateRowHighlight=function(cb){var row=cb.closest('.file-row');if(!row)return;if(cb.checked){row.classList.add('selected');}else{row.classList.remove('selected');}};"));
    res->print(F("window.filterFiles=function(term){var rows=document.querySelectorAll('.file-row');var filter=term.toLowerCase();rows.forEach(function(row){var nameEl=row.querySelector('[data-filter]');var name=(nameEl?nameEl.dataset.filter:'').toLowerCase();row.style.display=(filter===''||name.includes(filter))?'':'none';});};"));
    res->print(F("window.getStorageFromURL=function(){var t=document.querySelector('#files-table');if(t&&t.getAttribute('data-storage'))return t.getAttribute('data-storage');return(new URLSearchParams(window.location.search)).get('storage')||'sd';};")); 
    res->print(F("window.deleteSelectedFiles=function(){if(!window.selectedFiles||window.selectedFiles.length===0){customAlert(_ft('filesSelectFirst','Select files to delete'));return;}var storage=window.getStorageFromURL();customConfirm(_ft('filesDeleteConfirm','Delete {n} file(s)?').replace('{n}',window.selectedFiles.length),function(ok){if(!ok)return;window.showLoading();fetch('/api/sd/delete-multiple?storage='+storage,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({files:window.selectedFiles})}).then(function(r){return r.json();}).then(function(d){window.hideLoading();if(d.success){customAlert(d.deleted+_ft('filesDeletedOk',' file(s) deleted successfully'));location.reload();}else{var errMsg=_ft('filesDeleteError','Error deleting: ')+d.errors+_ft('filesDeleteDetail',' failure(s)');if(d.error_detail){errMsg+='\\n\\nDetails: '+d.error_detail;}customAlert(errMsg);}}).catch(function(e){window.hideLoading();customAlert(_ft('filesConnError','Connection error: ')+e);});});};"));
    res->print(F("window.updateSelectAllState=function(){var total=document.querySelectorAll('.file-checkbox').length;var checked=document.querySelectorAll('.file-checkbox:checked').length;var cb=document.getElementById('selectAll');if(cb){cb.checked=(total>0&&checked===total);cb.indeterminate=(checked>0&&checked<total);}};"));    res->print(F("window.createNewFolder=function(){var table=document.querySelector('#files-table');var cwd=window.decodeHtml(table?(table.getAttribute('data-cwd')||'/'):'/');var storage=window.getStorageFromURL();window.customPrompt(_ft('filesCtxNewFolderPrompt','Name of the new folder'),'nova_pasta',function(n){if(!n||!n.trim())return;window.showLoading();var body='cwd='+encodeURIComponent(cwd)+'&name='+encodeURIComponent(n.trim())+'&storage='+encodeURIComponent(storage)+'&json=1';fetch('/api/sd/create-dir',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body}).then(function(r){return r.json();}).then(function(d){window.hideLoading();if(d&&d.status==='ok'){location.reload();}else{customAlert((d&&d.msg)||_ft('filesCtxNewFolderError','Error creating folder'));}}).catch(function(e){window.hideLoading();customAlert(_ft('filesConnError','Connection error: ')+e);});});};"));
    res->print(F("window.renameSelected=function(){if(!window.selectedFiles||window.selectedFiles.length===0){customAlert(_ft('filesSelectFirst','Select files to rename'));return;}if(window.selectedFiles.length>1){customAlert(_ft('filesRenameOneOnly','Select only 1 file to rename'));return;}var path=window.selectedFiles[0];var name=path.split('/').pop();var table=document.querySelector('#files-table');var cwd=window.decodeHtml(table?(table.getAttribute('data-cwd')||'/'):'/');var storage=window.getStorageFromURL();window.customPrompt(_ft('filesCtxRenamePrompt','New name for: ')+name,name,function(novo){if(!novo||!novo.trim())return;var f=document.createElement('form');f.method='POST';f.action='/api/sd/rename';var fields={cwd:cwd,src:name,dst:novo.trim(),storage:storage};['cwd','src','dst','storage'].forEach(function(k){var i=document.createElement('input');i.type='hidden';i.name=k;i.value=fields[k];f.appendChild(i);});document.body.appendChild(f);f.submit();});};"));
    res->print(F("window.sortByType=function(){var tbody=document.querySelector('#files-table tbody');if(!tbody)return;var rows=Array.from(tbody.querySelectorAll('.file-row'));if(rows.length===0)return;function isDigitChar(ch){return ch>='0'&&ch<='9';}function isSep(ch){return ch==='-'||ch==='_'||ch==='.';}function hasDateInName(name){if(!name)return false;for(var i=0;i+10<=name.length;i++){if(isDigitChar(name[i])&&isDigitChar(name[i+1])&&isDigitChar(name[i+2])&&isDigitChar(name[i+3])&&isSep(name[i+4])&&isDigitChar(name[i+5])&&isDigitChar(name[i+6])&&isSep(name[i+7])&&isDigitChar(name[i+8])&&isDigitChar(name[i+9]))return true;if(isDigitChar(name[i])&&isDigitChar(name[i+1])&&isSep(name[i+2])&&isDigitChar(name[i+3])&&isDigitChar(name[i+4])&&isSep(name[i+5])&&isDigitChar(name[i+6])&&isDigitChar(name[i+7])&&isDigitChar(name[i+8])&&isDigitChar(name[i+9]))return true;}return false;}function readLast(row){var value=parseInt(row.dataset.last||'0',10);return isNaN(value)?0:value;}rows.sort(function(a,b){var isDirA=a.dataset.isdir==='1';var isDirB=b.dataset.isdir==='1';if(isDirA!==isDirB)return isDirA?-1:1;if(isDirA){var hasDateA=hasDateInName(a.dataset.name||'');var hasDateB=hasDateInName(b.dataset.name||'');if(hasDateA!==hasDateB)return hasDateA?1:-1;if(!hasDateA)return (a.dataset.name||'').localeCompare(b.dataset.name||'',undefined,{sensitivity:'base'});var lastA=readLast(a);var lastB=readLast(b);if(lastA!==lastB)return lastB-lastA;return (a.dataset.name||'').localeCompare(b.dataset.name||'',undefined,{sensitivity:'base'});}var lastA=readLast(a);var lastB=readLast(b);if(lastA!==lastB)return lastB-lastA;return (a.dataset.name||'').localeCompare(b.dataset.name||'',undefined,{sensitivity:'base'});});rows.forEach(function(row){tbody.appendChild(row);});};"));
    res->print(F("</script>"));

    if(!usesPsram && (!activeFS || !mounted)){
        res->print(F("<p style='color:#d33' data-i18n='filesNotAvailMsg'>&#9888;&#65039; File system not available</p></div></main></body></html>"));
        request->send(res);
        return;
    }

    if(usesPsram && !mounted){
        res->print(F("<p style='color:#d33' data-i18n='filesPsramUnavail'>&#9888;&#65039; PSRAM not available</p></div></main></body></html>"));
        request->send(res);
        return;
    }

    bool dirOpenFailed = false;
    bool scanTruncated = false;

    res->print(F("<div style='border:2px solid #3b82f6;border-radius:8px;padding:16px;background:#0f172a;margin-top:20px'>"));
    res->print(F("<h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='filesSectionTitle'>&#128193; Files and Folders</h2>"));
    if(scanTruncated){
        res->print(F("<div style='background:#78350f;border-left:4px solid #f59e0b;padding:10px 12px;margin:8px 0 12px 0;font-size:12px;line-height:1.6;color:#fef3c7;border-radius:4px'>&#9888;&#65039; List limited to the first 1200 items for stability. Navigate into subfolders to see the rest.</div>"));
    }
    res->print(F("<div style='margin-bottom:10px'><input type='text' id='filterInput' class='filter-input' placeholder='&#128269; Filter files/folders' onkeyup='filterFiles(this.value)'></div>"));

    // Breadcrumb navigation.
    res->print(F("<div style='font-size:12px;background:#0a1929;border:1px solid #3b82f6;border-radius:6px;padding:6px 10px;margin-bottom:10px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap' title='"));
    res->print(currentPath);
    res->print(F("'>"));
    struct Seg { String name; String full; };
    Seg segs[20]; int segCount=0;
    if(currentPath.length()>1){
        String acc=""; int start=1; while(start < (int)currentPath.length() && segCount < 20){ int next=currentPath.indexOf('/', start); String part=(next==-1)? currentPath.substring(start): currentPath.substring(start,next); if(part.length()>0){ acc+="/"; acc+=part; segs[segCount].name=part; segs[segCount].full=acc; segCount++; } if(next==-1) break; start=next+1; }
    }
    auto printSeg=[&](int i){
        res->print(F(" / <a style='color:#60a5fa;text-decoration:none' href='/files?storage="));
        res->print(storageType);
        res->print(F("&path="));
        res->print(segs[i].full);
        res->print(F("&sort="));
        res->print(sortMode);
        res->print(F("'>"));
        res->print(segs[i].name);
        res->print(F("</a>"));
    };
    res->print(F("<a href='/files?storage="));
    res->print(storageType);
    res->print(F("&path=%2F&sort="));
    res->print(sortMode);
    res->print(F("' style='color:#60a5fa;text-decoration:none'>/</a>"));
    if(segCount>0){
        if(segCount <= 6){ for(int i=0;i<segCount;i++) printSeg(i); }
        else {
            for(int i=0;i<3 && i<segCount;i++) printSeg(i);
            res->print(F(" / <span style='color:#94a3b8'>&#8230;</span>"));
            for(int i=segCount-2;i<segCount;i++) printSeg(i);
        }
    }
    res->print(F("</div>"));

    // Storage selector (shown when multiple options are available).
    bool showStorageSelector = psramVFSIsEnabled();
#if ENABLE_SDCARD==1
    showStorageSelector = showStorageSelector || sdcardIsMounted();
#endif
#if ENABLE_FILESYSTEM==1
    bool flashReadyForSelector = littlefsBegin() && littlefsMounted();
    showStorageSelector = showStorageSelector || flashReadyForSelector;
#endif
    if(showStorageSelector){
        res->print(F("<div style='margin-bottom:10px;padding:8px;background:#0a1929;border:1px solid #3b82f6;border-radius:6px'>"));
        res->print(F("<label style='font-size:13px;color:#94a3b8;margin-right:8px' data-i18n='filesStorageLabel'>&#128190; Storage:</label>"));
        res->print(F("<select id='storage-select' onchange='switchStorage()' style='padding:4px 8px;border:1px solid #3b82f6;border-radius:4px;background:#0f172a;color:#e0e7ff;font-size:12px;cursor:pointer'>"));
#if ENABLE_SDCARD==1
        if (sdcardIsMounted()) {
            res->print(F("<option value='sd'"));
            if(storageType == "sd") res->print(F(" selected"));
            res->print(F(">&#128190; SD Card</option>"));
        }
#endif
#if ENABLE_FILESYSTEM==1
        res->print(F("<option value='flash'"));
        if(storageType == "flash") res->print(F(" selected"));
        res->print(F(">&#9889; Flash (LittleFS)"));
    if(!flashReadyForSelector) res->print(F(" (not mounted)"));
        res->print(F("</option>"));
#endif
        if (psramFound() || psramVFSIsEnabled()) {
            res->print(F("<option value='psram'"));
            if(storageType == "psram") res->print(F(" selected"));
            res->print(F(">&#129504; PSRAM Virtual"));
            if(!psramVFSIsEnabled()) res->print(F(" (not available)"));
            res->print(F("</option>"));
        }
        res->print(F("</select>"));
        res->print(F("<script>function switchStorage(){var s=document.getElementById('storage-select').value;var u=new URL(window.location);u.searchParams.set('storage',s);u.searchParams.delete('path');window.location.href=u.toString();}</script>"));
        res->print(F("</div>"));
    }

    // Action button grid: select/clear/sort/back/refresh/delete.
    res->print(F("<div style='display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:6px;margin-bottom:10px'>"));
    res->print(F("<button class='btn' style='padding:6px 12px;font-size:12px;background:#2563eb;border:1px solid #2563eb' onclick='selectAllFiles()' data-i18n='filesBtnSelect'>&#9989; Select</button>"));
    res->print(F("<button class='btn' style='padding:6px 12px;font-size:12px;background:#1e40af;border:1px solid #1e40af' onclick='clearAllFiles()' data-i18n='filesBtnClear'>&#9746; Clear</button>"));
    res->print(F("<button class='btn' style='padding:6px 12px;font-size:12px;background:#7c3aed;border:1px solid #7c3aed' onclick=\"var u=new URL(window.location);var m=u.searchParams.get('sort')||'date';u.searchParams.set('sort',m==='name'?'date':'name');window.location.href=u.toString()\" data-i18n='filesBtnSort'>&#128260; Sort</button>"));
    res->print(F("<button class='btn' style='padding:6px 12px;font-size:12px;background:#3b82f6;border:1px solid #3b82f6' onclick='(function(){var url=new URL(window.location);url.searchParams.delete(\"_t\");url.searchParams.set(\"_t\",Date.now());window.location.href=url.toString();})()' data-i18n='filesBtnRefresh'>&#128260; Refresh</button>"));
    res->print(F("<form method='GET' action='/files' style='margin:0'><input type='hidden' name='storage' value='"));
    res->print(storageType);
    res->print(F("'><input type='hidden' name='path' value='"));
    res->print(parentPath);
    res->print(F("'><input type='hidden' name='sort' value='"));
    res->print(sortMode);
    res->print(F("'><button class='btn' style='width:100%;padding:6px 12px;font-size:12px;background:#1e40af;border:1px solid #1e40af' data-i18n='filesBtnBack'>&#11013;&#65039; Back</button></form>"));
    res->print(F("<button class='btn' style='padding:6px 12px;font-size:12px;background:#dc2626;border:1px solid #dc2626' onclick='deleteSelectedFiles()' data-i18n='filesBtnDelete'>&#128465;&#65039; Delete</button>"));
    res->print(F("<button class='btn' style='padding:6px 12px;font-size:12px;background:#065f46;border:1px solid #065f46' onclick='createNewFolder()' data-i18n='filesBtnNewFolder'>&#128193; New Folder</button>"));
    res->print(F("<button class='btn' style='padding:6px 12px;font-size:12px;background:#7c3aed;border:1px solid #7c3aed' onclick='renameSelected()' data-i18n='filesBtnRename'>&#9999;&#65039; Rename</button>"));
    res->print(F("</div>"));


    res->print(F("<style>body{overflow-x:hidden}main.page{overflow-x:hidden}#table-wrapper{overflow-x:auto;overflow-y:visible;-webkit-overflow-scrolling:touch}#files-table{width:100%;table-layout:fixed}#files-table td{padding:0px 1px;font-size:12px;height:16px;line-height:16px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}#files-table td a{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}#files-table tr:nth-child(even){background:#042743}#files-table a{color:#fff !important;text-decoration:none}#files-table a:visited{color:#fff !important}#files-table th{padding:0px 1px;font-size:14px;font-weight:bold;height:18px;line-height:18px;position:sticky;top:0;background:#0a2540;z-index:1}.file-row{cursor:pointer}.file-row.selected{background:#1d5c92 !important}.file-row .file-type-cell{transition:.1s}.file-row:hover .file-type-cell{filter:brightness(1.3)}.filter-input{width:100%;padding:3px 6px;margin:3px 0;border:1px solid #3b82f6;border-radius:4px;background:#0a1929;color:#e0e7ff;font-size:12px}input[type='checkbox']{width:14px;height:14px;cursor:pointer}.dl-link{color:#7dd3fc!important;background:rgba(59,130,246,0.08);border-radius:3px;padding:0 2px}</style>"));

    res->print(F("<div id='table-wrapper'><table id='files-table' class='table' style='margin-top:0px;width:100%;border-collapse:collapse' data-cwd='"));
    res->print(escapedCwdAttr);
    res->print(F("' data-storage='"));
    res->print(storageType);
    res->print(F("'><thead><tr><th style='width:5%;padding:0px 1px'><input type='checkbox' id='selectAll' class='select-all-checkbox' onchange='toggleSelectAll(this)'></th><th style='width:8%;font-size:10px;white-space:nowrap'>Tipo</th><th style='flex:1;min-width:50px'>Nome</th><th style='width:20%;font-size:10px;white-space:nowrap'>Tamanho</th></tr></thead><tbody>"));

        // Collect directory entries with modification timestamps for sorting.
        struct FileEntry {
            String fname;
            String fullPath;
            bool isDir;
            uint64_t size;
            time_t lastWrite;
        };
        std::vector<FileEntry> entries;

        if(usesPsram){
            if(currentPath == "/"){
                std::vector<String> folders = psramVFSListFolders();
                for(const String& folder : folders){
                    FileEntry entry;
                    entry.fname = folder;
                    entry.fullPath = "/" + folder;
                    entry.isDir = true;
                    entry.size = 0;
                    entry.lastWrite = millis() / 1000;
                    entries.push_back(entry);
                    if(entries.size() >= MAX_FILES_PAGE_SCAN_ENTRIES){
                        scanTruncated = true;
                        break;
                    }
                    yield();
                }
            } else {
                std::vector<String> files = psramVFSListFiles(currentPath);
                for(const String& filename : files){
                    FileEntry entry;
                    entry.fname = filename;
                    entry.fullPath = currentPath;
                    if(!entry.fullPath.endsWith("/")) entry.fullPath += "/";
                    entry.fullPath += filename;
                    entry.fullPath.replace("//", "/");
                    entry.isDir = false;
                    entry.size = psramVFSGetFileSize(currentPath, filename);
                    entry.lastWrite = millis() / 1000;
                    entries.push_back(entry);
                    if(entries.size() >= MAX_FILES_PAGE_SCAN_ENTRIES){
                        scanTruncated = true;
                        break;
                    }
                    yield();
                }
            }
        } else {
            // Serialise with the chunked library scan (both acquire the same
            // recursive mutex; chunks are <50ms so this never times out).
            const bool sdAccess = (storageType == "sd");
#if ENABLE_SDCARD==1
            const bool sdLocked = sdAccess ? sdMutexLock(pdMS_TO_TICKS(3000)) : true;
#else
            const bool sdLocked = true;
#endif
            if (!sdLocked) {
                dirOpenFailed = true;
            } else {
                File dir = activeFS->open(currentPath.c_str());
                if ((!dir || !dir.isDirectory()) && currentPath != "/") {
                    if (dir) dir.close();
                    currentPath = "/";
                    parentPath = "/";
                    dir = activeFS->open(currentPath.c_str());
                }
                if ((!dir || !dir.isDirectory()) && currentPath == "/") {
                    if (dir) dir.close();
                    dir = activeFS->open("");
                }

                if (!dir || !dir.isDirectory()) {
                    if (dir) dir.close();
                    dirOpenFailed = true;
                } else {
                    File f = dir.openNextFile();
                    while(f) {
                        FileEntry entry;
                        entry.fname = String(f.name());
                        entry.isDir = f.isDirectory();
                        entry.size = f.size();
                        entry.lastWrite = f.getLastWrite();
                        if(entry.fname.startsWith("/")){
                            entry.fullPath = entry.fname;
                        } else {
                            entry.fullPath = currentPath;
                            if(!entry.fullPath.endsWith("/")) entry.fullPath += "/";
                            entry.fullPath += entry.fname;
                            entry.fullPath.replace("//", "/");
                        }
                        entries.push_back(entry);
                        f.close();
                        if(entries.size() >= MAX_FILES_PAGE_SCAN_ENTRIES){
                            scanTruncated = true;
                            break;
                        }
                        f = dir.openNextFile();
                        yield();
                    }
                    dir.close();
                }
#if ENABLE_SDCARD==1
                if (sdAccess) sdMutexUnlock();
#endif
            }
        }

        auto isDateSeparator = [](char c) -> bool {
            return c == '-' || c == '_' || c == '.';
        };
        auto hasDateInName = [&isDateSeparator](const String &fname) -> bool {
            const int len = fname.length();
            for(int i = 0; i + 10 <= len; ++i) {
                const bool yyyyMmDd =
                    isDigit(fname[i]) && isDigit(fname[i + 1]) &&
                    isDigit(fname[i + 2]) && isDigit(fname[i + 3]) &&
                    isDateSeparator(fname[i + 4]) &&
                    isDigit(fname[i + 5]) && isDigit(fname[i + 6]) &&
                    isDateSeparator(fname[i + 7]) &&
                    isDigit(fname[i + 8]) && isDigit(fname[i + 9]);
                if(yyyyMmDd) return true;
                const bool ddMmYyyy =
                    isDigit(fname[i]) && isDigit(fname[i + 1]) &&
                    isDateSeparator(fname[i + 2]) &&
                    isDigit(fname[i + 3]) && isDigit(fname[i + 4]) &&
                    isDateSeparator(fname[i + 5]) &&
                    isDigit(fname[i + 6]) && isDigit(fname[i + 7]) &&
                    isDigit(fname[i + 8]) && isDigit(fname[i + 9]);
                if(ddMmYyyy) return true;
            }
            return false;
        };

        // Sort based on mode — applies to ALL pages/subfolders via URL param.
        std::sort(entries.begin(), entries.end(), [&hasDateInName,&sortMode](const FileEntry& a, const FileEntry& b) -> bool {
            if(a.isDir != b.isDir) return a.isDir;
            if(sortMode == "name" || sortMode == "name_asc") {
                return strcasecmp(a.fname.c_str(), b.fname.c_str()) < 0;
            }
            if(a.isDir) {
                const bool aHasDate = hasDateInName(a.fname);
                const bool bHasDate = hasDateInName(b.fname);
                if(aHasDate != bHasDate) return !aHasDate;
                if(!aHasDate) return a.fname < b.fname;
                if(a.lastWrite != b.lastWrite) return a.lastWrite > b.lastWrite;
                return a.fname < b.fname;
            }
            if(a.lastWrite != b.lastWrite) return a.lastWrite > b.lastWrite;
            return a.fname < b.fname;
        });

        int totalCount = entries.size();
        bool hasMore = (totalCount > endIndex);
        (void)hasMore;

        std::vector<FileEntry> pageSlice;
        for(int i = startIndex; i < endIndex && i < totalCount; i++) {
            pageSlice.push_back(entries[i]);
        }

        for(const FileEntry& entry : pageSlice) {
            String escapedNameAttr = escapeHtmlAttr(entry.fname);
            String escapedFullAttr = escapeHtmlAttr(entry.fullPath);
            String encodedFullHref = urlEncode(entry.fullPath);
            String fnameLower = entry.fname;
            fnameLower.toLowerCase();
            const char *ext="&#128196;";
            if(fnameLower.endsWith(".txt")) ext="&#128221;";
            else if(fnameLower.endsWith(".pdf")) ext="&#128213;";
            else if(fnameLower.endsWith(".zip")||fnameLower.endsWith(".rar")||fnameLower.endsWith(".7z")) ext="&#128284;";
            else if(fnameLower.endsWith(".jpg")||fnameLower.endsWith(".png")||fnameLower.endsWith(".gif")||fnameLower.endsWith(".bmp")) ext="&#128444;";
            else if(fnameLower.endsWith(".csv")||fnameLower.endsWith(".xlsx")||fnameLower.endsWith(".xls")) ext="&#128202;";
            else if(fnameLower.endsWith(".mp3")||fnameLower.endsWith(".wav")||fnameLower.endsWith(".flac")||fnameLower.endsWith(".aac")||fnameLower.endsWith(".m4a")||fnameLower.endsWith(".ogg")||fnameLower.endsWith(".opus")) ext="&#127925;";
            else if(fnameLower.endsWith(".mp4")||fnameLower.endsWith(".avi")||fnameLower.endsWith(".mkv")) ext="&#127916;";

            res->print(F("<tr class='file-row' data-name='"));
            res->print(escapedNameAttr);
            res->print(F("' data-full='"));
            res->print(escapedFullAttr);
            res->print(F("' data-isdir='"));
            res->print(entry.isDir?F("1"):F("0"));
            res->print(F("' data-size='"));
            res->print(entry.size);
            res->print(F("' data-last='"));
            res->print((uint32_t)entry.lastWrite);
            res->print(F("' data-type='"));
            res->print(ext);
            res->print(F("'>"));

            res->print(F("<td style='text-align:center'><input type='checkbox' class='file-checkbox' value='"));
            res->print(escapedFullAttr);
            res->print(F("' data-size='"));
            res->print(entry.size);
            res->print(F("'></td>"));

            res->print(F("<td class='file-type-cell'>"));
            res->print(entry.isDir?F("&#128193;"):F(ext));
            res->print(F("</td>"));

            res->print(F("<td data-filter='"));
            res->print(escapedNameAttr);
            res->print(F("'>"));
            if(entry.isDir){
                res->print(F("<a href='/files?storage="));
                res->print(storageType);
                res->print(F("&path="));
                res->print(encodedFullHref);
                res->print(F("&sort="));
                res->print(sortMode);
                res->print(F("' onclick='return true'>"));
                res->print(escapedNameAttr);
                res->print(F("</a>"));
            } else {
                res->print(F("<a class='dl-link' href='/api/sd/download?storage="));
                res->print(storageType);
                res->print(F("&path="));
                res->print(encodedFullHref);
                res->print(F("' download='"));
                res->print(escapedNameAttr);
                res->print(F("' onclick='if(window.isDownloadInProgress){customAlert(\"&#9888;&#65039; SD card busy. Please wait.\");return false;}window.isDownloadInProgress=true;setTimeout(function(){window.isDownloadInProgress=false;},2000);'>"));
                res->print(escapedNameAttr);
                res->print(F("</a>"));
            }
            res->print(F("</td>"));

            res->print(F("<td>"));
            if(entry.isDir){
                res->print(F("&#8212;"));
            } else {
                uint64_t sz = entry.size;
                if(sz < 1024ULL){
                    res->print(sz);
                    res->print(F(" B"));
                } else {
                    double kb = (double)sz/1024.0;
                    if(kb < 1024.0){
                        char b[24];
                        snprintf(b,sizeof(b),"%.2f KB", kb);
                        res->print(b);
                    } else {
                        double mb = kb/1024.0;
                        char b2[24];
                        snprintf(b2,sizeof(b2),"%.2f MB", mb);
                        res->print(b2);
                    }
                }
            }
            res->print(F("</td>"));
            res->print(F("</tr>"));
        }

        if(totalCount == 0) {
            if (dirOpenFailed) {
                res->print(F("<tr class='empty-row'><td colspan='4' style='height:40px;color:#fca5a5;text-align:center' data-i18n='filesDirUnavailable'>Directory unavailable on this storage</td></tr>"));
            } else {
                res->print(F("<tr class='empty-row' style='cursor:pointer'><td colspan='4' style='height:40px'>&nbsp;</td></tr>"));
            }
        }

        res->print(F("</tbody></table>"));

        // Pagination controls.
        int totalPages=(totalCount+PAGE_SIZE-1)/PAGE_SIZE;
        if(totalPages<1) totalPages=1;
        res->print(F("<div style='text-align:center; margin:30px 0 20px;'>"));
        res->print(F("<div style='display:inline-block; background:#1e293b; border:2px solid #3b82f6; border-radius:6px; padding:10px 20px; margin-bottom:15px;'>"));
        res->print(F("<span style='color:#94a3b8; font-size:13px; font-weight:600;' data-i18n='filesPageLabel'>PAGE </span>"));
        res->printf("<span style='color:#60a5fa; font-size:18px; font-weight:700;'>%d</span>", page+1);
        res->print(F("<span style='color:#475569; font-size:13px; margin:0 6px;'>/</span>"));
        res->printf("<span style='color:#94a3b8; font-size:16px; font-weight:600;'>%d</span>", totalPages);
        res->print(F("</div>"));
        if(totalPages>1){
            res->print(F("<div style='display:flex; justify-content:center; gap:8px;'>"));
            if(page>0){
                res->printf("<a href='/files?storage=%s&path=%s&sort=%s&p=0' style='display:inline-block; background:#1e40af; color:#fff; padding:8px 16px; border-radius:6px; text-decoration:none; font-size:14px; font-weight:600; transition:background 0.2s;' onmouseover=\"this.style.background='#1e3a8a'\" onmouseout=\"this.style.background='#1e40af'\">&#9198; First</a>",
                    storageType.c_str(), urlEncode(currentPath).c_str(), sortMode.c_str());
                res->printf("<a href='/files?storage=%s&path=%s&sort=%s&p=%d' style='display:inline-block; background:#1e40af; color:#fff; padding:8px 16px; border-radius:6px; text-decoration:none; font-size:14px; font-weight:600; transition:background 0.2s;' onmouseover=\"this.style.background='#1e3a8a'\" onmouseout=\"this.style.background='#1e40af'\">&#9664; Previous</a>",
                    storageType.c_str(), urlEncode(currentPath).c_str(), sortMode.c_str(), page-1);
            } else {
                res->print(F("<span style='display:inline-block; background:#1e293b; color:#475569; padding:8px 16px; border-radius:6px; font-size:14px; font-weight:600; cursor:not-allowed;'>&#9198; First</span>"));
                res->print(F("<span style='display:inline-block; background:#1e293b; color:#475569; padding:8px 16px; border-radius:6px; font-size:14px; font-weight:600; cursor:not-allowed;'>&#9664; Previous</span>"));
            }
            if(page+1<totalPages){
                res->printf("<a href='/files?storage=%s&path=%s&sort=%s&p=%d' style='display:inline-block; background:#1e40af; color:#fff; padding:8px 16px; border-radius:6px; text-decoration:none; font-size:14px; font-weight:600; transition:background 0.2s;' onmouseover=\"this.style.background='#1e3a8a'\" onmouseout=\"this.style.background='#1e40af'\">Next &#9654;</a>",
                    storageType.c_str(), urlEncode(currentPath).c_str(), sortMode.c_str(), page+1);
                res->printf("<a href='/files?storage=%s&path=%s&sort=%s&p=%d' style='display:inline-block; background:#1e40af; color:#fff; padding:8px 16px; border-radius:6px; text-decoration:none; font-size:14px; font-weight:600; transition:background 0.2s;' onmouseover=\"this.style.background='#1e3a8a'\" onmouseout=\"this.style.background='#1e40af'\">Last &#9197;</a>",
                    storageType.c_str(), urlEncode(currentPath).c_str(), sortMode.c_str(), totalPages-1);
            } else {
                res->print(F("<span style='display:inline-block; background:#1e293b; color:#475569; padding:8px 16px; border-radius:6px; font-size:14px; font-weight:600; cursor:not-allowed;'>Next &#9654;</span>"));
                res->print(F("<span style='display:inline-block; background:#1e293b; color:#475569; padding:8px 16px; border-radius:6px; font-size:14px; font-weight:600; cursor:not-allowed;'>Last &#9197;</span>"));
            }
            res->print(F("</div>"));
        }
        res->print(F("</div>"));

        res->print(F("<script>"));
    res->print(F("document.addEventListener('DOMContentLoaded',function(){"));

    res->print(F("  const allCheckboxes = document.querySelectorAll('.file-checkbox');"));
    res->print(F("  allCheckboxes.forEach(function(cb){"));
    res->print(F("    cb.addEventListener('change', function(){"));
    res->print(F("      window.toggleFile(this, this.value);"));
    res->print(F("      window.updateSelectAllState();"));
    res->print(F("    });"));
    res->print(F("  });"));
    res->print(F("  window.updateSelectAllState();"));
    res->print(F("});"));
    res->print(F("document.addEventListener('keydown',e=>{"));
    res->print(F("  if(e.key==='Delete'){deleteSelectedFiles();}"));
    res->print(F("  else if(e.ctrlKey&&e.key==='a'){e.preventDefault();selectAllFiles();}"));
    res->print(F("  else if(e.key==='Escape'){clearAllFiles();const fi=document.getElementById('filterInput');if(fi){fi.value='';filterFiles('');}}"));
    res->print(F("});"));
    res->print(F("(function(){"));
    res->print(F("  const filterInput = document.getElementById('filterInput');"));
    res->print(F("  if(filterInput){"));
    res->print(F("    filterInput.addEventListener('keydown',e=>{"));
    res->print(F("      if(e.key==='Escape'){e.preventDefault();e.target.value='';filterFiles('');}"));
    res->print(F("    });"));
    res->print(F("  }"));
    res->print(F("})();"));
    res->print(F("(function(){"));

    res->print(F("var menu=document.createElement('div');menu.id='ctx';"));
    res->print(F("menu.style.cssText='position:fixed;z-index:9999;background:linear-gradient(135deg,#0a2742 0%,#083050 100%);border:2px solid #1e66c7;border-radius:8px;min-width:180px;box-shadow:0 8px 24px rgba(0,0,0,0.4);display:none;color:#e6f1ff;font-size:14px;overflow:hidden;font-family:system-ui,-apple-system,sans-serif';"));
    res->print(F("function addItem(emoji,label,cb){var it=document.createElement('button');it.type='button';it.style.cssText='all:unset;display:block;width:100%;padding:10px 14px;cursor:pointer;transition:background 0.15s;font-weight:500';it.innerHTML='<span style=\"display:inline-block;width:24px\">'+emoji+'</span>'+label;it.onmouseenter=function(){it.style.background='rgba(30,102,199,0.3)';};it.onmouseleave=function(){it.style.background='transparent';};it.onclick=function(e){e.preventDefault();hide();try{cb&&cb();}catch(ex){console.error('[ctx] Error in callback:', ex);}};menu.appendChild(it);}"));
    res->print(F("function show(x,y){menu.style.left=x+'px';menu.style.top=y+'px';menu.style.display='block';var rect=menu.getBoundingClientRect();var vw=window.innerWidth,vh=window.innerHeight,newX=x,newY=y;if(rect.right>vw)newX=vw-rect.width-10;if(rect.bottom>vh)newY=vh-rect.height-10;if(newX<10)newX=10;if(newY<10)newY=10;menu.style.left=newX+'px';menu.style.top=newY+'px';}"));
    res->print(F("function hide(){menu.style.display='none';}"));
    res->print(F("document.addEventListener('click',hide);"));
    res->print(F("document.addEventListener('keydown',function(e){if(e.key==='Escape')hide();});"));
    res->print(F("if(document.body)document.body.appendChild(menu);"));

    res->print(F("function submitPost(url,fields){var f=document.createElement('form');f.method='POST';f.action=url;for(var k in(fields||{})){var i=document.createElement('input');i.type='hidden';i.name=k;i.value=fields[k];f.appendChild(i);}document.body.appendChild(f);f.submit();}"));
    res->print(F("function handleContextMenu(e){var tr=e.target&&e.target.closest?e.target.closest('tr.file-row'):null;var emptyRow=e.target&&e.target.closest?e.target.closest('tr.empty-row'):null;if(!tr&&!emptyRow){return;}e.preventDefault();e.stopPropagation();var table=document.querySelector('#files-table');var cwd=window.decodeHtml(table?(table.getAttribute('data-cwd')||'/'):'/');var storage=window.getStorageFromURL();menu.innerHTML='';if(emptyRow){addItem('📄',_ft('filesCtxNewFile','New file'),function(){window.customPrompt(_ft('filesCtxNewFilePrompt','Name of the new file'),'novo.txt',function(n){if(!n||!n.trim())return;submitPost('/api/sd/create-file',{cwd:cwd,name:n.trim(),storage:storage});});});addItem('📁',_ft('filesCtxNewFolder','New folder'),function(){window.customPrompt(_ft('filesCtxNewFolderPrompt','Name of the new folder'),'nova_pasta',function(n){if(!n||!n.trim())return;window.createNewFolder();});});show(e.clientX||10,e.clientY||10);return;}var name=window.decodeHtml(tr.getAttribute('data-name')||'');var full=window.decodeHtml(tr.getAttribute('data-full')||'');var isdir=(tr.getAttribute('data-isdir')||'0')==='1';if(isdir){addItem('📂',_ft('filesCtxOpen','Open'),function(){var s=(new URLSearchParams(window.location.search)).get('sort')||'date';window.location.href='/files?path='+encodeURIComponent(full)+'&storage='+storage+'&sort='+s;});}else{addItem('⬇️','Download',function(){if(window.isDownloadInProgress){customAlert('⏳ Storage busy. Please wait.');return;}window.isDownloadInProgress=true;var link=document.createElement('a');link.href='/api/sd/download?path='+encodeURIComponent(full)+'&storage='+storage;link.download=name;document.body.appendChild(link);link.click();document.body.removeChild(link);setTimeout(function(){window.isDownloadInProgress=false;},2000);});}addItem('✏️',_ft('filesCtxRename','Rename'),function(){window.customPrompt(_ft('filesCtxRenamePrompt','New name for: ')+name,name,function(novo){if(!novo||!novo.trim())return;submitPost('/api/sd/rename',{cwd:cwd,src:name,dst:novo.trim(),storage:storage});});});addItem('🗑️',_ft('filesCtxDelete','Delete'),function(){window.customConfirm(_ft('filesCtxDeleteConfirm','Delete')+' \"'+name+'\"?',function(ok){if(!ok)return;fetch('/api/sd/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'cwd='+encodeURIComponent(cwd)+'&path='+encodeURIComponent(name)+'&storage='+encodeURIComponent(storage)+'&json=1'}).then(function(r){return r.json();}).then(function(d){if(d&&d.status==='ok'){location.reload();}else{customAlert((d&&d.msg)||_ft('filesCtxDeleteError','Error deleting'));}}).catch(function(e){customAlert(_ft('filesConnError','Connection error: ')+e);});});});var sep=document.createElement('div');sep.style.cssText='height:1px;background:rgba(30,102,199,0.2);margin:6px 0';menu.appendChild(sep);addItem('📄',_ft('filesCtxNewFile','New file'),function(){window.customPrompt(_ft('filesCtxNewFilePrompt','Name of the new file'),'novo.txt',function(n){if(!n||!n.trim())return;submitPost('/api/sd/create-file',{cwd:cwd,name:n.trim(),storage:storage});});});addItem('📁',_ft('filesCtxNewFolder','New folder'),function(){window.customPrompt(_ft('filesCtxNewFolderPrompt','Name of the new folder'),'nova_pasta',function(n){if(!n||!n.trim())return;window.createNewFolder();});});show(e.clientX||10,e.clientY||10);}"));
    res->print(F("if(!window.isDownloadInProgress){window.isDownloadInProgress=false;}"));
    res->print(F("document.addEventListener('contextmenu',handleContextMenu,true);"));
    res->print(F("})();"));
    res->print(F("</script>"));
    res->print(F("<script src='/auto.js?v=")); res->print(FIRMWARE_VERSION); res->print(F("&b=")); res->print(FIRMWARE_BUILD); res->print(F("' defer></script>"));
    res->print(F("</div></main></body></html>"));

        // Release the SD mutex before sending (only acquired for SD listings).
#if ENABLE_SDCARD==1
        if(!usesFlashForListing && !usesPsramForListing){
            releaseSDAccess();
        }
#endif
        request->send(res);
    });
}
