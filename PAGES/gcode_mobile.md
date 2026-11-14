# Mobile G-code wizard — `/m` (1 Send · 2 Preview · 3 Frame · 4 Play · 5 Jog)

> Five-step phone wizard for the CNC packs. Vertical, **one action per screen and
> no page scroll**: the five screens live in one route and the footer arrows (or a
> horizontal swipe) swap them (`#1`…`#5` keep the step across a reload).
> **Navigation never waits for a job** — every screen can be reached with nothing
> loaded, so the operator can jog, unlock or read the machine before choosing a file.
>
> The PC page (`/grbl-controller`) is a different asset, renderer and route and is
> deliberately untouched by this page.

---

## 1. Objective

Run a job from a phone without ever touching the PC page:

1. **Send** — pick the file from the **SD card list** (streamed straight from the
   card, any size) or from the phone (uploaded to the PSRAM workspace, ≤4 MB).
2. **Preview** — see the toolpath on a 2D canvas before cutting.
3. **Frame** — outline the job area on the machine (spindle at 5%).
4. **Play** — START / PAUSE / RESUME / STOP.
5. **Jog** — X/Y/Z jog with a step selector plus the essential GRBL buttons
   (HOME, UNLOCK, ZERO, HOLD/PAUSE, RESUME, CANCEL/STOP, RESET).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /m` |
| Feature flag | `ENABLE_GRBL`, route inside the CNC-pack guard (`PACK_P4_CNC` / `PACK_S3_CNC`) |
| WiFi server | `web_routes_monitor.cpp` (next to `/grbl-controller`), `streamGcodeMobilePageToPrint()` |
| Ethernet | not registered: the CNC packs have no Ethernet build (`ENABLE_ETHERNET` off) |
| Auth / caching | `ensureBasicAuth` + `allowHtmlClient`; `ETag = FIRMWARE_BUILD`, `private, max-age=15, must-revalidate` |
| Session | **not** never-kick: the page owns no long poll by itself (1 s status only while steps 4/5 are on screen) |
| Entry point | **`GET /grbl-controller` from a phone** redirects here (302) — decided by `ua_classify.h` on the User-Agent only; desktop and tablets keep the full controller, `?desk=1` forces it from any device. No home-page tile (that page is shared by every pack). |
| i18n | `gm*` keys in `lang_en_ui.h` + `lang_pt_ui.h`; static labels use `data-i18n`, runtime messages go through `t()/tf()` with English fallbacks |

Renderer: `renderers/gcode_mobile_page_renderer.cpp` → `assets/page_gcode_mobile_asset.cpp`
(single self-contained PROGMEM blob: CSS + HTML + JS).

---

## 3. Layout

Fixed viewport, single column (`#gm`, `height:100dvh`, `overflow:hidden`,
`html,body{overflow:hidden}`), so the page can never scroll:

```
header   steps ●○○○○ · title (Send|Preview|Frame|Play|Jog) · machine badge · ⚡ USB
screen   the active step only (flex:1, min-height:0, overflow:hidden)
  S1     tabs [SD card | Phone]
           SD    : directory line + ↻ reload + scrollable list (files/folders/..)
           Phone : dashed tap target — file name + size
                   + destination chips [Save on the SD card | PSRAM (4 MB)]
  S2     <canvas> toolpath + info line (points / partial) + ↻ reload
  S3     job name + measured area "X[..] Y[..] W x H" + hint
  S4     job name + machine state + progress bar + "sent / total · %" + ETA
         alarm block (message + UNLOCK / RESUME JOB) — only while in alarm
         overrides F− F+ 100% S− S+
         [PAUSE][RESUME][STOP]
  S5     state + MPos + step chips (0.1/1/10/50) + jog pad
         [HOME][UNLOCK][RESET] · [ZERO X][ZERO Y][ZERO Z] · [HOLD][RESUME][CANCEL]
         MDI line (command + ➤)
msg      one line of status / error (per-step default text)
bar      upload / measure / preview progress (hidden unless transferring)
primary  USE|SEND · (none on Preview) · FRAME · START · (none on Jog)
nav      ← · n / 5 · →
```

The SD list and the Jog screen scroll **inside** their screen (`overflow-y:auto`)
so a small phone never pushes a control off-screen; everything else is fixed.
A `@media (max-height:620px)` branch shrinks the title, cards, jog pad and buttons.
Machine state is colour-coded (RUN green, HOLD/DOOR amber, ALARM red) on both
the Play and Jog screens.

---

## 4. Inputs

| Control | Type | Behaviour |
|---|---|---|
| SD list row | button | file → select · folder → open · `↩ Parent folder` → up |
| SD reload `↻` | button | re-reads the current directory |
| File picker | `<input type=file accept=".gcode,.nc,.ngc,.txt,text/plain">` (hidden) | phone source: name sanitised to a safe basename, size shown |
| Destination chips | buttons | **SD card** (default, no 4 MB cap) or **PSRAM** (4 MB workspace) |
| Jog step chips | buttons | 0.1 / 1 / 10 / 50 mm (default 1) |
| Jog pad | 6 buttons | X±, Y±, Z± (Z hidden/disabled on a 2-axis machine) |
| Override | 5 buttons | feed −10 / +10, spindle −10 / +10 (10…200 %, applied together), middle label shows the feed % |
| Machine buttons | buttons | HOME, ZERO X/Y/Z and RESET arm on the first tap (4 s window); UNLOCK/HOLD/RESUME/CANCEL act immediately |
| MDI | text + ➤ | one GRBL line through `/api/grbl/send` (Enter also sends) |
| ⚡ | header button | only visible when `usb_running:false` — restarts the USB host |
| ← → | arrows | previous/next screen — **never disabled by a missing job**, only by `busy` or the ends |
| swipe | gesture | horizontal drag (`|dx| ≥ 60` and `|dx| > 1.5·|dy|`) = next/previous |

---

## 5. Buttons

| Button | Action |
|---|---|
| **USE** (SD tab) | confirms the selected SD file as the job and asks the controller for its bounds (`frame-bbox`) |
| **SEND** (Phone tab) | destination **SD**: `POST /api/grbl/upload-gcode-stream?path=<root>/<name>&overwrite=1` (then the file is listed, selected and measured) · destination **PSRAM**: `POST /api/grbl/psram-upload` |
| Preview `↻` | streams `/api/grbl/read-gcode-text` and rebuilds the toolpath |
| **FRAME** | outline batch (see §6) |
| **START** | `POST /api/grbl/stream?path=` for an SD job, `psram-stream` for a workspace job; refused while in alarm |
| **PAUSE** / **RESUME** | `POST /api/grbl/pause` / `POST /api/grbl/resume` (enabled by machine state) |
| **STOP** | `POST /api/grbl/cancel` |
| **UNLOCK** (alarm block) | `$X` |
| **RESUME JOB** (alarm block) | `POST /api/grbl/resume-alarm` with `confirm=1` — unlocks, homes and restarts from the line where it stopped (armed: two taps) |
| **F− F+ S− S+** | `POST /api/grbl/override` with `feed` + `spin` (10…200 %) |
| **HOME** | `$H` (armed) |
| **UNLOCK** | `$X` |
| **ZERO X / Y / Z** | `G92 X0` / `G92 Y0` / `G92 Z0` — one axis at a time (armed; Z refused on a 2-axis machine) |
| **HOLD** / **RESUME** / **CANCEL** (Jog screen) | same endpoints as PAUSE / RESUME / STOP |
| **RESET** | `Ctrl-X` (0x18) through `/api/grbl/send` (armed) |
| **➤** (MDI) | one manual line through `/api/grbl/send` |

---

## 6. Client logic

- `sid` — per-tab id in `sessionStorage` (`m-<ts>-<rand>`), sent with every write;
  the control token comes from `/api/grbl/control-token` and `can_write:false`
  keeps every action disabled with an explanation.
- **Free navigation** — `setStep()` only clamps 1…5; arrows/swipe are gated by
  `busy` and the ends. Nothing depends on `st.job`.
- **SD list** — `GET /api/grbl/list-gcode?dir=` on load and on every reload. Rows
  are built with `createElement` + `textContent` (never `innerHTML` with device
  data). Sizes go through `fmtBytes`.
- **Job model** — `st.job = {src:'sd'|'psram', name, bytes, path}`. An SD job is
  never copied to PSRAM: Play posts `/api/grbl/stream` with `path`.
- **Bounds** — phone files: parsed in the browser while reading the text;
  SD files: `GET /api/grbl/frame-bbox?path=` (fire-and-forget with an
  indeterminate bar, 45 s timeout). If the server cannot measure it the preview
  points are used as a fallback; if neither exists, FRAME stays disabled with
  `gmMeasureFail` and Play still works.
- **Preview** — `PointSink` parses XY modal moves (`G0/G1/G2/G3` endpoints) into a
  flat `[x,y,…]` array. The SD path streams `/api/grbl/read-gcode-text` with
  `fetch` + `TextDecoder`, cutting at 40 000 points (halved with a doubling
  stride, so the whole shape stays visible) or 25 s, then marks the preview
  `partial`. Drawn on a canvas (device-pixel-ratio aware, Y up, bbox frame plus an
  origin cross); no canvas support degrades to the info line only.
- **Jog** — every jog sends `M5` first and then `$J=G91 G21 <axis><±step> F<feed>`
  (`F1000` XY, `F300` Z) through `/api/grbl/send`, exactly like the PC page.
- **Run state** — steps 4/5 poll `/api/grbl/status` once per second (one timer,
  cleared when leaving them) and drive the badge, state colour (RUN/HOLD/ALARM),
  MPos, progress bar + %, ETA, the alarm block and the PAUSE/RESUME/STOP states.
- **Start pre-flight** — the controller refuses a start with `preflight_*` codes;
  every one of them is mapped to a readable sentence (USB off, no machine on the
  slot, workflow busy, homing required/ALARM:9, alarm active, door open, bad
  file), so the phone never shows a raw code.
- **Alarm** — `alarm_active` + `alarm_message` open the alarm block on Play with
  UNLOCK and RESUME JOB; the latter is armed (two taps) because it unlocks, homes
  and restarts the job.
- **Overrides** — feed/spindle 10…200 % in ±10 steps, sent together as
  `feed=<pct>&spin=<pct>`; the middle label shows the current feed %.
- **Upload destination** — SD (default, no cap) or PSRAM (4 MB), chosen with the
  chips under the picker; an SD upload ends by re-listing the directory,
  selecting the new file and measuring it.
- **USB** — the header ⚡ button is only rendered when `usb_running:false` and
  posts `/api/grbl/usb-restart`.
- On load (one shot each): control token, `/api/grbl/psram-file` (a job already on
  the controller keeps steps 3/4 usable after a reload) and one status read.

---

## 7. Server API

| Endpoint | Method | Used for |
|---|---|---|
| `/api/grbl/control-token` | GET | write token + `can_write` |
| `/api/grbl/status` | GET | badge, state, MPos, job progress (1 s on steps 4/5) |
| `/api/grbl/list-gcode` | GET | SD card directory listing (`dir`) |
| `/api/grbl/frame-bbox` | GET | XY bounds of an SD file (`path`) |
| `/api/grbl/read-gcode-text` | GET | SD file text for the preview (streamed) |
| `/api/grbl/psram-upload` | POST | stores a phone file in the PSRAM workspace |
| `/api/grbl/upload-gcode-stream` | POST | writes a phone file to the SD card (`path`, `overwrite=1`) |
| `/api/grbl/psram-file` | GET | job already on the controller (`has_file`, `name`, `size`) |
| `/api/grbl/send` | POST | one GRBL line (frame, jog, MDI, `$H`, `$X`, `G92`, reset) |
| `/api/grbl/stream` | POST | starts a job straight from its SD `path` |
| `/api/grbl/psram-stream` | POST | starts the workspace job (`slot=0`) |
| `/api/grbl/pause` / `resume` / `cancel` | POST | PAUSE / RESUME / STOP |
| `/api/grbl/override` | POST | feed + spindle override percentages |
| `/api/grbl/resume-alarm` | POST | unlock + home + restart from the stopped line (`confirm=1`) |
| `/api/grbl/usb-restart` | POST | restarts the USB host (⚡ button) |

---

## 8. Persistence

None on the board: the page stores only its own `sid` in the browser. A phone
upload lives in the controller's PSRAM workspace (same store the PC page uses)
until it is replaced or cleared; an SD job is only a path.

---

## 9. Runtime engine

- Steps 4 (**Play**) and 5 (**Jog**) poll `/api/grbl/status` every 1 s; the timer is
  cleared on every other screen.
- Upload is one XHR with progress; `frame-bbox` and the SD preview show the
  indeterminate bar while they run.
- Nothing else polls: the SD list and the preview are one-shot requests.

---

## 10. i18n keys

110 `gm*` keys in `lang_en_ui.h` + `lang_pt_ui.h` (3124 keys total, both packs in
sync; `validate_i18n.py` green, `generated_gzip_assets.h` regenerated): titles
`gmTitle1`/`gmTitlePreview`/`gmTitleFrame`/`gmTitlePlay`/`gmTitleJog`, tabs
`gmTabSd`/`gmTabPhone`, SD texts `gmSd*`, destination `gmDest*`, preview texts
`gmPreview*`, actions `gmSend`/`gmUse`/`gmFrameRun`/`gmPlay`/`gmPause`/`gmResume`/
`gmStop`, machine buttons `gmBtn*` (including `gmBtnZeroX/Y/Z`), alarm
`gmAlarm*`, override `gmOv*`, progress `gmEta`, MDI `gmMdi*`, USB `gmUsb*`,
results `gmAct*`, per-step hints `gmStep1..5Hint`, errors `gmErr*` (including the
`preflight_*`, resume and SD-upload mappings; `{code}`, `{name}`, `{path}`,
`{size}`, `{axis}`, `{feed}`, `{spin}`, `{cmd}`, `{m}`, `{s}` placeholders) and
`gmNextTip`/`gmConfirmAgain`.

---

## 11. Review findings table

| # | Finding | Status |
|---|---|---|
| 1 | No page scroll on any of the five screens at 320×568 and 390×844 (`scrollHeight == clientHeight`, `overflow:hidden` on body); only the SD list and Jog scroll inside | verified (Chrome) |
| 2 | The old page disabled `→` until a job existed and had no swipe, so the phone looked "locked on step 1" | fixed (arrows always usable, swipe added) |
| 3 | The old page had no SD list: a phone could only upload its own file | fixed (SD tab, streamed from the card, no 4 MB cap) |
| 4 | `frame-bbox` on a 9.8 MB file takes ~21 s on the ESP32 (whole-file parse); the page shows the indeterminate bar and a 45 s timeout, and Play does not depend on it | by design |
| 5 | SD previews are capped at 40 000 plotted points / 25 s and flagged `partial` (a 10 MB job cannot be fully parsed on a phone) | by design |
| 6 | Frame runs at a fixed 5% / F1200 and the jog feeds are fixed (1000 XY / 300 Z) — no power/feed UI on the phone | by design |
| 7 | HOME / ZERO X/Y/Z / RESET / RESUME JOB use a two-tap arm (4 s) instead of a modal, because a dialog on a phone interrupts a job | by design |
| 8 | START is refused on the phone while the machine reports an alarm; the alarm block explains it and offers UNLOCK / RESUME JOB | fixed |
| 9 | The SD upload buffers the whole file in PSRAM on the controller (server cap 48 MB); the phone guards at 48 MB before sending | by design |

---

## 12. Source map

| Piece | File |
|---|---|
| Asset (CSS + HTML + JS) | `chatgpt/src/webpages/assets/page_gcode_mobile_asset.cpp/.h` |
| Renderer | `chatgpt/src/webpages/renderers/gcode_mobile_page_renderer.cpp/.h` |
| Route | `chatgpt/src/webpages/routes/web_routes_monitor.cpp` (`server.on("/m", …)`, CNC pack guard) |
| SD mutex on `/api/grbl/read-gcode-text` | `web_routes_monitor.cpp` — released in `request->onDisconnect` (same pattern as `/api/files/download`) |
| i18n | `chatgpt/src/languages/lang_en_ui.h`, `lang_pt_ui.h` (`gm*`), regenerated `assets/generated_gzip_assets.h` |
| Tests | `tests/web/test_gcode_mobile.js` (36 cases), `tests/web/test_mobile_entry.js` (7 cases), fixture `tests/web/pages/gcode_mobile.html` (generated by `extract.py`) |
