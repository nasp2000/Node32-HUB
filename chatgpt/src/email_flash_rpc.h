// email_flash_rpc.h — stack-safe LittleFS access for the email pipeline.
//
// A task whose stack lives in PSRAM must NEVER touch LittleFS (or any flash
// op) directly: ESP-IDF disables the flash cache for those ops and asserts
// esp_task_stack_is_sane_cache_disabled() — a PSRAM stack pointer fails the
// assert and the board panics (panic_abort, HISTORIC/HomeE32 (8) 2026-10-15:
// emailQueueProc -> esp_partition_read -> spi_flash_disable_interrupts_...
// -> assert. SDMMC never disables the cache, which is why SD worked for
// months with PSRAM stacks and the SD->LittleFS parity crashed).
//
// RULE for every flash touch reachable from an email task:
//   * go through emailFlash*() — never LittleFS.* directly.
//
// Dispatch is decided PER CALL from the CALLING TASK'S STACK (not from a
// config flag), so it can never be wrong for a task that changes later:
//   * internal-stack caller (loop, alert tasks, sd_mount-less helpers):
//     runs the op directly, under the same lfsMutex the writers use.
//   * external-stack caller (emailTask, emailQueueProc): publishes the
//     request; emailFlashServe() (called every loop() iteration from the
//     internal loop stack) executes it and hands the result back.
// SD packs see zero behavior change: everything there runs internal/direct.
//
// Handles (int32_t >= 0) own an open file in a small shared table; reads go
// through emailFlashRead() so NO raw File ever leaks into an external stack.
// Every open MUST be closed by the same call chain (emailFlashClose).

#pragma once

#include <Arduino.h>
#include <vector>
#include <utility>

// Create the RPC request mutex. Call once from runtimeEmailInitBackgroundTasks
// BEFORE the email tasks start (direct/internal callers never need it).
void emailFlashRpcInit();

// True when the calling task's stack lives in PSRAM (external RAM) and must
// not touch flash itself.
bool emailFlashCallerNeedsRpc();

// Open read-only. Returns handle >= 0, or < 0 when open/lock/RPC failed.
int32_t emailFlashOpenRead(const String& path);
// Read up to len bytes at the current position. Returns bytes read (0 =
// EOF or error); handle still must be closed.
size_t emailFlashRead(int32_t h, uint8_t* buf, size_t len);
bool    emailFlashSeek(int32_t h, size_t pos);
void    emailFlashClose(int32_t h);

// Metadata probe: true + size when the file exists and opens (size may be 0).
bool emailFlashProbeSize(const String& path, size_t& outSize);
bool emailFlashExists(const String& path);

// Create/truncate and write len bytes in one op (len capped at 128KB).
bool emailFlashWriteFile(const String& path, const uint8_t* data, size_t len);
bool emailFlashRemove(const String& path);
bool emailFlashRename(const String& from, const String& to);
bool emailFlashMkdir(const String& path);

// Directory listing as (base name, size) pairs — files only, no subdirs
// (same shape as flashFbListDir).
bool emailFlashListDir(const String& dir, std::vector<std::pair<String, size_t>>& out);

// Serve ONE pending RPC request. Called from loop() every iteration; costs a
// single mux take when idle. Never called from an external stack (the loop
// task always has an internal stack).
void emailFlashServe();
