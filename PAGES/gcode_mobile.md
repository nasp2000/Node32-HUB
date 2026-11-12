# Mobile G-code wizard — `/m` (step 1: Send G-code)

> First page of the phone wizard for the CNC packs. Vertical, **one action per
> screen and no vertical scroll**. Steps 2 (*Frame*) and 3 (*Play*) are separate
> pages and are **not built yet** — the arrow is disabled and the counter reads
> `1 / 3`.
>
> The PC page (`/grbl-controller`) is a different asset, renderer and route and is
> deliberately untouched by this page.

---

## 1. Objective

Put a G-code job on the controller from a phone in one gesture: choose a file,
upload it to the controller's PSRAM workspace, confirm it is stored. Starting the
job belongs to step 3, so this page never sends `psram-stream`/`stream`.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /m` |
| Feature flag | `ENABLE_GRBL`, route inside the CNC-pack guard (`PACK_P4_CNC` / `PACK_S3_CNC`) |
| WiFi server | `web_routes_monitor.cpp` (next to `/grbl-controller`), `streamGcodeMobilePageToPrint()` |
| Ethernet | not registered: the CNC packs have no Ethernet build (`ENABLE_ETHERNET` off) |
| Auth / caching | `ensureBasicAuth` + `allowHtmlClient`; `ETag = FIRMWARE_BUILD`, `private, max-age=15, must-revalidate` |
| Session | **not** never-kick: it is a static page (one-shot GETs, no polling), so the normal static rules apply |
| Home entry | none yet (kept out on purpose: the home page is shared by every pack) |
| i18n | English fallback text only, no `data-i18n` keys yet — keys arrive with the complete wizard |

Renderer: `renderers/gcode_mobile_page_renderer.cpp` → `assets/page_gcode_mobile_asset.cpp`
(single self-contained PROGMEM blob: CSS + HTML + JS).

---

## 3. Layout

Fixed viewport, single column (`#gm`, `height:100dvh`, `overflow:hidden`,
`html,body{overflow:hidden}`), so the page can never scroll:

```
header  steps ●○○ · title · machine badge (online/offline)
pick    dashed tap target (flex:1, min-height:0) — file name + size once chosen
msg     one line of status / error
bar     upload progress (hidden until a transfer starts)
SEND    full-width primary (54 px), disabled until a file is chosen
nav     1 / 3 · "2 · Frame" · → (disabled)
```

A `@media (max-height:560px)` branch shrinks the title, icon and button so small
phones still fit without scrolling.

---

## 4. Inputs

| Control | Type | Behaviour |
|---|---|---|
| File picker | `<input type=file accept=".gcode,.nc,.ngc,.txt,text/plain">` (hidden, opened by the card) | name sanitised to a safe basename, size shown, >4 MB refused client-side |
| SEND | button | uploads; disabled while busy, without write control, or with no file |

---

## 5. Buttons

| Button | Action |
|---|---|
| Card ("Choose a file") | opens the native file picker |
| **SEND** | `POST /api/grbl/psram-upload` with the file as body |
| → | disabled: steps 2/3 are other pages (not built yet) |

---

## 6. Client logic

- `sid` — per-tab id in `sessionStorage` (`m-<ts>-<rand>`), sent with every write.
- On load (one shot each, no polling): `GET /api/grbl/control-token?sid=` → `{token, can_write}`
  and `GET /api/grbl/status` → `usb_running` for the badge. `can_write:false`
  keeps SEND disabled and explains that another client holds control.
- `send()` — `XMLHttpRequest` (progress events) to
  `/api/grbl/psram-upload?name=&sid=&token=` with `Content-Type: application/octet-stream`,
  `X-Filename`, `X-Filesize`. Success (200 + `success:true`) → `Job ready: <path> (<bytes>)`;
  any other reply → mapped message (`file_too_large`, `upload_empty`,
  `psram_store_failed`, `vfs_budget_exceeded`, `alloc_failed`, HTTP status).
- No job is started here: `psram-stream` / `stream` are step 3.

---

## 7. Server API

| Endpoint | Method | Used for |
|---|---|---|
| `/api/grbl/control-token` | GET | write token + `can_write` |
| `/api/grbl/status` | GET | machine badge (`usb_running`) |
| `/api/grbl/psram-upload` | POST | stores the uploaded file in the PSRAM workspace (`file_path`, `bytes`) |

---

## 8. Persistence

None on the board: the page stores only its own `sid` in the browser. The uploaded
job lives in the controller's PSRAM workspace (same store the PC page uses) until it
is replaced or cleared.

---

## 9. Runtime engine

None — no timers, no polling. The upload is a single XHR with progress.

---

## 10. i18n keys

None yet (English fallback text in the asset). Add `gm*` keys to `lang_en_ui.h` and
`lang_pt_ui.h` together with steps 2–3, when the whole wizard can be translated in one pass.

---

## 11. Review findings table

| # | Finding | Status |
|---|---|---|
| 1 | No scroll at 320×568, 360×640, 390×844, 412×915 — `scrollHeight == clientHeight` measured in Chromium with mobile emulation | verified |
| 2 | Steps 2 (Frame) and 3 (Play) missing; the arrow is intentionally disabled | open (next runs) |
| 3 | No entry point yet: the PC page and the shared home page were kept untouched, so `/m` is typed manually | open (decide in a later run) |
| 4 | Upload is limited to the 4 MB PSRAM workspace; larger jobs must go through the PC page's SD path | by design for now |

---

## 12. Source map

| Piece | File |
|---|---|
| Asset (CSS + HTML + JS) | `chatgpt/src/webpages/assets/page_gcode_mobile_asset.cpp/.h` |
| Renderer | `chatgpt/src/webpages/renderers/gcode_mobile_page_renderer.cpp/.h` |
| Route | `chatgpt/src/webpages/routes/web_routes_monitor.cpp` (`server.on("/m", …)`, CNC pack guard) |
| Tests | `tests/web/test_gcode_mobile.js`, fixture `tests/web/pages/gcode_mobile.html` (generated by `extract.py`) |
