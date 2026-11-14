# Camera mobile page — `/m` (Cam-M / Cam-P4)

> Phone page of the camera packs. `GET /camera` on a phone redirects here (302,
> `?desk=1` escape); the desktop page is untouched.

## 1. Objective

Live view, photo, video recording and the recordings list from a phone, plus the
shared System screen (summary / OTA / log / restart).

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /m` (registered in `web_routes_camera.cpp`, `ENABLE_CAMERA==1`) |
| Entry | `GET /camera` + phone UA → 302 `/m` (`ua_classify.h`, `?desk=1` keeps the full page) |
| Auth / caching | `ensureBasicAuth` + `allowHtmlClient`; `ETag = FIRMWARE_BUILD`, `private, max-age=15, must-revalidate` |
| Pack name | `PACK_SYSTEM_NAME` injected as `window.PACK_NAME` by the renderer |
| Renderer | `renderers/cam_mobile_page_renderer.cpp`: common CSS → `PAGE_CAM_MOBILE` → `MOBILE_SYSTEM_HTML` → `PAGE_CAM_MOBILE_TAIL` → common JS → system JS |

## 3. Layout

Five screens inside one fixed 100dvh column (no page scroll, content centred):

```
header  strip Live · Files · Camera · FTP · System (one column per page, line under each) + pack name + badge
S1 Live   <img> /cam/stream in a 16:9 box (aspect taken from /api/camera/status)
          info pill (fps · resolution · SD free · viewers)
          actions: PHOTO | REC (toggles to STOP)
S2 Files  row per recording: name (opens /cam/playback) · size · ⬇ download · ✕ delete
S3 Camera resolution (sensor resList or framesize table) · fps · quality ·
          brightness/contrast/saturation (−2…+2) · mirror/flip/lamp · SAVE
S4 FTP    server / remote folder / last send / counters · SEND NOW · TEST ·
          recent uploads
S5 System shared block (summary / firmware / log) + restart
msg/bar   status line
nav       ← · n / 3 · →
```

## 4. Inputs

| Control | Behaviour |
|---|---|
| Tapping the image | reconnects the MJPEG stream |
| PHOTO | `POST /api/camera/photo` (saved on the SD card) |
| REC / STOP | `POST /api/camera/recording/start` / `stop` |
| File row | opens `/cam/playback?f=<path>` in a new tab |
| ⬇ | same URL with `download` → saves the recording on the phone |
| ⛶ | immersive view (CSS full screen, works on iOS); ✕ returns |
| SEND NOW / TEST | `POST /api/ftp-upload-now` / `/api/ftp-test` (disabled when FTP is off) |
| ✕ | `POST /api/camera/delete` after `customConfirm` |
| ↻ | reloads `/api/camera/files` |
| Arrows / swipe / strip | change screen (never blocked by data) |

## 5. Client logic

- Status polled every 2 s **only** on Live (fps, resolution, recording, SD free,
  viewers); Files is one-shot on entry; System polls only while its tab is open.
- The live box aspect comes from the reported resolution (`1280x720` → 16:9).
- Recording state drives the REC button and the "Recording (Ns, N frames)" line.
- Restart is vetoed while recording (`MPSystem.setBlocker`).
- Shared 250 ms tap guard + swipe + press feedback come from `MOBILE_COMMON_JS`.

## 5b. Camera modes

`GET /api/camera/config` fills the form and `POST /api/camera/config` saves it:
`resolution` (WxH from `/api/camera/status` → `resList`, or `framesize` on boards
without a mode list), `fps` (capped by `fpsMax`), `quality` 0…63,
`brightness`/`contrast`/`saturation` −2…+2, `hmirror`, `vflip`, `lamp`
(hidden when `lampAvail:false`), `auto_mode` (0 off · 1 photo · 2 video ·
3 motion · 4 timelapse), `timelapse_secs`, `motion_enabled`,
`motion_sensitivity` (+ `POST /api/camera/motion/reset`). The SAVE button is the
screen's primary action.

## 6. Server API

`/cam/stream` · `/cam/playback` · `/api/camera/status` · `/api/camera/photo` ·
`/api/camera/recording/start` · `/api/camera/recording/stop` ·
`/api/camera/files` · `/api/camera/delete` + the shared System endpoints.

## 7. i18n

`cm*` keys (Live/Files/recordings/photo/recording/delete) in both packs, plus the
shared `gmSys*`/`gmOta*`/`gmLog*` keys. `validate_i18n.py` green.

## 8. Review findings

| # | Finding | Status |
|---|---|---|
| 1 | `.avi`/`.264` may not play in the browser → the row opens the file and the client can download it | by design |
| 2 | Live stream is MJPEG; several viewers are limited by `CAM_MAX_MJPEG_CLIENTS` on the device | by design |
| 3 | No recording settings on the phone (resolution/codec stay on the desktop page) | by design |
| 4 | The log needs the serial monitor enabled (Access → Monitoring) | by design |

## 9. Source map

| Piece | File |
|---|---|
| Live + Files screens | `chatgpt/src/webpages/assets/page_cam_mobile_asset.cpp/.h` |
| Shared System screen | `chatgpt/src/webpages/assets/mobile_system_asset.cpp/.h` |
| Shared common (CSS/JS) | `chatgpt/src/webpages/assets/mobile_common_asset.cpp/.h` |
| Renderer | `chatgpt/src/webpages/renderers/cam_mobile_page_renderer.cpp/.h` |
| Routes | `chatgpt/src/webpages/routes/web_routes_camera.cpp` (`/m`, `/camera` redirect) |
| Tests | `tests/web/test_m_cam.js` (8), fixture `tests/web/build_m_cam.py` |
| Review shots | `SHARE/Tarefas/anexos/CAM/` |
