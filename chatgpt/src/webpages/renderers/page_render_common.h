#pragma once

#include <Arduino.h>

extern const char* FIRMWARE_VERSION;

inline size_t streamPageShellStart(Print& out, const char* title) {
    size_t n = 0;
    const char* head1 = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>";
    const char* head2 = "</title><link rel='stylesheet' href='/core.css'></head><body><div id='loadingOverlay' style='display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(10,25,41,0.95);z-index:99999;align-items:center;justify-content:center'><div style='text-align:center'><div style='border:4px solid #1e40af;border-top:4px solid #3b82f6;border-radius:50%;width:50px;height:50px;animation:spin 1s linear infinite;margin:0 auto'></div><p style='color:#e6f1ff;margin-top:15px;font-size:14px' data-i18n='loading'>A processar...</p></div></div><style>@keyframes spin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}</style>";

    out.print(head1);
    n += strlen(head1);
    out.print(title);
    n += strlen(title);
    out.print(head2);
    n += strlen(head2);
    return n;
}

// Build prefix/suffix strings for chunked PROGMEM page streaming.
// Use with beginChunkedResponse to avoid the large heap buffer of AsyncResponseStream.
// Call once (lazily) and cache the result in static Strings.
//   prefix = DOCTYPE + title + CSS link + loading overlay
//   suffix = auto.js tag (optional) + extraFooterScript (optional) + </body></html>
// The strings match exactly what streamPageShellStart/streamAutoJsFooter/streamPageShellEnd produce.
inline void buildPageFrame(String& prefix, String& suffix,
                           const char* title,
                           bool includeAutoJs = true,
                           const char* extraFooterScript = nullptr) {
    prefix  = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>");
    prefix += title;
    prefix += F("</title><link rel='stylesheet' href='/core.css'></head><body>"
                "<div id='loadingOverlay' style='display:none;position:fixed;top:0;left:0;right:0;bottom:0;"
                "background:rgba(10,25,41,0.95);z-index:99999;align-items:center;justify-content:center'>"
                "<div style='text-align:center'><div style='border:4px solid #1e40af;border-top:4px solid #3b82f6;"
                "border-radius:50%;width:50px;height:50px;animation:spin 1s linear infinite;margin:0 auto'></div>"
                "<p style='color:#e6f1ff;margin-top:15px;font-size:14px' data-i18n='loading'>Processing...</p>"
                "</div></div><style>@keyframes spin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}</style>");
    suffix = "";
    if (includeAutoJs) {
        suffix += F("<script src='/auto.js?v=");
        suffix += FIRMWARE_VERSION;
        suffix += F("'></script>");
    }
    if (extraFooterScript) {
        suffix += extraFooterScript;
    }
    suffix += F("</body></html>");
}

// Local-processing pages never expire (client inactivity-watchdog disarm).
// Inject UNCONDITIONALLY on every local-processing page: local pages are
// ALWAYS exempt — the Access-page charts_exempt option only governs
// static/config pages. MUST print before any <script src='/auto.js'> tag
// (auto.js reads window._localPagesNeverExpire synchronously at load).
inline size_t streamNeverExpireFlag(Print& out) {
    static const char kNeverExpire[] PROGMEM =
        "<script>window._localPagesNeverExpire=true;</script>";
    out.print(FPSTR(kNeverExpire));
    return strlen_P(kNeverExpire);
}

inline size_t streamAutoJsTag(Print& out, bool defer = false) {
    const char* head = "<script src='/auto.js?v=";
    const char* middle = defer ? "' defer></script>" : "'></script>";
    out.print(head);
    out.print(FIRMWARE_VERSION);
    out.print(middle);
    return strlen(head) + strlen(FIRMWARE_VERSION) + strlen(middle);
}

inline size_t streamAutoJsFooter(Print& out) {
    return streamAutoJsTag(out, false);
}

inline size_t streamPageShellEnd(Print& out) {
    const char* tail = "</body></html>";
    out.print(tail);
    return strlen(tail);
}

// Serve a page as 3 sequential segments: RAM prefix + PROGMEM blob + RAM suffix.
// Use with beginChunkedResponse to avoid the large heap buffer of AsyncResponseStream.
// index is the global byte offset supplied by ESPAsyncWebServer on each callback.
inline size_t serveThreeSegments(uint8_t* buf, size_t maxLen, size_t index,
                                 const char* pPtr, size_t pLen,
                                 const char* aPtr, size_t aLen,
                                 const char* sPtr, size_t sLen) {
    const size_t total = pLen + aLen + sLen;
    if (index >= total) return 0;
    size_t written = 0;
    size_t remaining = maxLen;
    size_t pos = index;

    if (pos < pLen && remaining > 0) {
        size_t n = min(pLen - pos, remaining);
        memcpy(buf + written, pPtr + pos, n);
        written += n; remaining -= n; pos += n;
    }
    if (pos >= pLen && pos < pLen + aLen && remaining > 0) {
        size_t pgOff = pos - pLen;
        size_t n = min(aLen - pgOff, remaining);
        memcpy_P(buf + written, aPtr + pgOff, n);
        written += n; remaining -= n; pos += n;
    }
    if (pos >= pLen + aLen && remaining > 0) {
        size_t sfOff = pos - pLen - aLen;
        size_t n = min(sLen - sfOff, remaining);
        memcpy(buf + written, sPtr + sfOff, n);
        written += n;
    }
    return written;
}