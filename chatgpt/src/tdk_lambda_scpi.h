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
