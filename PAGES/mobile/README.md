# Mobile pages (`/m`) — model, images and where things live

Working folder for the **phone pages**: one image per screen, the shared blocks
once, and the rules every pack follows. All phone screenshots live here (versioned).
The **desktop** pages get the same treatment in `PAGES/desktop/` (inventory of
all 50 desktop pages raised; `home.png`, `status.png`, `files.png`, `sdcard.png`,
`monitor.png`, `serial_monitor.png`, `buffer_viewer.png`, `access.png`, `ota.png`,
`proximity.png`, `proximity_dashboard.png`, `camera.png`, `hub.png`, `wifi.png`,
`ethernet.png`, `email.png`, `recipients.png`, `alerts.png`, `ftp.png`, `commands.png`,
`commands_datetime.png`, `printer.png`, `do.png`, `sensor_config.png`, `mq135_config.png`,
`rs232.png`, `modbus_tcp.png`, `sensor_charts.png`, `dualfile.png`, `api_reference.png`,
`networks.png`, `structures.png`, `buzzer.png`, `lpt_db25.png`, `grbl_controller.png` and
`modbus_rtu.png`, `modbus_live.png`, `rambox.png`, `hx711.png`, `ina226.png`, `max31865.png`,
`sound.png`, `can.png`, `uda1334a.png`, `radio_ui.png`, `ipg_laser.png`,
`google_script.png`, `falas.png` and `ecra.png` (the last twenty-one are smoke renders) captured and
verified, rest pending).

---

## 1. The model (every pack)

- **Route** — `GET /m` serves the pack's phone page. The pack's desktop entry
  redirects phones to it (`/grbl-controller`, `/camera`, `/rambox` → 302);
  `?desk=1` forces the desktop page, which is **never modified**.
- **Screens** — one action per screen, **no page scroll** (only lists/log scroll
  inside), content **vertically centred** (`safe center`, so a short viewport
  never clips the first row).
- **Top strip** — **lines only, no labels**: one equal, tappable column per page
  (12 px rail, 6 px gap); the current page is the bright blue one, visited pages
  keep a blue rail. The page name is the **title next to the pack name**. Tapping
  a rail jumps straight to that page.
- **Navigation** — arrows, horizontal **swipe** (never presses the control under
  the finger) and tapping a name. Navigation is **never blocked by missing data**.
- **Tap guard** — a press counts unless the finger **dragged** (>14 px = scroll or
  swipe). No duration gate: a fast tap is still a tap (a 250 ms gate made every
  button dead on the phone). Buttons dim while pressed.
- **No shared footer** — `window._noBottomNav = true` in the common JS, so
  `auto.js` never adds its Home/Back bar to `/m`.
- **Confirmations** — OTA and restart ask **OK/Cancel** (`customConfirm`);
  HOME / ZERO / RESET / jog `0,0` arm on a second tap.
- **Pack name** — `PACK_SYSTEM_NAME` printed by the renderer as `window.PACK_NAME`
  and shown in the header (`GcodeSender-P4`, `Cam-P4`, `Rambox32`, …).

## 2. Shared blocks (pack-agnostic)

| File | Contents |
|---|---|
| `chatgpt/src/webpages/assets/mobile_common_asset.cpp` | `MOBILE_COMMON_CSS` (root column, strip, tiles, list/rows, actions, nav, chips, option rows, message bar, log) + `MOBILE_COMMON_JS` (`window.MU`: `t/tf/get/post/fmtSize/fmtEta/msg/arm/strip/swipe` + the 250 ms tap guard) |
| `chatgpt/src/webpages/assets/mobile_system_asset.cpp` | `MOBILE_SYSTEM_HTML` + `MOBILE_SYSTEM_JS` (`window.MPSystem`): summary tiles, **OTA**, **log**, **restart**; `MPSystem.open()/close()` by the host page, `setBlocker(fn)` to veto a restart |

A pack page = its own screens (inside `<div id="gm" class="mp">`) + the shared
blocks, streamed in this order by the pack renderer:

```
shell → window.PACK_NAME → MOBILE_COMMON_CSS → <pack>_HEAD → MOBILE_SYSTEM_HTML
      → <pack>_TAIL → MOBILE_COMMON_JS → MOBILE_SYSTEM_JS → auto.js
```

## 3. Images — shared screens (one copy for every pack)

| Image | Screen |
|---|---|
| `common_system_summary.png` | **System → Summary**: firmware, uptime, heap, PSRAM, SD free, WiFi + **Restart** (OK/Cancel) |
| `common_system_firmware.png` | **System → Firmware (OTA)**: current version, pick `.bin`, progress |
| `common_system_log.png` | **System → Log** (serial monitor) with Pause / Clear / Bottom |

## 4. Images — pack screens (one per screen)

### GcodeSender-P4 / -S3 (CNC)
| Image | Screen |
|---|---|
| `gcodesender_send.png` | **Send**: SD card list (folders, `↻`) + Phone tab (SD card or PSRAM 4 MB) |
| `gcodesender_preview_frame.png` | **Preview + FRAME**: 2D toolpath (cuts blue, rapids dashed amber, 10% grid, origin cross), point count, `X[..] Y[..] W x H`, **pinch to zoom / drag to pan / double-tap to fit** and the FRAME button (outline at 5 % of `spindle_max_rpm`, tool off on the way to the corner, and a tool-off return to the **centre** at the end) |
| `gcodesender_play.png` | **Play**: START/PAUSE/RESUME/STOP, progress, %, ETA, feed/spindle overrides (also *paused* and *alarm* states). The write token is refreshed every 10 min and a rejected write is retried once, so a second START after a finished job works without reloading |
| `gcodesender_jog.png` | **Jog**: X/Y cross with a **0,0** centre, 0.1/1/10/50 steps, Z, HOME/UNLOCK/RESET, ZERO X/Y/Z, MDI |

### Cam-M / Cam-P4
| Image | Screen |
|---|---|
| `cam_live.png` | **Live**: MJPEG in a 16:9 box, info (fps · resolution · SD · viewers), PHOTO, REC, ⛶ (also full screen and recording) |
| `cam_files.png` | **Files**: recordings (name, size), ⬇ download, › play, ✕ delete |
| `cam_modes.png` | **Camera**: the desktop's whole option set in four sub-tabs — **Image** (profiles DAY/NIGHT/FAST/MAX, resolution, fps, quality, brightness/contrast/saturation, mirror/flip/lamp, AWB/AEC/AGC/LENC, effect, white balance, exposure, AEC value, AGC gain, gain ceiling, source, lamp level), **Rec** (auto mode, timelapse, dashcam + minutes, codec, bitrate), **Motion** (enable, sensitivity, confirm/stop/post/cooldown, action, lamp auto, reset), **Vision** (mode, threshold, burst, BURST/RESET) — SAVE writes them all |
| `cam_ftp.png` | **FTP**: server, remote folder, last send, sent/failed, SEND NOW, TEST, recent uploads |

### Rambox32 (pigeon clock + RS232)
| Image | Screen |
|---|---|
| `rambox_status.png` | **Status**: clock, fancier, race, arrivals, RS232, last poll + SYNC/POLL |
| `rambox_arrivals.png` | **Arrivals / Pombal / Races**: ring · time · race, ✉ resend email, ↻, ✕ |
| `rambox_config.png` | **Config**: Bricon/Unikon, polling ON/OFF + interval, **Serial CLOCK/MANUAL**, RS232 (baud/parity/bits), SAVE |

## 5. Tests (same place as the pages)

| Suite | File |
|---|---|
| CNC page | `tests/web/test_gcode_mobile.js` (43) — fixture `tests/web/build_m_gcode.py` |
| Camera page | `tests/web/test_m_cam.js` (11) — fixture `tests/web/build_m_cam.py` |
| Rambox page | `tests/web/test_m_rambox.js` (11) — fixture `tests/web/build_m_rambox.py` |
| Phone entry (UA redirect) | `tests/web/test_mobile_entry.js` (7) |

Docs: `PAGES/gcode_mobile.md`, `PAGES/camera_mobile.md`, `PAGES/rambox_mobile.md`;
cross-page table: `PAGES/WEB_PAGES_REFERENCE.md` (one `/m` row per pack).

## 6. Adding a pack (checklist)

1. `assets/page_<pack>_mobile_asset.cpp/.h` — screens + `_TAIL` split at the
   message bar (the shared System screen goes between).
2. `renderers/<pack>_mobile_page_renderer.cpp/.h` — the print order in §2, guarded
   by the pack's feature flags.
3. Route `/m` in the pack's route file + **302 from the pack's desktop entry**
   (phone UA, `?desk=1` escape).
4. i18n keys (`<prefix>*`) in both languages; `generated_gzip_assets.h` regenerated.
5. Fixture builder + jsdom suite; one screenshot per screen dropped in this folder.
6. `PAGES/<pack>_mobile.md` + a `/m` row in `PAGES/WEB_PAGES_REFERENCE.md`.
