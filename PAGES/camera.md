# Camera — `/camera`

> One-stop reference for the **Cam-M camera page**: purpose, layout, every
> button/input, client logic, API endpoints, persistence and the capture engine.
> Written for both humans and AI agents.

---

## 1. Objective

Live MJPEG view plus full control of the ESP32-S3 camera (OV2640/OV3660/OV5640):
stream start/stop, snapshot to SD, AVI recording to SD, dashcam auto-rotate,
timelapse capture, grayscale-diff motion detection with a configurable action
(record video / snap photo / email alert), and an auto-start mode selector that
boots the camera into the chosen mode (off/photo/video/motion/timelapse).
Resolution is bounded by the **detected sensor** — OV3660 (the 3MP module
actually fitted on device .17) up to 2048×1536, OV5640 up to 2592×1944 (5MP).
Home page of the Cam-M pack (`pack_cam.h`): Camera + Monitor buttons.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /camera` |
| Feature flag | `ENABLE_CAMERA` (page + all routes compiled out when `0`) |
| WiFi server | `web_routes_camera.cpp:registerCameraRoutes` — chunked 3-segment HTML |
| Ethernet (W5500) | API via `registerApiRoute()` (shared); `/cam/stream` stays WiFi-only manual route + separate blocking `ethernetHandleCameraStreamGet` (`ethernet_bootstrap_handlers.h`) — single-client sequential by design |
| Auth | Basic auth + HTML-client check + ETag (`FIRMWARE_BUILD`) |
| Session exemption | `/camera` + `/hub` ALWAYS exempt (local processing) — unconditional flag, never kicked by time; `/cam/*` streams use no HTML session at all |

Renderer assembles: shell (`page_render_common.h` `buildPageFrame`) → body
(`CAMERA_PAGE`, PROGMEM, `page_camera_asset.cpp`) → `/auto.js`.
Pack Cam-M disables Ethernet, so the WiFi path is the production path.

---

## 3. Layout

Desktop ≥1024px — slim toolbar (26px, equal 84×22 buttons) + sidebar LEFT
(320px: 52px icon rail + titled panels) + stream CENTER (hero). Blue theme
(`--bg-0:#081226`, blue borders/accents). Layout concept inspired by
s60sc/ESP32-CAM_MJPEG2SD (icon rail + panels + status footer); all code
original — nothing copied (that project is AGPL-3.0, see `docs/camera.md`
§12.5):

```
div.wrap (column)
 ├ div.top (26px): [🧠 heap] [🌡️ temp] (left) + [▶ Start/■ Stop] [📷 Photo] [● Rec] [Hub] [Raw] (equal 84×22, centered; status pill same height)
 └ div.main (row)
    ├ div.side (LEFT: .rail + .panelwrap)
    │   ├ .rail: 4 vertical separators — each tab button fills 1/4 of the rail height with a large emoji (26px) +
    │   │         name rotated 90° in Title case (🎬 Camera / 🎨 Pictures / 📹 Recording / 📈 Motion);
    │   │         10px rounded corners, 1px border + inset bevel (3D), `writing-mode:vertical-rl`
    │   ├ #psecCam — phead "Camera Control" + presets + Res/Qual/FPS/…
    │   ├ #psecPic — phead "Picture Settings" + sliders + 7 checkboxes
    │   ├ #psecRec — phead "Record and Files" + actions + DashCam/Timelapse
    │   └ #psecMot — phead "Motion Detect" + sliders + Diff view link
    └ div.str (CENTER, bordered hero, object-fit:contain)
        ├ #recBadge (REC overlay only — FPS badge removed, lives in top status pill)
        ├ #camPh placeholder ("Waiting for camera..." + #phSub state line)
        └ #camImg (stream element)
 └ div.statusbar: #ftModel | #ftSd | #ftRec | #ftMot (always visible)
```

Mobile ≤1023px: `div.main` becomes column — stream on top (`min-height:32vh`),
controls below with page scroll. ≤480px: smaller buttons/labels.

Rules honoured: 🏠 is the first child of `.wrap`; no arrow functions in PROGMEM
JS; feedback via `customAlert`; `d.success` (never `d.ok`).

---

## 4. Inputs

| ID | Tab | Type / range | Backend key |
|---|---|---|---|
| `selFr` | Cam | select framesize. S3/legacy: values are the framework `framesize_t` enum ints: 320×240(6), 640×480(10), 800×600(11), 1024×768(12), 1280×720(13), 1280×1024(14), 1600×1200(15), 1920×1080(16), 2048×1536(19), 2592×1944(24). **Options above the detected sensor max are disabled** when known (see `applyMaxFrameSize`, §6). **P4 adaptive:** `resList` rebuilds the menu to seven OV5647 candidate modes, **sorted by pixel count** (`2592x1944` stills-only, `1920x1080`, `1280x960`, `800x1280`, `1280x720`, `800x800`, `800x640`) — 720p sorts below the RAW8/binning modes because its RAW10 profile stalls the pipeline (CAM-62), so the working modes lead the list — or USB negotiated geometry. MIPI selection posts `resolution=WxH`, persists as `p4_res_w/h`, and is hydrated from `p4_resolution`; live `width/height` takes precedence while capturing. The new 720p profile still needs HW verification. | `framesize` (legacy) + `resolution=WxH` (P4 MIPI) |
| `slQ` / `vQ` | Cam | range 4–63, def 20 (lower = better) | `quality` |
| `selFps` / `vFps` | Cam | range 1–30, def 12. **P4 adaptive:** status `fpsMax` caps the slider (MIPI 6 = measured sustained MJPEG; USB = negotiated, else 30); over-range value is clamped and posted. **Fase 4:** with the H.264 codec selected the cap is status `fpsMaxH264` (MIPI 30 = sensor rate); MJPEG keeps 6 | `fps` (clamped per framesize, see §9; H.264 recording uses the 1–30 POST clamp instead)
| `slGc` | Cam | select 2x–128x | `gainceiling` |
| `selSfx` | Cam | select None/Neg/Gray/Red/Green/Blue/Sepia | `special_effect` |
| `selWb` | Cam | select Auto/Sun/Cloud/Off/Home | `wb_mode` |
| `selSrc` (`#rwSrc`) | Cam | select `Native` (0 = MIPI/CSI, e.g. OV5647) / `USB camera` (1). P4-only: always visible on Cam-P4 (status flag `p4src`), hidden on other boards. Changing posts `source=N` and takes effect on the next Start (source is read at `begin`). **Attached-camera list (P4):** every status poll rebuilds the two options from the presence fields (`detMipi`/`detUsb`/`usbId`) — e.g. `Native — OV5647 (CSI)` / `USB — 1b3f:1167`, or `no CSI camera` / `none detected` when absent — so plugged cameras are visible before any Start; the persisted choice is kept when still valid | `source` |
| `slBr/slCt/slSa` | Pic | range −2…+2 | `brightness/contrast/saturation` |
| `slAeL` | Pic | range −2…+2 | `ae_level` |
| `slAgcG` / `vAgcG` | Pic | range 0–30 (shown +1×) | `agc_gain` (manual-gain mode) |
| `slAecV` / `vAecV` | Pic | range 0–1200 | `aec_value` (manual-exposure mode) |
| `chkHM/chkVF/chkAWB/chkAEC/chkAec2/chkAGC/chkLenc` | Pic | checkboxes | `hmirror/vflip/awb/aec/aec2/agc/lenc` |
| `chkDash` + `slDash` / `vDash` | Rec | checkbox + range 1–30 min | `dashcam_mins` (0 = off) |
| `slTl` / `vTl` | Rec | range 0–600 s, 0 = off | `timelapse_secs` |
| `selAuto` | Rec | select Off/Photo/Video/Motion/Timelapse (0–4) | `auto_mode` (boot auto-start mode, see §9) |
| `selCodec` (`#codecBlock`) | Rec | select MJPEG (`.avi`, default) / H.264 (`.264`). **P4-only:** the whole block is hidden unless status `h264Avail` (P4 MIPI + esp_h264 core); S3 never sees it. Changing posts `rec_codec=0|1` (server clamps to MJPEG off-P4) | `rec_codec` |
| `slH264Br` / `vH264Br` + `#h264Est` | Rec | range 1–8 Mbps, def 4. Shown only when H.264 is selected. Posts `h264_bitrate` in kbps; the estimate line shows `~X MB/min` (`Mbps*60/8/1024`, same formula as `cameraH264MbPerMin()`) | `h264_bitrate` |
| `selH264Cont` (`#rwH264Cont`) | Rec | select Raw (`.264`, default) / AVI (`.avi`). Shown only when H.264 is selected (P4-only block). Posts `h264_container` (server clamps to raw off-P4) | `h264_container` |
| `selVisMode` | Vis (P4-only tab) | select Off/Count/Defect (0–2, placeholders). Posts `vision_mode` (forced Off off-P4) | `vision_mode` |
| `slVisTh` / `vVisTh` | Vis | range 0–100, def 50. Posts `vision_thresh` | `vision_thresh` |
| `slVisN` / `vVisN` + `inVisLabel` | Vis | burst plan: 1–50 photos + label (≤16, FAT-safe). One-shot `POST /api/camera/vision/burst` (`n` + `label`); progress in `#visBurstInfo` (status `visBurstLeft`) | — (one-shot, not persisted as plan) |
| `chkMot` | Mot | checkbox | `motion_enabled` |
| `slMot/slMotCf/slMotSf/slMotPs` | Mot | 1–100 / 1–20 / 2–30 / 0–30 | `motion_sensitivity/_confirm_frames/_stop_frames/_post_record_secs` |
| `slMotCd` | Mot | range 0–120 s (0=off) | `motion_cooldown_secs` (0–600) |
| `chkMotLamp` | Mot | checkbox | `motion_lamp_auto` (full lamp during event) |
| `slRoiX/slRoiY/slRoiW/slRoiH` | Mot | 0–39 / 0–29 / 0–40 / 0–30 (0 = full frame) | `motion_roi_*` |
| `selMotAct` | Mot | select Record video/Snap photo (+ Email alert 0–2 only when SMTP compiled) | `motion_action` (clamped 0–2, or 0–1 without SMTP; legacy 2 folds to 1) |

Read-only: `recInfo` (⏱ elapsed | frames), `photoInfo` (last saved path),
`sdInfo` (free/total MB), `tlInfo` (next timelapse capture countdown + last saved
path, shown only while timelapse is active), `motSc` (motion score %),
`stText/stInfo` (model/fps/REC, `Starting...` while `initInProgress`).

Config POSTs are debounced 400 ms (`pCfg`) as `application/x-www-form-urlencoded`.
`loadCfg()` hydrates every input above (including `aec2`, AGC/AEC values, FX/WB,
timelapse) on page load. The Mot tab also links `View diff` → `/cam/motion/diff`
(new tab, calibration visualisation).

Tooltips: every button carries a native `title` (+ `data-i18n-title`), and the
toolbar / action-row / preset buttons show an instant styled tooltip via
`:hover::after` fed by `attr(title)` (rail excluded — horizontal overflow would
clip; full names already live in `.phead`). The Mot tab shows a `camMotHint`
line (camera must be ON; detection pauses while streaming).

---

## 5. Buttons

| Button | Calls | Effect |
|---|---|---|
| `bToggle` (single button) | `camToggle()` → `POST /api/camera/start` / `stop` | Green "▶ Start" → red "■ Stop" → neutral "■ Starting…" (`initInProgress`); async init / full stop; toggles stream element |
| `bSnap` (top) + Photo (Rec tab) | `snapPhoto()` → `POST /api/camera/photo` | Saves `/CAM/PHOTOS/<ts>.jpg`; shows path in `photoInfo` |
| `bRecS` (top, shows REC timer) + Rec (tab) | `recStart()` → `POST /api/camera/recording/start` | MJPEG: opens `/CAM/<day>/<ts>_<RES>_<FPS>.avi` on `/CAM/YYYY-MM-DD/` (ISO naming, shared with sniffer/sensor_history); pre-NTP sync → staged in `/CAM/PENDING/NOSYNC_<uptime>.avi` and auto-migrated to `/CAM/YYYY-MM-DD/` once the clock is ready. **H.264 (P4 MIPI, Fase 1 Tarefa 5):** opens `<ts>_<RES>_<FPS>.264` (raw Annex B NALs, SPS/PPS on every IDR, no RIFF header); finish appends `_<DUR>s_<TAG>` the same way; empty sessions delete the file and report failure. **Fase 2 container:** `h264_container=1` opens `<ts>_<RES>_<FPS>.avi` instead — H.264-in-AVI (`H264` fccHandler/biCompression, SPS/PPS extradata in strf, fixed 480-byte header); `/cam/playback` only decodes MJPEG AVIs (H.264 AVI plays as garbage there — known limit, raw `.264` stays the fallback) |
| Stop (Rec tab) | `recStop()` → `POST /api/camera/recording/stop` | Finalises AVI (header rewrite); shows `Saved N fr → path` |
| Stop (Timelapse) | `tlStop()` → `POST /api/camera/config` `timelapse_secs=0` | Stops periodic capture, zeroes the interval slider |
| Collect (Vis tab) | `visBurst()` → `POST /api/camera/vision/burst` | Arms N-photo dataset burst (`/CAM/VISION/<label>/`); needs camera ready; answers `{"success":true,"n","label"}` or `Need n=1..50 and label` / `Burst busy or invalid` / `Camera not ready` |
| Reset (Vis tab) | `visReset()` → `POST /api/camera/vision/reset` | Zeroes RAM OK/NOK counters |
| Presets HD/VGA/Fast/Night/Safe/Eco | `preset()` | Per-camera by design: **OV5647 (RPi v1.3)** posts an explicit `resolution=WxH` from `kP4CsiModes`; **USB-UVC** posts quality/fps/picture only (geometry is negotiated, so no `resolution` is sent). Both tables carry all six names, so the rail never has a dead button. Covered by `tests/web/test_camera.js` "preset coverage". One-shot config push (res/quality/fps/picture/AWB/AEC/AGC/gain). **S3/legacy:** `framesize` enum push. **Safe = measured stable on OV3660+S3** (HD 1280×720, q12, 10 fps → ~8 fps real, ~2 Mbps, heap flat). **Eco = lowest-cost** (QVGA 320×240 `fr:6`, quality 24, 8 fps) — least CPU/PSRAM/TCP per frame for long monitoring. **P4/Cam-P4 (CAM-68, per detected sensor):** OV5647 (RPi v1.3) posts explicit `resolution=WxH` from the sensor mode table — HD 1920×1080/q10/6fps, VGA 800×640/q20/6fps, Fast 800×640/q35/6fps, Night 800×800/q15/4fps (+1 bright, AWB off), **Safe 800×800/q12/5fps (HW-verified)**, Eco 800×640/q24/4fps; never 1280×720 (experimental profile stalls, CAM-62). USB-UVC posts quality/fps/picture only (geometry stays negotiated). fps clamped to status `fpsMax`. Tooltips follow the sensor (`applyPresetTitles()`); unknown/pre-start models keep the legacy path. |
| File select + Play/Delete (Rec tab) | `loadFiles()` / `playFile()` | Lists **both** containers, MJPEG `.avi` and raw H.264 `.264` (`/api/camera/files` accepts both extensions). `/cam/playback` only decodes MJPEG — a `.264` opens as garbage there and needs an external player (VLC). |
| Rail icons Cam/Pic/Rec/Mot | `showPsec()` | Switch settings panel (full name in `.phead`). Vision rail (`railVis`) appears only with P4 caps (`p4src`/`visMode` in status), and its panel must carry no inline `display:none` (the `.on` class drives visibility). Motion rail always visible (P4 motion via UYVY luma tap, S3 via sensor switch) |

---

## 6. Client logic

- **Explicit start only.** No auto-start on page load (was the main freeze
  trigger: stream + 3 s poll + config fetch all at once). `poll()` (5 s)
  syncs `R` with `/api/camera/status` and attaches/detaches the stream.
- **Stream element:** `<img src=/cam/stream>`; `#phSub` shows
  `Connecting to /cam/stream...`, then `Stream unreachable or busy (1 viewer max)
  — retrying...` on error, restored on load. `onerror` retries with 2 s
  backoff while `R`. `pagehide` detaches (`onerror=null`, `removeAttribute('src')`)
  so no zombie streams.
- **`upd(d)`:** Toggle button class/text (green/red/neutral), status pill (`model + fps`,
  `Starting...` while `initInProgress`), REC badge only, heap/temp pills
  (`🧠 134K`, `🌡️ 36.5°C`, `--` when unknown), motion score
  (always), SD free/total, REC timer (`mm:ss`) + frame count, timelapse
  countdown + last saved path.
  When recording stops the top REC button resets its label to `●` (a stale
  `● mm:ss` timer would otherwise linger forever).
- **Resolution limiting:** `applyMaxFrameSize()` (fed by `maxFrameSize` from
  `/api/camera/status`) disables every `selFr` option above the detected sensor
  max and auto-clamps a previously-selected out-of-range value back to the
  highest allowed (one-shot per detected max — guarded by `LastMaxFs`). So an
  OV3660 (max QXGA) never offers 5MP, and selecting one of the momentarily
  available larger values is impossible → no surprise server-side VGA/UXGA fallback.
- **Adaptive sizes/FPS (P4):** `applyResList()` rebuilds `selFr` to the
  `WxH` candidates; MIPI lists seven OV5647 modes (`2592x1944` stills-only +
  six video modes) and USB lists its negotiated
  geometry. MIPI selection posts `resolution=WxH`, persists `p4_res_w/h`, and
  is hydrated from `p4_resolution`; live status `width/height` overrides the
  saved choice during capture. The runtime switch tries `VIDIOC_S_SENSOR_FMT`
  and then SCCB fallback with revert-on-failure. **Panic RCA (2026-09-29):**
  the custom sensor format and `isp_info` were stack locals, but
  `esp_cam_sensor_set_format()` retains the format pointer; CSI later
  dereferenced expired data in `csi_get_input_bayer_order` (`Addr=0x20`). They
  are now persistent static descriptors. This fix, FHD and the new 720p profile
  still need build, OTA and HW verification; the current FHD Start crashed
  before the lifetime fix. Photos and AVI use the active capture mode, so the
  validated 1280x720 profile will serve both.
  `fpsMax` is 15 for MIPI (the OV5647 RAW8 modes are rated 50; the old fixed 6 was measured at HD and left the small modes idle) and `cameraClampFps()` uses the backend ceiling on P4 instead of the legacy enum. Absent `resList` (S3 or USB
  before negotiation), the legacy path applies unchanged.
- **Capture-stall guard (CAM-62, P4):** `VIDIOC_DQBUF` blocks inside the
  driver when the sensor stops delivering; a stalled 720p mode wedged the
  HTTP task inside a snapshot and rebooted the board via TASK_WDT (HW
  2026-09-29). Three layers now protect every capture path: (1) a 1.5 s
  driver DQBUF timeout set on the capture node when this core's esp_video
  supports `VIDIOC_S_DQBUF_TIMEOUT`; (2) a dequeue heartbeat with fast-fail
  (no good frame for 4 s → return 0 immediately, one recovery retry every
  3 s); (3) the loop task re-syncs a stalled MIPI session
  (stop/start capture, max once per 10 s). Status exposes `capStall` (1 =
  no frame for 4 s) and the page pill shows `[NO FRAMES]`, so a wedged
  sensor is diagnosed remotely instead of looking like a dead HTTP path.
- **Timelapse:** `tlCfg()` posts the interval (0 = off); `tlStop()` zeroes the
  slider and posts `timelapse_secs=0` in one tap; `upd()` renders `tlInfo`
  (`Next capture in Ns · <last path>`) while active.
- **Server-busy aware:** third viewer gets HTTP 503 (`Stream busy`) → img
  `onerror` retries until a slot frees (multi-viewer policy, max 2, §9).
- **View self-healing (CAM-63):** the live view must never need a full
  refresh. (1) The page only stops the img after 3 consecutive not-ready
  polls (15 s) — a single transient `ready:false` no longer kills the view;
  a later ready re-runs `strStart()` by itself. (2) The monotonic
  `streamFrames` counter is watched: unchanged across 3 polls (15 s) while
  the stream is wanted forces an in-page `/cam/stream` reconnect (frozen
  MJPEG fires no img event). (3) `/cam/raw` has the same watchdog on a 10 s
  status poll plus its `onerror` retry. (4) Server-side zombie eviction:
  dead TCP without FIN leaks the 2 viewer slots and every retry 503s
  forever; since a live stream always moves bytes (fresh or re-sent
  frames), no bytes for 20 s resets the counter and admits the newcomer
  (live response objects keep working).
- **Stream ACK timeout (CAM-67):** `AsyncClient` aborts a connection whose
  sent data goes unacknowledged for 5 s (`CONFIG_ASYNC_TCP_MAX_ACK_TIME`
  default) — any WiFi TX stall longer than that silently kills an
  actively-transferring stream every few minutes (HW-proven: a Pi probe
  died at 168 s mid-transfer with 4.3 MB flowing). `/cam/stream` and
  `/cam/playback` now set 30 s per viewer (`setAckTimeout`); genuinely dead
  clients are still reaped by the 20 s zombie eviction, so slots cannot
  leak. WiFi modem-sleep is already off (`WIFI_PS_NONE`), and RSSI/link
  were healthy during the deaths, pointing at TX stalls, not signal.
- **Source selector (P4 only):** `#rwSrc` shows when the status model is
  `OV5647`/`USB-UVC` **or** the status carries `p4src` (sent by Cam-P4 on every
  poll) — so on the P4 the row is always visible, even with `model: "Not
  detected"` before the first successful start. `srcCfg()` posts `source=0|1`;
  the backend reads it at the next `begin`, so Stop → Start (or Start) is
  needed to apply.
- **Attached cameras before Start (P4):** `cameraP4DetectSources()` runs on
  every status poll while idle (throttled to 10 s, never touches a live
  pipeline) — MIPI presence via SCCB ACK at `0x36`, USB presence via a quiet
  bus walk for a UVC VideoStreaming interface (`0x0E/0x02`, so CDC adapters
  never pose as cameras), VID/PID remembered from the scan or the last begin.
  Status carries `detMipi`/`detUsb`/`usbId`; `applySrcList()` rebuilds the
  Source options (`Native — OV5647 (CSI)` / `USB — 1b3f:1167`, honest
  `no CSI camera` / `none detected` labels when absent) and keeps the
  persisted choice when still valid. New log `[CAM-P4] detected: …` fires
  only on change (no 5 s poll spam); status doc grew 768→1024 B PSRAM.
- **Per-sensor presets (CAM-68, P4):** `preset()` reads `CurModel`/`CurIsP4`/`CurFpsMax`
  (latched from every status poll). OV5647 posts `resolution=WxH` + quality/fps/picture;
  USB-UVC posts quality/fps/picture only (no `resolution`, no `framesize` — the negotiated
  geometry is kept). Legacy enum presets apply only on S3 or pre-start unknown models.

---

## 7. Server API

| Method | Endpoint | Handler | Notes |
|---|---|---|---|
| `GET` | `/cam/stream` | `web_routes_camera.cpp` MJPEG state machine | `multipart/x-mixed-replace;boundary=--frame`; **max 2 clients** (3rd gets 503 `Stream busy`); **zero-copy** — each viewer `cameraStreamFramePin()`s the shared cache and sends straight from it (no per-viewer PSRAM frame copy), releasing the pin after each frame; **non-blocking** phase-0 (bounded ≤250 ms wait then resend, ACK clock never stalls); `TCP_NODELAY` disabled-Nagle on the viewer socket; `onDisconnect` unpins, decrements count and calls `cameraStreamStop()` when zero |
| `GET` | `/cam/raw` | `web_routes_camera.cpp` (PROGMEM one-liner) | Zero-chrome fullscreen `<img src=/cam/stream>` for a second PC — no auto.js, no polls; same pump cost as a stream viewer, counts toward the 2-client limit. **Local-processing page by construction**: no inactivity script (nothing to redirect), no session slot taken, inline `onerror` retry revives the img after hiccups — never closes by time (no session, no watchdog). |
| `GET` | `/cam/snapshot` | `handleCameraSnapshotGet` | Cache frame when streaming, else single direct capture; **never auto-starts** the stream (leak fix). While streaming it prefers a fresh frame but falls back to the latest cached JPEG younger than 5 s (CAM-64) — a slow viewer link pin-blocks the pump, so "fresh only" 503d every photo; older than 5 s still fails honestly |
| `GET` | `/api/camera/status` | `handleCameraStatusGet` | Existing runtime/recording/SD state + P4-only `p4src`, `resList`, `width`, `height`, `fpsMax`, `capStall`, `detMipi`, `detUsb`, `usbId`; the geometry fields drive resolution options/highlight, presence fields drive the pre-Start Source selector, `capStall` drives the `[NO FRAMES]` pill. **Fase 1:** `h264Avail` (HW encoder present → Codec block visible) + `recCodec` (0=AVI, 1=H.264 while recording). **Fase 2:** `recContainer` (0=raw `.264`, 1=H.264-in-AVI while recording). **Fase 4:** `fpsMaxH264` (MIPI 30, USB negotiated/else 30) — slider cap when H.264 is selected. **Follow-up:** `resList` is sorted by pixel count (working RAW8/binning modes ahead of the stalling 720p profile, CAM-62); `h264Diag` = last `p4H264Begin()` outcome — `ok`, `sw` (software encoder in use), a step tag (`res`/`alloc_in`/`alloc_out`/`open`) or `hw_new:<err>/sw:<err>` (`ESP_H264_ERR_ARG`=-2, `MEM`=-3, `UNSUPPORTED`=-5) — tells an H.264 start failure apart without a serial console. |
| `GET` / `POST` | `/api/camera/config` | `handleCameraConfigGet/Post` | Full struct incl. `aec2`, `timelapse_secs`, `auto_mode`, `lampAvail`, `motionEmailAvail`, nested `motion{}`; P4 GET also includes `p4_res_w/h` and effective `p4_resolution` for selector hydration. **Fase 1:** `rec_codec` (0 MJPEG default, 1 H.264; forced 0 off-P4), `h264_bitrate` (1000–8000 kbps, clamped), `h264Avail`. **Fase 2:** `h264_container` (0 raw default, 1 AVI; forced 0 off-P4). **Fase 5:** `vision_mode` (0 Off default, 1 Count, 2 Defect; forced Off off-P4), `vision_thresh` (0–100), `vision_burst_n` (0–50), `vision_burst_label` (≤16, sanitised on use). POST body may be **form-encoded (`key=value&…`) or JSON** (flat keys + nested `motion{}`); numeric fields are clamped server-side; P4 MIPI `resolution=WxH` must match a known OV5647 mode. A live sensor-mode switch failure returns `success:false` and rolls back saved config; malformed JSON returns `success:false "Invalid JSON body"`. |
| `POST` | `/api/camera/start` / `stop` | `handleCameraStartPost/StopPost` | `cameraBeginAsync(0)` / `cameraStop()` (now also `esp_camera_deinit()` — full HW stop of DMA/ISR/sensor clocks) + `cameraStreamResetClients()` (a leaked `onDisconnect` never holds slots across stop/start cycles) |
| `POST` | `/api/camera/photo` | `handleCameraPhotoPost` | **Cache-first** when streaming; **waits ≤400 ms** for motion sampling window; SD lock **2 s fail-fast** (was 8 s); returns `{"success":true,"filename":...}` |
| `GET` | `/api/camera/recording` | `handleCameraRecordingStatusGet` | `active/elapsed/frames/path` (page uses `/status` instead) |
| `POST` | `/api/camera/recording/start` / `stop` | `handleCameraRecordingStart/StopPost` | AVI open / header-rewrite + close; start clamps fps via `cameraClampFps()` so header duration matches sensor output; finish renames to `..._<DUR>s_<TAG>.avi` (`M` manual, `MOT` motion, `DASH` dashcam, `TL` timelapse). **Fase 1 (P4 MIPI):** `rec_codec=1` opens `.264` instead (encoder `esp_h264_enc_hw_new` with `O_UYY_E_VYY`, GOP=fps, RC at `h264_bitrate`; NALs verbatim, no header); non-MIPI source or a refused encoder answers `success:false` (`H.264 needs the P4 MIPI source` / `Record start failed (H.264 encoder)`). **Input format is silicon-dependent** (`ESP_H264_HW_IS_SUPPORTED_PIC_TYPE()`): the CSI pipeline yields UYVY, which the HW block accepts only on P4 rev < v3.0 — rev >= v3.0 takes `O_UYY_E_VYY` alone and rejects UYVY with `ESP_H264_ERR_ARG`; the software fallback takes YUYV/I420. `cameraH264PackUyvy()` (pure, native-tested) repacks the frame into whichever layout the opened encoder asked for, and `h264Diag` reports `sw` when the software encoder is in use. **Fase 4:** H.264 start uses the 1–30 POST-clamped `cfg.fps` (own ceiling, GOP=fps = up to 1 IDR/s at 30), not the MJPEG stable budget. Start failures report the **granular cause** (CAM-3): `Record start failed (SD unavailable)`, `(mkdir /CAM)`, `(mkdir folder)`, `(open file)` or `(header)` — returned by `cameraRecordStartEx()`/`cameraRecordStartCauseMessage()`; `mkdir` only counts as a failure when the folder still does not exist after the call (some FS layers report a false `mkdir` for a folder that was created). |
| `POST` | `/api/camera/motion/reset` | `handleCameraMotionResetPost` | Zeroes the persistent event counter |
| `POST` | `/api/camera/vision/burst` | `handleCameraVisionBurstPost` | **Fase 5 (P4-only):** arms a dataset burst (form `n=N&label=TAG` or JSON `{n,label}`); needs camera ready; vision task captures at ~1/s into `/CAM/VISION/<label>/`. Refusals: `Camera not ready`, `Need n=1..50 and label`, `Burst busy or invalid`, `Vision needs the P4` |
| `POST` | `/api/camera/vision/reset` | `handleCameraVisionResetPost` | **Fase 5:** zeroes RAM OK/NOK counters |
| `GET` | `/cam/motion/diff` | `handleCameraMotionDiffGet` | 40×30 visualisation JPEG (red = changed); calibration aid; same binary pattern as snapshot; also on Ethernet |

All `/api/camera/*` registered via `registerApiRoute()` (WiFi + Ethernet).
Responses < 4 KB (binary snapshot excepted).

---

## 8. Persistence

`/config/camera.json` via `camera_config.cpp` — **atomic write** (`.tmp` →
validate `bytes > 0` → `remove` + `rename`). Defaults: QVGA, quality 20,
**fps 12**, AWB/AEC/AGC/LENC on, `aec_value` 204, `agc_gain` 5, dashcam off,
timelapse off, `auto_mode` 0 (off), motion 20/3/10/5 + `action` 0 (record video).
Saved on every `cameraApplyConfig()`
(incl. fallback-clamped framesize/fps).
**Fase 1:** `rec_codec` 0 (MJPEG default), `h264_bitrate` 4000 kbps.
**Fase 2:** `h264_container` 0 (raw `.264` default).
**Fase 5:** `vision_mode` 0 (Off), `vision_thresh` 50, `vision_burst_n` 0, `vision_burst_label` "".

---

## 9. Runtime engine

- **Pre-NTP staging (recording):** while the RTC has not synced
  (`time(nullptr) < SD_CLOCK_SYNC_EPOCH`, i.e. booted before 2020-09-13), AVI/DASH/TL/motion
  files are written to `/CAM/PENDING/` with a `NOSYNC_<uptimeSec>` base name — never a
  fabricated date folder. Once the clock is ready the active file is migrated on finish
  to `/CAM/YYYY-MM-DD/YYYY-MM-DD_HHMMSS_<name>`, and `cameraRecordTick()` runs a
  **one-shot** sweep that moves any staged leftovers (reboots) into the dated
  folder and removes the now-empty `/CAM/PENDING`. Decision helpers live in
  `src/camera_record_naming.h` (pure, native-tested).
- **H.264 `.264` recording (Fase 1, Tarefa 5, CAM_P4 / P4 MIPI only):**
  `cameraP4CaptureWithRaw()` taps each dequeued UYVY frame once (raw emitted
  before the JPEG encode; null tap = old behaviour, zero cost) into a reused
  PSRAM staging buffer sized from the live geometry; `p4H264EncodeUyvy()`
  (esp_h264 HW, UYVY pic_type, GOP=fps, RC at `h264_bitrate`, 16-px row-pad —
  only 1080p needs it) returns the Annex B blob, appended verbatim by
  `cameraRecordFrameH264()` (first blob scanned for SPS/PPS+IDR, warn-only).
  MJPEG default everywhere; dashcam/timelapse/motion stay AVI. NAL/Annex B
  decisions live in `src/camera_p4_h264.h` (pure, native-tested:
  `test_camera_p4_h264`). esp_h264 is Apache-2.0, linked from the 3.3.12 P4
  core (SBOM + notices row, no vendored code); cores without it compile the
  wrapper to a stub and recording start refuses with a clear message. S3
  compiles nothing (all entry points stubbed) and the Rec Codec block is
  hidden without `h264Avail`.
- **H.264-in-AVI (Fase 2, Tarefa 5, CAM_P4 / P4 MIPI only):** `h264_container=1`
  records `<ts>_<RES>_<FPS>.avi` with `H264` fccHandler/biCompression and the
  SPS/PPS sets (stashed from the first IDR by `cameraH264ExtractSpsPps()`,
  all-or-nothing into a 256-byte reservation) in strf. Placeholder (start)
  and final (finish) headers share the fixed 480-byte layout, so `00dc`
  offsets never move; frames reuse the shared `00dc` chunk path (queue slots
  when available, sync buffered fallback). Finish rewrites the header with
  real counts + stashed sets, then the same duration/tag rename + pre-NTP
  migration as MJPEG. Muxer is first-party MIT (no new dependency, no SBOM
  change). `/cam/playback` walks `00dc` chunks as MJPEG only — H.264 AVIs are
  listed but play as garbage there (known limit); raw `.264` stays the
  default fallback.
- **Vision foundation (Fase 5, Tarefa 5, CAM_P4 / P4 MIPI only):** own task
  (`p4vision`, prio 2, 1 Hz) that never blocks capture/record — dataset
  burst (N JPEGs + sanitised label → `/CAM/VISION/<label>/`, ~1/s, needs
  camera ready), throttled model scan (`/CAM/VISION/model.espdl`, FTP upload
  path, presence + size to the Vision tab), and the inference hook probe:
  when a mode is armed AND a model exists AND the pump is idle, one UYVY
  frame is HW-resized full-res → 320x320 via PPA SRM (cover-fit crop math in
  `camera_p4_vision.h`, `PPA_SRM_COLOR_MODE_YUV422_UYVY` both ends, blocking
  trans); the infer-fps pill reports the preprocess rate (0 = hook idle).
  No model run yet — PicoDet/YOLO + real training stay off-device (next
  task). OK/NOK counters + last path are RAM (persistence follows models).
  PPA is an IDF driver (covered by the ESP-IDF SBOM row); ESP-DL links from
  the core (SBOM row added, Apache-2.0, hook-only).
- **Init:** `cameraBeginAsync()` (core 0, 6 KB task) with resolution fallback
  chain (requested → SXGA → XGA → SVGA → VGA → QVGA); **sensor is probed at
  init and the upper framesize bound is chosen from the detected model**:
  `cameraDetectedMaxFrameSize()` returns UXGA before init (matching the old
  hard cap), then PID-based afterwards — OV3660 (3MP, detected on .17) → QXGA
  2048×1536 (19), OV5640 (5MP) → 2592×1944 (24), OV2640/GC2145 → UXGA (15),
  NT99141/SC101IOT → HD (13), unknown → VGA; `cameraGetDetectedModel()` now
  reads the **full 16-bit PID** (`sensor_id_t.PID` is `uint16_t`; the old code
  truncated it to `uint8_t` and compared against high-byte constants
  `0x26/0x36/0x56`, producing `Unknown (0x60)` for the OV3660 whose low byte is
  0x60); **XCLK 10 MHz** (was
  20 MHz — S3 freeze workaround: halves the LCD_CAM/GDMA burst rate into
  PSRAM, correlated with a silent dual-core freeze under Wi-Fi STA);
  FB in PSRAM (`fb_count` 1 for ≥XGA, else 2–3 by free PSRAM);
  `CAMERA_GRAB_LATEST`; fps clamped (`recommendedMaxFps`: ≥XGA 10, SVGA 15,
  VGA 20, else 25; XCLK 10 MHz roughly halves the delivered frame rate vs
  20 MHz, so high-fps configs reach only ~half the clamp except in raw-burst modes).
  `clampFrameSize()` (framesize bounds): below 96×96 → VGA (safe floor); **above
  the detected sensor max → clamped DOWN to that max** (e.g. a 5MP request on an
  OV3660 yields QXGA 19, not a jarring 640×480 drop). Init is **serialised by a
  spinlock** (`s_camMux`): a boot auto-start racing a page Start (loop task vs
  HTTP task, different cores) creates exactly one CamInit task — two concurrent
  `esp_camera_init()` smash the driver queues (`cam_task` LoadProhibited in
  `xQueueGenericSend`, decoded via addr2line). The fallback chain settles
  200 ms between deinit and re-init. `cameraStop()` waits bounded (≤5 s) for an
  in-flight init, clears `s_ready` first, gives in-flight captures 400 ms grace,
  and deinits only if the camera was live.
- **Loop (`cameraLoop`, core 1):** when streaming, pre-capture at configured fps
  into a fixed **512 KB PSRAM cache** + `xSemaphoreGive(frameSema)` (sized to hold
  5MP 2592×1944 JPEG frames; QXGA/5MP don't fit the old 192 KB cache and were
  dropped); **the active
  AVI recorder is fed from the same cache** (single frame source — no second
  `fb_get` fighting for `fb_count=1` buffers; queue-full drops newest).
  **Zero-copy publish guard:** a stream viewer pins the cache while sending
  (`s_streamRefs > 0`) and the loop will not overwrite that buffer — the
  captured frame is dropped for the cache but **the AVI recorder is still fed
  from the freshly captured frame** (recording never starves behind a slow
  viewer). The viewer releases the pin after each frame, then the loop publishes
  the next one. This removes the old per-viewer `ps_malloc`/`free` JPEG copy
  (a recurring PSRAM fragmentation source).
  Motion sampling stays paused while streaming (sensor pixformat switch would
  tear the JPEG pipeline). When idle: 120 ms-throttled `cameraRecordTick`
  (dashcam/timelapse) + adaptive `cameraMotionTick`.
- **Auto-mode (Task 3):** `auto_mode` (0–4) is a persisted preference. When set
  to a non-zero mode, a one-shot boot hook (`cameraMaybeAutoStart()`, called from
  `loop()`) schedules `cameraBeginAsync()` after a ~3 s settle + 1.5 s task delay,
  so the chosen mode auto-starts at boot. `cameraArmAutoMode()` (called at the end
  of a successful `cameraBegin()` and on config change) fills in an unset default
  only: video → sets `dashcam_mins=5` if 0, motion → enables `motion.enabled`,
  timelapse → sets `timelapse_secs=10` if 0, photo → no continuous engine (camera
  simply initialised ready for snapshots). The MJPEG stream is **never**
  auto-started (stays viewer-driven); the continuous engine for video/motion/
  timelapse runs from `cameraLoop()` once the camera is live. The historical
  CAM-7 policy (the web *page* never auto-starts on load) is preserved — boot
  auto-start is strictly the opt-in `auto_mode` path. Precedence rule (CAM-50):
  an explicit engine-off in a config POST (`motion_enabled=0` / `dashcam_mins=0`
  / `timelapse_secs=0`) demotes the matching `auto_mode` (3/2/4) back to 0 in
  the same apply — otherwise `cameraArmAutoMode()` would resurrect the engine
  on every save and the checkbox could never stick. An explicitly (re)selected
  non-zero `auto_mode` in the same request wins.
- **Timelapse (Task 4):** driven by `cameraRecordTick()` at the configured
  `timelapse_secs` interval; `cameraTimelapseActive()` / `cameraTimelapseNextInSecs()`
  / `cameraTimelapseLastPath()` expose live state to the UI (`tlInfo` countdown
  + last path in `/api/camera/status`).
- **Motion:** 40×30 grayscale thumbnails (PSRAM ping-pong + PSRAM scratch),
  per-pixel diff vs
  sensitivity, ≥2 % changed pixels, confirm/stop run state machine.
  **Action dispatch (Task 5):** `motion_action` (0–2, or 0–1 without SMTP) fires once per event
  (rising edge, `s_actionFired` guard) — `record_video` (AVI auto-record +
  post-record seconds, the historical behaviour), `snap_photo` (single JPEG to
  `/CAM/MOTION/` via `cameraMotionSnapSave`), `email_alert` (text-only alert via
  `startInactivityAlertAsync`, single-flight + priority-gated, no attachment;
  compiled out with `ENABLE_SMTP==0`). Without SMTP a legacy stored `2` is folded
  to Snap photo (POST clamp + `cameraMotionBegin/SetConfig` sanitize), and the page
  removes the Email option (`motionEmailAvail` flag).
  Sampling opens a **guard window**
  (`cameraMotionSamplingActive()`, ~50–100 ms): photo/snapshot wait ≤400 ms,
  record/timelapse ticks skip the frame — never persists a grayscale thumbnail
  as JPEG. **Ownership rule:** the post-motion timeout only finalises sessions
  motion itself started; manual/dashcam recordings are immune. Cadence:
  idle 1-in-5 (~600 ms), active every other tick (~240 ms) at the 120 ms loop
  throttle → ~1.8 s to trigger with confirm=3. Diff visualisation served at
  `/cam/motion/diff` (previously dead code: `cameraMotionBuildDiffJpeg` had no callers).
- **Recording:** background `CamRecWr` task (core 0) + 16 KB staging buffer,
  adaptive flush policy under SD backpressure, drop-newest on full queue.
- **PSRAM (Task 2):** frame buffers (`CAMERA_FB_IN_PSRAM`), the 512 KB stream
  cache, motion thumbnails + scratch, AVI staging buffer + packet slots, and
  motion diff RGB all live in PSRAM (`ps_malloc`). The last DRAM static (the
  `thumb[]` scratch in `cameraMotionTick`) was moved to PSRAM, keeping the
  internal heap free (~150 KB+ after init).
- **`cameraFrameSizeToDims`:** the framesize→pixels table was corrected to the
  real `framesize_t` enum (0–24); it previously used a shifted enum (e.g. `10`
  mapped to 1024×768 instead of VGA 640×480), silently corrupting AVI headers
  and resolution labels. Now single-sourced in `camera_sensor_map.h`.
- **Ethernet note:** `ethernetHandleCameraStreamGet` is a blocking loop in the
  sequential bootstrap server (other Ethernet clients queue) — acceptable for
  camera boards; Cam-M pack disables Ethernet.

---

## 10. i18n keys

`camTitle/camWaiting/camStart/camInit/camOff/camHeap/camStartFail`,
`camTipHome/camTipStart/camTipStop/camTipCapture/camTipRecord/camTipTogglePanel(unused, kept)/camTipTabCam/camTipTabPic/camTipTabRec/camTipTabMot`,
`camTipPresetHd/camTipPresetVga/camTipPresetFast/camTipPresetNight/camTipPresetEco`,
`camTipResolution/camTipQuality/camTipFps/camTipGainCeil/camTipFx/camTipWb/camTipBright/camTipContrast/camTipSaturation/camTipAeLevel/camTipAgcGain/camTipAecValue`,
`camTipMirror/camTipFlip/camTipAwb/camTipAec/camTipAec2/camTipAgc/camTipLenc`,
`camTipRecStart/camTipRecStop/camTipDashcam/camTipDashRot/camTipTimelapse/camTipTlStop`,
`camTipMotion/camTipMotSens/camTipMotConfirm/camTipMotStop/camTipMotPost/camTipMotDiff`,
`camTipMotAction/camTipAutoMode`.
**Fase 1:** `camCodec/camTipCodec/camBitrate/camTipBitrate/camTipEst`.
**Fase 2:** `camContainer/camTipContainer`.
**Fase 3:** `camPic5mpNote`.
**Fase 5:** `camTipTabVis/camPanelVis/camVisMode/camTipVisMode/camVisThresh/camTipVisThresh/camTipVisModel/camTipVisOk/camTipVisNok/camTipVisLast/camTipVisFps/camVisBurst/camVisN/camTipVisN/camVisLabel/camTipVisLabel/camTipVisBurst/camVisGo/camTipVisReset/camVisResetC/camTipVisBurstInfo`.
EN + PT kept in sync (`lang_en_ui.h` / `lang_pt_ui.h`).

---

## 11. Review findings table

| ID | Finding | Status |
|---|---|---|
| CAM-1 | MJPEG phase-0 blocked AsyncTCP up to 1 s/frame → whole web UI froze. Non-blocking take + `TRY_AGAIN`. | fixed |
| CAM-2 | Unlimited parallel streams exhausted Async clients/heap. Single-viewer 503 + stop-on-zero-clients. | fixed |
| CAM-3 | Photo/snapshot/record each did own `fb_get` vs loop pre-capture (`fb_count=1`). Cache-first + record-fed-from-cache. | fixed |
| CAM-4 | Snapshot auto-started stream and leaked `s_streamActive` with no viewers. Removed auto-start. | fixed |
| CAM-5 | Photo SD lock 8 s stalled HTTP task. Fail-fast 2 s + `SD busy`. | fixed |
| CAM-6 | `cfg()` read `$('chkAec2')` but element missing → TypeError, no config ever saved. Checkbox added + null-guard + `loadCfg` hydration. | fixed |
| CAM-7 | Page auto-started camera on load. Explicit Start only; poll 3 s → 5 s; `pagehide` detaches stream. | fixed |
| CAM-8 | `timelapse_secs` + `aec2` persisted but had no UI; no recording Stop; photo filename discarded; SD free ignored. All surfaced. | fixed |
| CAM-9 | Defaults fps 20 aggressive for first connect. Default 12 (caps per resolution kept). | fixed |
| CAM-10 | Motion score only updated while recording (early `return` in `upd`). Always updated. | fixed |
| CAM-11 | Motion ROI (`roi_x/y/w/h`) persists + API but has no UI. Open — numeric ROI inputs still missing. | open |
| CAM-12 | Ethernet `/cam/stream` remains a blocking loop (sequential bootstrap server). By design; Cam-M disables Ethernet. | wontfix |
| CAM-13 | Motion grayscale window raced photo/snapshot/record captures → 96×96 gray frame persisted as JPEG. **Fixed:** sampling guard (wait ≤400 ms / skip) in all three paths. | fixed |
| CAM-14 | Motion post-timeout finalised ANY active recording incl. manual/dashcam. **Fixed:** ownership flag, only own sessions. | fixed |
| CAM-15 | Idle sample ~1.2 s + confirm 3 → ~3.6 s to trigger motion. **Fixed:** idle skip 9→4 (~600 ms, ~1.8 s trigger). | fixed |
| CAM-16 | Direct-API POSTs unclamped (`quality=200` → uint8 wrap). **Fixed:** clamp table on all 22 numeric fields + framesize enum guard. | fixed |
| CAM-17 | Recording start used raw cfg fps (e.g. 30 at UXGA, sensor capped 10) → AVI duration desync. **Fixed:** `cameraClampFps()` in start path (new public helper, also used by apply). | fixed |
| CAM-18 | `cameraMotionBuildDiffJpeg` dead code; header documented `/cam/motion/diff.jpg` which did not exist. **Fixed:** wired `GET /cam/motion/diff` (WiFi + Ethernet) + Mot-tab link. | fixed |
| CAM-19 | `POST /api/camera/config` with a JSON body returned `"No changes"` (form-only parser) — blocked API-based config (tests, camera-tab JSON clients). **Fixed:** JSON body support (flat keys + nested `motion{}`; booleans/numbers normalised to the shared clamp rule). | fixed |
| CAM-20 | Enum audit vs framework `framesize_t` (6=QVGA … 24=5MP): firmware already on the new numbers via `camera_sensor_map.h`; native stream-test constants still used the legacy 5/8/9/10. **Fixed:** test constants + default-sync (api test default 6). | fixed |
| CAM-21 | `streamFps` reads 0 while frames flow (rolling 1 s window) — no way to tell "no data" from "window not elapsed". **Fixed:** monotonic `streamFrames` per session in `/api/camera/status` + LIVE badge `12 FPS · 345 fr` + curl check (must increase). | fixed |
| CAM-22 | Dark theme + stretched Start/Stop + clipped tab/preset labels ("C/PI/RI/M", "NIG"). **Fixed:** blue theme, 32px topbar with fixed-size buttons, 2×2 full-label tabs, unclipped presets, slim rows. | fixed |
| CAM-20 | S3 silent freeze (~1 s after `[CAM] Camera init OK`, no panic, power-cycle-only recovery): continuous LCD_CAM/GDMA JPEG capture into PSRAM under Wi-Fi STA. RCA: ESP32-S3 dual-core + camera-DMA silicon/driver issue (community: works unicore; espressif mitigation is `LCD_CAM_ISR_IRAM_SAFE`, n in the prebuilt framework camera lib — not settable from this repo). **Mitigations:** XCLK 20→10 MHz (halved burst rate) + `cameraStop()` now calls `esp_camera_deinit()` (parks the capture engine between sessions). Definitive fix candidates pending HW validation: framework camera rebuild with `LCD_CAM_ISR_IRAM_SAFE=y`, or a unicore build. | open |
| CAM-21 | **Resolution capped at 1280×720** (HD) — the "5MP" module on .17 never delivered beyond 720p. RCA: three stacked caps — (a) API `framesize` guard `fs<=13`; (b) `clampFrameSize()` hard cap UXGA; (c) page `selFr` only offered value≤13 and those values were **shifted −1 vs the framework enum** (e.g. `1280×720` option had value `11`, framework `HD=13`), so every UI selection picked a *lower* resolution than its label; presets `hd.fr=11/vga.fr=8/fast.fr=5/night.fr=5` sent the same wrong enums. **Fixed:** API accepts 0–24; clamp bound now `cameraDetectedMaxFrameSize()` (sensor-based, UXGA pre-init); `selFr` values aligned to the framework enum + added 1920×1080(16)/2048×1536(19)/2592×1944(24); presets `hd.fr=13/vga.fr=10/fast.fr=6/night.fr=6`. | fixed |
| CAM-22 | `cameraGetDetectedModel()` returned `Unknown (0x60)` for the .17 sensor. RCA: `sensor_id_t.PID` is `uint16_t` but the code read it as `uint8_t` then compared to high-byte constants (`0x26/0x36/0x56`) — the truncated low byte never matches. Live symptom `0x60` ⇒ real PID low-byte `0x60` ⇒ **OV3660 (3MP)**, matching a camera that initialises: OV5640 would truncate to `0x40`. **Fixed:** full 16-bit PID + framework PID table in `camera_sensor_map.h`; init now logs `PID=0x….#### model=… maxFs=…`. | fixed |
| CAM-23 | `cameraFrameSizeToDims()` and `frameSizeLabel()` used a **shifted enum** (e.g. `case 10 → 1024×768`, but `FRAMESIZE_VGA = 10 = 640×480`; `case 13 → 1600×1200`, but `13 = HD = 1280×720`), corrupting AVI frame dimensions and resolution labels. **Fixed:** table rewritten to the real 0–24 enum, single-sourced in `camera_sensor_map.h` (`cameraSensorFrameSizeDims`), `frameSizeLabel` aligned (incl. FHD/QXGA/5MP). | fixed |
| CAM-24 | No auto-start / timelapse / motion-action control: camera always booted inert, timelapse had no live countdown, motion could only auto-record AVI. **Fixed (Tasks 3–5):** `auto_mode` (0–4) armed at init/config-change; timelapse countdown (`timelapseActive/NextInSecs/Last`) surfaced in `/api/camera/status` + `tlInfo`; `motion_action` (record_video/snap_photo/email_alert) with once-per-event dispatch. | fixed |
| CAM-25 | Motion scratch thumbnail (`static uint8_t thumb[1200]`) sat in DRAM. **Fixed (Task 2):** moved to a PSRAM `s_thumbScratch` buffer allocated in `cameraMotionBegin()`. | fixed |
| CAM-26 | `auto_mode` only *armed* the continuous engine but never auto-initialised the camera — Nuno's "ligar automaticamente no boot" (Task 3) was not actually delivered. **Fixed:** `cameraMaybeAutoStart()` boot hook (loop) schedules `cameraBeginAsync()` when `auto_mode != 0` after a boot-settle delay; `cameraArmAutoMode()` (unchanged) still fills the unset default so the chosen mode runs once live. Plus Timelapse Stop button + last-path in `tlInfo` (Task 4 UI). | fixed |
| CAM-27 | Configuring while the camera is OFF returned `"Config apply failed"` (spurious) and the handler re-saved the raw `cfg` over the already-clamped persisted values. **Fixed:** `cameraApplyConfig()` returns true when persisted-but-not-ready (sensor apply runs on next init); handler no longer double-saves. | fixed |
| CAM-28 | Picking a resolution above the detected sensor max silently fell back to **VGA** (`clampFrameSize()` collapsed out-of-range to `FRAMESIZE_VGA`): selecting 5MP on the OV3660 gave 640×480 with no hint. **Fixed (Muse audit):** `clampFrameSize()` now clamps DOWN to the sensor max (5MP→QXGA on OV3660); `/api/camera/status` exposes `maxFrameSize`; page `applyMaxFrameSize()` disables `selFr` options above it and auto-clamps the current selection once per detected sensor. | fixed |
| CAM-29 | Top-bar REC button (`bRecS`) showed a stale `● mm:ss` timer forever after recording ended (`upd()` early-returned before restoring it). **Fixed (Muse audit):** when `recording` is false the button label is restored to `●` if a timer had been shown. | fixed |
| CAM-30 | `CRASH_PANIC Task=cam_task Cause=28(LoadProhibited) Addr=0x2a` at ~5 s uptime. Decoded (addr2line on `firmware.elf`): `cam_task` → `xQueueGenericSend` on a dead queue. RCA: **double `esp_camera_init()`** — boot auto-start (loop task) racing page Start (HTTP task, other core) passed the volatile check-then-set and created two CamInit tasks. **Fixed:** `s_camMux` spinlock makes check+set atomic; 200 ms settle between fallback deinit and re-init. | fixed |
| CAM-31 | Same crash signature via Stop path: `cameraStop()` called `esp_camera_deinit()` while the loop task was inside `fb_get` (single `s_ready` check, no drain). **Fixed:** Stop waits bounded (≤5 s) for in-flight init, clears `s_ready` first, 400 ms capture grace, deinits only if live. Guard protocol covered by `test_camera_initguard`. | fixed |
| CAM-32 | Layout restyle: MJPEG2SD-inspired icon rail + titled panels + bottom status bar, our blue palette, original code only (AGPL no-copy). Rail captions short by design; full section names in sticky `.phead`; footer (`ftModel/ftSd/ftRec/ftMot`) always visible; mobile rail becomes horizontal. i18n `camPanelCam/Pic/Rec/Mot` EN+PT. | fixed |
| CAM-33 | Motion event counter + cooldown + best-frame + lamp-auto + ROI UI (OpenCV-inspired product-counter lote A). Counter persisted throttled; `motionEvents` in status; `POST /api/camera/motion/reset`; best JPEG saved on falling edge for SNAP/EMAIL; lamp full during event then restore; ROI sliders 0–39/0–29. | fixed |
| CAM-34 | Timelapse wrote loose JPEGs to `/CAM/TIMELAPSE/` (legacy `cameraTimelapseSave`, since removed — no callers). Now appends 1 frame/interval to a 10 fps `_T` AVI (day-rotated on `/CAM/YYYY-MM-DD/`, yields to manual/motion/dashcam, dashcam wins on conflict). | fixed |
| CAM-35 | AVI filenames had no duration/origin. Finish now renames to `..._RES_FPS_<DUR>s_<TAG>.avi` (`M`/`MOT`/`DASH`/`TL`); playback fps parser finds the token after the RES label. | fixed |
| CAM-36 | Live stream froze after ~7 frames while the loop cache kept updating (snapshots differed). RCA: pure TRY_AGAIN spin — the chunked framework only retries on TCP activity, so one empty poll with no unacked data deadlocks the pump. **Fixed:** phase-0 waits ≤250 ms then (re)sends the current frame (ACK clock never stalls; fps dedup stays honest). Playback pacing removed for the same reason (runs at SD speed). **Validated live 12-09:** 60 s single viewer → 6.75 MB / 705 frames / ~12.4 fps VGA, heap flat ~131 KB, clean disconnect. Earlier "drops" were test artefacts (status read after curl exit). | fixed |
| CAM-37 | Manual recordings died on the next loop tick (`Not recording` 2 s after start). RCA: the dashcam-off branch finished ANY active session. **Fixed:** `s_dashOwn` ownership (only closes leftover dash files) + generic idle pump feeding manual/motion sessions at session fps (these also recorded zero frames when idle). | fixed |
| CAM-38 | Recording stop returned the pre-rename path (no `_DURs_TAG`). **Fixed:** path read after finish. Stream viewer connect/disconnect now logged; `streamClients` exposed in status for the single-viewer audit. | fixed |
| CAM-42 | Camera dated folders/files used `YYYYMMDD` while sniffer/sensor_history/PSRAM used `YYYY-MM-DD`. **Fixed:** `camera_record_naming.h` now emits ISO `YYYY-MM-DD` everywhere (`/CAM/YYYY-MM-DD/YYYY-MM-DD_HHMMSS...` incl. the TL AVI day-rotation key). Pure header, tests updated (files are never emailed, so the change is safe). | fixed |
| CAM-43 | `cameraTimelapseSave` (single loose JPEG `/CAM/TIMELAPSE/`) had zero callers — dead code. **Removed** (def + decl + comments; `CAM_TL_FOLDER` kept for the native naming test). Also removed the unused `SDRotationManager` `/logs/data_*.txt` fan-out (no `getFilesForEmail` callers → nothing was emailed from it). | fixed |
| CAM-40 | Hardware sweep OV3660+S3 (q15/fps10): all 9 resolutions stream stall-free, bytes exactly 2× over 5→10 s, heap 130–136 KB flat. Delivered ceiling (XCLK 10 MHz): VGA ~13, HD ~8, UXGA ~6–7, QXGA ~5 fps. **Safe preset**: HD/q12/10 fps. | fixed |
| CAM-41 | **Pack Cam-P4 created** (`pack_cam_p4.h`, "Cam-P4", same pages/concept: /camera, /hub, /monitor, /files, /sdcard; Ethernet RMII + WiFi-via-C6, no USB/GRBL). Wiring: selector ID 11, user_config hint, home subtitle, build_firmware.sh + .bat menus/lists, rename map. **P4 build BLOCKED: no upstream P4 camera driver exists** — esp32-camera (all releases + master) has zero P4/CSI support and the Arduino core ships no P4 esp_camera; `esp_camera.h` is absent for esp32p4. Unblocked when upstream ships P4 CSI (or a DVP bring-up is done with hardware on desk). S3 path unaffected. | open |
| CAM-42 | **P4 backend implemented + statically audited (uncompiled, no HW here)**: `camera_p4_backend.*` drives Arduino ESP_Video (MIPI CSI + ISP JPEG, OV5647) and prebuilt `usb_host_uvc` (any MJPEG webcam) behind the shared pump — stream cache, AVI, snapshot/photo, record tick, playback, Ethernet paths all reuse generic code via a `camera_fb_t` shim; `framesize_t` fallback enum in `camera_sensor_map.h` keeps camera.json compatible (order-independent via `__has_include`). Audit fixes: P4 stream cache always MAX (pipelines negotiate size — Res select accepted-but-unverified), zero-alloc P4 loop path (reusable PSRAM staging), Source row hidden on S3 / P4 motion note in UI, explicit IDF includes, `s_video.end()` verified idempotent/reusable. v1 limits: motion sampling + diff JPEG off on P4, picture knobs beyond quality/flip persisted no-ops, no USB hot-plug, USB quality/flip no-ops. **HW checklist before first P4 build**: ESP_Video present, MIPI Kconfig on, OV5647 on CSI (or MJPEG webcam + Source=USB), then Start → snapshot → stream → AVI. | open |
| CAM-44 | **P4 build fixes from first esp32p4 compile**: `#ifndef` on enumerator names can never detect the framework enum (only macros) — S3 double-defined `framesize_t`; fixed via `CAM_FRAMESIZE_MIRROR` on (P4 OR no-ARDUINO). Latent `ftp_client.cpp` default-args redeclaration exposed by header touch + GCC14 (fixed). Lesson: never run two pack builds sharing `.pio-linux` (linker ate retrieved objects). | fixed |
| CAM-45 | Stream viewers 1→2 (shared cache, private pump state each, 3rd gets 503) + `/cam/raw` zero-chrome fullscreen page (same pump cost, no page overhead) + Raw button on the toolbar (EN+PT). | fixed |
| CAM-46 | Toolbar unified: Start/Stop/Photo/Rec/Hub/Raw all equal 84×22, centered group, full labels, 3px radius; status pill matched to 22px. Root cause of the giant buttons: shell `core.css` (`button{min-height:40px;font-size:16px}`) beat the asset — guard now neutralizes every core.css button property with scoped `!important`. | fixed |
| CAM-47 | Long-session heap churn: status/config/recording JSON built a heap `String` per call (5 s polls). Telemetry (`minHeap`/`maxAlloc` in status + footer readout) kept. A zero-heap slot (`HttpResponse::sendJsonBuf` + dispatcher `beginResponse(ptr)`) was tried and **reverted**: that overload builds `AsyncProgmemResponse`, which never copies — the slot died with the handler frame (use-after-free, intermittent garbled JSON on the wire). Lesson: only `beginResponse_P` (static PROGMEM) or heap-owned buffers with `freeFn` may bypass the copy. | fixed |
| CAM-49 | Tooltips everywhere: instant styled `:hover::after` (from native `title`) on toolbar/action/preset buttons + `camMotHint` line in Mot tab (EN+PT). | fixed |
| CAM-50 | FPS badge removed from stream area (was duplicated in top status pill); top bar gains left-aligned heap + board-temp pills (`🧠 134K`, `🌡️ 36.5°C`, `--` unknown; `boardTemp` S3-only in status, P4 omitted); status doc bumped 512→768 (PSRAM, headroom for the new field). | fixed |
| CAM-51 | Start/Stop merged into single `bToggle` button: green "▶ Start" ↔ red "■ Stop" ↔ neutral "■ Starting…" (`initInProgress`); `camTipStop` removed (EN+PT), `camTipStart` reused. | fixed |
| CAM-48 | Leaked `onDisconnect` held MJPEG slots forever (stop/start never cleared `s_mjpegClients`). **Fixed:** `cameraStreamResetClients()` on stop; live-audit script warns when `streamClients>0` at start (browser open). | fixed |
| CAM-43 | **P4 USB-UVC source (uncompiled)**: `uvc_host_uvc` ships prebuilt in the P4 framework libs, so USB webcams ride the same backend — MJPEG format negotiation, PSRAM frame slot fed by the driver-task callback, capture waits for fresh generations. `CameraConfig.source` (0=MIPI/CSI, 1=USB; persisted, clamped, UI select, status model shows `USB-UVC`/`OV5647`); pack enables `USB_HOST` (OTG becomes host — USB CDC dead, debug via UART0/web monitor); quality/flip persist-only on USB. v1 limits: no hot-plug while running (re-Start), MJPEG-only. | open |
| CAM-39 | Options lost on reboot (all defaults). RCA: schema grew to ~37 keys (~950 B) but the JSON doc stayed 896 B — save wrote truncated JSON, load failed parse. **Fixed:** doc 1536 B + `overflowed()` guard that refuses to persist a truncated doc. | fixed |
| CAM-53 | **Cam packs must not offer email.** Owner decision: no emails in Cam-M / Cam-P4 (no `/email` config page, no motion email alerts). **Fixed:** `pack_cam.h` + `pack_cam_p4.h` set `ENABLE_SMTP=0` + `ENABLE_GOOGLE_EMAIL=0` (same pattern as Radio/S3-CNC/Touch7/P4-CNC packs, so all email routes/buttons/handlers compile out incl. `/email` + home button, WiFi and Ethernet); camera motion exposes `motionEmailAvail=false` in `/api/camera/config`, the page drops the "Email alert" option and folds a legacy stored 2 to Snap photo (1); server clamps `motion_action` 0–1 and `cameraMotionBegin/SetConfig` sanitize a legacy 2. | fixed — needs `pio run -e n16r8` (Cam) + `pio run -e esp32p4cam` (Cam-P4) + board checks |
| CAM-52 | **Stream cost reduction (this change).** (a) The MJPEG pump allocated a private PSRAM buffer per viewer and `memcpy`'d every frame into it (a varying-size `ps_malloc`/`free` per frame → PSRAM fragmentation, plus an extra full-frame copy). **Fixed:** zero-copy `cameraStreamFramePin()`/`Unpin()` — viewers send straight from the shared cache; the loop defers the cache publish (not the capture) while pinned and keeps feeding the AVI recorder from the freshly captured frame. (b) Nagle added latency on the viewer socket. **Fixed:** `TCP_NODELAY` (`AsyncClient::setNoDelay(true)`). (c) No low-cost capture preset. **Fixed:** `Eco` preset (QVGA/q24/8 fps). Net per-viewer cost: one less full-frame PSRAM copy + one less alloc/free per frame. | fixed — needs `pio run -e n16r8` + live HW check (board .17) |
| CAM-54 | Sidebar rail restyle (owner request): CAM/PIC/REC/MOT tabs now divide the rail's full vertical height as separators — each button fills 1/4 (`flex:1 1 0`, `width:100%`) and carries emoji + full name rotated 90° (🎬 CAMERA / 🎨 PICTURES / 📹 RECORDING / 📈 MOTION via `writing-mode:vertical-rl` + `text-orientation:upright`, `letter-spacing:2px`). Names hardcoded English (same pattern as the old short labels — no new i18n keys). Mobile <1024px keeps the horizontal rail, text hidden (emoji-only buttons). Rail tooltip exclusion comment updated. Total rail width unchanged (52px). | fixed |
| CAM-55 | Rail separators polish (owner follow-up): emoji doubled to 26px desktop / 30px mobile, 10px gap between emoji and name, name no longer uppercased (Title case: Camera/Pictures/Recording/Motion), 10px corner radius, and a 3D "raised" look — 1px light border line + inset top/bottom/left/right bevel highlights & shadows (`.on` gets a brighter variant). Guard in the `core.css` neutraliser updated to the new radius. | fixed |
| CAM-56 | Source row always visible on Cam-P4 (owner request — hidden row was misleading: it only appeared after a successful start, so with `model: "Not detected"` there was no way to switch MIPI↔USB). **Fixed:** `/api/camera/status` now sends `p4src:true` on P4 (`camera_handler.cpp`, `#if TARGET_BOARD_P4==1`; omitted on other boards); `upd()` shows `#rwSrc` when `model` is `OV5647`/`USB-UVC` **or** `p4src` is present — S3/others unchanged (still hidden). `srcCfg()` unchanged (`source=0|1`, applied at next Start). Web tests: 4 new `srcRowDisplay` cases (P4 pre-start visible, non-P4 hidden, OV5647/USB-UVC legacy visible). | fixed |
| CAM-57 | P4 resolution UI and AVI geometry. Status lists six OV5647 candidates and live `width/height`; explicit resolution persists in `p4_res_w/h`, GET config returns `p4_resolution`, and all AVI start paths now use actual output dimensions. Stream/cache limit is 2 MiB for larger P4 JPEGs. Six-mode runtime switch via `VIDIOC_S_SENSOR_FMT`/SCCB is experimental pending build+OTA+HW validation; previous confirmed mode remains 800x800 (~5.8 fps). New 1280x720 profile is binned/cropped RAW10 at a target 30 fps, derived from the local Espressif 1280x960 profile and the OV5647 datasheet; unverified. | needs owner build + OTA + MIPI mode/photo/video HW check |
| CAM-19b | CAM-19 regressed: the unified-registry rewrite (`route_registry_api.cpp`) rebuilt `HttpRequest.body` from form params only, so **every** JSON POST silently arrived empty (`No changes`, even malformed JSON). **Fixed:** `onBody` accumulator (4 KB cap, form/multipart zero-overhead) + real `contentType` forwarding; `_tempObject` freed on all paths. Restores the documented form-or-JSON contract for all unified routes; OTA/firmware-upload unaffected (legacy route). | fixed — needs board check (JSON POST + form POST + OTA still fine) |
| CAM-58 | USB webcam (1b3f:1167) accepts MJPEG probe/commit at 1280x720 and 640x480 yet delivers zero frames in 8s, zero xfer errors (HW-tested 2026-09-24, v1.115). **Fixed in backend (uncompiled):** `p4UsbBegin` now (1) retries `stream_open` 3× (stale claim after stop/start → transient NOT_FOUND), (2) dumps the device's offered VS frames via `uvc_host_get_frame_list` to the Web Serial Monitor, (3) iterates MJPEG size candidates — requested, device's own MJPEG entries (dedup), VGA last resort — with 4 s frame-proven wait each (worst ~20 s on Start), (4) YUY2 data-path probe distinguishing dead-device vs wrong-format (pump serves MJPEG only; YUY2 needs SW JPEG encode = future), (5) prime in `cameraP4Begin` trimmed 8 s→2 s confirm (begin already proves frames). No API/route/UI change. **HW retest of that build (2026-09-25):** both candidates still silent (4 s each, no xfer error, no frame/underflow event) → confirmed the prebuilt `uvc_host` binds+starts but the isoch pipe yields nothing. Cause of the blind spot found: `DEVICE_CONNECTED` only fires on a real plug, so `dev_addr` was never captured (`frame list: no device addr seen yet`). **Round 2 (uncompiled):** address/function discovery via `usb_host_device_addr_list_fill` + UVC descriptor probe (logs bus devices, picks addr+`uvc_stream_index` 0..2 and uses them for open), frame dump now with fps, `urb_size` 4xMPS→16 KB and URBs 3→8 (the 3×4 KB default cannot saturate a HS isoch pipe at 720p30 MJPEG), per-candidate post-start `format_get` log, YUY2 probe with explicit open/start/data logs, and `uvc_host_desc_print` (full config descriptor: endpoints, alternate settings, frame descriptors) to the console on total failure. No API/route/UI change. **Rounds 3-4 (HW-tested 2026-09-25, builds 13:44/13:55/14:07/14:16/14:27/14:36/19:32/19:44/19:49/19:54/20:04/20:09/20:33, v1.115):** (a) the frame list is now read **before** the stream opens (the component only answers it while nothing has claimed the device): 24 VS frames on stream 0 — MJPEG 1920x1080/2560x1440/1280x720/800x480/640x480/320x240/640x360/800x600, YUY2 7 sizes, H264 8 sizes, all @3 fps; (b) bus scan + full configuration-descriptor dump through a temporary `usb_host` client taken **before** `uvc_host_install` (async clients need a non-null `async.client_event_callback`, else `ESP_ERR_INVALID_ARG`): webcam 1b3f:1167 is High-Speed, bcdUSB 0x200, bcdUVC **0x104** (UVC 1.4), control EP 0x83, VideoStreaming interface 1 with **8 alternate settings** — alt 0 without endpoints, alt 1..3 256/512/1024 B per microframe, alt 4..7 1536/2048/2556/3072 B per microframe (high-bandwidth isoch on 0x85), plus an audio interface; (c) MJPEG candidates are now tried smallest-first (320x240, 640x360, 640x480, 800x480, 800x600, 1920x1080) so a low-bandwidth alternate setting can be selected — still silent in all of them; (d) **raw isocronous probe** (`P4_USB_RAW_ISOC_PROBE`, off by default) bypasses the bundled UVC driver completely: it claims interface 1 on the widest alternate (alt 7, 0x85, 3072 B/us), does `GET_CUR`+`SET_CUR` of VS_PROBE/VS_COMMIT itself and reads the endpoint with its own transfers. Results: control transfers OK (26-byte UVC 1.4 probe: format 2, frame 3, interval 333077 = 30 fps, dwMaxVideoFrameSize 0, dwMaxPayloadTransferSize 0x1C200000), probe/commit accepted, **3644 isoch packets delivered, every one with 0 bytes**; a format x frame sweep (3x3) with a commit per pair also returned only zero-length packets. **Conclusion: the P4 USB host, the alternate-setting claim, the UVC probe/commit and isoch reception all work; the camera itself transmits nothing after accepting every negotiation, in any offered format/frame. Not a Node32-HUB firmware or `uvc_host` limitation — this unit (1b3f:1167) needs something else (vendor/VideoControl terminal init, or a different camera).** The diagnostics stay in: the next webcam can be qualified in one Start. No API/route/UI change. | diagnosed — camera does not transmit; USB backend ready for a different UVC camera |
| CAM-60 | Attached cameras visible before Start: `cameraP4DetectSources()` status scan, throttled to 10 s while idle (MIPI SCCB `0x36`, USB UVC interface `0x0E/0x02` + VID/PID); `applySrcList()` renders attached models. Device v1.115 build 14:16 reports both MIPI and USB `1b3f:1167`. Presence does not imply that a UVC camera transmits frames (this USB unit does not). | fixed — needs user page verification after next OTA |
| CAM-61 | FHD Start panic (2026-09-29): `Task=CamInit`, `PC=0x400f3762`, `Cause=0x5`, `Addr=0x20`. `addr2line` resolves PC to `csi_get_input_bayer_order`; `esp_cam_sensor_set_format()` stores its format pointer in `dev->cur_format`, but P4 passed stack-local `fmt` and `isp`. **Fixed in source:** persistent sensor-format descriptors + ISP metadata; propagate live mode-switch failure, rollback config, hydrate saved resolution; actual dimensions for all AVI paths, 2 MiB P4 stream cache. Native resolution tests 5/5 (48 assertions), web camera tests 88/88. Added a 1280x720 RAW10 binned/cropped candidate at target 30 fps; not verified. Firmware not rebuilt/flashed; OTA then test FHD/720p Start, snapshot, stream and AVI. | fixed in source — needs `pio run -e esp32p4cam`, OTA + HW verification |
| CAM-70 | **Fase 1 H.264 `.264` (Tarefa 5, CAM_P4/P4-only, sem bump/OTA):** `camera_p4_h264.h` (codec/bitrate/Annex B helpers, 8 casos nativos), `camera_p4_h264_enc.{h,cpp}` (HW `esp_h264_enc_hw_new` UYVY/GOP/RC + stub p/ cores sem headers), `cameraP4CaptureWithRaw()` (tap raw antes do JPEG), `.264` no recorder (`StartH264/FrameH264/IsH264`, finish sem header), `rec_codec`+`h264_bitrate` em camera.json, bloco Codec na tab Rec (P4-only via `h264Avail`, estimativa MB/min), i18n EN+PT, SBOM+notices (esp_h264 Apache-2.0). MJPEG default; dashcam/timelapse/motion ficam AVI. | fixed in source — needs `pio run -e esp32p4cam` + HW check na .18 (Start H.264 → `.264` c/ SPS/PPS+IDR → Stop) |
| CAM-71 | **Fase 2 H.264-in-AVI (Tarefa 5, CAM_P4/P4-only, sem bump/OTA):** `h264_container` em camera.json (0=raw default, 1=AVI) + select Contentor na Rec (P4-only, só visível com H.264); `buildAviHeaderH264()` (480 bytes fixos: 224 base + 256 reservados p/ SPS/PPS, `H264` fcc/biCompression); `cameraH264ExtractSpsPps()` all-or-nothing (12 casos nativos); frames via path `00dc` partilhado; finish reescreve header c/ sets + rename/migração iguais ao MJPEG. Muxer nosso MIT (sem SBOM). **Limite conhecido:** `/cam/playback` só descodifica MJPEG — AVI H.264 lista mas mostra lixo. | fixed in source — needs `pio run -e esp32p4cam` + HW check na .18 (Start H.264+AVI → `.avi` abre em VLC c/ SPS/PPS → Stop) |
| CAM-72 | **Fase 3 foto 5MP (Tarefa 5, CAM_P4/P4-only, sem bump/OTA):** `2592x1944` nas tabelas (`kP4CsiModes`, `kCamResModes` 6→7, `resList` c/ 7 modos) como STILLS-ONLY (`cameraP4StillsOnly()`: w>1920; vídeo/stream/record/H.264 ficam ≤1080p); entrada `kP4SensorFmts`+ISP DERIVADA (factos: pclk 87.5 MHz, hts 2844, vts 1968 ≈15.6 fps, link 218.75 MHz — zero bytes GPL/ArduCAM) com `regs=nullptr` → `p4SetSensorFormat()` recusa limpo + revert até haver perfil de registos (HW homework na .18 c/ datasheet); buffers stills (slot JPEG 2→5 MB, staging 4→6 MB; cache stream fica 2 MB); nota `pic5mpNote` na Pictures (w>1920, sem API nova); i18n EN+PT. Nativos resolution 8/8. | fixed in source — needs `pio run -e esp32p4cam` + HW na .18 (detMipi, derivar regs, Start 5MP → foto → revert check) |
| CAM-73 | **Fase 4 fps por path (Tarefa 5, CAM_P4/P4-only, sem bump/OTA):** `cameraP4FpsMaxH264()` (MIPI 30 = sensor rate; USB negotiated/else 30) + `fpsMaxH264` no status; slider cap por codec (H.264→30, MJPEG→6); start H.264 usa `cfg.fps` (clamp 1–30 puro, `cameraH264FpsClamp()`) em vez do budget MJPEG; GOP=fps acompanha (1 IDR/s a 30). MJPEG intacto (6 medidos). Nativos p4_h264 13/13. | fixed in source — needs `pio run -e esp32p4cam` + HW na .18 (Start H.264 a 30 fps → NALs fluem → Stop) |
| CAM-74 | **Fase 5 fundação Vision (Tarefa 5, CAM_P4/P4-only, sem bump/OTA):** `camera_p4_vision.h` (modos/thresh/burst/label/crop-320 puros, 6 casos nativos) + `camera_p4_vision_rt.h/.cpp` (task própria `p4vision`: burst N JPEGs+label → `/CAM/VISION/`, scan modelo `.espdl` via FTP, probe PPA UYVY→320x320 HW c/ yield a stream/record); `vision_mode/thresh/burst_n/burst_label` em camera.json + mirror no manager (POST/boot); endpoints burst/reset + bloco vision no status; tab Vision P4-only (modo/thresh/modelo/OK-NOK/last/fps/burst); i18n EN+PT; SBOM esp-dl (hook-only). Sem modelo treinado (próxima tarefa); contadores em RAM. | fixed in source — needs `pio run -e esp32p4cam` + HW na .18 (burst 10x → SD/FTP, tab Vision, hook idle sem modelo) |

---

## 12. Notes

- **Vision pill labels its own rate:** `visFpsX10` reports the PPA preprocess
  pass while no model is loaded (`preprocess N fps`) and inference once a
  `/CAM/VISION/model.espdl` exists (`infer N fps`) — the hook is armed by the
  mode alone, not by the model, so the path is provable without one. Count/Defect
  semantics still need a trained model (Fase 5 follow-up).
- **FPS below configured value** (e.g. 10 fps configured → ~5 fps reported): not a bug. The `streamFps` pill shows *actual* throughput. On ESP32-S3 the bottleneck is JPEG encode + async-TCP pump, not the sensor. Higher resolution or lower quality pushes more bytes per frame, worsening the bottleneck. The reported FPS is the real stream rate.
- **`streamFps` counts NEW frames only** (duplicate re-sends that keep the TCP ACK clock alive are not new content), rounded to nearest (`camera_stream_fps.h`, `StreamFpsWindow`). A wire that re-sends the same frame therefore reads lower than the send rate — 2.9 shows as 3, 0.9 as 1 (previously truncated to 2 and 0). High-res streams genuinely publish ~2–3 new frames/s (capture-bound); lowering the resolution raises it.

---

## 13. Source map

| Layer | File |
|---|---|
| Pack flags | `chatgpt/packs/pack_cam.h` |
| Engine | `chatgpt/src/camera_manager.{h,cpp}`, `camera_config.{h,cpp}`, `camera_motion.{h,cpp}`, `camera_record.{h,cpp}`, `camera_record_naming.h`, `camera_pins.h`, `camera_sensor_map.h`, `camera_p4_h264.h`, `camera_p4_h264_enc.{h,cpp}` (Fase 1 T5), `camera_p4_vision.h`, `camera_p4_vision_rt.{h,cpp}` (Fase 5 T5) |
| Page | `chatgpt/src/webpages/assets/page_camera_asset.{h,cpp}` (`CAMERA_PAGE`) |
| Renderer | `chatgpt/src/webpages/renderers/camera_renderer.{h,cpp}` |
| API handlers | `chatgpt/src/webpages/handlers_unified/camera_handler.{h,cpp}` |
| Routes (WiFi) | `chatgpt/src/webpages/routes/web_routes_camera.{h,cpp}` |
| Ethernet | `chatgpt/src/ethernet_bootstrap_handlers.h` (status/config/photo/record + blocking stream), `ethernet_bootstrap_routes.h` |
| Loop wiring | `chatgpt/src/loop_impl.h:24` (`cameraMaybeAutoStart` + `cameraLoop`) |
| i18n | `chatgpt/src/languages/lang_en_ui.h`, `lang_pt_ui.h` (`cam*`) |
| Tests | `tests/native/test_camera_stream.cpp` (20 cases: non-blocking, multi-viewer, record-from-cache, fps clamp, **zero-copy pin/refcount**), `test_camera_api.cpp` (19 cases: form + JSON parser, truthy, changed-flag, all clamp groups incl. framesize 0–24 + auto_mode + motion_action **(0–2 SMTP / 0–1 no-SMTP)**, checkbox round-trip, nested motion JSON), `test_camera_motion.cpp` (13 cases: sampling guard, ownership, cadence, action dispatch, timelapse next-in-secs), `test_camera_sensor.cpp` (15 cases: compiles the real `camera_sensor_map.h` — PID→name, PID→max framesize, framesize→pixel dims 0–24, clamp-down-to-max guard), `test_camera_autostart.cpp` (15 cases: Task 3 boot hook latch/settle/ready-gating + `cameraArmAutoMode` default-fill + `cameraApplyConfig` persist-gating incl. max-clamp), `tests/web/test_camera.js` (109 cases: fmt helpers, presets incl. fixed fr values + **Eco**, resolution selector mapping + `applyMaxFrameSize` disable/clamp, **motion-action option filtering by email availability**, SD/photo/timelapse/aec2 + tlStop + tlInfo last path, poll, **adaptive `applyResListOpts`/`clampFpsSlider`/`framesizeField`, P4 per-sensor presets `getP4Preset`/`buildP4PresetBody`/`presetTitles**), `tests/packs/cam/test_real_cam.sh` (live curl audit: status/config/clamps/start/snapshot/stream-503/diff/photo/record/stop + restore) |
