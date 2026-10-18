# Dual File Manager — `/dualfile`

> One-stop reference for the **two-panel file manager page**: purpose, layout, every
> button/input, client logic, API endpoints and the firmware-side SD/PSRAM/LittleFS access.
> Written for both humans and AI agents.

---

## 1. Objective

A **Norton-Commander-style dual-pane file manager** (left + right panels) for copying,
moving, viewing, editing, renaming, creating, deleting, downloading and uploading files
across **SD**, **Flash (LittleFS)** and **PSRAM VFS**. Keyboard-first: F-keys, cursor
navigation, quick search, wildcard selection and a command line. Served as a standalone
page (no auto.js dependency — self-contained inline JS).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /dualfile` |
| Feature flag | `ENABLE_DUALFILE` (`config_defaults.h:282`, **default `0`**) |
| WiFi server | `web_routes_dualfile.cpp:14` — `sendDualFilePageResponse` (PROGMEM `beginResponse_P`, `Cache-Control: no-cache`) |
| Ethernet (W5500) | not registered on Ethernet (page is WiFi/manual-route only) |
| Home entry | Home page "🗂 Dual File" button (`home_page_renderer.h:277`, `target='_blank'`) |
| Auth | `ensureBasicAuth` + `allowHtmlClient` |
| Session exemption | ALWAYS exempt (included by decision; no `auto.js` on the page) — never kicked by time |

Page is a **single PROGMEM blob** `PAGE_DUALFILE` (`page_dualfile_asset.h`, ~1151 lines)
with all CSS + JS inline. `lang="pt"`, no `data-i18n` except tooltips (`dfTip*`).

---

## 3. Layout

```
body (flex column, full-height)
 ├ #toolbar
 │  ├ L: path input (#path-input-left) + refresh (#refresh-left-btn)
 │  ├ auto-refresh indicator (#auto-refresh-indicator, "⟳ AR")
 │  └ R: path input (#path-input-right) + refresh (#refresh-right-btn)
 ├ #panels
 │  ├ #panel-left.panel.active
 │  │   ├ header: path label, storage select #stor-left (sd/flash/psram), refresh
 │  │   ├ filter: #filter-left + clear
 │  │   ├ list #list-left (table: [sel] Name | Size | Date)
 │  │   └ footer #footer-left ("N dirs, M files" + free KB)
 │  └ #panel-right.panel  (mirror: #stor-right, #filter-right, #list-right, #footer-right)
 ├ #fkey-bar  → F1 Help | F2 Menu | F3 View | F4 Edit | F5 Copy | F6 RenMov | F7 MkDir | F8 Delete | F9 Menu | F10 Exit
 │   (mobile ≤600px: panels stack vertically; fkey-bar scrolls horizontally,
 │   table min-width 600px so key labels never wrap mid-word)
 ├ #cmd-line  → prompt ">" + #cmd-input
 ├ #menu-bar  → File | Command | Options | Right (dropdowns)
 ├ #toast, #loadingOverlay, #help-modal, #viewer-modal, #upload-progress
 └ hidden #file-upload-input (multiple)
```

Row content: `*` when selected, icon per extension (📁 dir, ⚙ gcode/nc/ngc, 📄 txt/log/csv/json,
📦 zip/gz/tar, ⚙ bin/hex, 📄 other), name, size (files), date `YYYY-MM-DD HH:MM`.

---

## 4. Inputs / buttons

| Element | ID | Behaviour |
|---|---|---|
| Panel path | `#path-input-left` / `-right` | Enter → navigate to typed path; Esc blurs |
| Panel storage | `#stor-left` / `#stor-right` | select sd/flash/psram → reset to `/` and reload |
| Panel refresh | `#refresh-left/right` + `#refresh-left-btn/-right-btn` | reload panel list |
| Panel filter | `#filter-left/right` | live name filter; Esc / ✕ clears |
| Command line | `#cmd-input` | Enter runs `processCommand`; ↑/↓ history (persisted in `localStorage['dualfile_cmd_history']`, max 50) |
| File upload | `#file-upload-input` | multiple files → `/api/dualfile/upload` with XHR progress bar |

### Function keys

| Key | Action |
|---|---|
| F1 | help modal |
| F2 / F9 | File menu |
| F3 | view file (text, 64 KB) |
| F4 | edit file (text ≤ 64 KB, Ctrl+S save) |
| F5 | copy selected → other panel |
| F6 | rename / move |
| F7 | new folder |
| F8 | delete selected |
| F10 | `window.close()` (exit) |

### Keyboard

`Tab` switch panel · `↑↓` cursor · `PgUp/PgDn` ±10 · `Home/End` · `Enter` open ·
`Backspace` parent · `Insert`/`Space` select · `+`/`-` wildcard select · `*` invert ·
`Ctrl+R` refresh · `Ctrl+L` left · `Ctrl+U` swap panels · `Alt+F1/F2` cycle storage ·
`Esc` close modal/menu · `[a-z0-9]` quick search (NC-style, 1 s window).

### Command line commands

`cd <path>` · `copy` · `move` · `del` · `mkdir` · `view` · `edit` · `help` · `exit`.

---

## 5. Client-side logic

- **State**: `state.{left,right}` each `{path,storage,entries[],selected{},sortBy,sortAsc,cursor,filter,storageInfo}` + `active` (left/right) + `cmdHistory` + `quickSearch`.
- **`loadPanel(side)`**: `GET /api/dualfile/list?path=&storage=` (+`filter=`), guards stale responses with a per-side request id; shows error via `customAlert` (toast).
- **`renderPanel(side)`**: builds table; dirs first, sort by name/size/time/ext (`toggleSort`); highlights cursor + selection rows.
- **Selection**: `toggleSort`, `onRowClick`, `onRowDblClick` (dir → enter, file → `viewFile`).
- **`viewFile` / `openEditor`**: `GET /api/dualfile/view?mode=text&limit=65536`; editor refuses >64 KB; `saveEditorFile` → `POST /api/dualfile/write`.
- **`copySelected` (F5)**: copies each selected/cursor file via `POST /api/dualfile/copy` (src/dst + src_storage/dst_storage), refreshes both panels when done.
- **`renameFile` (F6)**: `prompt()` → `POST /api/sd/rename` (`json=1`).
- **`createDir` (F7)**: `prompt()` → `POST /api/sd/create-dir` (`json=1`).
- **`deleteSelected` (F8)**: confirm → `POST /api/sd/delete` (`json=1`) per item.
- **`uploadFile`**: XHR `POST /api/dualfile/upload?path=&storage=` per file, sequential with progress bar.
- **`swapPanels` (Ctrl+U)**: exchanges both panels' full state.
- **`cycleStorage` (Alt+F1/F2)**: rotates sd→flash→psram.
- **Auto-refresh** (Options menu): `setInterval` reload of both panels every 30 s + `⟳ AR` indicator.
- Menus via `menuDefs` + `openMenu`/`closeMenu`; help modal with key reference.
- i18n via `t(k,fb)` helper (window._t → localStorage cache → fallback) for the few `dfTip*` titles; **most UI strings are hardcoded EN** ("Loading...", "Filter...", "Delete N item(s)?", etc.); dialogs use native `prompt()`/`confirm()` (project-rule divergence — finding DF-2).

---

## 6. Server API

Routes in `web_routes_dualfile.cpp`; handlers in `handlers_unified/dualfile_handler.cpp`
(guarded `ENABLE_DUALFILE`; 404 stubs when off). All responses `{success:…, error?}`.

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/dualfile/list?path&storage&filter` | GET | list dir → `{success,path,parent,separator,entries:[{n,d,s,t}],total_kb,used_kb,free_kb}`; PSRAM listing returns `"PSRAM VFS listing not supported"` (Phase 1); path-safe via `SDSecurity`; 429 rate limit (1 s) / 503 SD busy |
| `/api/dualfile/copy` | POST | `src&src_storage&dst&dst_storage` → stream copy ≤ 8 MB; `"File too large (>8MB)"`; watchdog yield every 10 ms |
| `/api/dualfile/view?path&storage&mode=text|hex&offset&limit` | GET | read file (default limit 4096, max 65536) → `{content,hex,ascii,total_size,truncated}`; errors `ENOENT`/`EISDIR`/`OOM` |
| `/api/dualfile/write` | POST | `path&storage&content` (urlencoded) → write ≤ 64 KB (`413` over) |
| `/api/dualfile/upload?path&storage` | POST | body accumulator (manual route) → streaming write with progress; SD/Flash only |
| `/api/sd/rename` | POST | reused for rename (F6), `json=1` |
| `/api/sd/create-dir` | POST | reused for mkdir (F7), `json=1` |
| `/api/sd/delete` | POST | reused for delete (F8), `json=1` |

`isSdOperationAllowed()` rate-limits SD operations to 1/s (429 otherwise); SD access uses
`acquireSDAccess`/`releaseSDAccess`; Flash writes increment the flash write counter.

---

## 7. Persistence

- **Command history** persisted in browser `localStorage['dualfile_cmd_history']`.
- File operations read/write the active storage directly (SD / LittleFS / PSRAM); no config
  files are written by this page.

---

## 8. Runtime engine

- **Storage resolution** — `resolveStorageType(req)` / `getFileSystem(req)`: prefers the
  requested storage, falls back to SD (mounted) → PSRAM → Flash depending on flags.
- **SD access** — guarded by `acquireSDAccess` (8 s) + 1-op-per-second rate limit; PSRAM
  mode short-circuits SD requests when `currentStorageMode == STORAGE_MODE_PSRAM`.
- **Flash writes** — counted via `flashIncrementWriteCount`.
- **Path safety** — `SDSecurity::normalizePath` + `isPathSafe` on every read/write/list
  (traversal blocked).
- **Upload streaming** — `DualFileUploadCtx` accumulates body in chunks straight to the open
  `fs::File` (no RAM copy), closes + releases the SD lock on completion.

---

## 9. i18n keys

`dfTipRefresh`, `dfTipStorage`, `dfTipFilter`, `dfTipFilterClear`, `dfTipFKey`, `dfTipUpload`,
`dfTipCmd` (tooltips only). **All other strings are hardcoded EN**; the page is `lang="pt"`
and native dialogs are used (`prompt`/`confirm`) — see findings.

---

## 10. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| 1 | **`/api/dualfile/list` on PSRAM returns "PSRAM VFS listing not supported"** but the UI offers a PSRAM storage option and cycles into it — dead/broken option. | Medium | Implement PSRAM listing (mirror `psramVFSListFiles` like `/files`) or hide the option. |
| 2 | **Native `prompt()`/`confirm()` used** (rename, mkdir, delete, wildcard, command help) — violates the `customAlert`/`customConfirm` project rule (page has its own toast-based `customAlert`/`customConfirm` but the native ones bypass it). | Medium | Replace native dialogs with the page's overlay dialogs. |
| 3 | Page is `lang="pt"` but **all UI text is hardcoded EN** — **fixed:** `lang="en"`. | Low | **fixed** |
| 4 | **Command history and menus are English-only** with no i18n keys; `menuDefs` labels are raw strings. | Low | Add i18n keys if translations are desired. |
| 5 | `customConfirm` is defined but delete uses the **native** `confirm()` path (only `deleteSelected`); the overlay `customConfirm` is effectively unused. | Low | Route confirmations through the overlay. |
| 6 | Copy is limited to **8 MB** with no UI hint; >8 MB files silently fail per file. | Info | Document or stream in chunks. |
| 7 | Page not registered on Ethernet; `registerDualFileRoutes` only adds the WiFi manual route. | Info | Note: dual-file is WiFi-only by design. |

---

## 11. Source map

| Concern | File |
|---|---|
| Page HTML/CSS/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_dualfile_asset.h` |
| Renderer | `chatgpt/src/webpages/renderers/dualfile_page_renderer.{h,cpp}` |
| Routes | `chatgpt/src/webpages/routes/web_routes_dualfile.{h,cpp}` |
| API handlers | `chatgpt/src/webpages/handlers_unified/dualfile_handler.{h,cpp}` |
| SD access / safety | `chatgpt/src/sdcard.{h,cpp}`, `sd_security.h` |
| Flash (LittleFS) | `chatgpt/src/littlefs_manager.{h,cpp}` |
| PSRAM VFS | `chatgpt/src/psram_vfs.{h,cpp}` |
| Home entry | `chatgpt/src/webpages/renderers/home_page_renderer.h:277` |
| Self-test manifest | `test_manifest_handler.cpp:293`, `selftest_handler.cpp:90,162` |