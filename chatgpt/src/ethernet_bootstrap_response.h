#include "languages/lang_active.h"
// ethernet_bootstrap_response.h - Utility and response helpers for the Ethernet bootstrap server
// Implementation-include: include ONLY from ethernet.cpp inside #if ENABLE_ETHERNET==1
// Do NOT add include guards - this file must be included exactly once.

extern const char* FIRMWARE_BUILD;

#include "webpages/handlers_unified/core_admin_handler.h"
#include "webpages/handlers_unified/storage_handler.h"
#include "webpages/handlers_unified/network_handler.h"

#include "psram_helpers.h"

static bool ethernetHasUsableIP(const IPAddress& ip) {
    return (ip[0] != 0) || (ip[1] != 0) || (ip[2] != 0) || (ip[3] != 0);
}

static String ethernetHtmlEscape(const String& input) {
    String output;
    output.reserve(input.length() + 16);
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        switch (c) {
            case '&': output += F("&amp;"); break;
            case '<': output += F("&lt;"); break;
            case '>': output += F("&gt;"); break;
            case '"': output += F("&quot;"); break;
            case '\'': output += F("&#39;"); break;
            default: output += c; break;
        }
    }
    return output;
}

static String ethernetJsonEscape(const String& input) {
    String output;
    output.reserve(input.length() + 16);
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        switch (c) {
            case '"': output += F("\\\""); break;
            case '\\': output += F("\\\\"); break;
            case '\n': output += F("\\n"); break;
            case '\r': output += F("\\r"); break;
            case '\t': output += F("\\t"); break;
            default:
                if ((uint8_t)c < 32) {
                    char escaped[7];
                    snprintf(escaped, sizeof(escaped), "\\u%04X", (unsigned char)c);
                    output += escaped;
                } else {
                    output += c;
                }
                break;
        }
    }
    return output;
}

static String ethernetFormatBytes(uint64_t value) {
    char buffer[48];
    if (value >= (1024ULL * 1024ULL * 1024ULL)) {
        snprintf(buffer, sizeof(buffer), "%.2f GB", (double)value / (1024.0 * 1024.0 * 1024.0));
    } else if (value >= (1024ULL * 1024ULL)) {
        snprintf(buffer, sizeof(buffer), "%.2f MB", (double)value / (1024.0 * 1024.0));
    } else if (value >= 1024ULL) {
        snprintf(buffer, sizeof(buffer), "%.2f KB", (double)value / 1024.0);
    } else {
        snprintf(buffer, sizeof(buffer), "%llu B", (unsigned long long)value);
    }
    return String(buffer);
}

static String ethernetBuildPageShell(const String& title, const String& body, const String& extraHead = String()) {
    String html;
    html.reserve(body.length() + extraHead.length() + 1400);
    html += F("<!doctype html><html lang='pt'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>");
    html += ethernetHtmlEscape(title);
    html += F("</title><style>");
    html += F("*{box-sizing:border-box}body{margin:0;font-family:Arial,sans-serif;background:#060d28;color:#dbeafe}main{max-width:920px;margin:0 auto;padding:24px}h1,h2{color:#60a5fa}section{background:#0f172a;border:2px solid #3b82f6;border-radius:12px;padding:18px;margin-top:16px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px}.card{background:#111c3a;border:1px solid #334155;border-radius:10px;padding:14px}.btn{display:inline-block;background:#1e66c7;color:#fff;text-decoration:none;padding:10px 18px;border-radius:999px;border:1px solid #20a3b9;margin:6px 6px 0 0;cursor:pointer}.btn:hover{background:#20a3b9;color:#04111f}pre{background:#020617;border:1px solid #334155;border-radius:10px;padding:14px;overflow:auto;white-space:pre-wrap}label{display:block;margin:10px 0 4px;color:#93c5fd;font-weight:700}input,select,button{font:inherit}input,select{width:100%;padding:10px;border-radius:8px;border:1px solid #3b82f6;background:#0b1630;color:#e2e8f0}.muted{color:#94a3b8}.ok{color:#34d399}.warn{color:#fbbf24}.err{color:#f87171}#msg{margin-top:12px;font-weight:700}code{color:#bfdbfe}a{color:#7dd3fc}@media(max-width:640px){main{padding:14px}}</style>");
    if (extraHead.length() > 0) {
        html += extraHead;
    }
    html += F("</head><body><main>");
    html += body;
    html += F("</main></body></html>");
    return html;
}

static String ethernetUrlDecode(const String& value) {
    String output;
    output.reserve(value.length());
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value[i];
        if (c == '+') {
            output += ' ';
            continue;
        }
        if (c == '%' && (i + 2) < value.length()) {
            char hi = value[i + 1];
            char lo = value[i + 2];
            if (isxdigit((unsigned char)hi) && isxdigit((unsigned char)lo)) {
                char hex[3] = {hi, lo, '\0'};
                output += (char)strtol(hex, nullptr, 16);
                i += 2;
                continue;
            }
        }
        output += c;
    }
    return output;
}

static String ethernetGetFormValue(const String& body, const char* key) {
    String prefix = String(key) + '=';
    int start = 0;
    while (start <= (int)body.length()) {
        int amp = body.indexOf('&', start);
        if (amp < 0) {
            amp = body.length();
        }
        String pair = body.substring(start, amp);
        if (pair.startsWith(prefix)) {
            return ethernetUrlDecode(pair.substring(prefix.length()));
        }
        start = amp + 1;
    }
    return String();
}

static bool ethernetFormHasTruthyValue(const String& body, const char* key) {
    String value = ethernetGetFormValue(body, key);
    value.toLowerCase();
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

static String ethernetGetRequestValue(const String& body, const char* key) {
    String trimmed = body;
    trimmed.trim();
    if (trimmed.startsWith(F("{"))) {
        SpiRamJsonDocument doc(512);
        if (deserializeJson(doc, trimmed) == DeserializationError::Ok) {
            return String(doc[key] | "");
        }
        return String();
    }
    return ethernetGetFormValue(body, key);
}

static String ethernetGetQueryParam(const String& query, const char* key) {
    String prefix = String(key) + '=';
    int start = 0;
    while (start <= (int)query.length()) {
        int amp = query.indexOf('&', start);
        if (amp < 0) {
            amp = query.length();
        }
        String pair = query.substring(start, amp);
        if (pair.startsWith(prefix)) {
            return ethernetUrlDecode(pair.substring(prefix.length()));
        }
        start = amp + 1;
    }
    return String();
}

static String ethernetUrlEncode(const String& value) {
    static const char hexDigits[] = "0123456789ABCDEF";
    String encoded;
    encoded.reserve(value.length() * 3);
    for (size_t i = 0; i < value.length(); ++i) {
        uint8_t c = (uint8_t)value[i];
        if ((c >= '0' && c <= '9')
            || (c >= 'A' && c <= 'Z')
            || (c >= 'a' && c <= 'z')
            || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            encoded += (char)c;
        } else if (c == ' ') {
            encoded += F("%20");
        } else {
            encoded += '%';
            encoded += hexDigits[(c >> 4) & 0x0F];
            encoded += hexDigits[c & 0x0F];
        }
    }
    return encoded;
}

static bool ethernetValidIPv4(const String& value) {
    int parts = 0;
    int start = 0;
    while (start < (int)value.length()) {
        int dot = value.indexOf('.', start);
        String segment = (dot == -1) ? value.substring(start) : value.substring(start, dot);
        segment.trim();
        if (segment.length() == 0) {
            return false;
        }
        for (size_t i = 0; i < segment.length(); ++i) {
            if (!isDigit((unsigned char)segment[i])) {
                return false;
            }
        }
        int part = segment.toInt();
        if (part < 0 || part > 255) {
            return false;
        }
        parts++;
        if (dot == -1) {
            break;
        }
        start = dot + 1;
    }
    return parts == 4;
}

static bool ethernetValidMac(String mac) {
    mac.trim();
    if (!mac.length()) {
        return true;
    }
    mac.toUpperCase();
    mac.replace('-', ':');
    int colons = 0;
    int hexCount = 0;
    for (size_t i = 0; i < mac.length(); ++i) {
        char c = mac[i];
        if (c == ':') {
            if (hexCount != 2) {
                return false;
            }
            hexCount = 0;
            colons++;
            continue;
        }
        if (!isxdigit((unsigned char)c)) {
            return false;
        }
        hexCount++;
        if (hexCount > 2) {
            return false;
        }
    }
    return (hexCount == 2) && (colons == 5);
}

static void ethernetSendUnauthorized(EthernetClient& client) {
    client.println(F("HTTP/1.1 401 Unauthorized"));
    client.println(F("WWW-Authenticate: Basic realm=\"ESP32 Web\""));
    client.println(F("Content-Type: text/plain; charset=utf-8"));
    client.println(F("Connection: close"));
    client.println(F("Content-Length: 13"));
    client.println();
    client.print(F("Unauthorized"));
}

static bool ethernetIsAuthorized(const String& authorizationHeader) {
#if ENABLE_HTTP_BASIC_AUTH==1
	// HTTP auth master switch (persisted, default OFF). When off, no login.
	if (!configManager.getWebAuthEnabled()) return true;
	String username = configManager.getWebUsername();
	String password = configManager.getWebPassword();
	if (username.length() == 0 && password.length() == 0) {
		return true;
	}
    String expected = String(F("Basic ")) + base64::encode(username + ':' + password);
    return authorizationHeader == expected;
#else
    (void)authorizationHeader;
    return true;
#endif
}

static String ethernetBootstrapGetUiUrl() {
#if ENABLE_WIFI==1
    if (WiFi.status() == WL_CONNECTED) {
        IPAddress wifiIP = WiFi.localIP();
        if (ethernetHasUsableIP(wifiIP)) {
            return String(F("http://")) + wifiIP.toString() + '/';
        }
    }

    IPAddress apIP = WiFi.softAPIP();
    if (ethernetHasUsableIP(apIP)) {
        return String(F("http://")) + apIP.toString() + '/';
    }
#endif

    return String();
}

#if 0
static String ethernetBuildHomePage() {
    String fullUiUrl = ethernetBootstrapGetUiUrl();
    String body;
    body.reserve(2400);
    body += F("<h1>🌐 Node32-HUB Ethernet</h1><p class='muted'>Interface web leve servida diretamente pelo W5500.</p>");
    body += F("<section><h2>") + L(activeLang->ethQuickStatus) + F("</h2><div class='grid'>");
    body += F("<div class='card'><strong>Ethernet IP</strong><div>");
    body += ethernetHtmlEscape(ethernetLocalIP().toString());
    body += F("</div></div>");
    body += F("<div class='card'><strong>WiFi IP</strong><div>");
    body += ethernetHtmlEscape(WiFi.localIP().toString());
    body += F("</div></div>");
    body += F("<div class='card'><strong>AP IP</strong><div>");
    body += ethernetHtmlEscape(WiFi.softAPIP().toString());
    body += F("</div></div></div>");
    body += F("<a class='btn' href='/status'>📊 Status</a><a class='btn' href='/ethernet'>⚙️ Ethernet</a><a class='btn' href='/sdcard'>💾 microSD</a><a class='btn' href='/monitor'>🖨️ Monitor</a><a class='btn' href='/api/status'>JSON status</a><a class='btn' href='/api/wifi-status'>JSON rede</a>");
    if (fullUiUrl.length() > 0) {
        body += F("<a class='btn' href='/ui'>") + L(activeLang->ethOpenFullUi) + F("</a><p class='muted'>") + L(activeLang->ethFullUiAvailable) + F("");
        body += ethernetHtmlEscape(fullUiUrl);
        body += F("</p>");
    } else {
        body += F("<p class='warn'>") + L(activeLang->ethFullUiUnavailable) + F("</p>");
    }
    body += F("</section><section><h2>") + L(activeLang->ethScopeTitle) + F("</h2><p>") + L(activeLang->ethScopeDesc) + F("</p></section>");
    return ethernetBuildPageShell(String(F("Node32-HUB Ethernet")), body);
}

static String ethernetBuildMonitorPage() {
    String body;
    body.reserve(4200);
    body += F("<h1>🖨️ Monitor</h1><p class='muted'>") + L(activeLang->ethMonitorDesc) + F("</p><section>");
    body += F("<a class='btn' href='/'>") + L(activeLang->ethNavHome) + F("</a><a class='btn' href='/status'>📊 Status</a><button class='btn' type='button' id='refreshBtn'>") + L(activeLang->ethBtnRefresh) + F("</button><button class='btn' type='button' id='clearBtn'>") + L(activeLang->ethBtnClearBuffer) + F("</button>");
    body += F("<div class='grid' style='margin-top:12px'><div class='card'><strong>") + L(activeLang->ethLabelState) + F("</strong><pre id='monitorStatus'>") + L(activeLang->ethLabelLoading) + F("</pre></div><div class='card'><strong>") + L(activeLang->ethViewTitle) + F("</strong><div class='muted'>") + L(activeLang->ethViewDesc) + F("</div><label for='limitLines'>Linhas</label><input id='limitLines' type='number' min='10' max='500' value='120'><label for='maxBytes'>Janela (bytes)</label><input id='maxBytes' type='number' min='256' max='16384' value='4096'></div></div><pre id='rxText'>A carregar...</pre><div id='monitorMsg'></div></section>");
    body += F("<script>const rxText=document.getElementById('rxText');const monitorStatus=document.getElementById('monitorStatus');const monitorMsg=document.getElementById('monitorMsg');let lastSeq=-1;let pollTimer=0;async function loadText(force){const limit=document.getElementById('limitLines').value||'120';const maxBytes=document.getElementById('maxBytes').value||'4096';const r=await fetch('/api/received-text?limit='+encodeURIComponent(limit)+'&max_bytes='+encodeURIComponent(maxBytes)+'&include_partial=1&t='+Date.now(),{cache:'no-store'});if(!r.ok)throw new Error('HTTP '+r.status);const text=await r.text();if(force||text!==rxText.textContent){rxText.textContent=text||'[sem dados]';rxText.scrollTop=rxText.scrollHeight;}}async function loadStatus(){const r=await fetch('/api/monitor-status',{cache:'no-store'});if(!r.ok)throw new Error('HTTP '+r.status);const d=await r.json();monitorStatus.textContent=JSON.stringify(d,null,2);const seq=Number(d.buffer_seq||0);if(seq!==lastSeq){lastSeq=seq;await loadText(true);}}async function clearBuffer(){monitorMsg.className='muted';monitorMsg.textContent='A limpar...';const r=await fetch('/api/clear-buffer',{method:'POST'});const d=await r.json();if(!r.ok||d.status!=='ok')throw new Error(d.status||'erro');monitorMsg.className='ok';monitorMsg.textContent='Buffer limpo.';lastSeq=-1;await loadStatus();}function startMonitor(){fetch('/api/monitor-open',{method:'POST'}).catch(()=>{});window.addEventListener('beforeunload',()=>{try{if(navigator.sendBeacon){navigator.sendBeacon('/api/monitor-close');}else{fetch('/api/monitor-close',{method:'POST',keepalive:true});}}catch(_){}});document.getElementById('refreshBtn').addEventListener('click',()=>loadStatus().catch(e=>{monitorMsg.className='err';monitorMsg.textContent='Erro: '+e.message;}));document.getElementById('clearBtn').addEventListener('click',()=>clearBuffer().catch(e=>{monitorMsg.className='err';monitorMsg.textContent='Erro: '+e.message;}));loadStatus().catch(e=>{monitorMsg.className='err';monitorMsg.textContent='Erro inicial: '+e.message;});pollTimer=setInterval(()=>loadStatus().catch(()=>{}),2000);}startMonitor();</script>");
    return ethernetBuildPageShell(String(F("Monitor Ethernet")), body);
}
#endif

static void ethernetHandleSdActionPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    HttpResponse hresp;
    handleSdActionPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetSendMonitorStatus(EthernetClient& client) {
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", sharedWebBuildMonitorStatusJson());
}

static void ethernetHandleReceivedTextGet(EthernetClient& client, const String& query) {
    int limit = ethernetGetFormValue(query, "limit").toInt();
    if (limit <= 0) {
        limit = 100;
    }
    int maxBytesWindow = ethernetGetFormValue(query, "max_bytes").toInt();
    if (maxBytesWindow <= 0) {
        maxBytesWindow = 4096;
    }
    bool includePartial = ethernetFormHasTruthyValue(query, "include_partial");

    String output;
    if (!sharedWebBuildReceivedText(limit, maxBytesWindow, includePartial, output)) {
        ethernetBootstrapSendResponse(client, "503 Service Unavailable", "text/plain; charset=utf-8", String(F("buffer busy")));
        return;
    }
    ethernetBootstrapSendResponse(client, "200 OK", "text/plain; charset=utf-8", output);
}

static void ethernetHandleClearBufferPost(EthernetClient& client) {
    String responseBody;
    if (!sharedWebClearReceivedBuffer(responseBody)) {
        ethernetBootstrapSendResponse(client, "503 Service Unavailable", "application/json", String(F("{\"status\":\"busy\"}")));
        return;
    }
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", responseBody);
}

static void ethernetHandleSystemCommandPost(EthernetClient& client, const String& body) {
    ethernetBootstrapBeginStreamResponse(client, "200 OK", "application/json");
    sharedWebWriteSystemCommandResponse(client, ethernetGetRequestValue(body, "command"));
}

static void ethernetHandleRs232CommandPost(EthernetClient& client, const String& body) {
    ethernetBootstrapBeginStreamResponse(client, "200 OK", "application/json");
    sharedWebWriteRs232CommandResponse(client, ethernetGetRequestValue(body, "command"), ethernetGetRequestValue(body, "template"));
}

#if ENABLE_MODBUS==1
static void ethernetHandleModbusCommandPost(EthernetClient& client, const String& body) {
    ethernetBootstrapBeginStreamResponse(client, "200 OK", "application/json");
    sharedWebWriteModbusCommandResponse(client, ethernetGetRequestValue(body, "command"));
}
#endif

static void ethernetHandleSerialMonitorGet(EthernetClient& client, const String& query) {
    String limitMsgsValue = ethernetGetFormValue(query, "limit_msgs");
    String sinceValue = ethernetGetFormValue(query, "since");
    String lastSeqValue = ethernetGetFormValue(query, "last_seq");
    String maxBytesValue = ethernetGetFormValue(query, "max_bytes");
    String limitLinesValue = ethernetGetFormValue(query, "limit_lines");
    String debugValue = ethernetGetFormValue(query, "debug");

    ethernetBootstrapBeginStreamResponse(client, "200 OK", "application/json");
    sharedWebStreamSerialMonitorJson(
        client,
        limitMsgsValue.toInt(),
        sinceValue.length() > 0,
        (size_t)sinceValue.toInt(),
        ethernetFormHasTruthyValue(query, "no_cmds"),
        lastSeqValue.length() > 0,
        (unsigned long)strtoul(lastSeqValue.c_str(), nullptr, 10),
        maxBytesValue.toInt(),
        limitLinesValue.toInt(),
        debugValue == "1" || debugValue.equalsIgnoreCase("true")
    );
}

// Write adapter for EthernetClient that prevents W5500 socket TX buffer truncation.
// The W5500 SSIZE cap (typically 2KB/socket) silently drops data in large writes.
// This adapter buffers byte-level writes (from FPSTR) and sends bulk writes in
// 512-byte chunks so each call stays within the SSIZE limit.
struct EthernetChunkedPrint : public Print {
    EthernetClient& _c;
    // 512 bytes: halves the number of W5500 SPI writes versus 256-byte chunks while
    // still staying comfortably below the 2 KB TX buffer per socket.
    // The W5500 library's socketSend() accepts any size <= TX buffer; chunking here only
    // serves the byte-at-a-time write(uint8_t) path used by FPSTR printing.
    static const size_t CHK = 512;
    uint8_t _buf[CHK];
    size_t _pos;
    explicit EthernetChunkedPrint(EthernetClient& c) : _c(c), _pos(0) {}
    // Buffered path — called by Print::print(const __FlashStringHelper*) char by char.
    size_t write(uint8_t b) override {
        _buf[_pos++] = b;
        if (_pos >= CHK) flushBuf();
        return 1;
    }
    // Bulk path — called for const char* and String prints.
    // Breaks into CHK-byte chunks so each socketSend stays below SSIZE.
    size_t write(const uint8_t* buf, size_t size) override {
        flushBuf();
        size_t off = 0;
        while (off < size) {
            size_t n = (size - off > CHK) ? CHK : (size - off);
            _c.write(buf + off, n);
            off += n;
            esp_task_wdt_reset();
        }
        return size;
    }
    // Must be called after streaming to flush any remaining buffered bytes.
    void flushBuf() {
        if (_pos > 0) {
            _c.write(_buf, _pos);
            _pos = 0;
            // Each flush is a synchronous SPI transfer + possible wait for W5500 TX buffer space.
            // Reset the task WDT so that streaming a large page does not trigger a 60 s abort.
            esp_task_wdt_reset();
        }
    }
};

static void ethernetBootstrapSendResponse(EthernetClient& client, const char* status, const char* contentType, const String& body) {
    client.print(F("HTTP/1.1 "));
    client.println(status);
    client.print(F("Content-Type: "));
    client.println(contentType);
    client.println(F("Connection: close"));
    client.print(F("Content-Length: "));
    client.println((unsigned int)body.length());
    client.println();
    // Write body in 512-byte chunks (well under the 2 KB W5500 TX buffer per socket with
    // MAX_SOCK_NUM=8). 1436 bytes risked blocking if >612 bytes were already in the TX
    // buffer, causing the W5500 to stall socketSend() until a TCP ACK arrived.
    const char* data = body.c_str();
    size_t len = body.length();
    const size_t CHUNK = 512;
    size_t off = 0;
    while (off < len) {
        size_t n = (len - off > CHUNK) ? CHUNK : (len - off);
        client.write((const uint8_t*)(data + off), n);
        off += n;
        esp_task_wdt_reset(); // Prevent WDT abort while waiting for W5500 TX buffer space
    }
}

// Sends a body held in a PSRAM buffer (HttpResponse binaryData). Keeps large
// JSON payloads out of the internal heap and frees the buffer after sending.
static void ethernetBootstrapSendBinaryResponse(EthernetClient& client, const char* status, const char* contentType,
                                                const uint8_t* data, size_t len, void (*freeFn)(const uint8_t*)) {
    client.print(F("HTTP/1.1 "));
    client.println(status);
    client.print(F("Content-Type: "));
    client.println(contentType);
    client.println(F("Connection: close"));
    client.print(F("Content-Length: "));
    client.println((unsigned int)len);
    client.println();
    const size_t CHUNK = 512;
    size_t off = 0;
    while (off < len) {
        size_t n = (len - off > CHUNK) ? CHUNK : (len - off);
        client.write(data + off, n);
        off += n;
        esp_task_wdt_reset();
    }
    if (freeFn && data) freeFn(data);
}

static void ethernetBootstrapBeginStreamResponse(EthernetClient& client, const char* status, const char* contentType, const char* cacheControl, const char* etag, const char* contentLanguage) {
    bool isHtml = (contentType && strncmp(contentType, "text/html", 9) == 0);
    const char* effectiveCache = cacheControl;
    const char* effectiveEtag = etag;
    if (!effectiveCache && isHtml) effectiveCache = "private, max-age=15, must-revalidate";
    if (!effectiveEtag && isHtml) effectiveEtag = FIRMWARE_BUILD;

    client.print(F("HTTP/1.1 "));
    client.println(status);
    client.print(F("Content-Type: "));
    client.println(contentType);
    if (contentLanguage && contentLanguage[0] != '\0') {
        client.print(F("Content-Language: "));
        client.println(contentLanguage);
    }
    if (effectiveCache && effectiveCache[0] != '\0') {
        client.print(F("Cache-Control: "));
        client.println(effectiveCache);
    }
    if (effectiveEtag && effectiveEtag[0] != '\0') {
        client.print(F("ETag: "));
        client.println(effectiveEtag);
    }
    client.println(F("Connection: close"));
    client.println();
}

static void ethernetBootstrapSendNotModified(EthernetClient& client, const char* etag, const char* cacheControl = "public, max-age=3600") {
    client.println(F("HTTP/1.1 304 Not Modified"));
    client.print(F("ETag: "));
    client.println(etag);
    client.print(F("Cache-Control: "));
    client.println(cacheControl);
    client.println(F("Connection: close"));
    client.println(F("Content-Length: 0"));
    client.println();
}

static void ethernetBootstrapSendProgmemResponse(EthernetClient& client, const char* status, const char* contentType, const char* body, size_t bodyLen, const char* etag = nullptr) {
    client.print(F("HTTP/1.1 "));
    client.println(status);
    client.print(F("Content-Type: "));
    client.println(contentType);
    // PROGMEM assets (CSS, JS) are compiled into firmware; version-busted URLs (?v=BUILD)
    // already invalidate this cache after OTA. 1-hour cache eliminates 3-4 fetches/page.
    client.println(F("Cache-Control: public, max-age=3600"));
    if (etag && etag[0] != '\0') {
        client.print(F("ETag: "));
        client.println(etag);
    }
    client.println(F("Connection: close"));
    client.print(F("Content-Length: "));
    client.println((unsigned long)bodyLen);
    client.println();

    // static: PROGMEM copy buffer lives in BSS (not on the loopTask stack).
    // This function is only ever called from the main loop (single-threaded).
    static const size_t chunkSize = 512;
    static char buffer[chunkSize];
    size_t offset = 0;
    while (offset < bodyLen) {
        size_t current = min(chunkSize, bodyLen - offset);
        memcpy_P(buffer, body + offset, current);
        client.write((const uint8_t*)buffer, current);
        offset += current;
        esp_task_wdt_reset(); // Prevent WDT abort while sending large PROGMEM assets
    }
}

static void ethernetBootstrapSendProgmemAttachmentResponse(EthernetClient& client, const char* status, const char* contentType, const char* filename, const char* body, size_t bodyLen) {
    client.print(F("HTTP/1.1 "));
    client.println(status);
    client.print(F("Content-Type: "));
    client.println(contentType);
    client.print(F("Content-Disposition: attachment; filename=\""));
    client.print(filename);
    client.println(F("\""));
    client.println(F("Cache-Control: no-cache"));
    client.println(F("Connection: close"));
    client.print(F("Content-Length: "));
    client.println((unsigned long)bodyLen);
    client.println();

    const size_t chunkSize = 512;
    char buffer[chunkSize];
    size_t offset = 0;
    while (offset < bodyLen) {
        size_t current = min(chunkSize, bodyLen - offset);
        memcpy_P(buffer, body + offset, current);
        client.write((const uint8_t*)buffer, current);
        offset += current;
    }
}

static void ethernetBootstrapSendRedirect(EthernetClient& client, const String& location) {
    client.println(F("HTTP/1.1 302 Found"));
    client.print(F("Location: "));
    client.println(location);
    client.println(F("Connection: close"));
    client.println(F("Content-Length: 0"));
    client.println();
}

#if 0
static void ethernetBootstrapSendRoot(EthernetClient& client) {
    String fullUiUrl = ethernetBootstrapGetUiUrl();
    String html;
    html.reserve(1100);
    html += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>Ethernet Bootstrap</title><style>body{font-family:Arial,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:24px}main{max-width:760px;margin:0 auto}section{border:1px solid #334155;border-radius:12px;padding:20px;background:#111827}a{color:#38bdf8}code{color:#f8fafc}pre{white-space:pre-wrap;background:#020617;padding:12px;border-radius:8px;overflow:auto}</style></head><body><main>");
    html += F("<h1>Ethernet bootstrap</h1><section><p>This interface confirms that W5500 HTTP is reachable.</p><pre>");
    html += F("Ethernet IP: ");
    html += ethernetLocalIP().toString();
    html += F("\nWiFi IP: ");
    html += WiFi.localIP().toString();
    html += F("\nAP IP: ");
    html += WiFi.softAPIP().toString();
    html += F("</pre>");

    if (fullUiUrl.length() > 0) {
        html += F("<p>Full Web UI is available on: <a href='");
        html += fullUiUrl;
        html += F("'>");
        html += fullUiUrl;
        html += F("</a></p><p><a href='/ui'>Open full UI</a></p>");
    } else {
        html += F("<p>Full Web UI is not available on WiFi/AP right now.</p>");
    }

    html += F("<p>Bootstrap JSON: <a href='/api/bootstrap-status'>/api/bootstrap-status</a></p>");
    html += F("</section></main></body></html>");
    ethernetBootstrapSendResponse(client, "200 OK", "text/html; charset=utf-8", html);
}
#endif

static void ethernetBootstrapSendStatus(EthernetClient& client) {
    String fullUiUrl = ethernetBootstrapGetUiUrl();
    SpiRamJsonDocument doc(384);
    doc["status"] = "ok";
    doc["ethernet_ip"] = ethernetLocalIP().toString();
#if ENABLE_WIFI==1
    doc["wifi_ip"] = WiFi.localIP().toString();
    doc["ap_ip"] = WiFi.softAPIP().toString();
#else
    doc["wifi_ip"] = "";
    doc["ap_ip"] = "";
#endif
    doc["full_ui_available"] = (fullUiUrl.length() > 0);
    if (fullUiUrl.length() > 0) {
        doc["full_ui_url"] = fullUiUrl;
    } else {
        doc["full_ui_url"] = nullptr;
    }

    String json;
    serializeJsonPretty(doc, json);
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}

static void ethernetBootstrapSendWifiStatus(EthernetClient& client) {
    SpiRamJsonDocument doc(1024);
    fillNetworkWifiStatusJson(doc);

    String json;
    serializeJsonPretty(doc, json);
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}

static void ethernetBootstrapSendApiStatus(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleStatusGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

