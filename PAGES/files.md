# Files — `/files`

> One-stop reference for the **file browser page**: purpose, layout, every button/input,
> client logic, server API, persistence and the firmware-side SD/PSRAM/LittleFS access.
> Written for both humans and AI agents.

---

## 1. Objective

Browse, filter, select, download, delete, rename, create and upload files across **three
storage backends**: microSD card, internal Flash (LittleFS) and PSRAM virtual filesystem.
Server-rendered table (pagination 10/page, sortable by name or date), keyboard shortcuts,
context menu and multi-select deletion.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /files?storage=sd\|flash\|psram&path=…&sort=name\|date&p=…` |
| Feature flag | `ENABLE_PAGE_FILES` (page route) — storage backends gated by `ENABLE_SDCARD`, `ENABLE_FILESYSTEM`, PSRAM VFS |
| WiFi server | `web_routes_storage.cpp:78` → `registerFilesPageRoute` (`handlers/files_page_handler.cpp:96`) |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:199` → `ethernetStreamFilesPage` |
| Home entry | Home page "🗂 Files" button |
| Session limit | **max 3 concurrent browser sessions** (per-IP, 2 min idle eviction, `charts_exempt` = never evict) → else 503 "Explorer Busy" |
| Caching | `Cache-Control: no-cache, must-revalidate` + `Pragma: no-cache` + `Expires: 0`; `CSP` header set |
| Auth | `ensureBasicAuth` + `allowHtmlClient`; SD listing acquires the SD mutex (8 s fail-fast → 503 "SD Ocupado") |

Page is a **single dynamic render** in `files_page_handler.cpp` (no separate renderer
asset). All text is injected server-side; PT hardcoded fallbacks throughout (findings).

---

## 3. Layout

```
main.page > div.wrap
 ├ [🏠 Home]                          (#nav-inicio)
 ├ H1 "🗂 File Explorer"
 ├ Green hint bar: right-click menu · Ctrl+A select all · Delete · Esc clear
 ├ Storage stats card
 │  ├ H2 "🗄 Espaço no Cartão SD" | "🧠 Espaço Virtual PSRAM" | "⚡ Espaço na Flash Interna"
 │  ├ not-mounted warning (SD mounting spinner / "iniciada em background…")
 │  └ Total / Usado / Livre + usage % bar
 ├ Files and Folders card
 │  ├ input #filterInput (filter files, onkeyup → filterFiles)
 │  ├ breadcrumb (/ a / b / c, ellipsis "…" when > 6 segments)
 │  ├ storage selector #storage-select (SD / PSRAM / Flash; shown when >1 available)
 │  ├ action grid: Select | Clear | Sort | Refresh | Back | Delete | New Folder | Rename
 │  └ table#files-table
 │      ├ thead: [selectAll checkbox] | Tipo | Nome | Tamanho
 │      └ tbody: .file-row rows (checkbox · icon · name link · size)
 └ Pagination: PAGE N/M + First/Previous/Next/Last (when >1 page)
```

Download links point to `/api/sd/download?storage=…&path=…` with a 2 s in-progress guard.

---

## 4. Inputs / buttons

### Global

| Element | ID / trigger | Behaviour |
|---|---|---|
| Filter | `#filterInput` | live filters `.file-row` by name (`filterFiles`) |
| Storage selector | `#storage-select` | `switchStorage()` reloads page with `?storage=` and resets path |
| Select all | `#selectAll` checkbox | `toggleSelectAll(this)` → selects/clears every `.file-checkbox` |
| Row checkboxes | `.file-checkbox` | toggles `window.selectedFiles[]` + row highlight |

### Action buttons

| Button | Trigger | Behaviour |
|---|---|---|
| ✅ Select | `selectAllFiles()` | check all |
| ☒ Clear | `clearAllFiles()` | uncheck all |
| 🔄 Sort | toggle URL `sort=name\|date` | reload sorted |
| 🔄 Refresh | reload with `_t` cache-buster | refresh listing |
| ⬅ Back | GET form → parent path | up one level |
| 🗑 Apagar | `deleteSelectedFiles()` | confirm → `POST /api/sd/delete-multiple` JSON `{files}` |
| 📁 Nova Pasta | `createNewFolder()` | `customPrompt` → `fetch POST /api/sd/create-dir` JSON → reload on ok |
| ✏️ Renomear | `renameSelected()` | 1 file only → `customPrompt` → `POST /api/sd/rename` (form submit) |

### Keyboard shortcuts

`Delete` → deleteSelectedFiles · `Ctrl+A` → selectAllFiles · `Esc` → clear + reset filter.

### Context menu (right-click)

On a row: Download (file) / Abrir (dir) · ✏️ Renomear · 🗑 Apagar · 📂 Mover (uses
`/api/sd/move`). On empty row: 📄 Novo ficheiro (`/api/sd/create-file`) · 📁 Nova pasta
(`/api/sd/create-dir`, JSON response). Context menu "Nova pasta" reuses `createNewFolder()`.

---

## 5. Client-side logic

Injected as inline `<script>` by the handler:

- `window._ft(k, fb)` i18n helper (auto.js `_t` → localStorage cache → fallback).
- Custom overlay dialogs: `customAlert`, `customConfirm`, `customPrompt` (project rule:
  never native dialogs).
- Selection state: `window.selectedFiles[]`, `toggleFile`, `toggleSelectAll`,
  `updateSelectAllState` (indeterminate state), `updateRowHighlight`.
- `filterFiles(term)` (name substring, case-insensitive), `getStorageFromURL()`.
- `deleteSelectedFiles()` → confirm with `{n}` count → JSON POST → reload; shows deleted
  count / errors.
- `sortByType()` — server-side sorting is authoritative; the client-side sort-by-name
  helper exists but the URL-based `sort` param drives actual ordering (date-dir aware:
  folders with `YYYY-MM-DD`/`DD-MM-YYYY` in the name sort newest-first).
- `submitPost(url, fields)` helper used by context-menu create-file/rename/move.
- Arrow-function usage (`e=>` in keyboard listener) — minor rule divergence (findings).
- Page loads `/auto.js` with `defer`.

---

## 6. Server API (page-facing)

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/sd/download?storage&path` | GET | stream file as download (SD/PSRAM/Flash), 2 s guard client-side |
| `/api/sd/delete-multiple` | POST | JSON `{files:[…]}` bulk delete (body accumulator) |
| `/api/sd/create-file` | POST | form `cwd,name,storage` → create empty file |
| `/api/sd/create-dir` | POST | form `cwd,name,storage` → mkdir; returns `{status:ok}` / `{status:error,msg}` JSON (no redirect) |
| `/api/sd/rename` | POST | form `cwd,src,dst,storage` → rename |
| `/api/sd/delete` | POST | form → delete single |
| `/api/sd/move` | POST | form → move (used by context menu) |
| `/api/sd/mount`, `/api/sd/unmount` | POST | mount/unmount SD (registered in `web_routes_storage.cpp:72-73`) |

Routes registered under `ENABLE_PAGE_FILES` in `web_routes_storage.cpp:72-79` + `registerFilesApiRoutes` (`handlers/files_api_handler.cpp:122`). All handlers enforce `ensureBasicAuth`.

---

## 7. Persistence

The page stores **no config** — it only reads/writes files on the active storage. Directory
listing caps at **1200 entries/request** (truncation banner shown). Pagination is
server-side (10 rows/page, `p` param). Storage stats come live from `sdcardTotalBytes()`,
`psramVFSGetUsedBytes()`, `littlefsTotalBytes()` depending on backend.

---

## 8. Runtime engine

- **SD access** — listings/operations hold the shared SD mutex (`acquireSDAccess` /
  `sdMutexLock`); contended access returns the "SD Ocupado" 503 page instead of blocking
  the event loop. Background mount (`sdcardBeginAsync`) is triggered when SD not mounted.
- **PSRAM VFS** — `psram_vfs.cpp` virtual filesystem when no SD; folder list at `/`.
- **Flash (LittleFS)** — `littlefs_manager.h` (`getLittleFS()`), mount on demand.
- **Session guard** — `MAX_BROWSER_SESSIONS = 3` with 2-min idle eviction
  (`cleanupExpiredSessions`), bypassed when `charts_exempt` is on.

---

## 9. i18n keys

**Firmware-side i18n (also added):** the API messages above are no longer hardcoded Portuguese in the handler — they now come from the firmware pack (`LangStrings`, `L(activeLang->…)`) and follow `system_language`. Page `<title>` and tab labels follow the same rule.

`filesShortcutHint`, `filesSpacePsram/Flash/SD`, `filesFlashNotMounted`, `filesFlashDisabled`,
`filesSDMounting`, `filesSDMountingBg`, `filesTotalLabel`, `filesUsedLabel`, `filesFreeLabel`,
`filesSDNotMounted`, `filesNotAvailMsg`, `filesPsramUnavail`, `filesSectionTitle`,
`filesStorageLabel`, `filesBtnSelect/Clear/Sort/Refresh/Back/Delete/NewFolder/Rename`,
`filesSelectFirst`, `filesDeleteConfirm`, `filesDeletedOk`, `filesDeleteError`,
`filesDeleteDetail`, `filesConnError`, `filesRenameOneOnly`, `filesCtxNewFile`,
`filesCtxNewFilePrompt`, `filesCtxNewFolder`, `filesCtxNewFolderPrompt`, `filesCtxOpen`,
`filesCtxRename`, `filesCtxRenamePrompt`, `filesCtxDelete`, `filesCtxMove`,
`filesPageLabel`, `filesDirUnavailable`, `navFiles`, `btnHome`, `loading`.

> **Almost all HTML fallbacks are PT** (`Explorador de Ficheiros`, `Apagar`, `Nova Pasta`,
> `Renomear`, `Selecionar`, `PÁGINA`, …) — systematic violation of the EN-fallback rule
> (finding FIL-1). `doc.lang='pt'`.

---

## 10. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| 1 | **All fallback text hardcoded PT** (`Explorador de Ficheiros`, `Apagar`, `Selecionar`, `PÁGINA`, dialogs `Cancelar`/`Confirmar`/`OK`, 503 pages) and `doc.lang='pt'` — **fixed:** all English fallbacks, `lang='en'`, 503 pages translated. | Medium | **fixed** |
| 2 | **`/api/sd/download` has a client-side 2 s in-progress guard only** — a second download within 2 s is silently blocked ("SD card busy. Please wait."), and there is no server-side guard beyond the SD mutex. | Low | open |
| 3 | **`sortByType()` is dead-ish**: the page reloads via `sort=name|date` URL param (server sort), while `sortByType` reorders rows client-side without persisting — two competing sort behaviours. | Low | open |
| 4 | Keyboard handler uses **arrow functions** (`e=>`), diverging from the no-arrow project rule; some event strings are English, most UI is PT. | Low | open |
| 5 | PSRAM-only boards show PSRAM as default; when PSRAM VFS disabled the selector shows `(not available)` but is still clickable → dead option. | Low | open |
| 6 | Move/delete/rename via hidden-form `POST` can navigate away and lose multi-select context (only delete uses JSON+reload). | Info | open |

---

## 11. Source map

| Concern | File |
|---|---|
| Page render + route | `chatgpt/src/webpages/handlers/files_page_handler.cpp` |
| File API handlers | `chatgpt/src/webpages/handlers/files_api_handler.cpp` |
| Route registration | `chatgpt/src/webpages/routes/web_routes_storage.cpp` |
| Ethernet page | `chatgpt/src/ethernet_bootstrap_routes.h:199` |
| SD access / mutex | `chatgpt/src/sdcard.{h,cpp}`, `sd_security.h` |
| PSRAM VFS | `chatgpt/src/psram_vfs.{h,cpp}` |
| Flash (LittleFS) | `chatgpt/src/littlefs_manager.{h,cpp}` |
| Session guard | `files_page_handler.cpp:30-94` (`MAX_BROWSER_SESSIONS`) |
| Self-test manifest | `test_manifest_handler.cpp` (`PAGE("/files", ENABLE_PAGE_FILES)`), `selftest_handler.cpp` |