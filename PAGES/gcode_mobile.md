# Mobile G-code wizard — `/m` (1 Send · 2 Frame · 3 Play)

> Three-step phone wizard for the CNC packs. Vertical, **one action per screen and
> no vertical scroll**: the three screens live in one route and the footer arrows
> swap them (`#1`/`#2`/`#3` keep the step across a reload).
>
> The PC page (`/grbl-controller`) is a different asset, renderer and route and is
> deliberately untouched by this page.

---

## 1. Objective

Run a job from a phone with three single-action screens:

1. **Send** — choose a G-code file, upload it to the controller's PSRAM workspace
   and keep its XY bounds (computed in the browser while reading the file).
2. **Frame** — outline that area on the machine before cutting (spindle 5%).
3. **Play** — start the stored job (`psram-stream`) and stop it (`cancel`).

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
| Entry point | **`GET /grbl-controller` from a phone** redirects here (302) — decided by `ua_classify.h` on the User-Agent only; desktop and tablets keep the full controller, `?desk=1` forces it from any device. No home-page tile (that page is shared by every pack). |
| i18n | English fallback text only, no `data-i18n` keys yet — keys arrive with the complete wizard |

Renderer: `renderers/gcode_mobile_page_renderer.cpp` → `assets/page_gcode_mobile_asset.cpp`
(single self-contained PROGMEM blob: CSS + HTML + JS).

---

## 3. Layout

Fixed viewport, single column (`#gm`, `height:100dvh`, `overflow:hidden`,
`html,body{overflow:hidden}`), so the page can never scroll:

```
header   steps ●○○ · title (Send|Frame|Play) · machine badge (online/offline)
screen   the active step only (flex:1, min-height:0, overflow:hidden)
  S1     dashed tap target — file name + size
  S2     job name + measured area "X[..] Y[..] W x H" + hint
  S3     job name + machine state + "sent / total" lines
msg      one line of status / error (per-step default text)
bar      upload progress (hidden unless transferring)
primary  SEND | FRAME | START (54 px, disabled while busy/no file/no control)
STOP     appears on step 3 only while the job runs
nav      ← · n / 3 · →
```

A `@media (max-height:560px)` branch shrinks the title, icon and button so small
phones still fit without scrolling.

---

## 4. Inputs

| Control | Type | Behaviour |
|---|---|---|
| File picker | `<input type=file accept=".gcode,.nc,.ngc,.txt,text/plain">` (hidden, opened by the card) | name sanitised to a safe basename, size shown, >4 MB refused |
| Primary | button | SEND (upload) / FRAME (outline) / START (stream), one per step |
| STOP | button | `cancel`; visible only on step 3 while a job runs |
| ← → | arrows | previous/next step; `→` waits for a job on step 1, disabled on 3 |

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
- `upload()` — `XMLHttpRequest` (progress) to
  `/api/grbl/psram-upload?name=&sid=&token=` with `Content-Type: application/octet-stream`,
  `X-Filename`, `X-Filesize`. Success → `Job ready: <path> (<bytes>)`; otherwise a mapped
  message (`file_too_large`, `upload_empty`, `psram_store_failed`, `vfs_budget_exceeded`,
  `alloc_failed`, `no_control`, HTTP status).
- `boundsFromText()` — XY bounds from the picked file (G-code comments and Z-only moves
  ignored), shown on step 2 as `X[min..max] Y[min..max] W x H`.
- `runFrame()` — refuses unless the machine is idle, then posts one batch to
  `/api/grbl/send` 150 ms apart: `M5, G90, G21, G1 Xmin Ymin F1200, M3 S<5%>,`
  the four corners, `M5` (same sequence as the PC page's frame).
- `startJob()` / `stopJob()` — `/api/grbl/psram-stream` (`slot=0`) and `/api/grbl/cancel`.
  On step 3 the page polls `/api/grbl/status` once per second (state + `lines_sent/lines_total`).
- On load: control token, `/api/grbl/psram-file` (a job already on the controller keeps
  steps 2/3 usable after a reload) and the machine badge (one-shot).

---

## 7. Server API

| Endpoint | Method | Used for |
|---|---|---|
| `/api/grbl/control-token` | GET | write token + `can_write` |
| `/api/grbl/status` | GET | machine badge (`usb_running`) |
| `/api/grbl/psram-upload` | POST | stores the uploaded file in the PSRAM workspace (`file_path`, `bytes`) |
| `/api/grbl/psram-file` | GET | job already on the controller (`has_file`, `name`, `size`) |
| `/api/grbl/send` | POST | one outline line at a time (step 2) |
| `/api/grbl/psram-stream` | POST | starts the stored job (`slot=0`) |
| `/api/grbl/cancel` | POST | stops the job |

---

## 8. Persistence

None on the board: the page stores only its own `sid` in the browser. The uploaded
job lives in the controller's PSRAM workspace (same store the PC page uses) until it
is replaced or cleared.

---

## 9. Runtime engine

None on steps 1–2. Step 3 polls `/api/grbl/status` every 1 s while it is on screen
(the timer is cleared when leaving the step), and the upload itself is one XHR with progress.

---

## 10. i18n keys

38 `gm*` keys in `lang_en_ui.h` + `lang_pt_ui.h` (kept in sync; `validate_i18n.py` green):
titles `gmTitle1..3`, actions `gmSend`/`gmFrameRun`/`gmPlay`/`gmStop`, per-step hints
`gmStep1..3Hint`, frame texts `gmFrame*`, play texts `gmPlay*`, badges `gmOnline`/`gmOffline`,
errors `gmErr*` (`{code}`, `{name}`, `{path}`, `{size}` placeholders) and `gmNextTip`.
Static labels use `data-i18n`; runtime messages go through `t()/tf()` with English
fallbacks so the page is readable before the dictionary loads.

---

## 11. Review findings table

| # | Finding | Status |
|---|---|---|
| 1 | No scroll on any of the three steps at 320×568, 360×640, 390×844, 412×915 (12/12 combinations: `scrollHeight == clientHeight`, `overflow:hidden` on body and screen) | verified |
| 2 | Frame uses the browser-computed bounds, so a job uploaded in a previous session needs the file picked again (the controller only reports name/size) | by design for now |
| 3 | Entry point: `GET /grbl-controller` from a phone now redirects here (UA-based, `?desk=1` escape); tablets and desktop are untouched | fixed (GM-4) |
| 4 | Upload is limited to the 4 MB PSRAM workspace; larger jobs must go through the PC page's SD path | by design for now |
| 5 | Frame runs at a fixed 5% / F1200 (no power/feed UI on the phone) | by design for now |

---

## 12. Source map

| Piece | File |
|---|---|
| Asset (CSS + HTML + JS) | `chatgpt/src/webpages/assets/page_gcode_mobile_asset.cpp/.h` |
| Renderer | `chatgpt/src/webpages/renderers/gcode_mobile_page_renderer.cpp/.h` |
| Route | `chatgpt/src/webpages/routes/web_routes_monitor.cpp` (`server.on("/m", …)`, CNC pack guard) |
| i18n | `chatgpt/src/languages/lang_en_ui.h`, `lang_pt_ui.h` (`gm*`), regenerated `assets/generated_gzip_assets.h` |
| Tests | `tests/web/test_gcode_mobile.js` (16 cases), fixture `tests/web/pages/gcode_mobile.html` (generated by `extract.py`) |
