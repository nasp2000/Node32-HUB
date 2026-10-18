#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// ============================================================================
// radio_favicon.h — first-party station-favicon derivation for streams.
//
// When a stream is started without an explicit station favicon (queue entry,
// manual URL, old favorite, resume, UPnP item without albumArtURI), the UI
// still needs fanart for the now-playing card. Radio servers conventionally
// serve a favicon at https://<host>/favicon.ico, so we derive that URL from
// the stream URL. Local SD files never get one (caller guards on isStream).
// ============================================================================

namespace radio_favicon {

// Build the best-effort favicon URL for an http(s) stream URL.
// Returns the derived URL (heap/static buffer written by the caller via the
// out+cap arguments) and its length; 0 when the URL is not an http(s) stream.
inline uint32_t deriveForStreamUrl(const char* url, char* out, size_t cap) {
    if (!url || !out || cap == 0) return 0;

    const char* p = url;
    if (strncmp(p, "https://", 8) == 0) p += 8;
    else if (strncmp(p, "http://", 7) == 0) p += 7;
    else return 0;

    // Host = up to the first '/' or ':' (port) — no path, no query.
    size_t hostLen = 0;
    while (p[hostLen] && p[hostLen] != '/' && p[hostLen] != ':') hostLen++;
    if (hostLen == 0) return 0;

    // "https://" + host + "/favicon.ico"
    static const char kPre[] = "https://";
    static const char kSuf[] = "/favicon.ico";
    const size_t preLen = sizeof(kPre) - 1;
    const size_t sufLen = sizeof(kSuf) - 1;
    if (preLen + hostLen + sufLen >= cap) return 0;

    size_t o = 0;
    for (size_t i = 0; i < preLen; i++) out[o++] = kPre[i];
    for (size_t i = 0; i < hostLen; i++) out[o++] = p[i];
    for (size_t i = 0; i < sufLen; i++) out[o++] = kSuf[i];
    out[o] = '\0';
    return (uint32_t)o;
}

} // namespace radio_favicon
