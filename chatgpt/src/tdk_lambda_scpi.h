#pragma once
// tdk_lambda_scpi.h — dependency-free SCPI reply parsers for the TDK-Lambda
// module (host-testable, no Arduino/feature_flags includes).
//
// Used by src/tdk_lambda.cpp and by tests/native/test_tdk_lambda_parser.cpp.
// Reply shapes come from the Genesys manual cap. 7 (docs/Fonte/COMANDOS.md §4).

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>

// Parse up to maxFloats comma/space separated numbers. Returns the count
// (0 when the line is not numeric, e.g. "OK"/"CV"/"C01").
inline uint8_t tdkParseFloats(const char* s, float* out, uint8_t maxFloats) {
    if (!s || !out || maxFloats == 0) return 0;
    uint8_t got = 0;
    const char* p = s;
    while (got < maxFloats) {
        while (*p == ' ' || *p == '\t' || *p == ',') ++p;
        if (!*p) break;
        char* end = nullptr;
        const float v = strtof(p, &end);
        if (end == p) break;
        out[got++] = v;
        p = end;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') break;
        if (*p != ',') break;
        ++p;
    }
    return got;
}

// Parse "599.99,600.00,000.50,001.30,660.0,0.000" (DVC?) into 6 floats.
// Tolerates spaces around fields; rejects anything that is not 6 numbers
// (e.g. "OK", "CV", "C01").
inline bool tdkParseSixFloats(const char* s, float out[6]) {
    if (!s || !out) return false;
    int got = 0;
    const char* p = s;
    while (got < 6) {
        while (*p == ' ' || *p == '\t' || *p == ',') ++p;
        if (!*p) break;
        char* end = nullptr;
        const float v = strtof(p, &end);
        if (end == p) return false;
        out[got++] = v;
        p = end;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') break;
        if (*p != ',') return false;
        ++p;
    }
    return got == 6;
}

// Single numeric reply (MV?, MC?, OVP?, UVL?). False when not a number.
inline bool tdkParseFloat(const char* s, float* out) {
    if (!s || !out) return false;
    char* end = nullptr;
    const float v = strtof(s, &end);
    if (end == s) return false;
    *out = v;
    return true;
}

// Copy an upper-case, trimmed command into out (" dvc? \r\n" -> "DVC?").
// Leading/trailing whitespace (and CR/LF) is dropped; inner spaces are kept
// because commands with a parameter need them ("PV 100").
inline void tdkUpperTrim(const char* in, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!in) return;
    while (*in == ' ' || *in == '\t' || *in == '\r' || *in == '\n') ++in;
    size_t o = 0;
    for (const char* p = in; *p && o + 1 < outSize; ++p) {
        if (*p == '\r' || *p == '\n') continue;
        out[o++] = (char)toupper((unsigned char)*p);
    }
    while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '\t')) --o;
    out[o] = '\0';
}


// ── Auto-profile from a real reply (host-tested) ───────────────────────────
// The Genesys manual documents DVC? as (Vmes, Vset, Imes, Iset, OVP, UVL), but
// firmware versions differ. This checks that structure against the unit's
// ratings and, when the reply is unambiguous, returns the field map + scales
// that fit. It only rearranges the documented pair structure (8 layouts); it
// never guesses between equally plausible options (returns 0).
//
// out: map[6] = indexes into f[], scaleV/scaleI multipliers.
// Returns 1 when confident, 0 when not (outputs untouched).
inline int tdkSuggestMap(const float* f, uint8_t n, float rateV, float rateI,
                         uint8_t outMap[6], float* outScaleV, float* outScaleI,
                         char* why, size_t whySize) {
    if (!f || n < 4 || !outMap || !outScaleV || !outScaleI) return 0;
    if (rateV <= 0) rateV = 600.0f;
    if (rateI <= 0) rateI = 1.3f;

    // The 8 layouts that keep the documented (V,V,I,I) pairing.
    static const uint8_t kLayouts[8][6] = {
        {0,1,2,3,4,5}, {1,0,2,3,4,5}, {0,1,3,2,4,5}, {1,0,3,2,4,5},
        {2,3,0,1,4,5}, {3,2,0,1,4,5}, {2,3,1,0,4,5}, {3,2,1,0,4,5},
    };
    static const float kScales[] = {1.0f, 0.001f, 0.01f, 0.1f};

    float bestErr = 1e9f;
    uint8_t bestLayout = 0xFF;
    float bestScale = 1.0f;

    for (uint8_t si = 0; si < 4; ++si) {
        const float s = kScales[si];
        for (uint8_t li = 0; li < 8; ++li) {
            const uint8_t* m = kLayouts[li];
            const float vM = f[m[0]] * s, vS = f[m[1]] * s;
            const float iM = f[m[2]] * s, iS = f[m[3]] * s;

            // Magnitudes must fit the unit ratings, and the voltage pair must be
            // the bigger one (the discriminator between V and I pairs).
            if (vS < 0 || vM < 0 || iS < 0 || iM < 0) continue;
            if (vS > rateV * 1.25f || vM > rateV * 1.25f) continue;
            if (iS > rateI * 1.25f || iM > rateI * 1.25f) continue;
            if (vS < rateV * 0.02f) continue;                 // nothing to detect at ~0 V
            if (iS > 0.0f && (vS / iS) < 5.0f) continue;      // V pair is much larger

            // Voltage setpoint vs measured: the only field pair that must agree
            // (current measured can legitimately be below the limit).
            float err = fabsf(vM - vS) / (vS > 1.0f ? vS : 1.0f);
            // Measured current cannot exceed its own limit (direction matters:
            // it tells Imeas from Iset when the values differ a lot).
            if (iM > iS + (iS * 0.05f) + 0.01f) err += 0.3f;
            err += (float)si * 1e-4f;   // tie-break: prefer the largest scale that fits
            if (n >= 5) {
                const float ovp = f[m[4]] * s;
                if (ovp < vS * 0.95f || ovp > rateV * 1.35f) err += 0.5f;
            }
            if (err < bestErr) { bestErr = err; bestLayout = li; bestScale = s; }
        }
    }

    if (bestLayout == 0xFF || bestErr > 0.35f) return 0;   // not unambiguous
    for (uint8_t i = 0; i < 6; ++i) outMap[i] = kLayouts[bestLayout][i];
    *outScaleV = bestScale;
    *outScaleI = bestScale;
    if (why && whySize) {
        snprintf(why, whySize, "map %u,%u,%u,%u,%u,%u scale %.3f",
                 outMap[0], outMap[1], outMap[2], outMap[3], outMap[4], outMap[5], bestScale);
    }
    return 1;
}

// Query classification (the reply parser needs to know what it asked for).
enum TdkPending : uint8_t {
    TDK_P_NONE = 0, TDK_P_DVC, TDK_P_MV, TDK_P_MC,
    TDK_P_MODE, TDK_P_OUT, TDK_P_STT, TDK_P_OTHER
};

inline TdkPending tdkClassify(const char* upperCommand) {
    if (!upperCommand) return TDK_P_OTHER;
    if (!strncmp(upperCommand, "DVC?", 4))  return TDK_P_DVC;
    if (!strncmp(upperCommand, "MV?", 3))   return TDK_P_MV;
    if (!strncmp(upperCommand, "MC?", 3))   return TDK_P_MC;
    if (!strncmp(upperCommand, "MODE?", 5)) return TDK_P_MODE;
    if (!strncmp(upperCommand, "OUT?", 4))  return TDK_P_OUT;
    if (!strncmp(upperCommand, "STT?", 4))  return TDK_P_STT;
    return TDK_P_OTHER;
}

// What kind of reply to expect for a command the operator typed (or the poll
// sends). A known query wins over the profile's single-value slot: in "dvc"
// mode s_cmdV is "DVC?" itself, and matching it first turned every bulk reply
// into a single float (vMeas only, auto-detect never ran). A custom profile
// command ("VOUT?" as cmd_v) still falls back to the slot it belongs to.
inline TdkPending tdkPendingForSend(const char* upperCommand,
                                    const char* cmdV, const char* cmdI,
                                    const char* cmdState) {
    if (!upperCommand) return TDK_P_OTHER;
    const TdkPending cls = tdkClassify(upperCommand);
    if (cls != TDK_P_OTHER) return cls;
    if (cmdV && !strcmp(upperCommand, cmdV))         return TDK_P_MV;
    if (cmdI && !strcmp(upperCommand, cmdI))         return TDK_P_MC;
    if (cmdState && !strcmp(upperCommand, cmdState)) return TDK_P_MODE;
    return TDK_P_OTHER;
}

// ── Field mapping + CSV row (pure, host-tested) ───────────────────────────
// Apply the profile map (indexes into the reply), then scale/offset. This is
// what turns a DVC? line into the six values the module reports.
inline void tdkApplyMap(const float* f, uint8_t n, const uint8_t map[6],
                        float scaleV, float scaleI, float offV, float offI,
                        float* vMeas, float* vSet, float* iMeas, float* iSet,
                        float* ovp, float* uvl) {
    if (!f || !map) return;
    auto pick = [&](uint8_t slot) -> float {
        const uint8_t idx = map[slot];
        return (idx < n) ? f[idx] : 0.0f;
    };
    if (vMeas) *vMeas = pick(0) * scaleV + offV;
    if (vSet)  *vSet  = pick(1) * scaleV + offV;
    if (iMeas) *iMeas = pick(2) * scaleI + offI;
    if (iSet)  *iSet  = pick(3) * scaleI + offI;
    if (ovp)   *ovp   = pick(4) * scaleV + offV;
    if (uvl)   *uvl   = pick(5) * scaleV + offV;
}

// True when `cmd` is a SCPI query (trailing '?', spaces ignored). The poll
// profile must only ever contain queries unless "allow write" is on: a setter
// there would change the unit every poll.
inline bool tdkIsQueryCmd(const char* cmd) {
    if (!cmd) return false;
    const char* p = cmd;
    while (*p == ' ' || *p == '\t') ++p;
    size_t n = strlen(p);
    while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t')) --n;
    return n > 0 && p[n - 1] == '?';
}

// Output state from the MODE? reply: "CV"/"CC" mean the output is on, "OFF"
// means it is disabled. Anything else (empty, "--", a STAT? reply) keeps the
// previous state. The poll cycle asks MODE? and not OUT?, so this is what makes
// the CSV `out` column and the session energy right.
inline bool tdkOutFromMode(const char* mode, bool previous) {
    if (!mode || !mode[0]) return previous;
    if (mode[0] == 'C' && (mode[1] == 'V' || mode[1] == 'C')) return true;
    if (mode[0] == 'O' && mode[1] == 'F' && mode[2] == 'F') return false;
    return previous;
}

// How long the poller waits for a reply before giving up. A dead or silent line
// must never wedge the cycle (measured: one TX per boot, then nothing). Clamped
// to 3..15 s: at 19200 a real reply arrives in milliseconds, and a 1 h poll
// should not block the console for an hour either.
inline uint32_t tdkReplyTimeoutMs(uint32_t pollMs) {
    uint32_t t = pollMs ? pollMs : 60000UL;
    if (t > 15000UL) t = 15000UL;
    if (t < 3000UL) t = 3000UL;
    return t;
}

// Window aggregator: one CSV row can cover several polls. A single 1-minute
// sample hides a spike; min/max inside the window shows it, and the energy is
// integrated with the rectangle rule between samples (Wh).
struct TdkAgg {
    uint16_t n;
    float    vMin, vMax, iMin, iMax;
    double   vSum, iSum;
    double   wh;        // energy of the window
    uint32_t lastMs;    // millis() of the previous add (for the integral)
    float    prevV, prevI;
    bool     started;

    TdkAgg() { clear(0); }

    void clear(uint32_t nowMs) {
        n = 0;
        vMin = vMax = iMin = iMax = 0;
        vSum = iSum = wh = 0;
        prevV = prevI = 0;
        lastMs = nowMs;
        started = false;
    }

    void add(float v, float i, uint32_t nowMs) {
        if (!started) {
            vMin = vMax = v;
            iMin = iMax = i;
            started = true;
            lastMs = nowMs;
            prevV = v;
            prevI = i;
            vSum += v;
            iSum += i;
            n = 1;
            return;
        }
        if (v < vMin) vMin = v;
        if (v > vMax) vMax = v;
        if (i < iMin) iMin = i;
        if (i > iMax) iMax = i;
        if (nowMs > lastMs) {
            const double dtH = (double)(nowMs - lastMs) / 3600000.0;
            wh += (double)prevV * (double)prevI * dtH;   // power held since the last sample
        }
        prevV = v;
        prevI = i;
        lastMs = nowMs;
        vSum += v;
        iSum += i;
        ++n;
    }

    // Integrate the tail up to `nowMs` (the last sample is held until the row is written).
    void close(uint32_t nowMs) {
        if (started && nowMs > lastMs) {
            const double dtH = (double)(nowMs - lastMs) / 3600000.0;
            wh += (double)prevV * (double)prevI * dtH;
            lastMs = nowMs;
        }
    }

    float vAvg() const { return n ? (float)(vSum / (double)n) : 0.0f; }
    float iAvg() const { return n ? (float)(iSum / (double)n) : 0.0f; }
};

// Session counters (since boot, RAM only): energy, time in CC, time at the
// current limit, time with the output off, CV<->CC transitions.
struct TdkSession {
    double   wh;
    uint32_t ccMs, limMs, offMs, transitions;
    char     lastMode[4];

    TdkSession() { clear(); }

    void clear() {
        wh = 0;
        ccMs = limMs = offMs = transitions = 0;
        lastMode[0] = '\0';
    }

    // `limit` = the current is at the setpoint (the supply is in current limit).
    void add(float v, float i, const char* mode, bool outOn, bool limit, uint32_t dtMs) {
        const double dtH = (double)dtMs / 3600000.0;
        if (outOn) wh += (double)v * (double)i * dtH;
        else       offMs += dtMs;
        const bool cv = (mode && mode[0] == 'C' && mode[1] == 'V');
        const bool cc = (mode && mode[0] == 'C' && mode[1] == 'C');
        if (cc) ccMs += dtMs;
        if (limit) limMs += dtMs;
        const bool wasCv = (lastMode[0] == 'C' && lastMode[1] == 'V');
        const bool wasCc = (lastMode[0] == 'C' && lastMode[1] == 'C');
        if ((cv && wasCc) || (cc && wasCv)) ++transitions;
        if (mode && mode[0]) snprintf(lastMode, sizeof(lastMode), "%s", mode);
    }
};

// One CSV row. sensorFmt = the temperature-sensor style (key, value pairs);
// otherwise a plain column line. Returns the written length (0 on bad args).
// The row carries the window aggregate: average V/I, power, min/max and Wh.
//   sensor: "2026-11-26 10:00:00, vout, 599.99, vmin, 599.10, ..., wh, 13.060, n, 6"
//   table : "2026-11-26 10:00:00,599.99,600.00,0.500,1.300,300.00,660.0,0.0,CV,1,6,599.10,600.40,0.480,0.520,13.060"
inline size_t tdkFormatCsvRow(char* out, size_t outSize, const char* ts, bool sensorFmt,
                              float vM, float vS, float iM, float iS,
                              float ovp, float uvl, const char* mode, bool outOn,
                              const TdkAgg* agg) {
    if (!out || outSize == 0) return 0;
    const char* md = (mode && mode[0]) ? mode : "--";
    const uint16_t n = (agg && agg->n) ? agg->n : 1;
    const float vA = (agg && agg->n) ? agg->vAvg() : vM;
    const float iA = (agg && agg->n) ? agg->iAvg() : iM;
    const float vMin = (agg && agg->n) ? agg->vMin : vM;
    const float vMax = (agg && agg->n) ? agg->vMax : vM;
    const float iMin = (agg && agg->n) ? agg->iMin : iM;
    const float iMax = (agg && agg->n) ? agg->iMax : iM;
    const double wh = (agg && agg->n) ? agg->wh : 0.0;
    const int k = sensorFmt
        ? snprintf(out, outSize,
                   "%s, vout, %.2f, vmin, %.2f, vmax, %.2f, iout, %.3f, imin, %.3f, imax, %.3f, "
                   "power, %.2f, wh, %.3f, n, %u, vset, %.2f, iset, %.3f, "
                   "ovp, %.1f, uvl, %.1f, mode, %s, out, %s",
                   ts ? ts : "", vA, vMin, vMax, iA, iMin, iMax,
                   (double)vA * iA, wh, (unsigned)n, vS, iS, ovp, uvl, md, outOn ? "ON" : "OFF")
        : snprintf(out, outSize,
                   "%s,%.2f,%.2f,%.3f,%.3f,%.2f,%.1f,%.1f,%s,%d,%u,%.2f,%.2f,%.3f,%.3f,%.3f",
                   ts ? ts : "", vA, vS, iA, iS, (double)vA * iA, ovp, uvl, md,
                   outOn ? 1 : 0, (unsigned)n, vMin, vMax, iMin, iMax, wh);
    if (k <= 0) return 0;
    return ((size_t)k < outSize) ? (size_t)k : (outSize - 1);
}
