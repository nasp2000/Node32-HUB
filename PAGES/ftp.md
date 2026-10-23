# FTP Upload — `/ftp`

> Reference for the **FTP client configuration page**: plain-FTP (PASV) connection
> settings with login/password or anonymous, plus the automatic folder monitor
> (SD card, PSRAM VFS or internal flash/LittleFS, recursive) with status,
> step-wise connection test and upload-now. Written for both humans and AI agents.

---

## 1. Objective

Configure the lightweight **FTP client** that uploads files from the device to a
Windows FTP server (IIS / FileZilla Server), and drive the **automatic monitor**
that watches a chosen local folder and uploads **recursively** (all files in all
subfolders, mirroring the tree on the server) on an interval. The local source
is one of three filesystems — SD card, PSRAM VFS or internal flash (LittleFS) —
selected with an `SD:`/`PSRAM:`/`LFS:` prefix. No TLS on the wire — LAN/VLAN use
only.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /ftp` |
| Feature flag | `ENABLE_PAGE_FTP==1` (requires `ENABLE_FTP_CLIENT==1`; both default `0`) |
| WiFi server | `web_routes_config.cpp` → `streamFtpPageToPrint()` (`ftp_page_renderer.cpp:6`) |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h` (`/ftp`) → `streamSharedFtpPage` (`web_shared_pages.cpp`); else `404 not found` |
| Home button | `home_page_renderer.h` (`%%FTP_BUTTON%%`) — REDES section, `#if ENABLE_PAGE_FTP==1`, label `homeFtp` ("📤 FTP Upload") |
| Route registry | `{"GET", "/ftp"}` + `{"GET", "/api/ftp-status"}` + `{"GET", "/api/ftp-folders"}` + `{"POST", "/api/ftp-test"}` + `{"POST", "/api/ftp-upload-now"}` (`route_registry_api.cpp`) |
| Self-test manifest | `PAGE("/ftp", ENABLE_PAGE_FTP)` (`selftest_handler.cpp`, `test_manifest_handler.cpp`) |
| Auth / caching | `ensureBasicAuth` + `allowHtmlClient`; `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` |
| Renderer shell | `streamPageShellStart` + fragment + `/config-manager.js` + auto.js footer |

---

## 3. Layout

```
main.page.ftp > div.wrap
├ nav.actions.nav-inicio: 🏠 Home link (/)
├ H1 "📤 FTP Upload"                        [ftpPageTitle]
├ form#ftp-config-form  action=/api/config  hidden input _html=1
│  ├ 🔌 Card "FTP Connection" [ftpConnTitle] (blue border)
│  │  ├ FTP Host [ftpHostLbl] + hint → input#ftp_host placeholder 192.168.1.10
│  │  ├ Port [ftpPortLbl] → input#ftp_port type=number min=1 max=65535 placeholder 21
│  │  ├ User [ftpUserLbl] → input#ftp_user
│  │  ├ Password [ftpPassLbl] → input#ftp_pass type=password (never pre-filled)
│  │  ├ ☑ Anonymous login [ftpAnonLbl] → input#ftp_anonymous checkbox
 │  │  ├ Remote path [ftpRemoteLbl] + hint → input#ftp_remote_path (default `/` + system name)
 │  │  └ Local folder [ftpLocalLbl] + hint → select#ftp_local_dir (dropdown grouped by filesystem: `SD:/` + SD primary folders, `PSRAM:/` + VFS folders, `LFS:/` + LittleFS primary folders; NO custom-path option — legacy bare values like `/logs` are kept as a first option for backwards compat)
│  ├ ⏱ Card "Automatic upload" [ftpAutoTitle] (green border)
│  │  ├ ☑ Enable FTP client [ftpEnabledLbl] → input#ftp_enabled checkbox
│  │  ├ ☑ Monitor folder [ftpAutoLbl] → input#ftp_auto_upload checkbox
│  │  ├ Interval (hours) [ftpIntervalLbl] → select#ftp_interval_hours with fixed options `5H` / `12H` / `24H` / `36H` (default **12H** = 720 min). On save JS sends `ftp_interval_min` = h×60; on load `ftpNearestHourMins()` maps the saved minutes to the nearest option
│  │  ├ ☑ Only new/changed files [ftpOnlyNewLbl] → input#ftp_only_new checkbox
│  │  ├ ☑ Delete after verified upload [ftpDeleteLbl] → input#ftp_delete_after checkbox
 │  │  ├ Max files per cycle [ftpMaxLbl] → input#ftp_max_files number 1–50
 │  │  └ File filter [ftpFilterLbl] + hint → extension checkbox strip + custom text (full-width row, `grid-column:1/-1`)
│  └ 💾 Save (submit, form attr)
└ 📊 Card "Status" [ftpStatusTitle] (purple border)
   ├ div#ftp-status (source | last cycle time | last file | last result | OK/FAIL counters)
   ├ 📋 "Recent uploads" [ftpRecentTitle]: table#ftp-recent (File | Size | Time | Result)
   │  └ tbody#ftp-recent-body — newest first, ✔ green / ✘ red; div#ftp-recent-empty
   │     [ftpRecentEmpty] shown instead when the device has no recorded sends
   ├ button #btn-ftp-test "Test connection" [ftpTestBtn]
   └ button #btn-ftp-now "Upload now" [ftpNowBtn]
```

Cards use a 2-column grid (`fgrid`, gap 6px); option rows are half-width cells,
left-aligned. Only the File filter strip spans the full row. The extension
checkboxes sit directly on the blue card — no inner dark box (the former
`#ftp_ext_checks` dark background/border was removed).

---

## 4. Inputs

| Input | ID / name | Type / range | Notes |
|---|---|---|---|
| Host | `ftp_host` | text | from `GET /api/ftp-status` → `host` |
| Port | `ftp_port` | number 1–65535 | clamped both sides, default 21 |
| User | `ftp_user` | text | ignored when anonymous is on |
| Password | `ftp_pass` | password | never pre-filled; server masks as `********`, masked POST skipped |
| Anonymous | `ftp_anonymous` | checkbox | `anonymous` login (`anonymous`/`node32@hub`) |
| Remote path | `ftp_remote_path` | text | normalized to leading `/`, no trailing `/` (except root); default shown is `/` + system name when unset/root |
| Local folder | `ftp_local_dir` | dropdown, values `SD:/…`, `PSRAM:/…`, `LFS:/…` | populated by `GET /api/ftp-folders` (each mounted filesystem contributes its root + primary 1-level folders, grouped in `<optgroup>`s; no custom-path option); legacy bare values (e.g. `/logs` from older configs) resolve as `FTP_FS_AUTO` → SD when `sdcardShouldUseSD()` else PSRAM; default for new selections `SD:/`. A persisted legacy log root (`/LOGs`, any case, with/without the `SD:`/`PSRAM:`/`LFS:` prefix) is migrated to `/logs` on config load (`ConfigManager::ensureAllFieldsExist`) because the boot migration in `log.cpp` renamed the folder. **AUTO without SD merges LittleFS mirrors + PSRAM hot tier (deduped)** so the upload covers what SD would hold. **`/config` is never uploaded** (any backend/prefix, entry + scans; refused loudly) |
| Enabled | `ftp_enabled` | checkbox | master switch |
| Auto upload | `ftp_auto_upload` | checkbox | monitor task active |
| Interval | `ftp_interval_hours` (UI) → `ftp_interval_min` (backend) | select fixed 5/12/24/36 H | default **12H** (= 720 min); backend stores minutes, clamps 300–2160 (5H–36H) |
| Only new | `ftp_only_new` | checkbox | skip entries already in `/config/ftp_sent.log` — match is exact `relPath:size` (mirror path, no fixed-size buffer, so deep paths skip correctly), so a changed size re-uploads (default on). Same size + different content still skips (size-keyed by design) |
| Delete after | `ftp_delete_after` | checkbox | local delete only after remote `SIZE` matches (default off) |
| Max files | `ftp_max_files` | number 1–50 | flood protection, default 10 |
| Filter | `ftp_filter` | text | `;`-separated globs, e.g. `*.csv;*.log`, case-insensitive. The matcher understands `*.ext` (by extension), a bare `*` (match all) or an exact filename — and, for backwards compat, reduces **any** glob to its last extension (`*.*.json` / `*.*.*.json` → `.json`). The UI stores customs as `.ext` only and regenerates `*.ext`, so the string never grows. Empty = match all. **Default = all shipped extensions** (`*.csv;*.log;*.txt;*.bin;*.wav;*.gcode;*.nc;*.avi;*.jpg;*.mp3;*.flac`), all checkboxes ticked |

---

## 5. Buttons

| Button | ID / handler | Behaviour |
|---|---|---|
| 🏠 Home | link `/` | first child of `.wrap` |
| 💾 Save | submit of `#ftp-config-form` | `section=ftp`, explicit `'1'/'0'` for checkboxes, drops empty `ftp_pass`; `d.success` → saved alert + `showSavedCard()` |
| 🔌 Test connection | `#btn-ftp-test` | `POST /api/ftp-test` → stepwise detail (`control → login → PASV` + whether the configured remote path exists/`will be created`); 10 s throttle (429) |
| ⬆ Upload now | `#btn-ftp-now` | `POST /api/ftp-upload-now` → immediate monitor cycle, returns `{uploaded, message}` |

---

## 6. Client-side logic

Inline `<script>` (no arrow functions; `customAlert`, `disableBtn`/`enableBtn`, `showLoading`/`hideLoading`):

- `ftpText(k, fb)` — i18n helper with fallback.
- `ftpLoad()` — `GET /api/ftp-status` (`cache:'no-store'`); fills all inputs + checkboxes; renders `#ftp-status` line (`Source | Last cycle | last file | result | OK/FAIL`) and calls `ftpRenderRecent(d.recent)`.
- `ftpRenderRecent(list)` — rebuilds `#ftp-recent-body` (file, `ftpFmtSize()`, `time_str`, ✔/✘) with `textContent` only (filenames are never parsed as markup); shows `#ftp-recent-empty` when the persisted list is empty.
- `ftpFmtSize(b)` — bytes → `B` / `KB` / `MB` for the recent list.
- `ftpLoadFolders()` — `GET /api/ftp-folders`; rebuilds `#ftp_local_dir` grouped by filesystem (`<optgroup>` SD card / PSRAM VFS / Internal flash); selects the exact saved value, else the group root; a legacy bare value not in the list is prepended and kept selected.
- Test/now buttons — POST with empty JSON body; `customAlert(d.message)`; `ftpLoad()` refresh after.
- Submit — `FormData` → `section=ftp`, sends the dropdown value as-is (`SD:/…` etc.), forces checkbox `'1'/'0'`, deletes empty `ftp_pass` so the NVS password is kept; urlencoded `POST /api/config`.

---

## 7. Server API

| Endpoint | Method | Handler | Purpose / shape |
|---|---|---|---|
| `/api/ftp-status` | GET | `ftp_handler.cpp` (`handleFtpStatusGet`) | full ftp section (password `"********"`) + `last_file/last_result/last_code/ok_count/fail_count/busy/source/monitor_stack_free` + the persisted summary `last_time_str`/`last_epoch` and `recent[]` (`{file,size,ok,time_str,ts}`, newest first, ≤8); JSON built in PSRAM |
| `/api/ftp-folders` | GET | `ftp_handler.cpp` (`handleFtpFoldersGet`) | JSON array of selectable local folders: `SD:/` + SD primary folders (SD mutex taken/released per entry + 1 ms yield so the SD never stays blocked), `PSRAM:/` + VFS folders, `LFS:/` + LittleFS primary folders (both guarded by their `ENABLE_*` flags) |
| `/api/ftp-test` | POST | `ftp_handler.cpp` (`handleFtpTestPost`) | 429 throttle 10 s; 400 `config_incomplete`; else stepwise `control+login+PASV` probe (+ remote path existence check) → 200/500 with detail |
| `/api/ftp-upload-now` | POST | `ftp_handler.cpp` (`handleFtpUploadNowPost`) | immediate monitor cycle → 200 `{uploaded, message}` when the cycle ran (incl. 0 new files); 400 when unconfigured; 500 `success:false` when the cycle never ran (`busy` / `disabled/incomplete`) |
| `/api/config?section=ftp` | GET | `config_handler.cpp:143` | `{ftp:{...}}` slice of `getFullConfig(true)` (password masked) |
| `/api/config` | POST | `config_handler.cpp` | accepts `ftp_*` keys; skips masked password; clamps port/interval/max; saves stem `ftp` |

Disabled build (`ENABLE_FTP_CLIENT==0`) → all three FTP APIs return 503 `ftp_disabled`.

---

## 8. Persistence

| Data | Where |
|---|---|
| Full `ftp` section JSON (all fields except real password) | LittleFS `/config/ftp.json` (stem `ftp`, 4096 B, atomic `.tmp` → validate → rename) |
| FTP password real value | **NVS** namespace `esp32_config`, key `cfg_ftp_pwd` (`NVS_KEY_FTP_PWD`, `ConfigManager.h`) — disk copy stores `"********"` |
| Load path | NVS password merged over file section at boot (`ConfigManager.cpp` `loadPwd`) |
| Sent-files log (`name:size` lines, only-new tracking) | LittleFS `/config/ftp_sent.log` (append-only; read takes the NEWEST ~8 KB tail, line-aligned via `ftpSentTailWindow()` + `ftpSentKeepOffset()`; rotated at 16 KB keeping the newest ~8 KB from a line start — audit 2026-09-24: the old head-read forgot recent entries past 8 KB and re-uploaded them every cycle until rotation) |
| Last-sends summary for the page (`v/last_time/ok_count/fail_count/last_result/recent[]`) | LittleFS `/config/ftp_status.json` (`ftp_status_store.h` + `ftp_client.cpp`; atomic `.tmp` → remove → rename, counted with `storNoteLfsWrite()`). Written **only on an upload event** (≤ `max_files` per cycle, 12 h apart) — never on a timer. Separate file from `ftp_sent.log` on purpose: the journal format is a `name:size` parser input and must not change. Restored in `ftpClientInit()` so `ok_count`/`fail_count`/`last_file`/`last_result` survive a reboot (they used to reset to 0, hiding the last cycle). |

---

## 9. Runtime engine

Compiled under `#if ENABLE_FTP_CLIENT==1` (`ftp_client.cpp`):

- `ftpClientInit()` — mutex + `ftpMon` FreeRTOS task (16384-byte stack, 30 s tick; **was 4096** — the auto cycle runs the whole deep path inside it: recursive SD scan `f_stat → sdmmc_read_sectors_dma → sdmmc_host_wait_for_event` + WiFi/FTP stream, and 4096 crashed on the first auto cycle; manual uploads run in the larger web task and never did). Called at boot from `setup_impl.h` (audit 2026-09-24 — it used to be created lazily on the first FTP page visit, so **any reboot silently killed automatic uploads** until someone opened `/ftp`; this was the "no uploads since day 20" outage on Equip15). Idempotent: safe to call from page handlers too.
- `ftpMonitorTick()` — skipped unless `enabled && auto_upload`; skipped without touching the cycle timer unless the network is up (`hasNetworkConnectivity()` — no burnt cycles/fails while WiFi reconnects after boot); runs at most every `interval_min`. **First automatic cycle waits a full interval after boot**: `ftpClientInit()` sets `s_lastCycle = millis()` so with interval = 3 h the first run happens at ~3 h of uptime, not at the first 30 s tick. This pulls FTP SD access out of the boot-critical window (SDMMC DMA × FatFS cache in PSRAM can corrupt heap in the first minutes — see `sd_status_impl.h`; loop `Task=ftpMon` «Addr=0x0» killed boards with FTP enabled at boot). Manual "Upload now" (`/api/ftp-upload-now`) still runs immediately and resets the interval. On failure: stops the cycle only for session-level errors (control connect / greeting / login, see `ftpIsConnectFailure()`); per-file failures (rejected STOR, bad data socket, unreadable file) skip that file loudly and continue with the rest of the queue instead of wedging behind one bad file. Files ≥ 4 MB (`FTP_MAX_UPLOAD_BYTES`) are skipped loudly instead (they can never upload — single STOR pass stages the whole file in RAM — and must not wedge the queue).
- Source selection — `ftpParseLocalRef()` splits the saved `ftp_local_dir` into filesystem + path (`SD:/logs` → `FTP_FS_SD` + `/logs`); legacy bare paths → `FTP_FS_AUTO`, resolved by `ftpResolveLocalFs()` (SD when `sdcardShouldUseSD()`, else PSRAM VFS). Status `source` reports `SD`/`PSRAM`/`LFS`.
- Listing — `ftpListCandidates()` walks the chosen root **recursively** (`ftpSdScan` / `ftpLfsScan` / `ftpPsramScan`, capped at `FTP_MAX_PENDING` 64 entries so `max_files` 50 is fully effective). **The FTP cycle holds the SD exclusivity mutex for the WHOLE scan+upload pass** (`ftpRunCycle` `sdMutexLock(8 s)` when the resolved source is SD; the walk no longer releases between entries — the old per-entry unlock + 1 ms yield let concurrent writers / web pages interleave with the scan and wedge the SDMMC host, the Equip15 crash-loop, HISTORIC #57). While the mutex is held, other SD users behave as designed: writers (proximity CSV/weekly appends, chart reads) wait their short (1.5–2 s) timeout and **skip**, page handlers answer `sd_busy`, the metrics walk skips its refresh. Each candidate carries its filesystem, local read path and remote mirror path (`relPath`, e.g. `logs/2026-09-19/f.csv`).
- Upload — `ftpUploadFileImpl(fs, readPath, relPath, …)`: reads from SD / PSRAM VFS / LittleFS according to the candidate's filesystem, `USER/PASS` (or anonymous), `TYPE I`, `PASV`, then **`CWD` into the configured remote base** (creating it with `MKD` when missing, see `ftpRemoteDirList()`), best-effort `MKD` of each remote subfolder level (relative when the CWD succeeded, existing dirs ignored), then `STOR` with the **relative** path (falls back to the absolute `<remote>/<relPath>` if the base could not be reached/created). Using `CWD` + relative `STOR` avoids servers that reject absolute `STOR` paths and fixes `553 Could not create file` when the target folder does not exist. **Streaming read** (`ftpStoreStream`/`FtpFileReaderCtx`/`FtpVfsReaderCtx`): file read in 8 KB chunks into an **internal-RAM** buffer (`heap_caps_malloc`, `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`), each chunk sized/trimmed and written in ≤1460 B segments — the file is never staged whole, so a 4 MB file peaks at ~8 KB of stream buffers at a time (plus a libc + lwIP/per-file allocations; heap checked + trimmed before each STOR). Chunk buffers are freed with `heap_caps_free` (never plain `free` on PSRAM). LFS paths are compiled only under `#if ENABLE_FILESYSTEM==1`.
- Delete-after — remote `SIZE` must equal local size before local delete (`SD_MMC.remove()` / LittleFS remove; PSRAM VFS files are kept local); mismatch keeps the local file.
- Logging — every test/upload result via `sendVirtualMessage(..., bypassDedup=true)` so it reaches Serial **and** the Web Serial Monitor.
- Heap — idle ~200 B DRAM; active cycle ~8–10 KB DRAM (2× lwIP sockets + task + stream chunks, now internal) + ~8 KB PSRAM (JSON + candidates + journal). STOR/stream buffers are freed with `heap_caps_free` (never plain `free` on PSRAM). No TLS (plain FTP by design).
- Concurrency — the `busy` flag is check-and-set: a test/upload cycle refuses to start while another runs (previously the test could interleave mid-cycle and both cleared the flag early). **SD ownership during the cycle**: scan + all uploads run with the SD mutex held continuously (recursive — the per-upload lock inside `ftpUploadFileImpl`/delete-after just nests); `post` releases it after the last file and before clearing `busy`, so the next 30 s writer attempt resumes the CSV appends. Page handlers in this window answer `sd_busy` instead of blocking minutes.
- Cycle visibility + stack health — every auto cycle now records its peak stack headroom (`uxTaskGetStackHighWaterMark` → `FtpStatus.monitorStackFree`, exposed in `/api/ftp-status` as `monitor_stack_free`) and pushes a Web-monitor-visible line `FTP cycle done: N file(s) (monitor stack free X B)` (`bypassDedup`). The old code logged only this loop's `FTP upload …` per file, and the cycle-done line never reached the monitor when data-activity suppression was active; the stack headroom gives an early warning before another ftpMon overflow crash.

---

## 10. i18n keys

**Added (full PT/EN coverage, `lang_en_ui.h` + `lang_pt_ui.h`):** 11 key(s) — `ftpTipExtAvi`, `ftpTipExtBin`, `ftpTipExtCsv`, `ftpTipExtFlac`, `ftpTipExtGcode`, `ftpTipExtJpg`, `ftpTipExtLog`, `ftpTipExtMp3`, `ftpTipExtNc`, `ftpTipExtTxt`, `ftpTipExtWav`. HTML fallback text is English; PT is served from the packs.

**Added 2026-10-23 (last sends):** `ftpStatusSource`, `ftpStatusLastCycle`, `ftpRecentTitle`, `ftpRecentEmpty`, `ftpColFile`, `ftpColSize`, `ftpColTime`, `ftpColResult` — EN + PT.

`homeFtp`, `ftpPageTitle`, `ftpConnTitle`, `ftpHostLbl`, `ftpHostDesc`,
`ftpPortLbl`, `ftpUserLbl`, `ftpPassLbl`, `ftpAnonLbl`, `ftpRemoteLbl`,
`ftpRemoteDesc`, `ftpLocalLbl`, `ftpLocalDesc`, `ftpAutoTitle`, `ftpEnabledLbl`,
`ftpAutoLbl`, `ftpIntervalLbl`, `ftpOnlyNewLbl`, `ftpDeleteLbl`, `ftpMaxLbl`,
`ftpFilterLbl`, `ftpFilterDesc`, `ftpStatusTitle`, `ftpTestBtn`, `ftpNowBtn`,
`ftpTestDone`, `ftpNowDone`, `ftpTipHome`, `ftpTipHost`, `ftpTipPort`,
`ftpTipUser`, `ftpTipPass`, `ftpTipRemote`, `ftpTipLocal`, `ftpTipInterval`,
`ftpTipMax`, `ftpTipFilter`, `ftpTipSave`, `ftpTipTest`, `ftpTipNow`.
Shared: `btnHome`, `btnSave`, `alertSaved`, `alertSaveError`, `emailErrorPrefix`, `accessUnknown`.
All present in `lang_en_ui.h` and `lang_pt_ui.h`; HTML fallback text is English.

---

## 11. Review findings table

| # | Finding | Severity | Status |
|---|---|---|---|
| 1 | JS test `test_ftp.js` + fixture `pages/ftp.html` added but cannot run in CI without node — validated statically (IDs, no arrow functions, i18n keys) instead. | Info | open — run `node test_ftp.js` on a dev PC |
| 2 | Firmware build not run in this session (owner runs `pio run`); FTP code paths are compile-guarded but unbuilt here. | Info | open — owner to build `n16r8` + one FTP pack |
| 3 | `ftp_sent.log` grows unbounded on busy devices (and past the 8 KB read window old entries silently re-uploaded). | Low | **fixed** — rotated at 16 KB keeping the newest ~8 KB from a line start (`ftpTrimSentLog`, atomic rewrite); `ftpSentKeepOffset()` natively tested |
| 4 | `handleFtpFoldersGet` returned an empty list unless storage mode was `STORAGE_MODE_SD` — dropdown showed only "Custom path…", and root `/` never appeared. **Fixed:** always include `/`, list SD folders whenever SD is mounted (regardless of mode) + PSRAM VFS folders, dedupe, strip double slash. **Remote path** default now shows `/` + system name when unset (`/` treated as unset). | Info | fixed |
| 5 | Local folder selector allowed free custom paths and only showed one level from SD/PSRAM (no LittleFS, no recursion). **Fixed:** prefixed `SD:/PSRAM:/LFS:` dropdown grouped by filesystem (root + primary folders of all three, no custom paths); engine uploads **recursively** with remote `MKD` mirroring; SD mutex released per scan entry so the card never stays blocked. Legacy bare values still resolve (AUTO → SD/PSRAM). | Info | fixed — needs `node test_ftp.js` + `pio run -e n16r8` validation by owner |
| 6 | **`STOR rejected: 553 Could not create file`** when the configured remote folder (e.g. default `/Equip15`) did not exist on the server — the uploader only `MKD`'d sub-folders of `relPath`, never the base folder, and always `STOR`'d an absolute path. **Fixed:** `CWD` into the remote base (best-effort `MKD` of the base and parents via `ftpRemoteDirList()` when missing), then `STOR` the relative path; absolute `STOR` kept as fallback. The connection test now also reports whether the configured remote path exists. Native test added (`test_ftp_client` "remote base directory list for MKD"). | High | fixed — needs `pio run` + real-server validation (upload to a non-existent folder succeeds, forked by server permissions) |
| 7 | **FTP uploaded 0 files — filter corrupted by the UI.** Presets are stored as `*.csv` but `ftpSetFtpExtChecks()` only treated `.csv` as "known", so `*.csv` fell into the Custom box; on the next save `ftpGetFtpExtFilter()` prefixed it again → `*.*.csv`, then `*.*.*.csv`, … (live config showed `*.*.*.json`). `ftpMatchFilter()` only understood `*.ext` / `*` / exact, so every grown pattern matched nothing. **Fixed (both sides):** the page marks `*.ext` as known and normalizes customs to `.ext` (no growth); the matcher now resolves any glob to its last `.ext`, so already-grown patterns upload without re-saving. Native test extended with `*.*.json`, `*.*.*.json`, `/config/main.json`. | High | fixed — needs `pio run` + real-server validation |
| 8 | Auto-upload scanned a folder that no longer existed after the SD log root was unified `/LOGs` → `/logs`: a persisted `ftp_local_dir = "SD:/LOGs"` still pointed at the old root, so the cycle found 0 files. **Fixed:** `ConfigManager::ensureAllFieldsExist()` normalizes any legacy `/LOGs` path (any case, with/without FS prefix) to `/logs` on load, logging the migration to the Web monitor. | High | fixed — needs `pio run` |
| 9 | **Audit 2026-09-22** — `ftpShouldUpload` built the `name:size` needle in a 96-char stack buffer: deep `relPath`s truncated → never matched → re-uploaded every cycle despite only-new. **Fixed:** zero-alloc two-step compare + `FTP_MAX_UPLOAD_BYTES` (4 MB) cap in the decision (natively tested, incl. >96-char paths). | Medium | fixed — needs `pio run` + device check (deep tree uploads once, then skips) |
| 10 | **Audit 2026-09-22** — STOR buffers (`heap_caps_malloc` PSRAM, up to 4 MB) freed with plain `free()` (wrong API contract, heap risk) + dead `ftpAllocChunk/ftpFreeChunk` pair. **Fixed:** `heap_caps_free()` everywhere (repo convention), dead pair removed. | Medium | fixed — needs `pio run` (upload + monitor device heap) |
| 11 | **Audit 2026-09-22** — files ≥ 4 MB failed every cycle and the `break`-on-first-failure wedged the whole queue behind them forever. **Fixed:** skipped loudly (`continue` + monitor-visible message) instead. | Medium | fixed — needs `pio run` |
| 12 | **Audit 2026-09-22** — `busy` race: Test-connection set `busy` without checking and `ftpUploadFileImpl` cleared it mid-cycle, so a test/cycle could interleave. **Fixed:** check-and-set in both entries; cycle owns the flag. Dead `s_uploadNow` removed. | Low | fixed — needs `pio run` |
| 13 | **Audit 2026-09-22** — `ftpSetFtpExtChecks()` matched presets by substring: filter `*.inc` wrongly checked the `.nc` box (growing the filter on next save). **Fixed:** token-exact match (jsdom-tested). | Low | fixed — needs `pio run` |
| 14 | `max_files` allows 50 but the listing capped at 32, so >32 was silently ineffective. **Fixed:** `FTP_MAX_PENDING` 32 → 64 (PSRAM vector). | Low | fixed — needs `pio run` |
| 15 | **Audit 2026-09-24 (outage: no uploads since day 20 on Equip15)** — `ftpClientInit()` was only called from the FTP page handlers, never at boot: any reboot silently killed the hourly monitor until someone opened `/ftp`. Manual "Upload now" worked (it creates the task as a side effect), masking the cause. **Fixed:** `ftpClientInit()` at boot in `setup_impl.h` (`ENABLE_FTP_CLIENT`); plus a network gate in `ftpMonitorTick()` so the first post-boot tick does not burn a cycle/fail while WiFi reconnects. Live-verified on the board: config intact, `ftp-test` OK (login+PASV+CWD), `upload-now` synced 8 files, second run 0 (journal works). | **High (outage)** | fixed — needs `pio run` + flash + watch one hourly cycle on `/ftp` status |
| 16 | **Audit 2026-09-24** — sent journal read the FIRST 8 KB (head) while the trim keeps the NEWEST ~8 KB (tail): once the journal passed 8 KB, recent entries were forgotten and those files re-uploaded every cycle (with duplicate lines) until the 16 KB rotation. **Fixed:** `ftpLoadSentList()` reads the newest `8192+512` B tail, line-aligned (`ftpSentTailWindow()` + `ftpSentKeepOffset()`, natively tested). | Medium | fixed — needs `pio run` |
| 17 | **Audit 2026-09-24** — `break` on the first upload failure: one file the server persistently rejects (e.g. 553) wedged the whole queue behind it every cycle. **Fixed:** break only on session-level failures (`ftpIsConnectFailure()`: control/greeting/login, natively tested); per-file failures skip loudly and continue. | Medium | fixed — needs `pio run` |
| 18 | **Audit 2026-09-24** — `POST /api/ftp-upload-now` always returned `success:true`, even when nothing ran (`busy` / `disabled/incomplete`). **Fixed:** `success:false` + 500 when the cycle never ran; `cycle done: 0` (nothing new) stays success. | Low | fixed — needs `pio run` |
| 19 | **Audit 2026-09-24** — stale docs: header claimed 4 KB chunked STOR streaming (`FTP_CHUNK_SIZE`, never used) and a "full STOR probe" test (actually PWD+PASV); `PAGES/ftp.md` repeated the 4 KB chunk claim. **Fixed:** header + page doc describe whole-file PSRAM staging (cap 4 MB); dead define removed. | Info | fixed |
| 20 | **Audit 2026-10-23 (board .29)** — the page's whole status was RAM-only: 10 files uploaded at 10:01, reboot at 10:02, `/api/ftp-status` then showed `last_file:"" ok_count:0 fail_count:0`, so the operator could not see what was sent or whether it failed. **Fixed:** `ftp_status_store.h` (pure, natively tested) + `/config/ftp_status.json` written only on an upload event (counted via `storNoteLfsWrite`, atomic `.tmp` swap); `ftpClientInit()` restores totals into the RAM status; `/api/ftp-status` adds `last_time_str`/`last_epoch`/`recent[]` (all legacy keys kept); the page renders a "Recent uploads" table (newest first, ✔/✘) with a readable empty state. `ftp_sent.log` untouched. | High (no diagnosis after reboot) | fixed — needs `pio run` + OTA |

---

## 12. Source map

| Concern | File |
|---|---|
| Pure helpers + runtime (PASV, filter, monitor, STOR) | `chatgpt/src/ftp_client.{h,cpp}` |
| Config getters/setters + NVS key + stem | `chatgpt/src/ConfigManager.{h,cpp}` (`ftp` section, `cfg_ftp_pwd`, `STEM_FTP`) |
| Config GET/POST (section=ftp) | `chatgpt/src/webpages/handlers_unified/config_handler.cpp` |
| Status/test/now APIs | `chatgpt/src/webpages/handlers_unified/ftp_handler.{h,cpp}` |
| HTML/JS asset (PROGMEM fragment) | `chatgpt/src/webpages/assets/page_ftp_asset.{h,cpp}` |
| Renderer | `chatgpt/src/webpages/renderers/ftp_page_renderer.{h,cpp}` |
| WiFi routes (page + APIs) | `chatgpt/src/webpages/routes/web_routes_config.cpp` |
| Ethernet mirror | `chatgpt/src/ethernet_bootstrap_routes.h` (`/ftp`), `web_shared_pages.{h,cpp}` |
| Home button (REDES) | `page_home_asset.cpp` (`%%FTP_BUTTON%%`), `home_page_renderer.h` |
| Route registry + manifests | `route_registry_api.cpp`, `selftest_handler.cpp`, `test_manifest_handler.cpp` |
| Flags + pack validation | `config_defaults.h`, `packs/pack_validator.h`, packs `pack_cam/cam_p4/waveshare.h` (FTP on); CNC packs (`pack_s3_cnc_usb/p4_cnc.h`) have FTP client + page OFF |
| i18n | `chatgpt/src/languages/lang_en_ui.h`, `lang_pt_ui.h` (ftp* keys) |
| Tests | `tests/native/test_ftp_client.cpp`, `tests/native/test_ftp_status_store.cpp`, `tests/web/test_ftp.js`, fixture `tests/web/pages/ftp.html` |
| Persisted last-sends store | `chatgpt/src/ftp_status_store.h` (pure) + `/config/ftp_status.json` I/O in `chatgpt/src/ftp_client.cpp` |
| Flags sentinel | `tests/native/test_config_defaults.cpp`, `test_pack_validator.cpp` |
