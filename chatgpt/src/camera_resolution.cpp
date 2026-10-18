// camera_resolution.cpp — pure-C "WxH" parsing (see camera_resolution.h).
// Intentionally guard-free: stateless parser, no HW deps, ~200 B flash, used
// by the camera handler on every board (S3 included) and by native tests.
#include "camera_resolution.h"

int cameraParseResolution(const char* s, uint16_t* w, uint16_t* h) {
    if (!s || !w || !h) return 0;
    // Strict shape: <1-4 digits>x<1-4 digits>, nothing else.
    unsigned long rw = 0, rh = 0;
    const char* p = s;
    int digits = 0;
    while (*p >= '0' && *p <= '9' && digits < 4) { rw = rw * 10 + (unsigned)(*p - '0'); p++; digits++; }
    if (digits == 0 || *p != 'x') return 0;
    p++;
    digits = 0;
    while (*p >= '0' && *p <= '9' && digits < 4) { rh = rh * 10 + (unsigned)(*p - '0'); p++; digits++; }
    if (digits == 0 || *p != '\0') return 0;
    if (rw == 0 || rh == 0 || rw > 4096 || rh > 4096) return 0;
    *w = (uint16_t)rw;
    *h = (uint16_t)rh;
    return 1;
}
