#pragma once
// grbl_resume_scan.h — pure helpers for alarm resume ("restart from line N").
//
// After a GRBL ALARM the controller resets: position, feed, spindle, coolant
// and modal groups are lost. Resuming mid-file therefore needs:
//   1. skip counting  — how many source lines to skip (== lines acked), and
//   2. modal fold     — replay of modal state (units, distance mode, WCS,
//      plane, feed, spindle+state, coolant) scanned from file start up to
//      the resume line, emitted as a short preamble.
//
// Everything here is dependency-free (string.h/ctype only) so native tests
// compile it directly. No heap, no statics, fixed-size outputs.
//
// Line model: the sender counts every physical source line (including blanks
// and comments) in s_linesSent/s_linesAcked. resumeFromLine() counts the
// same way: skip the first N physical lines, resume sending at line N
// (0-based) == first line with index >= acked count.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

// Modal snapshot folded from scanned lines. has* flags distinguish "seen in
// file" from defaults so the preamble only replays what the job used.
struct GrblResumeModals {
    bool     hasUnits;      // G20/G21 seen
    bool     inches;        // true=G20, false=G21
    bool     hasDistance;   // G90/G91 seen
    bool     incremental;   // true=G91, false=G90
    bool     hasWcs;        // G54..G59 seen
    uint8_t  wcs;           // 0..5 for G54..G59
    bool     hasPlane;      // G17/G18/G19 seen
    uint8_t  plane;         // 17/18/19
    bool     hasFeed;       // F seen
    uint32_t feed;          // last F value (integer part)
    bool     hasSpindle;    // S seen
    uint32_t spindle;       // last S value (integer part)
    bool     hasSpindleOn;  // M3/M4/M5 seen
    uint8_t  spindleDir;    // 3=M3, 4=M4, 5=M5(off)
    bool     hasCoolant;    // M7/M8/M9 seen
    uint8_t  coolant;       // 7=M7, 8=M8, 9=M9(off)
};

// Zero a modal snapshot.
static inline void grblResumeModalsInit(GrblResumeModals* m) {
    if (m) memset(m, 0, sizeof(*m));
}

// Parse an unsigned integer starting at p (stops at first non-digit).
// Returns value, advances *end past the digits (or leaves it at p).
static inline uint32_t grblResumeParseUint(const char* p, const char** end) {
    uint32_t v = 0;
    while (p && *p >= '0' && *p <= '9') {
        v = v * 10u + (uint32_t)(*p - '0');
        p++;
    }
    if (end) *end = p;
    return v;
}

// Fold ONE stripped line (already uppercased, no ';' comments, no CR/LF)
// into the modal snapshot. Unknown words are ignored. Returns nothing.
static inline void grblResumeFoldLine(const char* line, GrblResumeModals* m) {
    if (!line || !m) return;
    const char* p = line;
    // Skip parenthesised comments "(...)".
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '(') {
            while (*p && *p != ')') p++;
            if (*p == ')') p++;
            continue;
        }
        char w = *p;
        if ((w == 'G' || w == 'M' || w == 'F' || w == 'S' || w == 'T') &&
            (p[1] >= '0' && p[1] <= '9')) {
            const char* e = nullptr;
            uint32_t v = grblResumeParseUint(p + 1, &e);
            if (w == 'G') {
                if (v == 20)      { m->hasUnits = true; m->inches = true; }
                else if (v == 21) { m->hasUnits = true; m->inches = false; }
                else if (v == 90) { m->hasDistance = true; m->incremental = false; }
                else if (v == 91) { m->hasDistance = true; m->incremental = true; }
                else if (v >= 54 && v <= 59) { m->hasWcs = true; m->wcs = (uint8_t)(v - 54); }
                else if (v >= 17 && v <= 19) { m->hasPlane = true; m->plane = (uint8_t)v; }
            } else if (w == 'M') {
                if (v == 3 || v == 4 || v == 5) { m->hasSpindleOn = true; m->spindleDir = (uint8_t)v; }
                else if (v == 7 || v == 8 || v == 9) { m->hasCoolant = true; m->coolant = (uint8_t)v; }
            } else if (w == 'F') {
                m->hasFeed = true; m->feed = v;
            } else if (w == 'S') {
                m->hasSpindle = true; m->spindle = v;
            }
            p = e ? e : p + 1;
            // Skip optional decimal fraction (F1500.0 / S12000.5).
            if (*p == '.') {
                p++;
                while (*p >= '0' && *p <= '9') p++;
            }
            continue;
        }
        // Skip any other word (X/Y/Z/I/J/K/R/P/Q/L/D/H/N/O...).
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            p++;
            if (*p == '+' || *p == '-') p++;
            while ((*p >= '0' && *p <= '9') || *p == '.') p++;
            continue;
        }
        if (*p) p++;
    }
}

// Uppercase + strip ';' comments + trim one raw line into dst (like the
// sender's stripLine, dependency-free copy for tests).
static inline size_t grblResumeStripLine(const char* src, char* dst, size_t dstSize) {
    size_t len = 0;
    if (!src || !dst || dstSize == 0) return 0;
    for (size_t i = 0; src[i] && len < dstSize - 1; i++) {
        if (src[i] == ';') break;
        if (src[i] == '\r' || src[i] == '\n') break;
        dst[len++] = (char)toupper((unsigned char)src[i]);
    }
    while (len > 0 && dst[len - 1] == ' ') len--;
    dst[len] = '\0';
    return len;
}

// Fold a raw (un-stripped) line: strip then fold. Helper for file loops.
static inline void grblResumeFoldRawLine(const char* raw, GrblResumeModals* m) {
    char tmp[128];
    if (grblResumeStripLine(raw, tmp, sizeof(tmp)) == 0) return;
    grblResumeFoldLine(tmp, m);
}

// Emit the resume preamble for snapshot m into out (lines separated by '\n',
// no trailing newline). Order is GRBL-safe: units, distance, WCS, plane,
// feed, spindle+state (+2 s dwell when the spindle restarts), coolant.
// Returns bytes written (excl. NUL). Emits nothing (returns 0) when the
// snapshot carries no modal info at all.
static inline size_t grblResumeEmitPreamble(const GrblResumeModals* m,
                                            char* out, size_t outSize) {
    size_t n = 0;
    if (!m || !out || outSize < 2) return 0;
    // Local appender; keeps total under outSize-1 (room for NUL).
    size_t cap = outSize - 1;
#define GRBL_RESUME_APPEND_STR(s) do { \
        const char* _s = (s); \
        while (*_s && n < cap) out[n++] = *_s++; \
        if (n < cap) out[n++] = '\n'; \
    } while (0)
    char num[24];
    if (m->hasUnits)      GRBL_RESUME_APPEND_STR(m->inches ? "G20" : "G21");
    if (m->hasDistance)   GRBL_RESUME_APPEND_STR(m->incremental ? "G91" : "G90");
    if (m->hasWcs) {
        num[0] = 'G'; num[1] = '5'; num[2] = (char)('4' + m->wcs); num[3] = '\0';
        GRBL_RESUME_APPEND_STR(num);
    }
    if (m->hasPlane) {
        num[0] = 'G'; num[1] = '1'; num[2] = (char)('0' + (m->plane - 10)); num[3] = '\0';
        GRBL_RESUME_APPEND_STR(num);
    }
    if (m->hasFeed) {
        size_t k = 0;
        num[k++] = 'F';
        uint32_t v = m->feed;
        char rev[12]; size_t rl = 0;
        do { rev[rl++] = (char)('0' + (v % 10)); v /= 10; } while (v && rl < sizeof(rev));
        while (rl && k < sizeof(num) - 1) num[k++] = rev[--rl];
        num[k] = '\0';
        GRBL_RESUME_APPEND_STR(num);
    }
    if (m->hasSpindle && m->hasSpindleOn && m->spindleDir != 5) {
        size_t k = 0;
        num[k++] = 'S';
        uint32_t v = m->spindle;
        char rev[12]; size_t rl = 0;
        do { rev[rl++] = (char)('0' + (v % 10)); v /= 10; } while (v && rl < sizeof(rev));
        while (rl && k < sizeof(num) - 3) num[k++] = rev[--rl];
        num[k++] = ' '; num[k++] = 'M'; num[k++] = (char)('0' + m->spindleDir); num[k] = '\0';
        GRBL_RESUME_APPEND_STR(num);
        GRBL_RESUME_APPEND_STR("G4 P2");
    } else if (m->hasSpindleOn && m->spindleDir == 5) {
        GRBL_RESUME_APPEND_STR("M5");
    }
    if (m->hasCoolant) {
        num[0] = 'M'; num[1] = (char)('0' + m->coolant); num[2] = '\0';
        GRBL_RESUME_APPEND_STR(num);
    }
#undef GRBL_RESUME_APPEND_STR
    if (n > 0 && out[n - 1] == '\n') n--;  // drop trailing newline
    out[n] = '\0';
    return n;
}

// Count physical lines in the first len bytes of buf (newline-terminated
// segments; a trailing unterminated tail counts as one line when non-empty).
// Used to verify skip arithmetic in tests.
static inline uint32_t grblResumeCountLines(const char* buf, size_t len) {
    uint32_t lines = 0;
    int haveTail = 0;
    if (!buf) return 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\n') { lines++; haveTail = 0; }
        else if (buf[i] != '\r') haveTail = 1;
    }
    return lines + (uint32_t)(haveTail ? 1 : 0);
}

// True when a stripped line is actually sent to GRBL (non-empty, not a
// ';' comment). Mirrors the feeders' send condition, so skip math matches
// executed lines exactly (blank/comment lines are never sent or acked).
static inline int grblResumeIsSendable(const char* stripped) {
    return (stripped && stripped[0] && stripped[0] != ';') ? 1 : 0;
}

// Byte offset just past the n-th sendable line in buf[0..len). Lines are
// split on '\n' (tolerates '\r'); each line is stripped with
// grblResumeStripLine before the sendable test, exactly like the feeders.
// Returns len when fewer than n sendable lines exist. n==0 returns 0.
static inline size_t grblResumeSkipOffsetSendable(const char* buf, size_t len,
                                                  uint32_t n) {
    size_t i = 0;
    uint32_t done = 0;
    char tmp[128];
    if (!buf || n == 0) return 0;
    while (i < len && done < n) {
        size_t start = i;
        while (i < len && buf[i] != '\n') i++;
        size_t segLen = (i > start) ? (i - start) : 0;
        // Copy segment into tmp (bounded), strip, test.
        size_t cp = (segLen < sizeof(tmp) - 1) ? segLen : (sizeof(tmp) - 1);
        for (size_t k = 0; k < cp; k++) tmp[k] = buf[start + k];
        tmp[cp] = '\0';
        char stripped[128];
        if (grblResumeStripLine(tmp, stripped, sizeof(stripped)) > 0 &&
            grblResumeIsSendable(stripped)) {
            done++;
        }
        if (i < len && buf[i] == '\n') i++;  // consume newline
        if (done >= n) return i;
    }
    return len;
}
