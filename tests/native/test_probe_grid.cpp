#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

// In-memory reimplementation of probe grid logic for testing.
// Avoids dependency on Arduino.h, grbl_sender.h, etc.

#define PROBE_GRID_MAX_POINTS 256

struct ProbeGridPoint {
    float x, y, z;
    bool valid;
};

struct ProbeGridConfig {
    float startX = 0, startY = 0;
    float endX   = 100, endY   = 100;
    float spacing = 10;
    float probeFeed = 50, probeMaxDepth = 20;
    float safeZ = 5, clearHeight = 10;
};

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
            for (int col = 0; col < cols && idx < total; ++col) {
                float x = cfg.startX + col * cfg.spacing;
                points[idx].x = x; points[idx].y = y;
                points[idx].z = 0; points[idx].valid = false;
                ++idx;
            }
        } else {
            for (int col = cols - 1; col >= 0 && idx < total; --col) {
                float x = cfg.startX + col * cfg.spacing;
                points[idx].x = x; points[idx].y = y;
                points[idx].z = 0; points[idx].valid = false;
                ++idx;
            }
        }
    }
    return idx;
}

static float interpolateZ(float x, float y, const ProbeGridPoint* points,
                           uint16_t count, const ProbeGridConfig& cfg, uint16_t cols) {
    if (count == 0 || cols == 0) return 0;
    uint16_t rows = count / cols;
    if (rows == 0) return 0;

    if (x <= cfg.startX && y <= cfg.startY) {
        for (uint16_t i = 0; i < count; ++i)
            if (points[i].valid && points[i].x == cfg.startX && points[i].y == cfg.startY)
                return points[i].z;
        return 0;
    }
    if (x >= cfg.endX && y >= cfg.endY) {
        for (uint16_t i = 0; i < count; ++i)
            if (points[i].valid && points[i].x == cfg.endX && points[i].y == cfg.endY)
                return points[i].z;
        return 0;
    }

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
    int storedColA = (row % 2 == 0) ? col : (cols - 1 - col);          // row
    int storedColB = ((row + 1) % 2 == 0) ? col : (cols - 1 - col);    // row+1
    uint16_t i00 = (uint16_t)(row * cols + storedColA);
    uint16_t i10 = (uint16_t)(row * cols + ((row % 2 == 0) ? (col + 1) : (cols - 2 - col)));
    uint16_t i01 = (uint16_t)((row + 1) * cols + storedColB);
    uint16_t i11 = (uint16_t)((row + 1) * cols + (((row + 1) % 2 == 0) ? (col + 1) : (cols - 2 - col)));

    float z00 = (i00 < count && points[i00].valid) ? points[i00].z : 0;
    float z10 = (i10 < count && points[i10].valid) ? points[i10].z : 0;
    float z01 = (i01 < count && points[i01].valid) ? points[i01].z : 0;
    float z11 = (i11 < count && points[i11].valid) ? points[i11].z : 0;

    float zTop = z00 + (z10 - z00) * fracX;
    float zBot = z01 + (z11 - z01) * fracX;
    return zTop + (zBot - zTop) * fracY;
}

// ── Tests ─────────────────────────────────────────────────────────────────

TEST_CASE("buildGridPoints: 3x3 grid gives 9 points") {
    ProbeGridConfig cfg;
    cfg.startX = 0; cfg.startY = 0;
    cfg.endX = 20; cfg.endY = 20;
    cfg.spacing = 10;
    ProbeGridPoint pts[PROBE_GRID_MAX_POINTS];
    uint16_t n = buildGridPoints(cfg, pts);
    CHECK(n == 9);
    CHECK(pts[0].x == 0);   CHECK(pts[0].y == 0);
    CHECK(pts[4].x == 10);  CHECK(pts[4].y == 10);
    CHECK(pts[8].x == 20);  CHECK(pts[8].y == 20);
}

TEST_CASE("buildGridPoints: serpentine alternates X direction") {
    ProbeGridConfig cfg;
    cfg.startX = 0; cfg.startY = 0;
    cfg.endX = 20; cfg.endY = 20;
    cfg.spacing = 10;
    ProbeGridPoint pts[PROBE_GRID_MAX_POINTS];
    uint16_t n = buildGridPoints(cfg, pts);
    CHECK(n == 9);
    // Row 0 (y=0): left→right: x=0,10,20
    CHECK(pts[0].x == 0); CHECK(pts[1].x == 10); CHECK(pts[2].x == 20);
    // Row 1 (y=10): right→left: x=20,10,0
    CHECK(pts[3].x == 20); CHECK(pts[4].x == 10); CHECK(pts[5].x == 0);
    // Row 2 (y=20): left→right: x=0,10,20
    CHECK(pts[6].x == 0); CHECK(pts[7].x == 10); CHECK(pts[8].x == 20);
}

TEST_CASE("buildGridPoints: zero spacing returns 0") {
    ProbeGridConfig cfg;
    cfg.spacing = 0;
    ProbeGridPoint pts[PROBE_GRID_MAX_POINTS];
    CHECK(buildGridPoints(cfg, pts) == 0);
}

TEST_CASE("buildGridPoints: negative span returns 0") {
    ProbeGridConfig cfg;
    cfg.startX = 100; cfg.endX = 0;
    ProbeGridPoint pts[PROBE_GRID_MAX_POINTS];
    CHECK(buildGridPoints(cfg, pts) == 0);
}

TEST_CASE("buildGridPoints: single column (X span < spacing)") {
    ProbeGridConfig cfg;
    cfg.startX = 0; cfg.endX = 5;
    cfg.startY = 0; cfg.endY = 20;
    cfg.spacing = 10;
    ProbeGridPoint pts[PROBE_GRID_MAX_POINTS];
    uint16_t n = buildGridPoints(cfg, pts);
    CHECK(n == 3);
    CHECK(pts[0].x == 0); CHECK(pts[1].x == 0); CHECK(pts[2].x == 0);
    CHECK(pts[0].y == 0); CHECK(pts[1].y == 10); CHECK(pts[2].y == 20);
}

TEST_CASE("buildGridPoints: capped at PROBE_GRID_MAX_POINTS") {
    ProbeGridConfig cfg;
    cfg.startX = 0; cfg.startY = 0;
    cfg.endX = 500; cfg.endY = 500;
    cfg.spacing = 1;
    ProbeGridPoint pts[PROBE_GRID_MAX_POINTS];
    uint16_t n = buildGridPoints(cfg, pts);
    CHECK(n <= PROBE_GRID_MAX_POINTS);
    CHECK(n == PROBE_GRID_MAX_POINTS);
}

// ── Interpolation tests ───────────────────────────────────────────────────

TEST_CASE("interpolateZ: flat grid returns same Z everywhere") {
    ProbeGridConfig cfg;
    cfg.startX = 0; cfg.startY = 0;
    cfg.endX = 20; cfg.endY = 20;
    cfg.spacing = 10;
    ProbeGridPoint pts[9];
    uint16_t n = buildGridPoints(cfg, pts);
    CHECK(n == 9);
    for (uint16_t i = 0; i < n; ++i) { pts[i].z = 5.0f; pts[i].valid = true; }
    uint16_t cols = (int)((cfg.endX - cfg.startX) / cfg.spacing) + 1;
    float z = interpolateZ(5, 5, pts, n, cfg, cols);
    CHECK(z == 5.0f);
    z = interpolateZ(15, 15, pts, n, cfg, cols);
    CHECK(z == 5.0f);
}

TEST_CASE("interpolateZ: bilinear at center of 2x2 grid") {
    ProbeGridConfig cfg;
    cfg.startX = 0; cfg.startY = 0;
    cfg.endX = 10; cfg.endY = 10;
    cfg.spacing = 10;
    ProbeGridPoint pts[4];
    uint16_t n = buildGridPoints(cfg, pts);
    CHECK(n == 4);
    // Serpentine storage: row0 (y=0) is (0,0),(10,0); row1 (y=10) is
    // reversed → (10,10),(0,10). Corner plane z = 0.2x + 0.4y:
    pts[0].z = 0; pts[0].valid = true;  // (0,0)
    pts[1].z = 2; pts[1].valid = true;  // (10,0)
    pts[2].z = 6; pts[2].valid = true;  // (10,10)
    pts[3].z = 4; pts[3].valid = true;  // (0,10)
    uint16_t cols = 2;
    // Center (5,5) = average of all 4 corners = (0+2+4+6)/4 = 3
    float z = interpolateZ(5, 5, pts, n, cfg, cols);
    CHECK(z == 3.0f);
    // Quarter (2.5, 2.5) should be 1.5
    z = interpolateZ(2.5f, 2.5f, pts, n, cfg, cols);
    CHECK(z == 1.5f);
}

TEST_CASE("interpolateZ: returns 0 with no valid points") {
    ProbeGridConfig cfg;
    cfg.startX = 0; cfg.startY = 0;
    cfg.endX = 20; cfg.endY = 20;
    cfg.spacing = 10;
    ProbeGridPoint pts[9];
    uint16_t n = buildGridPoints(cfg, pts);
    for (uint16_t i = 0; i < n; ++i) pts[i].valid = false;
    uint16_t cols = 3;
    CHECK(interpolateZ(5, 5, pts, n, cfg, cols) == 0.0f);
}

TEST_CASE("interpolateZ: clamps to nearest valid corner outside grid") {
    ProbeGridConfig cfg;
    cfg.startX = 0; cfg.startY = 0;
    cfg.endX = 20; cfg.endY = 20;
    cfg.spacing = 10;
    ProbeGridPoint pts[9];
    uint16_t n = buildGridPoints(cfg, pts);
    for (uint16_t i = 0; i < n; ++i) { pts[i].z = (float)i; pts[i].valid = true; }
    uint16_t cols = 3;
    // Below startX,startY should return (0,0) value
    float z = interpolateZ(-5, -5, pts, n, cfg, cols);
    CHECK(z == pts[0].z);
    // Beyond endX,endY should return (20,20) value
    z = interpolateZ(30, 30, pts, n, cfg, cols);
    CHECK(z == pts[8].z);
}

TEST_CASE("interpolateZ: edge case grid with single valid point") {
    ProbeGridConfig cfg;
    cfg.startX = 0; cfg.startY = 0;
    cfg.endX = 10; cfg.endY = 10;
    cfg.spacing = 10;
    ProbeGridPoint pts[4];
    uint16_t n = buildGridPoints(cfg, pts);
    pts[0].valid = true; pts[0].z = 3.5f;
    pts[1].valid = false;
    pts[2].valid = false;
    pts[3].valid = false;
    uint16_t cols = 2;
    float z = interpolateZ(5, 5, pts, n, cfg, cols);
    // Only one corner has a value, others default to 0
    // Bilinear of (3.5, 0, 0, 0) at center (0.5, 0.5)
    // zTop = 3.5 + (0-3.5)*0.5 = 1.75
    // zBot = 0 + (0-0)*0.5 = 0
    // result = 1.75 + (0-1.75)*0.5 = 0.875
    CHECK(z == 0.875f);
}

TEST_CASE("interpolateZ: serpentine row (odd) reads reversed corners correctly") {
    // Plane z = x + y. 2 rows × 3 cols, spacing 10.
    // Row0 (y=0): (0,0)=0, (10,0)=10, (20,0)=20
    // Row1 (y=10, serpentine/reversed): (20,10)=30, (10,10)=20, (0,10)=10
    ProbeGridConfig cfg;
    cfg.startX = 0; cfg.startY = 0;
    cfg.endX = 20; cfg.endY = 10;
    cfg.spacing = 10;
    ProbeGridPoint pts[6];
    uint16_t n = buildGridPoints(cfg, pts);
    CHECK(n == 6);
    for (uint16_t i = 0; i < n; ++i) {
        pts[i].valid = true;
        pts[i].z = pts[i].x + pts[i].y;
    }
    uint16_t cols = 3;
    // (5,10): row clamps to the last cell → z = x + y = 15
    float z = interpolateZ(5, 10, pts, n, cfg, cols);
    CHECK(z == 15.0f);
    // (15,10): between (10,10)=20 and (20,10)=30 → 25
    z = interpolateZ(15, 10, pts, n, cfg, cols);
    CHECK(z == 25.0f);
    // (5,0) is on the even row → 5
    z = interpolateZ(5, 0, pts, n, cfg, cols);
    CHECK(z == 5.0f);
}
