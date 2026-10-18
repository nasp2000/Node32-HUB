// grbl_sender_probe.cpp
// Tool height map / probe grid executor.
// Runs as part of grblSenderUpdate() — non-blocking state machine.
#include "feature_flags.h"
#include "grbl_sender_internal.h"

#if ENABLE_GRBL == 1

#include <math.h>
#include <string.h>

// ── State definitions (declared extern in grbl_sender_internal.h) ────────
ProbeGridConfig  s_probeGridConfig;
ProbeGridPhase   s_probeGridPhase = ProbeGridPhase::IDLE;
ProbeGridPoint   s_probeGridPoints[PROBE_GRID_MAX_POINTS];
uint16_t         s_probeGridTotal = 0;
uint16_t         s_probeGridIndex = 0;
uint16_t         s_probeGridCols = 0;
uint8_t          s_probeGridSlot = 0;
bool             s_heightMapEnabled = false;
uint32_t         s_probeGridWaitStartMs = 0;

// Last commanded XY (work coordinates) seen while compensating streamed lines,
// so Z-only motion lines are corrected at the right position. Only touched
// under s_mutex (all callers hold it).
static bool    s_heightMapLastValid = false;
static float   s_heightMapLastX = 0.0f;
static float   s_heightMapLastY = 0.0f;

void grblHeightMapResetTracker() {
    s_heightMapLastValid = false;
    s_heightMapLastX = 0.0f;
    s_heightMapLastY = 0.0f;
}

// ── Helpers ──────────────────────────────────────────────────────────────

// Generate grid XY positions. Uses a raster scan pattern:
//   Row 0: left->right, Row 1: right->left (serpentine)
// Returns the total number of points generated.
static uint16_t buildGridPoints(const ProbeGridConfig& cfg, ProbeGridPoint* points) {
    if (cfg.spacing <= 0) return 0;
    float xSpan = cfg.endX - cfg.startX;
    float ySpan = cfg.endY - cfg.startY;
    if (xSpan <= 0 || ySpan <= 0) return 0;
    int cols = (int)(xSpan / cfg.spacing) + 1;
    int rows = (int)(ySpan / cfg.spacing) + 1;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    uint16_t total = (uint16_t)(cols * rows);
    if (total > PROBE_GRID_MAX_POINTS) total = PROBE_GRID_MAX_POINTS;

    uint16_t idx = 0;
    for (int row = 0; row < rows && idx < total; ++row) {
        float y = cfg.startY + row * cfg.spacing;
        if (row % 2 == 0) {
            // Left → right
            for (int col = 0; col < cols && idx < total; ++col) {
                float x = cfg.startX + col * cfg.spacing;
                points[idx].x = x;
                points[idx].y = y;
                points[idx].z = 0;
                points[idx].valid = false;
                ++idx;
            }
        } else {
            // Right → left (serpentine)
            for (int col = cols - 1; col >= 0 && idx < total; --col) {
                float x = cfg.startX + col * cfg.spacing;
                points[idx].x = x;
                points[idx].y = y;
                points[idx].z = 0;
                points[idx].valid = false;
                ++idx;
            }
        }
    }
    return idx;
}

// Bilinear interpolation of Z at (x,y) from the 4 surrounding grid points.
// Returns 0 if no valid points exist in the grid.
static float interpolateZ(float x, float y, const ProbeGridPoint* points, uint16_t count,
                           const ProbeGridConfig& cfg, uint16_t cols) {
    if (count == 0 || cols == 0) return 0;
    uint16_t rows = count / cols;
    if (rows == 0) return 0;

    // Clamp to grid bounds
    if (x <= cfg.startX && y <= cfg.startY) {
        for (uint16_t i = 0; i < count; ++i) {
            if (points[i].valid && points[i].x == cfg.startX && points[i].y == cfg.startY)
                return points[i].z;
        }
        return 0;
    }
    if (x >= cfg.endX && y >= cfg.endY) {
        for (uint16_t i = 0; i < count; ++i) {
            if (points[i].valid && points[i].x == cfg.endX && points[i].y == cfg.endY)
                return points[i].z;
        }
        return 0;
    }

    // Find the grid cell containing (x, y)
    float colF = (x - cfg.startX) / cfg.spacing;
    float rowF = (y - cfg.startY) / cfg.spacing;
    int col = (int)colF;
    int row = (int)rowF;
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    if (col >= (int)cols - 1) col = (int)cols - 2;
    if (row >= (int)rows - 1) row = (int)rows - 2;

    float fracX = colF - col;
    float fracY = rowF - row;
    if (fracX < 0) fracX = 0;
    if (fracX > 1) fracX = 1;
    if (fracY < 0) fracY = 0;
    if (fracY > 1) fracY = 1;

    // buildGridPoints() fills rows serpentine: even rows left→right, odd rows
    // right→left. Map the physical column to the stored column for each row.
    int storedColA = (row % 2 == 0) ? col : (cols - 1 - col);          // row (even/odd)
    int storedColB = ((row + 1) % 2 == 0) ? col : (cols - 1 - col);    // row+1
    uint16_t i00 = (uint16_t)(row * cols + storedColA);
    uint16_t i10 = (uint16_t)(row * cols + ((row % 2 == 0) ? (col + 1) : (cols - 2 - col)));
    uint16_t i01 = (uint16_t)((row + 1) * cols + storedColB);
    uint16_t i11 = (uint16_t)((row + 1) * cols + (((row + 1) % 2 == 0) ? (col + 1) : (cols - 2 - col)));

    float z00 = (i00 < count && points[i00].valid) ? points[i00].z : 0;
    float z10 = (i10 < count && points[i10].valid) ? points[i10].z : 0;
    float z01 = (i01 < count && points[i01].valid) ? points[i01].z : 0;
    float z11 = (i11 < count && points[i11].valid) ? points[i11].z : 0;

    // Bilinear interpolation (fracX is physical, independent of storage order)
    float zTop = z00 + (z10 - z00) * fracX;
    float zBot = z01 + (z11 - z01) * fracX;
    return zTop + (zBot - zTop) * fracY;
}

// ── Public API ───────────────────────────────────────────────────────────

bool grblProbeGridStart(uint8_t usbSlot, const ProbeGridConfig& config) {
    if (s_probeGridPhase != ProbeGridPhase::IDLE &&
        s_probeGridPhase != ProbeGridPhase::COMPLETE &&
        s_probeGridPhase != ProbeGridPhase::CANCELLED &&
        s_probeGridPhase != ProbeGridPhase::ERROR) {
        return false;
    }

    s_probeGridConfig = config;
    s_probeGridSlot = usbSlot;
    s_probeGridIndex = 0;
    s_probeGridWaitStartMs = 0;

    uint16_t count = buildGridPoints(config, s_probeGridPoints);
    if (count == 0) return false;
    s_probeGridTotal = count;

    // Calculate columns for serpentine pattern
    float xSpan = config.endX - config.startX;
    s_probeGridCols = (uint16_t)(xSpan / config.spacing) + 1;
    if (s_probeGridCols < 1) s_probeGridCols = 1;

    s_probeGridPhase = ProbeGridPhase::MOVING_XY;
    return true;
}

void grblProbeGridCancel() {
    if (s_probeGridPhase == ProbeGridPhase::IDLE ||
        s_probeGridPhase == ProbeGridPhase::COMPLETE ||
        s_probeGridPhase == ProbeGridPhase::CANCELLED) {
        return;
    }
    s_probeGridPhase = ProbeGridPhase::CANCELLED;
}

ProbeGridPhase grblGetProbeGridPhase() { return s_probeGridPhase; }
uint16_t grblGetProbeGridTotal() { return s_probeGridTotal; }
uint16_t grblGetProbeGridCompleted() { return s_probeGridIndex; }
const ProbeGridConfig* grblGetProbeGridConfig() { return &s_probeGridConfig; }

const ProbeGridPoint* grblGetProbeGridPoints(uint16_t* count) {
    if (count) *count = s_probeGridTotal;
    return s_probeGridPoints;
}

void grblSetHeightMapEnabled(bool enabled) { s_heightMapEnabled = enabled; }
bool grblGetHeightMapEnabled() { return s_heightMapEnabled; }

void grblClearHeightMap() {
    for (uint16_t i = 0; i < s_probeGridTotal; ++i) {
        s_probeGridPoints[i].valid = false;
        s_probeGridPoints[i].z = 0;
    }
    s_probeGridTotal = 0;
    s_probeGridIndex = 0;
    s_probeGridPhase = ProbeGridPhase::IDLE;
}

// ── Internal executor ───────────────────────────────────────────────────

void probeGridUpdate() {
    if (s_probeGridPhase == ProbeGridPhase::IDLE ||
        s_probeGridPhase == ProbeGridPhase::COMPLETE ||
        s_probeGridPhase == ProbeGridPhase::CANCELLED ||
        s_probeGridPhase == ProbeGridPhase::ERROR) {
        return;
    }

    uint32_t now = millis();

    switch (s_probeGridPhase) {

    case ProbeGridPhase::MOVING_XY: {
        if (s_probeGridIndex >= s_probeGridTotal) {
            s_probeGridPhase = ProbeGridPhase::COMPLETE;
            sendVirtualMessage(F("Probe grid complete"), 0, true, false);
            return;
        }
        // Move to next grid XY at safe Z
        const ProbeGridPoint& pt = s_probeGridPoints[s_probeGridIndex];
        const ProbeGridConfig& cfg = s_probeGridConfig;
        // Lift to the configured safe travel height first (only when > 0) so
        // traversal never drags across the material; GRBL rapids Z then XY.
        if (cfg.safeZ > 0) {
            char zcmd[32];
            snprintf(zcmd, sizeof(zcmd), "G0 Z%.3f", cfg.safeZ);
            if (!grblSend(s_probeGridSlot, zcmd)) return; // buffer full, retry next cycle
        }
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "G0 X%.3f Y%.3f", pt.x, pt.y);
        if (!grblSend(s_probeGridSlot, cmd)) return; // buffer full, retry next cycle

        s_probeGridPhase = ProbeGridPhase::PROBING;
        s_probeGridWaitStartMs = now;
        break;
    }

    case ProbeGridPhase::PROBING: {
        // Wait for XY move to complete (G0 is usually instant, but wait for 'ok')
        if (s_inflight > 0) {
            if (now - s_probeGridWaitStartMs > 30000) {
                s_probeGridPhase = ProbeGridPhase::ERROR;
                sendVirtualMessage(F("Probe grid: XY move timeout"), 1, true, false);
            }
            return; // still in-flight
        }

        // Send probe command
        const ProbeGridConfig& cfg = s_probeGridConfig;
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "G38.2 Z-%.3f F%.1f", cfg.probeMaxDepth, cfg.probeFeed);
        if (!grblSend(s_probeGridSlot, cmd)) return;

        s_probeGridWaitStartMs = now;
        // Phase stays PROBING — wait for trigger or 'ok'/'error'
        break;
    }

    default:
        break;
    }

    // Check for probe trigger OR completion/error response regardless of phase
    // A probe command completes either by trigger (Hold + Pn:P) or by reaching
    // the target depth without trigger (no Pn:P, just 'ok').

    // Handle timeout and probe detection for PROBING phase
    if (s_probeGridPhase == ProbeGridPhase::PROBING) {
        // The probe command has finished when in_flight drops to 0
        if (s_inflight == 0) {
            uint32_t elapsed = now - s_probeGridWaitStartMs;
            if (elapsed < 50) return; // debounce

            ProbeGridPoint& pt = s_probeGridPoints[s_probeGridIndex];

            if (s_probeTriggered) {
                // Probe triggered — capture position in WORK coordinates so the
                // stored surface height is in the same frame as the G-code Z
                // (the compensation adds this offset to the commanded Z).
                pt.valid = true;
                pt.z = s_hasWorkPosition ? s_workPos[2] : s_machinePos[2];
                s_probeTriggered = false;
                sendVirtualMessage("Probe: Z=" + String(pt.z, 3), 0, true, false);
            } else {
                // Probe reached max depth without trigger
                pt.valid = false;
                pt.z = -s_probeGridConfig.probeMaxDepth;
                sendVirtualMessage(F("Probe: no trigger (max depth)"), 0, true, false);
            }

            // Retract
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "G0 Z%.3f", s_probeGridConfig.clearHeight);
            grblSend(s_probeGridSlot, cmd);

            s_probeGridPhase = ProbeGridPhase::RETRACTING;
            s_probeGridWaitStartMs = now;
        } else {
            // Still waiting — check timeout
            if (now - s_probeGridWaitStartMs > 60000) {
                s_probeGridPhase = ProbeGridPhase::ERROR;
                sendVirtualMessage(F("Probe grid: probe timeout"), 1, true, false);
            }
        }
    }

    if (s_probeGridPhase == ProbeGridPhase::RETRACTING) {
        if (s_inflight == 0) {
            uint32_t elapsed = now - s_probeGridWaitStartMs;
            if (elapsed < 50) return;

            // Move to next point
            s_probeGridIndex++;
            s_probeGridPhase = ProbeGridPhase::MOVING_XY;
            s_probeGridWaitStartMs = now;
        } else {
            if (now - s_probeGridWaitStartMs > 30000) {
                s_probeGridPhase = ProbeGridPhase::ERROR;
                sendVirtualMessage(F("Probe grid: retract timeout"), 1, true, false);
            }
        }
    }
}

// Apply height map compensation to a single G-code line, in place.
// Only G0/G1/G2/G3 motion commands that carry a Z word are rewritten.
// Returns the new line length (without trailing '\n'); returns lineLen
// unchanged when the line was not modified.
//
// Reference / sign: the height map stores the material surface height in WORK
// coordinates (probed with the same Z0 as the job). The commanded Z is the
// depth below the nominal surface at work Z=0, so the compensated target is
//     newZ = zVal + surfaceWork(x,y)
// which keeps the tool following the local surface instead of the nominal
// plane. Must be called with s_mutex held.
size_t grblApplyHeightMapToLine(char* line, size_t lineLen) {
    if (!s_heightMapEnabled || s_probeGridTotal == 0) return lineLen;
    if (lineLen == 0 || lineLen >= GRBL_MAX_LINE_LEN) return lineLen;

    // Only motion commands (G0/G1/G2/G3) are compensated.
    bool hasMove = false;
    for (const char* p = line; p < line + lineLen; ++p) {
        if (*p == 'G') {
            int gNum = -1;
            if (sscanf(p, "G%d", &gNum) == 1) {
                if (gNum == 0 || gNum == 1 || gNum == 2 || gNum == 3) { hasMove = true; break; }
            }
        }
    }
    if (!hasMove) return lineLen;

    // Track the commanded XY (work coords) on every motion line, even those
    // without a Z word, so later Z-only lines use the correct position.
    float px = s_heightMapLastValid ? s_heightMapLastX : s_machinePos[0];
    float py = s_heightMapLastValid ? s_heightMapLastY : s_machinePos[1];
    char* xPos = strstr(line, "X");
    char* yPos = strstr(line, "Y");
    bool hasXY = false;
    if (xPos && sscanf(xPos, "X%f", &px) == 1) hasXY = true;
    if (yPos && sscanf(yPos, "Y%f", &py) == 1) hasXY = true;
    if (hasXY) {
        s_heightMapLastValid = true;
        s_heightMapLastX = px;
        s_heightMapLastY = py;
    }

    char* zPos = strstr(line, "Z");
    if (!zPos) return lineLen;
    float zVal;
    if (sscanf(zPos, "Z%f", &zVal) != 1) return lineLen;

    float offset = interpolateZ(px, py, s_probeGridPoints, s_probeGridTotal,
                                s_probeGridConfig, s_probeGridCols);
    if (offset == 0.0f) return lineLen; // flat/no data here — leave untouched

    float newZ = zVal + offset;

    // Rebuild the line: text before Z, new Z value, then the rest.
    const char* rest = zPos + strspn(zPos, "Z+-.0123456789eE");
    size_t restLen = (size_t)((line + lineLen) - rest);
    size_t beforeZ = (size_t)(zPos - line);
    char newLine[GRBL_MAX_LINE_LEN + 1];
    size_t n = 0;
    memcpy(newLine, line, beforeZ);
    n += beforeZ;
    int written = snprintf(newLine + n, sizeof(newLine) - n, "Z%.3f", newZ);
    if (written <= 0 || (size_t)written >= sizeof(newLine) - n) return lineLen; // truncated/skip
    n += (size_t)written;
    if (n + restLen > GRBL_MAX_LINE_LEN) return lineLen; // does not fit — skip
    memcpy(newLine + n, rest, restLen);
    n += restLen;
    memmove(line, newLine, n);
    line[n] = '\0';
    return n;
}

#endif // ENABLE_GRBL == 1
