# GRBL Controller — `/grbl-controller`

> One-stop reference for the **CNC G-code sender / controller page**: purpose, layout,
> widgets, every button/input, client logic, API endpoints, streaming and persistence.
> Written for both humans and AI agents.

---

## 1. Objective

Full browser-based **G-code sender** for CNC machines running GRBL over USB/serial:
connect & set baud, live status/DRO, manual jog, overrides, pause/resume/cancel, G-code
preview (2D + 3D, contour/light rendering), SD and PSRAM file browser with upload/stream,
probe auto-zero, probe grid and height map. Served as a **pure control surface** — the page
itself persists no settings (all board settings live in ConfigManager / SD).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /grbl-controller` |
| Feature flag | `ENABLE_GRBL`; **route/API block gated to CNC packs** (`PACK_P4_CNC` / `PACK_S3_CNC`) in `web_routes_monitor.cpp:44` |
| WiFi server | `web_routes_monitor.cpp:486` — streams `streamGrblControllerPageToPrint()` |
| Ethernet | `ethernet_bootstrap_routes.h:186` → `streamSharedGrblControllerPage` (`web_shared_pages.cpp:103`) |
| Auth / caching | `ensureBasicAuth` + `allowHtmlClient`; `ETag` = `FIRMWARE_BUILD`, `private, max-age=15, must-revalidate` |
| Session exemption | ALWAYS exempt (local processing) — unconditional `window._localPagesNeverExpire=true`, never kicked by time |
| Home entry | Home page CNC section (packs only) |

Renderer: shell → `page_grbl_controller_asset.cpp` → includes **13 split header files**
(`chatgpt/src/webpages/assets/grbl/*.h`): CSS, HTML shell, state, UI, 3D/2D parsers,
contour, light, status, actions, SD, init.

---

## 3. Layout

```
div#grbl_page_root.page.grbl-controller > div.wrap
 └ cncjs-style shell grid (grbl_html_shell.h + grbl_css.h):
    ├ header / home button
    ├ widget docks (docked/undocked panels, saved per-browser in localStorage)
    ├ DRO panel (X/Y/Z, WCS display, feed/rapid, units)
    ├ status bar (state, mode badge, WCS, transport, projection)
    ├ action toolbar (connect, baud select, jog pad, overrides, pause/resume/cancel)
    ├ preview canvases (2D top/iso/side + 3D view, contour overlay, light)
    ├ SD/PSRAM file panel (tree, upload, load, stream)
    ├ probe panel (auto-zero, probe grid, height map)
    ├ console / macro input
    └ sim/scrub controls (timeline scrub, play/stop)
```

Widgets can be docked/undocked and their positions persisted per-browser
(`saveWidgetPrefs`/`restoreWidgetPrefs`). Dark theme (`data-grbl-theme='dark'`).

### Responsive layout (grbl_css.h media queries)

| Breakpoint | Behaviour |
|---|---|
| 1280px | compact spacing (columns/gaps) |
| 1040px | 2-col shell grid wraps |
| 980px | shell grid → single column (`grid-template-columns:1fr`) |
| 768px | **single column hard stack** (`.cncjs-shell-grid{grid-template-columns:1fr;justify-items:stretch}`); side/center columns, widgets, dock and section blocks forced `width:100%;min-width:0`; toolbar & frame params wrap; jog layout clamps to viewport; left panel capped `min(300px,85vw)` |
| 640px | quick-actions bar (`.grbl-cb-main`) becomes a **2×auto-wrap grid inside row blocks** (`.grbl-cb-row{grid-template-columns:repeat(2,minmax(0,1fr))}`) so the 8 buttons never overflow the viewport; jog pad/step controls full-width; text touch-target sizes |
| 480px | console send row stacks to one column; 2D toolbar stacks vertically with full-width actions/select; 2D scrub becomes 2-col grid with full-width range slider; jog buttons `56px`; job/SD actions single column |
| landscape ∩ max-height 520px | rotate warning replaces page |

Rules are additive (later blocks win), so ≤480px inherits the 768px/640px fixes.

---

## 4. Inputs / buttons (widgets)

| Widget | Purpose |
|---|---|
| Transport toggle (USB OTG on/off) | **S3 CNC only** (`GRBL_TRANSPORT_SELECTOR=true`): button lives in the USB Connection widget where P4 shows Reconnect USB. Boot is always OTG OFF; opening the page auto-starts the host (POST `/api/grbl/transport` `mode=usb`, session-only, **no localStorage persistence** — every visit re-fires; rejection e.g. job active simply stays off). Button state synced from `usb_running` on every status poll. P4: button removed |
| Baud select | sets serial baud (`/api/grbl/transport`, `/api/grbl/usb-line-coding`; USB endpoints 404 when USB Host is off) |
| Jog pad + step/distance inputs | manual jog (`/api/grbl/send` with jog codes) |
| Feed override | `/api/grbl/override` |
| Pause / Resume / Cancel | `/api/grbl/pause`, `/api/grbl/resume`, `/api/grbl/cancel` |
| Resume-from-alarm panel | shown when an ALARM interrupted a job (`resume_available`): file, 1-based line/total, alarm text, safety checkbox, `Resume from line` button → `/api/grbl/resume-alarm` (`confirm=1`); progress/error via `resume_busy`/`resume_error` polls |
| Probe auto-zero | sets Z zero on contact probe (`/api/grbl/send`) |
| Probe grid + height map | generate/clear probe grid + height map (`/api/grbl/probe-grid`, `/api/grbl/height-map-*`) |
| SD panel | list SD/PSRAM g-code, upload (SD ≤50 MB / PSRAM ≤ `GRBL_STREAM_PSRAM_MAX_BYTES`), load, stream |
| Command box | raw G-code / macro send (`/api/grbl/send`) |
| Macro templates | reusable command snippets (`grblMacro*` placeholders) |
| Poll interval | `grbl_poll_ms` (REST polling) — **no input exists** (dead ref, see findings) |
| Sim scrub / play / stop | timeline scrub through parsed preview |

Keyboard jog + "focus to interact" behaviour wired in `grbl_init.h`.

---

## 5. Client-side logic

- **Status**: WebSocket (`ws_handler.h`, ~100 ms) when available, else REST polling
  (`/api/grbl/status`, ~500 ms default). `applyStatusSnapshot` parses state, WCS,
  position, feed; mode badge, transport label, projection rebuilt per push via `wtSafe`.
- **Control token**: a session control token gates actions (`/api/grbl/control-token`).
  The token is **bound to the `sid` that requested it**: you must call
  `/api/grbl/control-token?sid=<sessionId>` and then send the token with the **same**
  `sid` on every mutating POST (`/api/grbl/send`, `/api/grbl/override`, ...). Requesting
  the token without `sid` binds it to `ip:unknown` and a later POST with a different
  `sid` fails with `400 auth_token_invalid` (verified on P4 .7, DIV-1).
- **Parsing**: inline async parser (`grbl_parser_3d.h`) + Web Worker parser
  (`grbl_parser_worker.h`) split G-code into segments; `grbl_2d_controls.h` orchestrates
  build → light preview → scrub/sim; `grbl_2d_render.h` draws top/iso/side (job-aware);
  `grbl_contour.h` produces light contours + wireframe; `grbl_light.h` applies light
  preview / from-bounds / from-wireframe.
- **Frame run / macros**: `frameRun` + macro execution in `grbl_light.h`.
- **Streaming**: `grbl_sd.h` — SD browser, upload (`/api/grbl/upload-gcode-stream`,
  `/api/grbl/psram-upload`), load (into queue/play), workspace load, stream
  (`/api/grbl/stream`); `setCommandMode` updates the mode badge
  (`manual|queue-local|sd-stream|runtime`).
- **Probe grid / height map**: action handlers in `grbl_actions.h` (`grblProbeAutoBtn`,
  `grblHmStartBtn`, `grblHmCancelBtn`, `grblHmClearBtn`, `grblHmEnableLbl`).
- i18n via `wtSafe(key, fb)` (auto.js `window._t` + localStorage cache); placeholders set
  manually in `applyPlaceholders()` because auto.js does **not** translate
  `data-i18n-placeholder`.

---

## 6. Server API (all `/api/grbl/*`)

Registered in `registerGrblControllerApiRoutes` (`web_routes_monitor.cpp:326-426`); handlers
in `handlers_unified/grbl_handler.cpp`; engine in `grbl_web_api.cpp` / `grbl_sender*.cpp`.

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/grbl/status` | GET | live GRBL state (machine/status/WCS/etc.); also `usb_slots[]` with per-slot `vid`/`pid`/`dev_addr` and `usb_diag` (see Runtime engine) |
| `/api/grbl/debug` | GET | debug snapshot |
| `/api/grbl/control-token` | GET | session control token |
| `/api/grbl/events` | GET | SSE event stream (registered; page doesn't use it) |
| `/api/grbl/list-gcode` | GET | list g-code files (SD/PSRAM) |
| `/api/grbl/frame-bbox` | GET | frame bounding box |
| `/api/grbl/psram-file` | GET | read PSRAM g-code file |
| `/api/grbl/psram-read-text` | GET | read PSRAM file as text |
| `/api/grbl/read-gcode-text` | GET | stream SD file text (≤50 MB, path-safe) |
| `/api/grbl/usb-restart` | POST | restart USB/serial (404 when `ENABLE_USB_HOST==0`) |
| `/api/grbl/transport` | POST | set transport (`mode=usb` launches async begin → `"starting":true` until `usb_running`; `mode=uart` stops host; 409 `switch_rejected` while a job runs; non-S3 packs: 404) |
| `/api/grbl/usb-line-coding` | POST | set USB line coding (404 when `ENABLE_USB_HOST==0`) |
| `/api/grbl/auto-detect-baud` | POST | auto-detect baud (registered; unused by page) |
| `/api/grbl/send` | POST | send a command line |
| `/api/grbl/override` | POST | feed/spindle override |
| `/api/grbl/stream` | POST | stream a file (SD or PSRAM) |
| `/api/grbl/queue-start` / `queue-stop` | POST | queue start/stop (registered; unused) |
| `/api/grbl/pause` / `resume` / `cancel` | POST | transport control |
| `/api/grbl/resume-alarm` | POST | assisted resume after ALARM (`confirm=1`): `$X` unlock → `$H` home → modal preamble → restart with line offset in a background task; 400 `confirm_required`, 409 `no_resume`/`resume_busy`/`job_active` |
| `/api/grbl/poll-interval` | POST | set poll interval (registered; no UI) |
| `/api/grbl/create-dir` / `delete-path` | POST | SD fs ops |
| `/api/grbl/psram-stream` / `psram-clear` | POST | PSRAM fs ops |
| `/api/grbl/upload-gcode-stream` | POST | **SD upload** (body accumulator, ≤50 MB, overwrite guard) |
| `/api/grbl/psram-upload` | POST | **PSRAM upload** (body accumulator, ≤ `GRBL_STREAM_PSRAM_MAX_BYTES`) |
| `/api/grbl/probe-grid` | GET/POST | probe grid get/start |
| `/api/grbl/probe-grid-cancel` | POST | cancel probe grid |
| `/api/grbl/height-map` | GET | height map data |
| `/api/grbl/height-map-enable` / `-clear` | POST | enable/clear height map |
| WebSocket | WS | `/api/grbl/ws` (via `initWsServer`, `ws_handler.h`) |

---

## 7. Persistence

- The page stores **widget layout/dock preferences** in browser `localStorage` only.
- Board-side settings live in `ConfigManager` `config["grbl"]` (e.g. `log_enabled`) and SD
  auto-cleanup keys; **no page-triggered writes** to NVS/LittleFS.
- Uploaded files persist on **SD** (or PSRAM VFS for the PSRAM upload path).

---

## 8. Runtime engine

- `grbl_web_api.cpp` — transport to the GRBL device (USB serial / other), status polling
  and event handling; `grbl_sender*.cpp` stream engine; `grbl_psram_gcode.{h,cpp}` PSRAM
  g-code store; `sd_security.h` path safety; `sdcard.h` access; `ws_handler.h` WebSocket
  status push (~100 ms).
- **S3 CNC is USB-OTG-only, boot OTG OFF (since 2026-10-11; host-at-boot plan B
  2026-09-14–2026-10-11 caused silent TG1WDT resets on empty port; lazy
  2026-09-09–14, UART-only 2026-09-06–08, `pack_s3_cnc_usb.h`:
  `ENABLE_USB_HOST=1`)**: `setup_impl.h` skips `usbHostBegin()` at boot on S3
  CNC — the GRBL page auto-starts the host on every visit
  (`/api/grbl/transport` `mode=usb`, session-only, no persistence). The old
  ~30 s loop was host tasks + Serial2 together and Serial2 is compiled out
  (`#if 0`). The 4-port hub UI is **P4-only** and hidden on S3; S3 shows a
  single USB OTG metric (ON/WAIT/OFF). P4 packs are unaffected (host at boot,
  no toggle).
- **S3 begin deadlock (fixed 2026-09-13)**: `usb_host_lib_set_root_port_power()` blocks
  until the hub driver processes the request inside `usb_host_lib_handle_events()`. It ran
  BEFORE the daemon task existed → caller (an HTTP worker) wedged forever, browser showed
  "transport switch failed". Fix: S3 power cycle moved to after daemon/client task creation
  (+500 ms settle), and begins run via `usbHostBeginAsync()` — never on a server task.
  OFF mid-begin cancels cooperatively (`usbHostCancelPendingBegin()`); stale begins expire
  after 60 s so retries are never blocked forever.
- Uploads use PSRAM-backed streaming buffers (avoid DRAM OOM for >3 MB) with watchdog
  resets during frame-bbox parse.
- **Alarm resume ("restart from line N")**: on ALARM mid-job, `processGrblResponseLine`
  snapshots path/counters into resume state *before* wiping the job. `grblResumeFromAlarm()`
  (background `grbl_resume` task) replays `$X` unlock (≤15 s) → `$H` home (≤150 s) → modal
  preamble scanned from job start (`grbl_resume_scan.h`: G20/21, G90/91, G54–59, G17–19, F,
  S+M3/M4 + `G4 P2` dwell, coolant; T/M6 intentionally left to the operator) → restart with
  offset. Skip is exact per feeder (drop first N *sent* lines at the push point; counters
  preset to N). SD staleness checked by file size; PSRAM reuses the surviving buffer;
  queue re-copies its buffer. New starts reject while busy; cancel aborts the flow.
  Status: `resume_available/_file/_line/_total/_alarm/_busy/_error`.
- **USB VID:PID exposure + stuck-open recovery** (`usb_host_manager.cpp`, native backend; P4):
  `/api/grbl/status` `usb_slots[]` carries `vid`/`pid`/`dev_addr` for every enumerated CDC
  device (decimal, `0` when the slot is empty). VID/PID is read from the device descriptor
  after `usb_host_device_open()` succeeds (ESP-IDF has no descriptor API without an open).
  When a device repeatedly fails to open with `ESP_ERR_INVALID_STATE` (stale
  `is_gone`/`waiting_free` entry in `usbh`), the client/connect path (never `usbHostBegin`)
  recovers in **two tiers**: Tier 1 = soft retry (the 500 ms scan cycle simply calls
  `onDeviceConnect()` again, so a transient hot-replug reopens on its own WITHOUT any bus
  action); Tier 2 = last-resort `usbHostRequestRestart()` (P4: direct-register root-port
  power cycle → re-enumeration) after **25** distinct debounced failures per address
  (debounce 1 s — the counter counts episodes, not scan ticks). Pacing is a 10 s cooldown;
  power cycles are **skipped while** `grblStreamIsActive()`/`grblQueueIsActive()` or while
  any other slot has received data in the last 10 s, so an active CNC link is never
  disturbed. There is **no per-boot cap** — recovery stays available for the whole uptime;
  counts are cleared on success or when the address leaves the bus (hot unplug). State
  visible in `usb_diag.auto_restart_attempted` (bool: the P4 PHY auto-restart fallback fired);
  each recovery action is logged to Serial + Web Monitor via
  `sendVirtualMessage` (`bypassDedup=true`).
- **Ghost-record prevention (stuck `ESP_ERR_INVALID_STATE`)** — empirically, a power cycle
  does **NOT** clear a stuck open record (ESP-IDF v5.5.1): a device object with
  `open_count>0` is only freed by `usb_host_device_close()` after a successful
  `usb_host_interface_release()`, and there is NO public API to release a record for a
  failed open (`usb_host_device_free_all()` only frees `open_count==0` objects;
  `usb_host_lib_uninstall()` is blocked while `num_devices>0`). The lib retires URBs
  asynchronously during DEV_GONE, so a too-early release/close returns
  `ESP_ERR_INVALID_STATE`. `usb_host_manager.cpp` therefore never drops a teardown:
  `closeSlotDevice()` parks an unresolved close (handle + interface + EP addrs + owned
  transfers) in a small list and `drainStaleCloses()` retries `releaseThenClose()` from the
  scan loop until the record is freed (typically a few ms); transfers are freed only after
  the close succeeds. Slots with a parked close are excluded from `findFreeSlot()` so a new
  device can never be assigned to a half-torn-down slot. `onDeviceDisconnect()` handles the
  unmatched-handle case (slot already closed/reused) with `forceReleaseAndClose()` — a
  descriptor walk releases every claimed interface before closing — so no ghost can be
  created from a late DEV_GONE either. Each park/resolve/drop event is logged to Serial +
  Web Monitor.
- **Auto-detect chip table** (`usb_host_manager.h`): single data-driven
  `UsbChipDef s_usbChipTable[]` maps VID:PID → family name + init flags
  (`USB_CHIP_FLAG_VENDOR_LINE_CODING`, used by the CH340-style vendor baud path).
  `usbChipName()`/`usbChipNeedsVendorLineCoding()`/`usbChipFamilyFlags()` key off it (exact
  VID:PID match, then PID-`0xFFFF` VID-only fallback). Both the native and TinyUSB backends
  use the same table — adding a chip (e.g. `067B:2303` PL2303, `10C4:EA60` CP210x) is one row,
  no logic changes. `chip`/`name` in `/api/grbl/status` become accurate as soon as vid/pid are
  read.
- **CH340 driver: kernel-order EP0-safe init + baud divisor** — CH340 (1A86:7523) init uses
  vendor control requests on EP0 (0x5F version read, 0xA1 serial init, 0x9A register write,
  0xA4 handshake) driven by `usb_ch340_init_seq.h` in the exact Linux kernel `ch341.c` v6.6
  `ch341_configure()` order: READ_VERSION → SERIAL_INIT → WRITE_REG(0x1312, divisor) →
  WRITE_REG(0x2518) only when version ≥ 0x30 → MODEM_CTRL(~0x60). `ch340NativeReadVersion()`
  reads the chip version (classic CH340G answers 0x27; on failure the code assumes 0x30/"new").
  Chips below version 0x30 use *separate* line-control registers and REJECT the LCR2 0x2518
  write (STALL → EP0 stuck) — the 0x2518 write is SKIPPED for them, and the divisor value
  gets BIT(7) only when version > 0x27. **No preventive EP0 clean**: the kernel submits
  control transfers directly, and a preventive halt/flush/clear on EP0 of a healthy CH340G was
  measured to make the chip reject the very next command (including the 0x5F version read —
  the suspected cause of the board STALL). `ch340ClearEp0()` therefore runs ONLY after a
  vendor request fails, then the request is retried once (`ch340CtrlRequest`); it remains
  harmless (ESP_ERR_NOT_FOUND on a healthy device). All CH340 init/reinit/version/stall
  diagnostics go through `sendVirtualMessage` so they ALWAYS reach the Web Serial Monitor
  (`directLogPrintf` only printed when RS232 data activity was detected — making the init
  invisible remotely). The divisor math lives in `usb_ch340_divisor.h` (`ch340CalcDivisor`,
  exact `ch341_get_divisor()` port: `clk_div = 1 << (12 - 3*ps - fact)` with dynamic fact 0/1,
  `div = 48e6/(clk_div*baud)`, halve base clock when div ∉ [9,255], round to nearest rate,
  prefer fact=0 for even divisors; encoded `(0x100-div)<<8 | fact<<2 | ps`) — previously a
  hardcoded `-1` in the clk_div shift produced a doubled clock (real 230400 when 115200 was
  requested). Locked by native tests: `115200→0xCC03`, `9600→0xB202`, `230400→0xE603`,
  `250000→0xE803`, plus the init-step machine (order + LCR2/BIT(7) gates for 0x27 vs 0x30).
  Reinit backoff: a slot that fails 3 consecutive reinit attempts is marked `broken=true` and
  STOPS retrying — it is deliberately NOT closed/reopened (closing would make the very next
  scan re-attach it in an infinite close/reopen spin); a fresh unplug/replug starts from zero.

---

## 9. i18n keys

**Firmware-side i18n (also added):** the API messages above are no longer hardcoded Portuguese in the handler — they now come from the firmware pack (`LangStrings`, `L(activeLang->…)`) and follow `system_language`. Page `<title>` and tab labels follow the same rule.

~**523 `grbl*` keys** each in `lang_en_ui.h`/`lang_pt_ui.h` (in sync) — e.g.
`grblPageTitle`, `grblTipHome`, `grblCmdPlaceholder`, `grblSdFilterPlaceholder`,
`grblMacroNamePlaceholder`, `grblTransportSwitched`, plus buttons/labels/tooltips for
transport, jog, overrides, probe, SD, macros, preview modes, view modes, sim. **28 keys are
missing from both files** (fall back to English) — see findings.

---

## 10. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| 1 | **Mojibake fallbacks**: `'âœ…'` (✅) and `'â€œ'` (❌) in `wtSafe` fallbacks / `showAlert` prefixes (visible when the i18n key is missing). | Medium | Fix the corrupted UTF-8 in `grbl_actions.h:250,276,437,447,458,461`, `grbl_light.h:390`. |
| 2 | **`\u{…}` escapes in HTML** (e.g. `\u{1F4F1}`) render literally until `data-i18n` replaces text; the rotate-warning emoji (line 4 of `grbl_html_shell.h`) has **no** `data-i18n` and always shows the raw escape. | Low | Use real emoji literals or escape properly. |
| 3 | **28 i18n keys missing from both lang files** (`grbl2dCapped`, `grbl2dEmpty`, `grblHeightMap*`, `grblPreview*`, `grblSdLoadWorkspace`, `grblToolChangeLbl`, `grblUploadFile`, `grblViewMode*`, `grblTip*`, …). | Low | Add to `lang_en_ui.h`/`lang_pt_ui.h` (kept in sync). |
| 4 | **Dead DOM refs / orphaned controls**: `grbl_poll_ms`, `btn_apply_poll`+`applyPollInterval()`, `grbl_step`, `grbl_sent`/`grbl_received`, `btn_backplot_toggle`, `btn_3d_proj`/`btn_3d_motion`, `grbl_local_file` — the UI was rewritten (queue/poll/backplot controls removed) but handlers remain. | Low | Remove or re-wire. |
| 5 | **Registered-but-unused APIs**: `/api/grbl/queue-start`, `queue-stop`, `events` (SSE), `auto-detect-baud`, `probe-grid` GET, `height-map` GET, `debug` — no UI caller. | Low | Keep for external clients or drop. |
| 6 | `setCommandMode('psram-stream')` is called but the mode map only has `manual|queue-local|sd-stream|runtime` → badge stays `MANUAL` during PSRAM stream. | Low | Add `psram-stream` to the mode map. |
| 7 | `poll-interval` server endpoint is orphaned — no `grbl_poll_ms` input exists so users can't change the REST poll interval (default 500 ms). | Low | Add the input or remove the endpoint. |
| 8 | On non-CNC packs with `ENABLE_GRBL`, the page serves but **every `/api/grbl/*` returns 404** (route block is pack-gated). | Info | Gate the page route with the same pack condition. |
| 9 | **S3 reboot loop with OTG+UART (fixed 2026-09-06, reworked 2026-09-09)**: board rebooted ~every 30 s with both the USB-OTG cable and the UART adapter plugged — host tasks + Serial2 contention (TWDT), GPIO20 boot glitch, VBUS backpower. | Fixed | Pack S3 is now USB-OTG-only with lazy host start (`ENABLE_USB_HOST=1`, host off at boot, UART `#if 0`-disabled); toggle button starts/stops host per session, hub UI hidden (P4-only). |

---

## 11. Source map

| Concern | File |
|---|---|
| Renderer | `chatgpt/src/webpages/renderers/grbl_controller_page_renderer.{h,cpp}` |
| Asset (includes 13 grbl headers) | `chatgpt/src/webpages/assets/page_grbl_controller_asset.{h,cpp}` |
| DOM shell / CSS | `chatgpt/src/webpages/assets/grbl/grbl_html_shell.h`, `grbl_css.h` |
| State / i18n helpers | `chatgpt/src/webpages/assets/grbl/grbl_state.h` |
| UI / widgets / dock prefs | `chatgpt/src/webpages/assets/grbl/grbl_ui.h` |
| Events / init / keyboard | `chatgpt/src/webpages/assets/grbl/grbl_init.h` |
| Actions (send/jog/probe/height map) | `chatgpt/src/webpages/assets/grbl/grbl_actions.h` |
| SD / upload / stream | `chatgpt/src/webpages/assets/grbl/grbl_sd.h` |
| Status / DRO / mode badge | `chatgpt/src/webpages/assets/grbl/grbl_status.h` |
| Parsers / render / contour / light | `grbl_parser_3d.h`, `grbl_parser_worker.h`, `grbl_2d_render.h`, `grbl_2d_controls.h`, `grbl_contour.h`, `grbl_light.h` |
| Routes (WiFi) | `chatgpt/src/webpages/routes/web_routes_monitor.cpp:326-426` |
| Handlers / engine / WS | `handlers_unified/grbl_handler.cpp`, `grbl_web_api.cpp`, `grbl_sender*.cpp`, `handlers_unified/ws_handler.h`, `grbl_psram_gcode.*`, `sd_security.h` |
| Ethernet page | `ethernet_bootstrap_routes.h:186` |
| Self-test manifest | `test_manifest_handler.cpp` (`PAGE("/grbl-controller", ENABLE_GRBL)`) |