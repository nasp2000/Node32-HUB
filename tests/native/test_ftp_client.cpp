#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstddef>

// Standalone reimpl of the pure FTP helpers in chatgpt/src/ftp_client.h/.cpp
// (production code needs Arduino/WiFi; logic duplicated here so host tests
// catch regressions in PASV parsing, filters and upload decisions).

namespace {

bool ftpReplyIs(const char* resp, int code) {
    if (!resp) return false;
    while (*resp == ' ' || *resp == '\r' || *resp == '\n') resp++;
    int v = (resp[0] - '0') * 100 + (resp[1] - '0') * 10 + (resp[2] - '0');
    return v == code;
}

bool ftpParsePasvResponse(const char* resp, char* outIp, size_t ipSize, uint16_t* outPort) {
    if (!resp || !outIp || !outPort || ipSize < 16) return false;
    const char* p = nullptr;
    for (const char* q = resp; *q; q++) if (*q == '(') p = q;
    if (!p) return false;
    int h1, h2, h3, h4, p1, p2;
    if (sscanf(p, "(%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2) != 6) return false;
    if (h1 < 0 || h1 > 255 || h2 < 0 || h2 > 255 || h3 < 0 || h3 > 255 || h4 < 0 || h4 > 255) return false;
    if (p1 < 0 || p1 > 255 || p2 < 0 || p2 > 255) return false;
    snprintf(outIp, ipSize, "%d.%d.%d.%d", h1, h2, h3, h4);
    *outPort = (uint16_t)(p1 * 256 + p2);
    return true;
}

int ftpClampPort(int p) { if (p < 1) return 1; if (p > 65535) return 65535; return p; }
int ftpClampIntervalMin(int m) { if (m < 1) return 1; if (m > 1440) return 1440; return m; }
int ftpClampMaxFiles(int m) { if (m < 1) return 1; if (m > 50) return 50; return m; }

void ftpNormalizeRemotePath(const char* in, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    if (!in || !*in) { strncpy(out, "/", outSize); out[outSize - 1] = 0; return; }
    size_t j = 0;
    if (in[0] != '/') { out[j++] = '/'; }
    for (size_t i = 0; in[i] && j + 1 < outSize; i++) out[j++] = in[i];
    out[j] = 0;
    while (j > 1 && out[j - 1] == '/') out[--j] = 0;
}

// Mirrors ftpRemoteDirList() in ftp_client.cpp: cumulative MKD prefixes for a
// remote base path (fix for STOR 553 when the target folder doesn't exist).
int ftpRemoteDirList(const char* remotePath, char out[][64], int maxOut) {
    if (!out || maxOut <= 0) return 0;
    char norm[128];
    ftpNormalizeRemotePath(remotePath, norm, sizeof(norm));
    if (strcmp(norm, "/") == 0) return 0;
    int count = 0;
    char prefix[64];
    size_t pj = 0;
    prefix[0] = 0;
    const char* s = norm;
    while (*s) {
        while (*s == '/') s++;
        if (!*s) break;
        const char* start = s;
        while (*s && *s != '/') s++;
        size_t seg = (size_t)(s - start);
        if (pj + 1 + seg >= sizeof(prefix)) break;
        prefix[pj++] = '/';
        memcpy(prefix + pj, start, seg);
        pj += seg;
        prefix[pj] = 0;
        if (count < maxOut) {
            strncpy(out[count], prefix, 63);
            out[count][63] = 0;
            count++;
        }
    }
    return count;
}

bool matchExt(const char* filename, const char* ext) {
    size_t fl = strlen(filename), el = strlen(ext);
    if (el == 0 || fl < el) return false;
    const char* tail = filename + fl - el;
    for (size_t i = 0; i < el; i++) {
        char a = tail[i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
    }
    return true;
}

bool ftpMatchFilter(const char* filename, const char* filter) {
    if (!filename || !*filename) return false;
    if (!filter || !*filter) return true;
    const char* base = filename;
    for (const char* q = filename; *q; q++) if (*q == '/' || *q == '\\') base = q + 1;
    char tmp[96];
    size_t fi = 0;
    for (size_t i = 0; ; i++) {
        char c = filter[i];
        if (c == ';' || c == ',' || c == 0) {
            tmp[fi] = 0;
            char* s = tmp;
            while (*s == ' ') s++;
            size_t L = strlen(s);
            while (L && s[L - 1] == ' ') s[--L] = 0;
            if (strchr(s, '*')) {
                // Glob: match by the extension after the last dot, so "*.csv",
                // "*.*.csv" and "*.*.*.json" all resolve to ".csv"/".json".
                // A bare "*" (no dot) matches everything.
                const char* dot = strrchr(s, '.');
                if (dot && dot[1]) { if (matchExt(base, dot)) return true; }
                else return true;
            }
            else if (*s && strcmp(s, base) == 0) return true;
            fi = 0;
            if (c == 0) break;
        } else if (fi + 1 < sizeof(tmp)) tmp[fi++] = c;
    }
    return false;
}

// Must match FTP_MAX_UPLOAD_BYTES in ftp_client.h (files at/above never upload).
const size_t FTP_MAX_UPLOAD_BYTES = (4UL * 1024UL * 1024UL);

bool ftpShouldUpload(const char* filename, size_t fileSize, const char* filter,
                     bool onlyNew, const char* sentList) {
    if (!filename || !*filename || fileSize == 0) return false;
    if (fileSize >= FTP_MAX_UPLOAD_BYTES) return false;
    if (!ftpMatchFilter(filename, filter)) return false;
    if (!onlyNew) return true;
    if (!sentList || !*sentList) return true;
    size_t fnLen = strlen(filename);
    const char* p = sentList;
    while (*p) {
        while (*p == '\r' || *p == '\n' || *p == ' ') p++;
        if (!*p) break;
        if (strncmp(p, filename, fnLen) == 0 && p[fnLen] == ':') {
            const char* num = p + fnLen + 1;
            char* end = nullptr;
            unsigned long v = strtoul(num, &end, 10);
            if (end != num && v == (unsigned long)fileSize &&
                (*end == '\r' || *end == '\n' || *end == 0)) return false;
        }
        while (*p && *p != '\n') p++;
    }
    return true;
}

size_t ftpSentKeepOffset(const char* buf, size_t len, size_t keepMax) {
    if (!buf || len <= keepMax) return 0;
    size_t cut = len - keepMax;
    if (cut > 0 && buf[cut - 1] == '\n') return cut;
    size_t i = cut;
    while (i < len && buf[i] != '\n') i++;
    if (i + 1 < len) return i + 1;
    return 0;
}

// Mirrors ftpSentTailWindow() in ftp_client.cpp: journal reads take the
// NEWEST `want` bytes (plus slack for line alignment).
void ftpSentTailWindow(size_t fileSize, size_t want, size_t slack,
                       size_t* seekOut, size_t* lenOut) {
    size_t cap = want + slack;
    size_t len = (fileSize > cap) ? cap : fileSize;
    if (seekOut) *seekOut = fileSize - len;
    if (lenOut) *lenOut = len;
}

// Mirrors ftpIsConnectFailure() in ftp_client.cpp: session-level failures
// (stop the cycle) vs per-file failures (skip the file, continue the queue).
bool ftpIsConnectFailure(const char* detail) {
    if (!detail || !*detail) return false;
    static const char* const kConnect[] = {
        "control connect failed",
        "greeting:",
        "USER timeout",
        "USER rejected:",
        "PASS timeout",
        "login failed:",
    };
    for (size_t i = 0; i < sizeof(kConnect) / sizeof(kConnect[0]); i++) {
        const char* t = kConnect[i];
        size_t n = strlen(t);
        if (strncmp(detail, t, n) == 0) return true;
    }
    return false;
}

// Mirrors the FS-prefix local reference parsing introduced with the
// SD:/PSRAM:/LFS:/ local folder drop-down (see ftp_client.h).
enum { FTP_FS_SD = 0, FTP_FS_PSRAM = 1, FTP_FS_LFS = 2, FTP_FS_AUTO = 3 };

const char* ftpLocalFsPrefix(const char* ref, const char*& outRest) {
    if (!ref) return nullptr;
    if (strncmp(ref, "SD:/", 4) == 0)    { outRest = ref + 3; return "SD"; }
    if (strncmp(ref, "PSRAM:/", 7) == 0) { outRest = ref + 6; return "PSRAM"; }
    if (strncmp(ref, "LFS:/", 5) == 0)   { outRest = ref + 4; return "LFS"; }
    return nullptr;  // legacy bare path (no FS prefix)
}

// Mirrors ftpIsProtectedUploadPath() in ftp_client.cpp: /config (any backend,
// any prefix) is never uploaded.
bool ftpIsProtectedUploadPath(const char* path) {
    if (!path || !*path) return false;
    const char* p = path;
    if (strncmp(p, "SD:", 3) == 0) p += 3;
    else if (strncmp(p, "LFS:", 4) == 0) p += 4;
    else if (strncmp(p, "PSRAM:", 6) == 0) p += 6;
    if (*p == '\0') return false;
    if (strcmp(p, "/config") == 0) return true;
    if (strncmp(p, "/config/", 8) == 0) return true;
    size_t len = strlen(p);
    while (len > 8 && p[len - 1] == '/') len--;
    if (len == 7 && strncmp(p, "/config", 7) == 0) return true;
    return false;
}

} // namespace

TEST_CASE("local reference FS prefix parsing") {
    const char* rest = nullptr;
    const char* fs = ftpLocalFsPrefix("SD:/logs", rest);
    CHECK(fs != nullptr);
    if (fs) CHECK(strcmp(fs, "SD") == 0);
    CHECK(rest != nullptr);
    if (rest) CHECK(strcmp(rest, "/logs") == 0);

    fs = ftpLocalFsPrefix("PSRAM:/folder/x.csv", rest);
    CHECK(fs != nullptr);
    if (fs) CHECK(strcmp(fs, "PSRAM") == 0);
    CHECK(rest != nullptr);
    if (rest) CHECK(strcmp(rest, "/folder/x.csv") == 0);

    fs = ftpLocalFsPrefix("LFS:/", rest);
    CHECK(fs != nullptr);
    if (fs) CHECK(strcmp(fs, "LFS") == 0);
    CHECK(rest != nullptr);
    if (rest) CHECK(strcmp(rest, "/") == 0);

    fs = ftpLocalFsPrefix("LFS:/config", rest);
    CHECK(fs != nullptr);
    if (fs) CHECK(strcmp(fs, "LFS") == 0);
    CHECK(rest != nullptr);
    if (rest) CHECK(strcmp(rest, "/config") == 0);

    // Whole-folder with trailing slash keeps the slash in the rest.
    fs = ftpLocalFsPrefix("SD:/", rest);
    CHECK(fs != nullptr);
    if (fs) CHECK(strcmp(fs, "SD") == 0);
    CHECK(rest != nullptr);
    if (rest) CHECK(strcmp(rest, "/") == 0);
}

TEST_CASE("legacy bare local path has no FS prefix") {
    const char* rest = nullptr;
    const char* fs = ftpLocalFsPrefix("/logs", rest);
    CHECK(fs == nullptr);

    fs = ftpLocalFsPrefix("", rest);
    CHECK(fs == nullptr);

    // A folder literally named "SD:/" lives inside root; prefix must span first
    // slash so "SD:" alone (no slash) is not a valid FS selection.
    fs = ftpLocalFsPrefix("SD:x", rest);
    CHECK(fs == nullptr);
}

TEST_CASE("local reference FS enum mapping") {    const char* rest = nullptr;
    const char* fs = ftpLocalFsPrefix("SD:/", rest);
    CHECK(fs != nullptr);
    if (fs) CHECK(strcmp(fs, "SD") == 0);
    fs = ftpLocalFsPrefix("PSRAM:/", rest);
    CHECK(fs != nullptr);
    if (fs) CHECK(strcmp(fs, "PSRAM") == 0);
    fs = ftpLocalFsPrefix("LFS:/", rest);
    CHECK(fs != nullptr);
    if (fs) CHECK(strcmp(fs, "LFS") == 0);
    fs = ftpLocalFsPrefix("whatever", rest);
    CHECK(fs == nullptr);
}

TEST_CASE("PASV response parsing") {
    char ip[24]; uint16_t port = 0;
    CHECK(ftpParsePasvResponse("227 Entering Passive Mode (192,168,1,10,19,136)", ip, sizeof(ip), &port));
    CHECK(strcmp(ip, "192.168.1.10") == 0);
    CHECK(port == 19 * 256 + 136);
    // Variation: IIS style with trailing text
    CHECK(ftpParsePasvResponse("227 Entering passive mode (10,0,0,5,4,20).", ip, sizeof(ip), &port));
    CHECK(port == 4 * 256 + 20);
    // Failures: no parens, out of range
    CHECK(!ftpParsePasvResponse("500 no pasv", ip, sizeof(ip), &port));
    CHECK(!ftpParsePasvResponse("227 Entering Passive Mode (999,1,1,1,1,1)", ip, sizeof(ip), &port));
}

TEST_CASE("reply code matching") {
    CHECK(ftpReplyIs("220 Welcome", 220));
    CHECK(ftpReplyIs("  226 Transfer complete\r\n", 226));
    CHECK(!ftpReplyIs("530 Login failed", 230));
    CHECK(!ftpReplyIs(nullptr, 200));
}

TEST_CASE("clamps keep config sane") {
    CHECK(ftpClampPort(0) == 1);
    CHECK(ftpClampPort(70000) == 65535);
    CHECK(ftpClampPort(2121) == 2121);
    CHECK(ftpClampIntervalMin(0) == 1);
    CHECK(ftpClampIntervalMin(2000) == 1440);
    CHECK(ftpClampMaxFiles(0) == 1);
    CHECK(ftpClampMaxFiles(99) == 50);
    CHECK(ftpClampMaxFiles(10) == 10);
}

TEST_CASE("remote path normalization") {
    char out[64];
    ftpNormalizeRemotePath("", out, sizeof(out));
    CHECK(strcmp(out, "/") == 0);
    ftpNormalizeRemotePath("uploads", out, sizeof(out));
    CHECK(strcmp(out, "/uploads") == 0);
    ftpNormalizeRemotePath("/uploads/", out, sizeof(out));
    CHECK(strcmp(out, "/uploads") == 0);
    ftpNormalizeRemotePath("/", out, sizeof(out));
    CHECK(strcmp(out, "/") == 0);
}

TEST_CASE("extension filter matching") {
    CHECK(ftpMatchFilter("data.csv", "*.csv;*.log"));
    CHECK(ftpMatchFilter("LOG.TXT", "*.txt"));
    CHECK(ftpMatchFilter("a.log", "*.csv;*.log"));
    CHECK(!ftpMatchFilter("photo.jpg", "*.csv;*.log"));
    CHECK(ftpMatchFilter("anything.bin", ""));
    CHECK(ftpMatchFilter("/logs/data.csv", "*.csv"));
    CHECK(!ftpMatchFilter("", "*.csv"));
    // Legacy page-grown globs must still match by extension (FTP filter bug).
    CHECK(ftpMatchFilter("main.json", "*.json"));
    CHECK(ftpMatchFilter("main.json", "*.*.json"));
    CHECK(ftpMatchFilter("main.json", "*.*.*.json"));
    CHECK(ftpMatchFilter("/config/main.json", "*.*.*.json"));
    CHECK(!ftpMatchFilter("main.txt", "*.*.*.json"));
    CHECK(ftpMatchFilter("data.csv", "*.csv;*.*.*.json"));
    CHECK(ftpMatchFilter("anything", "*"));            // bare glob matches all
    CHECK(!ftpMatchFilter("main.json", "*.csv;*.log"));
}

TEST_CASE("remote base directory list for MKD") {
    char out[8][64];
    // Single-level folder (the reported 553 case): "/Equip15" -> {"/Equip15"}
    int n = ftpRemoteDirList("/Equip15", out, 8);
    CHECK(n == 1);
    CHECK(strcmp(out[0], "/Equip15") == 0);
    // Nested: "/a/b" -> {"/a", "/a/b"}
    n = ftpRemoteDirList("/a/b", out, 8);
    CHECK(n == 2);
    CHECK(strcmp(out[0], "/a") == 0);
    CHECK(strcmp(out[1], "/a/b") == 0);
    // Root or empty -> nothing to create
    CHECK(ftpRemoteDirList("/", out, 8) == 0);
    CHECK(ftpRemoteDirList("", out, 8) == 0);
    // Missing leading slash and duplicate/trailing slashes normalize away
    n = ftpRemoteDirList("logs//2026-09-19/", out, 8);
    CHECK(n == 2);
    CHECK(strcmp(out[0], "/logs") == 0);
    CHECK(strcmp(out[1], "/logs/2026-09-19") == 0);
    // maxOut caps the list without overflowing
    CHECK(ftpRemoteDirList("/a/b/c", out, 2) == 2);
}

TEST_CASE("upload decision: deep relPaths (over old 96-char needle) skip correctly") {
    // Regression: the old fixed-size "name:size" needle truncated deep paths,
    // so they never matched and re-uploaded every cycle.
    std::string deep(120, 'a');
    deep = "logs/2026-09-19/nested/deep/" + deep + ".csv";
    std::string sent = deep + ":1234\n";
    CHECK(!ftpShouldUpload(deep.c_str(), 1234, "*.csv", true, sent.c_str()));
    // Same deep name, other size = new content -> upload.
    CHECK(ftpShouldUpload(deep.c_str(), 1235, "*.csv", true, sent.c_str()));
    // Prefix collisions must not match: "a.csv" entry must not skip "a.csv2"
    // ("*" filter so the extension itself is not the reason).
    const char* sent2 = "a.csv:100\n";
    CHECK(ftpShouldUpload("a.csv2", 100, "*", true, sent2));
    CHECK(!ftpShouldUpload("a.csv", 100, "*", true, sent2));
}

TEST_CASE("upload decision: oversize files never upload") {
    CHECK(!ftpShouldUpload("big.bin", FTP_MAX_UPLOAD_BYTES, "*.bin", false, ""));
    CHECK(!ftpShouldUpload("big.bin", FTP_MAX_UPLOAD_BYTES + 1, "*", true, ""));
    CHECK(ftpShouldUpload("big.bin", FTP_MAX_UPLOAD_BYTES - 1, "*.bin", false, ""));
}

TEST_CASE("sent-log keep offset never cuts an entry") {
    const char* buf = "a.csv:1\nbb.csv:22\nccc.csv:333\n";
    size_t len = strlen(buf);
    // Everything fits -> 0.
    CHECK(ftpSentKeepOffset(buf, len, 1024) == 0);
    CHECK(ftpSentKeepOffset(buf, len, len) == 0);
    CHECK(ftpSentKeepOffset(nullptr, 0, 8) == 0);
    // keepMax=12 keeps "ccc.csv:333\n" (12 chars, len=30): cut=18 lands on a
    // line start (buf[17] is '\n') -> 18.
    CHECK(ftpSentKeepOffset(buf, len, 12) == 18);
    // keepMax=5: cut inside the last line, no newline ahead -> keep all (0).
    CHECK(ftpSentKeepOffset(buf, len, 5) == 0);
    // keepMax=15: cut=15 lands mid-line ("bb.csv:22") -> jump to next line (18).
    CHECK(ftpSentKeepOffset(buf, len, 15) == 18);
}

TEST_CASE("upload decision with only-new list") {
    const char* sent = "a.csv:100\nb.log:50\n";
    CHECK(ftpShouldUpload("c.csv", 10, "*.csv;*.log", true, sent));
    CHECK(!ftpShouldUpload("a.csv", 100, "*.csv", true, sent));
    // Same name, different size = new content -> upload
    CHECK(ftpShouldUpload("a.csv", 101, "*.csv", true, sent));
    // onlyNew off ignores the list
    CHECK(ftpShouldUpload("a.csv", 100, "*.csv", false, sent));
    // Empty files never upload
    CHECK(!ftpShouldUpload("c.csv", 0, "*.csv", false, ""));
    // Filter still applies
    CHECK(!ftpShouldUpload("x.jpg", 99, "*.csv", false, ""));
}

TEST_CASE("sent journal read window takes the tail (newest entries)") {
    size_t seek = 0, len = 0;
    // Small journal: whole file from the start.
    ftpSentTailWindow(100, 8192, 512, &seek, &len);
    CHECK(seek == 0);
    CHECK(len == 100);
    // Empty journal: nothing to read.
    ftpSentTailWindow(0, 8192, 512, &seek, &len);
    CHECK(seek == 0);
    CHECK(len == 0);
    // Large journal: newest want+slack bytes only (trim keeps the tail too).
    ftpSentTailWindow(20000, 8192, 512, &seek, &len);
    CHECK(len == 8192 + 512);
    CHECK(seek == 20000 - (8192 + 512));
    // seek+len always covers the end of the file.
    CHECK(seek + len == 20000);
}

TEST_CASE("connect failure classification stops the cycle, file failures skip") {
    // Session-level: stop the whole cycle, retry next interval.
    CHECK(ftpIsConnectFailure("control connect failed"));
    CHECK(ftpIsConnectFailure("greeting: 421 too many connections"));
    CHECK(ftpIsConnectFailure("USER timeout"));
    CHECK(ftpIsConnectFailure("USER rejected: 530 bad user"));
    CHECK(ftpIsConnectFailure("PASS timeout"));
    CHECK(ftpIsConnectFailure("login failed: 530 wrong password"));
    // Per-file: skip the file and continue with the rest of the queue.
    CHECK(!ftpIsConnectFailure("TYPE failed: 500 unknown"));
    CHECK(!ftpIsConnectFailure("PASV failed: 502 passive denied"));
    CHECK(!ftpIsConnectFailure("PASV parse failed: 227 ???"));
    CHECK(!ftpIsConnectFailure("data connect failed: 192.168.1.35"));
    CHECK(!ftpIsConnectFailure("STOR rejected: 553 no permission"));
    CHECK(!ftpIsConnectFailure("data write failed"));
    CHECK(!ftpIsConnectFailure("STOR incomplete: 426 aborted"));
    CHECK(!ftpIsConnectFailure("open/read failed or empty"));
    CHECK(!ftpIsConnectFailure("OK a.csv (100B)"));
    CHECK(!ftpIsConnectFailure("cycle done: 3 file(s)"));
    CHECK(!ftpIsConnectFailure(""));
    CHECK(!ftpIsConnectFailure(nullptr));
}

TEST_CASE("protected upload path: /config never leaves the device") {
    // Bare paths, all backends.
    CHECK(ftpIsProtectedUploadPath("/config") == true);
    CHECK(ftpIsProtectedUploadPath("/config/") == true);
    CHECK(ftpIsProtectedUploadPath("/config/ftp_sent.log") == true);
    CHECK(ftpIsProtectedUploadPath("/config/alerts.json") == true);
    // Explicit FS prefixes must not smuggle the tree in.
    CHECK(ftpIsProtectedUploadPath("LFS:/config") == true);
    CHECK(ftpIsProtectedUploadPath("LFS:/config/ftp_sent.log") == true);
    CHECK(ftpIsProtectedUploadPath("SD:/config/x.json") == true);
    CHECK(ftpIsProtectedUploadPath("PSRAM:/config") == true);
    // Data roots stay uploadable.
    CHECK(ftpIsProtectedUploadPath("/logs") == false);
    CHECK(ftpIsProtectedUploadPath("/logs/log_14-10-2026") == false);
    CHECK(ftpIsProtectedUploadPath("/oee/wk42-oee.csv") == false);
    CHECK(ftpIsProtectedUploadPath("/Equip15/PROXIMITY_14-10-2026.csv") == false);
    CHECK(ftpIsProtectedUploadPath("LFS:/oee/x.csv") == false);
    CHECK(ftpIsProtectedUploadPath("/") == false);
    CHECK(ftpIsProtectedUploadPath("") == false);
    CHECK(ftpIsProtectedUploadPath(nullptr) == false);
    // Look-alikes are NOT protected (prefix match would over-block).
    CHECK(ftpIsProtectedUploadPath("/configx") == false);
    CHECK(ftpIsProtectedUploadPath("/configuration") == false);
}
