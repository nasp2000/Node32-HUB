# Mobile pages — `/m` (standard for every pack)

> One phone page per pack, built from **shared blocks**, with **no page scroll**:
> the screens live in one route and the footer arrows (or a horizontal swipe)
> swap them (`#1`…`#5` keep the step across a reload). **Navigation never waits
> for data** — every screen is reachable with nothing loaded.
>
> The desktop pages are **not touched**: phones are redirected here by
> `ua_classify.h`; `?desk=1` still forces the full desktop page.

> Screens: `mobile/gcodesender_*.png` + `mobile/common_system_*.png` (model and full index in `mobile/README.md`).

---

## 1. Objective

Give the operator the essentials of the pack from a phone, one action per screen,
with big readouts and everything reachable with the thumb.

**GcodeSender (CNC packs)** — five screens:

1. **Send** — file on the **SD card** (list/folders) or from the phone
   (→ SD card, no cap, or → PSRAM workspace, 4 MB).
2. **Preview** — 2D toolpath + the job area readout + **FRAME** (outline).
3. **Play** — START / PAUSE / RESUME / STOP, progress, ETA, overrides, alarm block
   (UNLOCK / RESUME JOB).
4. **Jog** — X/Y cross + Z row + `0,0` centre, per-axis zero, HOME/UNLOCK/RESET,
   MDI line.
5. **System** — firmware/uptime/heap/PSRAM/SD/WiFi, **OTA upload**, **serial log**
   and **restart** (shared block, see §11).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /m` |
| Feature flag | `ENABLE_GRBL`, route inside the CNC-pack guard (`PACK_P4_CNC` / `PACK_S3_CNC`) |
| WiFi server | `web_routes_monitor.cpp` (next to `/grbl-controller`), `streamGcodeMobilePageToPrint()` |
| Ethernet | not registered: the CNC packs have no Ethernet build (`ENABLE_ETHERNET` off) |
| Auth / caching | `ensureBasicAuth` + `allowHtmlClient`; `ETag = FIRMWARE_BUILD`, `private, max-age=15, must-revalidate` |
| Session | not never-kick: 1 s status poll only on Play/Jog, 2 s summary poll only on System |
| Entry point | `GET /grbl-controller` from a phone → 302 `/m` (UA only, `?desk=1` escape). Desktop/tablets keep the controller. |
| Pack name | `PACK_SYSTEM_NAME` is printed by the renderer as `window.PACK_NAME` and shown in the header (`GcodeSender-P4` / `-S3`) |
| i18n | `gm*` keys in `lang_en_ui.h` + `lang_pt_ui.h`; static labels use `data-i18n`, runtime text goes through `MU.t()/MU.tf()` |

Renderer order (`gcode_mobile_page_renderer.cpp`):
shell → `window.PACK_NAME` → **`MOBILE_COMMON_CSS`** → `PAGE_GCODE_MOBILE`
(screens 1-4 + header) → **`MOBILE_SYSTEM_HTML`** → `PAGE_GCODE_MOBILE_TAIL`
(msg/bar/actions/nav + wizard script) → **`MOBILE_COMMON_JS`** →
**`MOBILE_SYSTEM_JS`** → `/auto.js`.

---

## 3. Layout

Fixed viewport, single column (`#gm`, `height:100dvh`, `overflow:hidden`), so the
page can never scroll. **Each screen centres its content** in the free area
(`justify-content: center` + `safe center` so a short viewport never clips the
first row); the Send list and the Log pane fill instead.

```
header   strip: Send · Preview · Play · Jog · System  (one column per page,
                name on top, its own line below; current highlighted, visited blue)
         pack name (left) · link badge (right) · ⚡ USB restart when the host is down
screen   the active page only (flex:1, min-height:0, overflow:hidden)
msg      one line of status / error
bar      upload / measure / preview progress
primary  USE|SEND · FRAME · START · (none on Jog/System)
nav      ← · n / 5 · →
```

The SD list and the Log scroll **inside** their box; everything else is fixed.
`@media (max-height:620px)` shrinks the titles, cards, jog pad and buttons.

---

## 4. Inputs & gestures

| Control | Behaviour |
|---|---|
| Strip name | jumps to that page; the rail under it shows the position |
| Arrows `←` `→` | previous/next page (only `busy` and the ends stop them) |
| **Swipe** | horizontal drag anywhere: `|dx| ≥ 45` **and** `|dx| > |dy|` → next/prev. A swipe never presses the control under the finger (the click it generates is swallowed) and `touchcancel` is handled |
| **Tap guard** | every button/link needs **≥180 ms** of touch — a brushing finger does nothing |
| SD row | file → select · folder → open · `↩ Parent folder` → up |
| Phone picker | `.gcode/.nc/.ngc/.txt`; destination chips **SD card** (no cap) / **PSRAM (4 MB)** |
| Jog step chips | 0.1 / 1 / 10 / 50 mm |
| Jog cross | Y+ up, X− left, X+ right, Y− down, centre **0,0** (two taps) |
| Machine buttons | HOME, ZERO X/Y/Z, RESET and jog `0,0` arm on the first tap (4 s); UNLOCK/HOLD/RESUME/CANCEL act at once |
| MDI | one GRBL line (Enter also sends) |

---

## 5. Buttons

| Button | Action |
|---|---|
| **USE** (SD) | confirms the SD file and asks the controller for its bounds (`frame-bbox`) |
| **SEND** (Phone) | `POST /api/grbl/upload-gcode-stream` (SD) or `/api/grbl/psram-upload` (PSRAM) |
| Preview `↻` | streams `/api/grbl/read-gcode-text` and rebuilds the toolpath |
| **FRAME** (on Preview) | outline at **5 % of the controller's `spindle_max_rpm`** (1000 → `S50`), F1200, tool off on the way to the corner, and a tool-off move back to the **centre** at the end |
| **START** | `/api/grbl/stream?path=` (SD job) or `/api/grbl/psram-stream`; refused while alarmed |
| **PAUSE / RESUME / STOP** | `/api/grbl/pause` `/resume` `/cancel` |
| **UNLOCK / RESUME JOB** (alarm) | `$X` · `/api/grbl/resume-alarm` (`confirm=1`) |
| **F− F+ S− S+** | `/api/grbl/override` (10…200 %), shown only while a job is active |
| **HOME / UNLOCK / ZERO X/Y/Z / RESET** | `$H` · `$X` · `G92 X0|Y0|Z0` · `Ctrl-X` |
| Jog `0,0` | `$J=G90 G21 X0 Y0 F1000` |
| **➤** (MDI) | one manual line |
| **System → Restart** | `/api/restart` after an **OK/Cancel** dialog |
| **System → Upload firmware** | `/api/enable-ota` + `/api/firmware-upload` after an **OK/Cancel** dialog |

---

## 6. Client logic

- `sid` per tab in `sessionStorage`; control token from `/api/grbl/control-token`
  (`can_write:false` disables every action and explains why).
- **Write token lease** — the controller keeps a token for 15 min
  (`grbl_web_api.cpp` `kTokenLeaseMs`, renewed only by
  `validateSessionToken()` on a **write**), so a long job leaves a token that is
  already dead when the operator presses START again. The page refreshes it
  every 10 min (`TOKEN_REFRESH_MS`) before writing, and if a write still answers
  `auth_token_invalid` it renews once and **retries the same POST**, so the
  second START works without reloading the page. Any other failure keeps the
  server's code (`gmErrToken` explains a persisted expiry).
- **Job model** — `{src:'sd'|'psram', name, bytes, path}`; an SD job is never
  copied to PSRAM: Play posts `/api/grbl/stream` with `path`.
- **Bounds** — phone file: parsed while reading the text; SD file:
  `frame-bbox` once, in the background when Preview/Play is opened (45 s
  timeout). Fallback: bounds from the preview points.
- **Frame** — same contract as the desktop page: `M5, G90, G21`, move to the
  first corner **before** `M3 S<power>` (never travel with the beam on), trace,
  `M5`, then park at the **centre** of the box with the tool off. Power is
  `framePowerS()`: 5 % of `spindle_max_rpm` (default 1000 → `S50`); the old
  formula clamped to `S1000` (full power). Any failure mid-trace still sends a
  best-effort `M5` so the head is never left lit.
- **Start** — `sendCmd('M5')` runs first: a beam/spindle left on by an aborted
  frame must not travel across the part to the first point. If that `M5` is
  refused, the start is cancelled (`gmErrToolOff`) instead of streaming.
- **Preview** — `PointSink` parses modal XY moves into a flat array, decimating
  past 40 000 points; the SD text is streamed and cut at 25 s → `partial`. The
  canvas sizes itself to the job aspect (clamped) so the drawing fills it.
- **Alarm** — `alarm_active`/`alarm_message` open the alarm block; START is
  refused; RESUME JOB is armed (it unlocks, homes and restarts).
- **Pre-flight** — every `preflight_*` / resume / upload error code is mapped to a
  readable sentence (USB off, no machine, busy, ALARM:9, door, bad file, sd_busy).
- **Confirmation** — OTA and restart use the firmware's `customConfirm` (OK/Cancel);
  HOME/ZERO/RESET/0,0 use two-tap arming; everything else acts immediately.
- One-shot on load: token, `psram-file`, one status read, SD list.

---

## 7. Server API

`/api/grbl/control-token` · `/api/grbl/status` · `/api/grbl/list-gcode` ·
`/api/grbl/frame-bbox` · `/api/grbl/read-gcode-text` · `/api/grbl/psram-upload` ·
`/api/grbl/upload-gcode-stream` · `/api/grbl/psram-file` · `/api/grbl/send` ·
`/api/grbl/stream` · `/api/grbl/psram-stream` · `/api/grbl/pause` ·
`/api/grbl/resume` · `/api/grbl/cancel` · `/api/grbl/override` ·
`/api/grbl/resume-alarm` · `/api/grbl/usb-restart`
plus the shared System block: `/api/version` · `/api/health` · `/api/status` ·
`/api/sd-status` · `/api/enable-ota` · `/api/firmware-upload` ·
`/api/serial-monitor` · `/api/restart`.

---

## 8. Persistence

None on the board for the wizard (only the tab `sid` in the browser). A phone
upload lives in the PSRAM workspace or on the SD card; an SD job is only a path.

---

## 9. Runtime engine

| Screen | Poll |
|---|---|
| Play (3) | `/api/grbl/status` every 1 s (progress, ETA, alarm, overrides) |
| Jog (4) | `/api/grbl/status` every 1 s (state, MPos) |
| System (5) | summary every 2 s, log every 2 s **only while that tab is visible** |
| others | none (one-shot requests) |

---

## 10. i18n

`gm*` keys in both packs (3176 keys, EN=PT in sync — `validate_i18n.py` green,
`generated_gzip_assets.h` regenerated): titles `gmTitle*`, nav `gmNav1..6`, SD
`gmSd*`, destination `gmDest*`, preview `gmPreview*`, frame `gmFrame*`, play
`gmPlay*/gmPause/gmResume`, jog `gmBtn*/gmAct*/gmJog*`, system `gmSys*`/`gmOta*`/
`gmLog*`, errors `gmErr*` (including the `preflight_*`, resume and SD-upload
mappings) and `gmNextTip`/`gmConfirmAgain`.

---

## 11. Shared blocks (reuse for other packs)

| File | Contents |
|---|---|
| `mobile_common_asset.cpp` | `MOBILE_COMMON_CSS` (scoped to `.mp`: tiles, sub-tabs, bar, msg, buttons, log, reboot) + `MOBILE_COMMON_JS` (`window.MU`: t/tf/get/post/fmtSize/fmtEta/msg/arm + the 180 ms tap guard) |
| `mobile_system_asset.cpp` | `MOBILE_SYSTEM_HTML` + `MOBILE_SYSTEM_JS` (`window.MPSystem`): summary tiles, OTA upload, serial log, restart. `MPSystem.open()/close()` are called by the host page; `MPSystem.setBlocker(fn)` vetoes a restart |

A new pack only writes its own screens (inside a container with `class="mp"`),
includes the two blocks and prints them in the same order. The System block is
endpoint-only (no pack knowledge), so it works unchanged on Rambox32, CAM,
Environment, Node32-OEE, etc.

---

## 12. Review findings

| # | Finding | Status |
|---|---|---|
| 1 | No page scroll at 320×568 / 390×844; only the SD list and the log scroll inside | verified |
| 2 | Old page locked the operator on step 1 (next arrow disabled without a job) | fixed |
| 3 | Old page had no SD list and no preview | fixed |
| 4 | `frame-bbox` on a 9.8 MB file takes ~21 s on the ESP32 → indeterminate bar, 45 s timeout, Play does not depend on it | by design |
| 5 | Preview capped at 40 000 points / 25 s and flagged `partial` | by design |
| 6 | Frame runs at a fixed 5 % of `spindle_max_rpm` / F1200, starts from the corner with the tool off and returns to the centre; jog feeds fixed (1000 XY / 300 Z) | by design |
| 7 | SD upload buffers the file in PSRAM on the controller (48 MB cap); the phone guards at 48 MB | by design |
| 8 | Frame was merged into Preview (a button), so the wizard is 5 screens | fixed |
| 9 | OTA/restart confirmed with a dialog; HOME/ZERO/RESET/0,0 with two taps; taps are never swallowed (only a drag > 14 px cancels) | fixed |
| 10 | A job longer than the 15 min token lease left the page with a dead token: the next START (or PAUSE/STOP/jog) answered `auth_token_invalid` until a reload — the "second start does nothing" report | fixed (10 min refresh + one retry), desktop page now identical |
| 11 | Frame ran at **S1000 = full power** (the formula assumed a "5 % = 5 rpm" scale) and parked the head **lit on the last corner**, so the next START dragged a lit beam to the part | fixed (5 % of max RPM, centre return, `M5` before the stream) |

---

## 13. Source map

| Piece | File |
|---|---|
| Wizard screens (this pack) | `chatgpt/src/webpages/assets/page_gcode_mobile_asset.cpp/.h` (`PAGE_GCODE_MOBILE` + `_TAIL`) |
| Shared common | `chatgpt/src/webpages/assets/mobile_common_asset.cpp/.h` |
| Shared System screen | `chatgpt/src/webpages/assets/mobile_system_asset.cpp/.h` |
| Renderer | `chatgpt/src/webpages/renderers/gcode_mobile_page_renderer.cpp` |
| Route | `chatgpt/src/webpages/routes/web_routes_monitor.cpp` (`/m`, CNC guard) |
| SD mutex on `read-gcode-text` | released in `request->onDisconnect` (same pattern as `/api/files/download`) |
| Tests | `tests/web/test_gcode_mobile.js` (43), `tests/web/test_mobile_entry.js` (7), fixture built by `tests/web/build_m_gcode.py` |
| Review shots | `SHARE/Tarefas/anexos/GcodeSender/` |

> **User manual:** `PAGES/<pack>_mobile.md` for other packs once they exist.
