// ua_classify.h — which page a browser gets on GET /grbl-controller.
//
// UID 288 (Nuno, 2026-11-12): on a phone the mobile wizard (/m) is the default
// entry; desktop keeps the full controller page untouched. The decision is made
// on the User-Agent only, so it works for any client and costs nothing.
//
// A tablet is deliberately NOT treated as a phone: it has the width for the
// full controller (and the wizard's first step sends a job, which a workshop
// tablet may not need). Missing/unknown UA (curl, scripts, bots) is a desktop.
#pragma once

#include <stddef.h>
#include <string.h>

enum UaClientClass : int {
    UA_DESKTOP = 0,
    UA_PHONE = 1,
    UA_TABLET = 2
};

// Case-insensitive substring search (no strcasestr on all targets).
inline const char* uaFindNoCase(const char* hay, const char* needle) {
    if (!hay || !needle || !*needle) return nullptr;
    for (const char* h = hay; *h; ++h) {
        const char* a = h;
        const char* b = needle;
        while (*a && *b) {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
            if (ca != cb) break;
            ++a;
            ++b;
        }
        if (!*b) return h;
    }
    return nullptr;
}

inline bool uaHas(const char* ua, const char* token) {
    return uaFindNoCase(ua, token) != nullptr;
}

inline UaClientClass uaClassify(const char* ua) {
    if (!ua || !*ua) return UA_DESKTOP;                 // curl/scripts/bots: full page

    // Tablets first: an iPad UA also carries "Mobile", and an Android tablet is
    // "Android" without "Mobile".
    if (uaHas(ua, "iPad") || uaHas(ua, "Tablet") || uaHas(ua, "Kindle") ||
        uaHas(ua, "Silk/") || uaHas(ua, "PlayBook")) {
        return UA_TABLET;
    }
    if (uaHas(ua, "Android") && !uaHas(ua, "Mobile")) return UA_TABLET;

    if (uaHas(ua, "iPhone") || uaHas(ua, "iPod") || uaHas(ua, "Windows Phone") ||
        uaHas(ua, "IEMobile") || uaHas(ua, "BlackBerry") || uaHas(ua, "Opera Mini") ||
        uaHas(ua, "Mobi")) {
        return UA_PHONE;
    }
    return UA_DESKTOP;
}

// The route decision: only phones are sent to the wizard.
inline bool uaWantsMobileWizard(const char* ua) {
    return uaClassify(ua) == UA_PHONE;
}
