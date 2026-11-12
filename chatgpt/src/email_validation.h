// email_validation.h — minimal-but-sane e-mail checks for the alert slots.
//
// The alert configuration only verified that the field contained an '@'
// anywhere ("a@" passed, "@b.com" passed, "a@b" passed). These helpers check
// one address and a comma/semicolon separated list, and are shared by the
// server handlers (inactivity + OEE alert config) and mirrored in the alert
// page's inline JS so both sides reject the same values.
//
// Deliberately not RFC 5322: no quoted local parts, no domain literals, no
// comments. The rule is what the field actually needs:
//   local  : 1..64 chars, no spaces, at least one char, not starting/ending '.'
//   domain : at least two labels separated by '.', every label 1..63 chars,
//            letters/digits/hyphen, no leading/trailing hyphen, TLD >= 2 letters
#pragma once

#include <stddef.h>
#include <stdint.h>

static const size_t EMAIL_LOCAL_MAX = 64;
static const size_t EMAIL_ADDRESS_MAX = 254;

inline bool emailIsLocalChar(char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
    switch (c) {
        case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
        case '+': case '-': case '/': case '=': case '?': case '^': case '_':
        case '`': case '{': case '|': case '}': case '~': case '.':
            return true;
        default:
            return false;
    }
}

inline bool emailIsLabelChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-';
}

inline bool emailIsLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// One address, exactly len bytes (no NUL needed).
inline bool emailAddressLooksValidN(const char* s, size_t len) {
    if (!s || len == 0 || len > EMAIL_ADDRESS_MAX) return false;

    size_t at = (size_t)-1;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '@') {
            if (at != (size_t)-1) return false;   // exactly one '@'
            at = i;
        } else if (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') {
            return false;                          // no whitespace inside an address
        }
    }
    if (at == (size_t)-1 || at == 0 || at + 1 >= len) return false;

    const size_t localLen = at;
    if (localLen > EMAIL_LOCAL_MAX) return false;
    if (s[0] == '.' || s[at - 1] == '.') return false;
    for (size_t i = 0; i < localLen; i++) {
        if (!emailIsLocalChar(s[i])) return false;
        if (s[i] == '.' && i > 0 && s[i - 1] == '.') return false;   // no ".."
    }

    const size_t domStart = at + 1;
    const size_t domLen = len - domStart;
    if (domLen < 4) return false;                  // "b.co" is the shortest sane one
    if (s[domStart] == '.' || s[len - 1] == '.') return false;

    size_t labelLen = 0;
    size_t labels = 0;
    size_t tldStart = 0;
    for (size_t i = domStart; i < len; i++) {
        const char c = s[i];
        if (c == '.') {
            if (labelLen == 0 || labelLen > 63) return false;
            if (s[i - 1] == '-') return false;     // no trailing hyphen
            labels++;
            labelLen = 0;
            tldStart = i + 1;
            if (tldStart < len && s[tldStart] == '.') return false;   // no ".."
            continue;
        }
        if (!emailIsLabelChar(c)) return false;
        if (labelLen == 0 && c == '-') return false;                  // no leading hyphen
        labelLen++;
    }
    if (labelLen == 0 || labelLen > 63) return false;
    if (s[len - 1] == '-') return false;
    labels++;                                      // last label
    if (labels < 2) return false;                  // domain needs a dot

    const size_t tldLen = len - tldStart;
    if (tldLen < 2) return false;                  // ".c" is not a domain
    for (size_t i = tldStart; i < len; i++) {
        if (!emailIsLetter(s[i])) return false;    // TLD is letters only
    }
    return true;
}

// Same rule for a NUL-terminated string (length computed here).
inline bool emailAddressLooksValid(const char* s) {
    if (!s) return false;
    size_t n = 0;
    while (s[n] != '\0') n++;
    return emailAddressLooksValidN(s, n);
}

// Comma/semicolon separated list as stored in the alert slots
// ("a@b.com, c@d.org"). Surrounding spaces/tabs are trimmed per item, empty
// items are ignored, every remaining item must be a valid address, and the
// list must contain at least one.
inline bool emailListLooksValidN(const char* s, size_t len) {
    if (!s) return false;
    size_t i = 0;
    size_t valid = 0;
    while (i < len) {
        while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == ',' || s[i] == ';')) i++;
        const size_t start = i;
        while (i < len && s[i] != ',' && s[i] != ';') i++;
        size_t end = i;
        while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) end--;
        if (end == start) continue;                // empty item
        if (!emailAddressLooksValidN(s + start, end - start)) return false;
        valid++;
    }
    return valid > 0;
}

inline bool emailListLooksValid(const char* s) {
    if (!s) return false;
    size_t n = 0;
    while (s[n] != '\0') n++;
    return emailListLooksValidN(s, n);
}
