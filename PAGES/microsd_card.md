# Configurar Armazenamento — `/sdcard`

> One-stop reference for the **storage configuration page** (microSD / Flash LittleFS /
> PSRAM): purpose, layout, every button/input, client logic, API endpoints, persistence
> and the firmware storage engine. Written for both humans and AI agents.

---

## 1. Objective

Configure how received data is saved: **microSD** (persistent when mounted),
**Flash LittleFS** (persistent fallback when no SD card, quota + auto-cleanup, never
touches `/config`) or **PSRAM** (volatile, temporary only — G-code, buffers). Buffer write
interval, on/off switch, file format, email report, auto-cleanup (always active: only
threshold % + keep days), and mount/unmount/refresh of the card. The page is **always
available** — on SD-less boards it doubles as the Flash/PSRAM storage status/config page.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /sdcard` |
| Feature flag | `ENABLE_PAGE_SDCARD` (`config_defaults.h:270`, default `1`) |
| WiFi server | `web_routes_storage.cpp:84` — streams via `streamSdcardPageToPrint()` |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:176` → `streamSharedSdcardPage` (`web_shared_pages.cpp:95`) |
| Home entry | Home page "💾 microSD/PSRAM" button (`home_page_renderer.h:375`, shown when flag on) |
| Auth | Basic auth (`ensureBasicAuth`) + HTML clients only (`allowHtmlClient`) |
| Caching | `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` |

Renderer order (`sdcard_page_renderer.cpp`): shell → `PAGE_SDCARD` (content) →
`PAGE_SDCARD_PART2` (save button + inline `<script>`) → `/auto.js` → init call.

---

## 3. Layout

```
main.page.sdcard > div.wrap
  ├ [🏠 Home]                       ← always first child of .wrap (project rule)
  ├ H1 "💾 Configurar Armazenamento"
  ├ Status card (blue, #3b82f6)
  │  ├ #sd_info   (mono status line left-aligned: "storage_mode | Mounted=Yes | Total=X MB | Used=X MB | Flash=used/total")
  │  └ buttons: #sd_mount | #sd_unmount | #sd_refresh   (3-col grid, max-width 400px)
  ├ "⚙️ Save received data" card (all descriptions left-aligned)
  │  ├ info box: buffer write / active / format / email / 💾 where-it-saves (SD·Flash·PSRAM) / ⚠️ PSRAM volatile / folder name
  │  └ table: #sd_write_pause_ms | #sd_active | #sd_format | #sd_email
  ├ "🗑️ Auto Cleanup" card (always active — no on/off toggle)
  │  ├ table: #sd_cleanup_threshold | #sd_keep_days
  │  └ note: "cleanup always on: above the limit the oldest goes whatever its age; below the limit only older than keep_days; /config is never deleted"
 ├ [💾 Save]  (#sd_save)
 ├ #sd_config_msg   (result line under the button)
 └ footer JS: <script src='/auto.js'> + init call
```

> Note: a "⌨️ Commands" card (EMAILDAY / LOGDAY / SENSORDAY help) exists in the asset
> (`SDCARD_COMMANDS_SECTION`) but is **never rendered** — see finding SD-3.

---

## 4. Inputs

All inputs are `<select>` elements (no text/number fields on this page).

| Column | ID | Options | Server key | Default |
|---|---|---|---|---|
| Buffer write | `sd_write_pause_ms` | `10m,30m,60m` (values in ms; minimum 10m, default 30m) | `flush_interval` (string) | `30m` |
| Active | `sd_active` | `Yes(1)` / `No(0)` | `enabled` | `Yes` |
| Format | `sd_format` | `.txt` / `.csv` / `.bin` | `format` | `.csv` |
| Email | `sd_email` | `Yes(1)` / `No(0)` | `email_send` | `Yes` |
| Auto Cleanup | _(removed — always active)_ | — | `auto_cleanup` forced `true` | always on |
| Clean when > | `sd_cleanup_threshold` | `50,60,70,80,85,90,95` % | `cleanup_threshold` | `70` |
| Keep (days) | `sd_keep_days` | `∞(0),30,90,180,360` | `keep_days` | `∞ (0)` |

Server accepts exactly what the UI offers: `flush_interval ∈ {10m,30m,60m}`;
`format ∈ {txt,csv,bin}`; `max_size_mb 1..32` (see findings — max_size is
**not exposed** on this page).

---

## 5. Buttons

| Button | ID / handler | Behaviour |
|---|---|---|
| 🏠 Home | link `/` | first child of `.wrap` |
| 📂 Mount | `#sd_mount` → `act('mount')` | `POST /api/sd-action` `action=mount` → `sdcardBeginAsync()` (background) |
| 🗃️ Unmount | `#sd_unmount` → `act('unmount')` | `POST /api/sd-action` `action=unmount` → `sdcardUnmount()` |
| 🔄 Refresh | `#sd_refresh` → `act('refresh')` | `POST /api/sd-action` `action=refresh` → re-mounts if not mounted, else OK |
| 💾 Save | `#sd_save` → `saveConfig()` | collects selects → `POST /api/sdcard-config-save`; button disabled while saving |

Mount/unmount/refresh all trigger a `load()` status refresh afterwards. Action feedback is
written to `#sd_msg` — an element that **does not exist** on the page (see finding SD-4),
so action results are only visible through the status line refresh.

---

## 6. Client-side logic (auto.js — the LIVE `initSdcardPage()`)

`auto_js.h:716` is the authoritative implementation (the renderer loads `/auto.js` after the
inline asset script, so the auto.js definition wins the global name). Logic:

1. **Init guard** `if(window.sdcardPageInitialized) return;` then grab elements
   (`sd_info`, `sd_msg`→null, `sd_config_msg`, selects, `sd_save`). If `sd_info` missing →
   abort; if `sd_save` missing → page becomes read-only (warn).
2. **`load()`** — `GET /api/sd-status` → `render(j)`:
    - `j.status!=='ok'` → `"No SD data"` (EN fallback);
    - `j.disabled` → `"SD desativado"` (never set by API — dead branch, see SD-4);
    - else line = `storage_mode | Mounted=Yes/No | Total=X MB | Used=X MB | Flash=used/total`
      (`flash_total_bytes/flash_used_bytes` shown whenever LittleFS is mounted,
      with or without SD), and tracks `isPsramMode = (j.is_psram===true)`.
    - Polled **every 10 s** (`setInterval`), paused on `visibilitychange` hidden.
3. **`loadConfig()`** — `GET /api/sdcard-config`, maps `flush_interval` string → ms value
    for `sd_write_pause_ms`, then sets `enabled`/`format`/`email_send`/
    `cleanup_threshold` (default `70`)/`keep_days` onto the selects. No
    `sd_auto_cleanup` element exists anymore (cleanup is always active).
4. **`act(a)`** — `POST /api/sd-action` (FormData `action` + `_html`), then `load()`.
5. **`saveConfig()`** — validates required selects exist, builds FormData with
   `flush_interval` (ms→string map, default `30m`), `enabled`, `format`, `email_send`,
   `auto_cleanup`, `cleanup_threshold`, `keep_days` → `POST /api/sdcard-config-save`.
   - `status==='ok'` → `customAlert` + `showSavedCard()` + config msg
     `"✅ Configuration saved!"` (EN fallback);
   - else → error `customAlert`; clears msg + re-enables button after 2 s.
6. **UI-invisible fields are preserved** (`sdExtra` cached from
   `GET /api/sdcard-config`): `structured`, `email_from_sd`, `max_size_mb`,
   `emailday_extra_emails` are re-sent on every save (SD-1 fix).

Project rules: `customAlert`/`showSavedCard` (auto.js), no arrow functions, i18n via
`window._t(k, fallback)` — **but the fallbacks here are hardcoded PT** (rule violation,
finding SD-5).

### Dead inline code (must not be trusted)

`PAGE_SDCARD_PART2` (`page_sdcard_asset.cpp:132-205`) contains a **duplicate** inline
`initSdcardPage()` that uses `bindClick`/`apiGet`/`apiPost` (undefined helpers) and posts
mount/unmount to `/api/sdcard-config-save` with `{mount:1}` — the wrong endpoint, which the
save handler ignores. It is shadowed by the auto.js version at load time and never runs.
See finding SD-2.

---

## 7. Server API

All handlers in `handlers_unified/storage_handler.cpp`; response JSON uses
`{"status":"ok"}` / `{"status":"error"}` (access page uses `success`, this page does not).

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/sd-status` | GET | `handleSdStatusGet` → `sharedWebBuildSdStatusJson()`: `{status,mounted,sd_status,storage_mode,is_psram,total_bytes,used_bytes}` (sizes only when mounted) + `{flash_total_bytes,flash_used_bytes}` whenever LittleFS is mounted |
| `/api/sdcard-config` | GET | `handleSdcardConfigGet` → `sharedWebBuildSdcardConfigJson()`: `{status,enabled,flush_interval,format,structured,email_send,email_from_sd,max_size_mb,emailday_extra_emails,auto_cleanup,cleanup_threshold,keep_days}` (`auto_cleanup` always `true`, kept for API compat) |
| `/api/sdcard-config-save` | POST | `handleSdcardConfigSavePost` (guarded `ENABLE_SDCARD`) — form fields → `SharedSdcardConfigInput` (pre-seeded from current config; only **present** fields override) → `sharedWebApplySdcardConfig()` → `saveSection("sdcard")` |
| `/api/sd-action` | POST | manual route (`web_routes_storage.cpp:52`, Ethernet `:783`) → `handleSdActionPost` → `sharedWebApplySdAction(action)` |
| `/api/storage-retry-sd` | POST | registered, **not used** by this page (retry SD storage) |
| `/api/sd-buffers` | GET | registered (streamed, HeavyApiRequestGuard), **not used** by this page |
| `/api/sd/reset-write-counter` | POST | registered, **not used** by this page (PT messages) |

### `POST /api/sdcard-config-save` field mapping & validation

```
enabled=1|0  structured=1|0  email_send=1|0  email_from_sd=1|0
flush_interval=10m|30m|60m    (minimum 10m; legacy sub-10m coerced to 10m, unknown → 400)
format=txt|csv|bin
max_size_mb=1..32          (absent → 4)
emailday_extra_emails=…    (sent by the page since the SD-1 fix)
cleanup_threshold=50..95   (absent/invalid → 400; default 70)
keep_days=∞(0)|30|90|180|360 (absent/≤0 → 0; 0 = infinity: no age limit, files stay
                              until usage crosses the threshold, then oldest goes)
```

Writes happen at `flush_interval` (default 30m, all packs) or earlier when the
capture buffer fills (`SDCARD_BUFFER_THRESHOLD_FLUSH` notifies the writer task).

`auto_cleanup` from POST is ignored — cleanup is always active (`storage_handler.cpp`).
Validation in `sharedWebApplySdcardConfig` (`web_shared_logic.cpp:469`): invalid
`flush_interval`/`format`/`max_size_mb`/`cleanup_threshold` → 400 with **EN** `msg` codes
(`invalid_flush_interval`, `invalid_format`, `invalid_max_size`,
`invalid_cleanup_threshold`). Success → `{"status":"ok","msg":"Config saved"}` (EN).

### `POST /api/sd-action` (`sharedWebApplySdAction`, `web_shared_logic.cpp:500`)

| action | effect | success message (EN) |
|---|---|---|
| `mount` | `sdcardBeginAsync()` (background) | `Mount started in background` |
| `unmount` | `sdcardUnmount()` | `Unmounted` / `Unmount failed` |
| `refresh` | mounted → OK `Updated`; else `sdcardBeginAsync()` | `Updated` / `Mount started in background` |

---

## 8. Persistence

Config section **`sdcard`** in the global config document, saved atomically via
`saveSection("sdcard")` → `/config/sdcard.json` (`ConfigManager.cpp`, 4096 B budget).

```json
{
  "sdcard": {
    "enabled": true, "flush_interval": "30m", "format": "csv",
    "structured": true, "email_send": true, "email_from_sd": true,
    "emailday_extra_emails": "", "sensorday_extra_emails": "", "logday_extra_emails": "",
    "auto_cleanup": true, "cleanup_threshold": 70, "keep_days": 0
  }
}
```

Defaults created in `ensureAllFieldsExist()` (`ConfigManager.cpp:120-136`). Note that
`emailday_extra_emails`/`sensorday_extra_emails`/`logday_extra_emails` live **in the sdcard
section** but are edited on the **Recipients** page (Day Commands tab) — so a save from this
page can clobber them — but the SD-1 fix preserves them across saves. `keep_days` defaults
to ∞ (0): no age limit — files stay until usage crosses the threshold, then the oldest
goes whatever its age. With 30/90/180/360, files older than N days also go below threshold.

### Standard SD layout (all packs — `chatgpt/src/sd_layout.h`)

Single convention enforced by central helpers (`sd_layout::sysDir/dayFile/dayDir`),
so future packs get it by construction instead of hand-building paths:

```
/
{sys}/                                system folder (getSdcardName(), e.g. /Equip15/)
  PROXIMITY_DD-MM-YYYY.csv            single-file-per-day sources (FLAT)
  RS232_DD-MM-YYYY/…                  rotated multi-file sources (SUBFOLDER)
  wkNN-oee.csv, *.chart               cross-day aggregates (FLAT, never auto-deleted)
```

Rules: one file per day → `dayFile()` (flat); rotation with several files per day →
`dayDir()` (subfolder); aggregates → plain names under `sysDir()`. Email senders
(SMTP + Google) attach an explicit file path directly (single-file mode) and
date-filter flat files in `/{sys}@date` listings (`wk*-oee.csv` always attached,
as before). Auto-cleanup candidates are dated subfolders **plus** flat day-files
(`isDayFile()`); aggregates are never candidates. Readers use flat paths only
(no legacy fallback — old `PROXIMITY_<date>/` subfolders stay readable in `/files`
but are no longer written).

---

## 9. Runtime engine

- **Data capture + buffered flush** — `runtime_storage.cpp`: per-source double buffers
  (`activeBuffer`/`flushBuffer`) swap and a dedicated `sdCardWriteTask` flush data at the
  configured `flush_interval`; everything short-circuits when `getSdcardEnabled()` is false
  (data is used for email/alerts/recipients only, not stored).
- **SD mount management** — `sdcard.{h,cpp}`: `sdcardBeginAsync()` (background mount),
  `sdcardUnmount()`, `sdcardIsMounted()`, `sdcardTotalBytes()`/`UsedBytes()`,
  `sdcardStatus()`.
- **PSRAM fallback VFS** — `psram_vfs.cpp`: when SD unavailable, `currentStorageMode`
  becomes `STORAGE_MODE_PSRAM` and data is stored in volatile PSRAM (lost on reboot).
  PSRAM holds only temporaries (G-code blob, buffers, stream staging).
- **Flash LittleFS unload (no-SD only)** — `flash_fallback_store.h` (writers),
  `flash_fallback_cleanup.h` (cleanup): PSRAM unloads to SD or LittleFS at the
  configured `flush_interval` (minimum 10m, default 30m all packs), earlier when
  the capture buffer fills (64KB coalesced), and always before reboot/OTA
  (`safeReboot()` flushes all slots — same guarantee as the SD path). Single
  funnel: pipeline entry points + MODBUS direct (same TU as the pre-reboot
  flush, one slot instance). Layouts: `/data_fallback/<sys>/<SOURCE>_<DD-MM-YYYY>[_<idx>].<ext>`
  (256KB/file rotation), `/oee/PROXIMITY_<DD-MM-YYYY>.csv` + `/oee/wk<NN>-oee.csv`
  (OEE mirrors, same bytes/layout as SD), `/sensor_history/<YYYY-MM-DD>/` (`.csv` +
  `.chart`, identical paths), `/logs/...` (log mirror, identical paths).
  Only `.csv`/`.txt`/`.log`/`.chart` ever reach LittleFS; G-code, AVI, photos and
  other blobs stay in PSRAM (or SD when present). Every write is skipped when
  free flash < 1MB (`/config` reserve); pre-NTP data stays in VFS.
- **Read order is always SD first, then LittleFS** (PSRAM stays the hot tier in
  between: sub-flush-interval data lives only there). `storageListFiles` /
  `storageReadFile` merge SD → VFS → Flash (bulk overlap skipped once the hot tier
  hits; `/oee` + `/sensor_history` never overlap and always merge). Day-email
  senders (SMTP + Google: listing, streaming, partial ranges, scan queue) and the
  charts/OEE pages (`chartHistoryReadSensorJson`, shift-history, hourly breakdown)
  all resolve the three tiers. EMAILDAY/SENSORDAY accept explicit dates without SD
  via the Flash mirror (VFS only ever holds today).
- **Flash LittleFS mirror (no-SD only)** — `log.cpp:mirrorLogToLittleFS()`: event log +
  30min buffer flush are mirrored best-effort to `/logs/...` on LittleFS (primary stays
  PSRAM VFS, readers unchanged). Skips when free flash < 1MB so `/config` space is
  never starved. Sporadic events + 4KB/30min cap → negligible wear.
- **Auto-cleanup (always active, no toggle)** — `data_sd_pipeline_impl.h:sdAutoCleanupIfNeeded()`
  for SD; `flash_fallback_cleanup.h:flashAutoCleanupIfNeeded()` for Flash (allowlist
  `/logs,/data_fallback,/sensor_history,/oee,/DEBUG`; `/config`, `pages.reg`, `/falas`,
  `/playlists`, `/library` never candidates; aggregates/`PENDING`/`nodate` skipped).
  Two tiers, same on SD/Flash/PSRAM-VFS: HARD (usage > threshold %) deletes the oldest
  entry whatever its age; SOFT (at/below threshold) deletes only entries older than
  `keep_days` (default 30, no forever). One entry per 6h (SD/Flash) or per write (VFS),
  oldest first; PSRAM VFS keeps a 95% OOM backstop. GRBL files: `grbl_handler.cpp`
  same policy.
- **Email reports** — `manage_email_impl.h` / `runtime_email.cpp`: `getSdcardEmailSend()`
  gates sending data by email; `getSdcardStructured()` adds attachments when set;
  `getEmaildayExtraEmails()` feeds the EMAILDAY command (`command_executor.cpp:550`).
- **File format** — `log.cpp:1059` and `proximity_manager.cpp` flush CSV at the configured
  flush interval; folder name = `SystemName_DD-MM-YYYY`.
- **Waveshare clock-aligned flush** — `sensors/proximity_manager.cpp:1271`: on the Waveshare pack,
  when the flush interval is `30m`, CSV writes align to fixed clock boundaries (00:00, 00:30, …) so
  proximity rows land on predictable timestamps. Between boundaries, the buffer may still flush
  early when full to prevent data loss; the next scheduled boundary write still fires on time.
  The interval parser (`stringToInterval`/`intervalToMs`) lives in `sd_rotation_manager.h`
  (legacy `SDRotationManager` object removed — the data_*.txt /logs fan-out it performed had no callers).

---

## 10. i18n keys

**Added (full PT/EN coverage, `lang_en_ui.h` + `lang_pt_ui.h`):** 1 key(s) — `storageVolatile`. HTML fallback text is English; PT is served from the packs.

`sdPageTitle`, `sdTipHome`, `sdMountBtn`, `sdTipMount`, `sdUnmountBtn`, `sdTipUnmount`,
`sdRefreshBtn`, `sdTipRefresh`, `sdSavingTitle`, `sdInfoBuf`, `sdInfoActive`, `sdInfoModes`,
`sdInfoFormat`, `sdInfoEmail`, `sdInfoPsram`, `sdInfoFolder`, `sdThBuffer/Active/Format/Email`,
`sdTipWritePause`, `sdTipActive`, `sdTipFormat`, `sdTipEmailReport`, `sdAutoCleanupTitle`,
`sdThCleanupThreshold`, `sdThKeepDays`,
`sdTipCleanupThreshold`, `sdTipKeepDays`, `sdAutoCleanupNote`, `btnSave`, `sdTipSave`,
`sdCommandsTitle`, `sdCommandsHint`, `sdCmdEmaildayDesc`, `sdCmdEmaildayDateDesc`,
`sdCmdLogdayDesc`, `sdCmdLogdayDateDesc`, `sdCmdSensordayDesc`, `sdCmdSensordayDateDesc`,
plus runtime strings `alertSaved`, `alertSaveError`, `alertConnError`, `sdStatusNoData`,
`sdStatusDisabled`, `sdStatusMounted`, `sdStatusUsed`, `sdStatusFlash`, `sdStatusFailure`,
`opYes`, `opNo`,
and `sdGettingStatus` (initial "Getting status..." cell), `sdUnmountConfirm`
("Unmount the SD card?" confirm dialog).

> The HTML fallbacks are English (good) but the auto.js **runtime fallbacks are PT**
> (now English: `"No SD data"`, `"Mounted"`, `"Configuration saved!"` … — SD-5 fix).

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Status |
|---|---|---|---|
| 1 | **Save no longer wipes fields not exposed on this page.** `saveConfig()` now preserves and re-sends `structured`, `email_from_sd`, `max_size_mb`, `emailday_extra_emails` (cached from `GET /api/sdcard-config`), so a save here no longer erases EMAILDAY extra emails or resets those settings. | High | **fixed** (auto.js + client) |
| 2 | **Dead duplicate `initSdcardPage()`** in `page_sdcard_asset.cpp:132-205` — shadowed by the auto.js version. Uses undefined helpers `bindClick`/`apiGet`/`apiPost` and wrong keys (`write_pause_ms`, `active`, `email`); posts mount/unmount to `/api/sdcard-config-save` (wrong endpoint). If auto.js ever stops defining it, the page breaks with a ReferenceError. | Medium | open |
| 3 | **Commands card never rendered.** `SDCARD_COMMANDS_SECTION` (EMAILDAY/LOGDAY/SENSORDAY help) is defined but not printed by the renderer — users can't see it here. | Medium | open |
| 4 | **`#sd_msg` element missing + dead `j.disabled` branch.** auto.js writes mount/unmount/refresh feedback to `#sd_msg` which doesn't exist (only `sd_info`/`sd_config_msg`), so action results are invisible; and `render()` checks `j.disabled` which `sharedWebBuildSdStatusJson` never emits ("SD desativado" unreachable). | Low | open |
| 5 | **PT runtime fallbacks in auto.js + server fixed.** Runtime fallbacks and server codes/messages are now English (`No SD data`, `Mounted`, `Configuration saved!`, `invalid_flush_interval`, `Config saved`, `Mount started in background`, `Counter reset`). | Low | **fixed** |
| 6 | **`keep_days` "∞" semantics.** The option is back as default (0 = infinity): no age limit — files stay until usage crosses the threshold, then the oldest goes whatever its age to hold it. With 30/90/180/360, older files also go below threshold. | Medium | **fixed** |
| 7 | Page shows status of buffer only via `/api/sd-buffers` (exists but unused here); no buffer-usage widget on the page. | Info | open |
| 8 | **Storage page unified (Flash + always-on cleanup).** Title/button renamed to "Configurar Armazenamento"; descriptions left-aligned with SD·Flash·PSRAM modes box (`sdInfoModes`, PT+EN); autoclean toggle removed (always active, only threshold % default 70 + keep days); `/api/sd-status` + status line show LittleFS usage; Flash cleanup allowlist never touches `/config`; no-SD logs mirrored best-effort to LittleFS. Follow-up: full no-SD unload (`/data_fallback`, `/oee`, sensor `.chart`), read order SD → PSRAM → Flash in day-emails + charts/OEE pages, buffer-write minimum 10 m (default 30 m all packs), explicit EMAILDAY/SENSORDAY dates via the Flash mirror. | — | **fixed** |

---

## 12. Source map

| Concern | File |
|---|---|
| Renderer | `chatgpt/src/webpages/renderers/sdcard_page_renderer.{h,cpp}` |
| HTML assets (PROGMEM) | `chatgpt/src/webpages/assets/page_sdcard_asset.{h,cpp}` |
| Routes (WiFi + Ethernet) | `chatgpt/src/webpages/routes/web_routes_storage.cpp`, `ethernet_bootstrap_routes.h:176` |
| HTTP handlers | `chatgpt/src/webpages/handlers_unified/storage_handler.cpp` |
| Shared JSON builders / actions | `chatgpt/src/web_shared_logic.{h,cpp}` |
| Live client JS | `chatgpt/src/webpages/auto_js.h:716` |
| Config storage | `chatgpt/src/ConfigManager.{h,cpp}` (`sdcard` section) |
| Storage engine / PSRAM / cleanup | `chatgpt/src/runtime_storage.cpp`, `psram_vfs.cpp`, `flash_fallback_cleanup.h`, `log.cpp` (LittleFS mirror), `sdcard.{h,cpp}` |
| Email / day commands | `manage_email_impl.h`, `runtime_email.cpp`, `command_executor.cpp:550` |
| Self-test manifest | `test_manifest_handler.cpp:264` (`PAGE("/sdcard", ENABLE_PAGE_SDCARD)`) |