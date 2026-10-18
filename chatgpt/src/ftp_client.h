// ftp_client.h - FTP client upload (SD / PSRAM VFS / LittleFS -> FTP server)
//
// Plain FTP (PASV) with login/password or anonymous. Heap frugal: control +
// data sockets are transient; each STOR pass streams the file in 8 KB
// internal-RAM chunks (cap FTP_MAX_UPLOAD_BYTES) to the data socket in ~1460 B
// segments — never staged whole in PSRAM (SDMMC DMA into PSRAM corrupts
// memory on ESP32-S3). Disabled at compile time = zero RAM.
//
// Pure helpers in this header are host-testable (tests/native/test_ftp_client).
#pragma once

#include <stdint.h>
#include <stddef.h>

#if defined(ARDUINO)
#include <Arduino.h>
#else
#include <string>
#endif

// Max files uploaded per monitor cycle (flood protection).
#define FTP_MAX_FILES_PER_CYCLE 50
// Max pending entries listed per cycle (PSRAM vector, ~80 B/entry).
#define FTP_MAX_PENDING 64
// Files at/above this size are skipped, never uploaded (upload holds the
// exclusive SD lock for the whole STOR; larger would starve writers).
#define FTP_MAX_UPLOAD_BYTES (200UL * 1024UL * 1024UL)

// Local source selector. Values of the `ftp_local_dir` config field carry a
// filesystem prefix: "SD:/logs", "PSRAM:/", "LFS:/logs". FTP_FS_AUTO is a
// legacy bare path (e.g. "/logs") resolved at runtime by ftpResolveLocalFs.
enum FtpLocalFs { FTP_FS_SD = 0, FTP_FS_PSRAM, FTP_FS_LFS, FTP_FS_AUTO };

// Split a local_dir ref into filesystem + path part.
//   "SD:/logs"       -> FTP_FS_SD, outPath "/logs"
//   "PSRAM:/"        -> FTP_FS_PSRAM, outPath "/"
//   "LFS:/config"    -> FTP_FS_LFS, outPath "/config"
//   "/logs" (legacy) -> FTP_FS_AUTO, outPath "/logs"
// outPath points inside ref (no copy) — valid while ref is alive.
FtpLocalFs ftpParseLocalRef(const char* ref, const char*& outPath);

// Resolve FTP_FS_AUTO to an explicit filesystem given SD activity;
// explicit values pass through unchanged. Pure/host-testable.
// NOTE: AUTO without SD resolves to PSRAM (hot tier) here; ftpListCandidates
// additionally merges the LittleFS mirrors (durable tier) so no-SD uploads
// cover the same content SD would hold. See the parity rule below.
FtpLocalFs ftpResolveLocalFs(FtpLocalFs fs, bool sdActive);

// /config (all of LittleFS /config, any backend) is NEVER uploaded: it holds
// system configuration (passwords live in NVS, but nothing in /config may
// leave the device). Applies to explicit refs AND bare paths, on every
// backend. Pure/host-testable.
bool ftpIsProtectedUploadPath(const char* path);

// Parse PASV response "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)".
// Returns true and fills outIp (dotted) + outPort on success.
bool ftpParsePasvResponse(const char* resp, char* outIp, size_t ipSize, uint16_t* outPort);

// Check 3-digit FTP reply code prefix, e.g. ftpReplyIs(resp, 227).
bool ftpReplyIs(const char* resp, int code);

// Clamp helpers (also used by ConfigManager setters).
int ftpClampPort(int p);
int ftpClampIntervalMin(int m);
int ftpClampMaxFiles(int m);

// Normalize remote path: empty -> "/", ensure leading slash, no trailing
// slash (except root). out must hold outSize bytes.
void ftpNormalizeRemotePath(const char* in, char* out, size_t outSize);

// Cumulative directory prefixes that must exist for a remote base path, so the
// uploader can MKD them before STOR (a missing target folder returns 553).
//   "/Equip15" -> {"/Equip15"}
//   "/a/b"     -> {"/a", "/a/b"}
//   "/" or ""  -> none (FTP root/home already exists)
// Each entry is NUL-terminated and <= 63 chars. Returns the number written
// (<= maxOut). Pure/host-testable.
int ftpRemoteDirList(const char* remotePath, char out[][64], int maxOut);

// Extension filter: "(*.csv;*.log)" list, case-insensitive. Empty filter = match all.
bool ftpMatchFilter(const char* filename, const char* filter);

// Decide if a file should be uploaded: 0 < size < FTP_MAX_UPLOAD_BYTES,
// matches filter, and (when onlyNew) not present in sent list ("name:size"
// lines, exact line match — no fixed-size buffer, so deep relPaths skip
// correctly instead of re-uploading every cycle).
bool ftpShouldUpload(const char* filename, size_t fileSize, const char* filter,
                     bool onlyNew, const char* sentList);

// Offset of the first whole line within the last keepMax bytes of a sent-log
// buffer (for log rotation): never cut a "name:size" entry in half. Returns 0
// when everything fits or no newline exists in the window (keep all).
// Pure/host-testable.
size_t ftpSentKeepOffset(const char* buf, size_t len, size_t keepMax);

// Byte window to read from the sent journal on flash: the NEWEST `want` bytes
// (plus `slack` for line alignment, applied afterwards with
// ftpSentKeepOffset). The trim keeps the tail, so the read must too —
// reading the head silently forgets recent entries and re-uploads them.
// Pure/host-testable.
void ftpSentTailWindow(size_t fileSize, size_t want, size_t slack,
                       size_t* seekOut, size_t* lenOut);

// True when an upload detail line means the SESSION is unusable (control
// connect / greeting / login failures): the cycle must stop and retry next
// interval. Anything else (TYPE/PASV-data/STOR/size/read problems) is a
// per-file problem: skip that file and continue with the rest of the queue
// instead of wedging behind it. Pure/host-testable.
bool ftpIsConnectFailure(const char* detail);

#if defined(ARDUINO)
#include "feature_flags.h"
#if ENABLE_FTP_CLIENT==1

struct FtpStatus {
    bool enabled = false;
    bool autoUpload = false;
    char lastFile[64] = {0};
    char lastResult[96] = {0};
    int lastCode = 0;
    unsigned long lastTime = 0;
    uint32_t okCount = 0;
    uint32_t failCount = 0;
    bool busy = false;
    char activeSource[8] = {0};  // "SD", "PSRAM" or "LFS"
    uint16_t monitorStackFree = 0;  // peak stack left in ftpMon after the last auto cycle (bytes)
};

void ftpClientInit();
bool ftpIsAvailable();
void ftpGetStatus(FtpStatus& out);
// Non-blocking busy peek for 1s-tick gates (graceful reboot): try-take the
// status mutex with zero wait instead of ftpGetStatus()'s 2s blocking take.
bool ftpIsBusy();
// Stepwise connection test (greeting + login + PWD + PASV probe, no file
// written; also reports whether the remote base folder exists). Fills detail.
bool ftpTestConnection(String& detail);
// Upload one file from a resolved local source (SD / PSRAM VFS / LittleFS).
// localPath is the full read path (absolute for SD/LFS, "folder/file" for
// PSRAM VFS); remoteName is the mirror name under the remote path. The file
// is streamed in internal-RAM chunks (cap FTP_MAX_UPLOAD_BYTES), never staged
// whole in PSRAM. When deleteAfter and server SIZE matches, removes local file.
// fs selects the filesystem to read from.
bool ftpUploadFileImpl(FtpLocalFs fs, const String& localPath, const String& remoteName, String& detail, bool deleteAfter);
// Monitor tick: every interval_min, uploads up to max_files new files.
void ftpMonitorTick();
// Force immediate cycle (web "Upload now"). Returns files uploaded.
int ftpUploadNow(String& detail);

#endif
#endif
