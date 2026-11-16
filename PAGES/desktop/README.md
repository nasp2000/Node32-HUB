# Desktop pages — reference screenshots

Same convention as `PAGES/mobile/`, applied to the **desktop** pages:
**exactly one image per page, always the most recent**, stored here and versioned.

> Status: **inventory raised (50 desktop pages); first image captured** —
> `home.png` (common block, `GET /`). Every other row is **pending**.

---

## 1. Rules (identical to the mobile set)

- **One image per page**, named after the route in snake_case: `grbl_controller.png`,
  `camera.png`, `status.png`, …
- **Always the latest**: any change that alters a page (layout, buttons, text,
  state) must refresh its image **in the same change**.
- Docs stay in `PAGES/<page>.md`; the image is referenced from there and indexed in
  the table below.
- **Never** a screenshot of the phone page here — those live in `PAGES/mobile/`
  (`/m` is the phone default; the desktop page is the `?desk=1` / PC view).
- The image is the page **as the firmware serves it** (captured over HTTP from a real
  board, not a hand-built harness). `home.png` is the only page whose content is
  **pack-dependent** (hero brand + Modules section): the file holds the most recent
  capture and §4 names the board it came from.

## 2. How to capture (desktop)

| Item | Value |
|---|---|
| Viewport | **1440 × 900** (full page height when the page scrolls) |
| UI language | English (`Content-Language: en`, English fallbacks) |
| Data | live board or a bench board in a realistic state — no personal data |
| Path | `PAGES/desktop/<page>.png` |
| Name | route snake_case, one file per page (no `_v2`, no dates) |
| Harness | `tests/web/desktop_shot.js` (headless Chromium over CDP, no npm deps) |

```bash
cd tests/web
# verify only (no file written) — the serving proof for one page
node desktop_shot.js --host 192.168.1.29 --path / --verify-only
# capture (also runs every check below)
node desktop_shot.js --host 192.168.1.29 --path / --out ../../PAGES/desktop/home.png
```

Optional: `--auth user:pass` (default `admin:admin`), `--w` / `--h` (default 1440×900).

**The harness refuses to write an image that does not prove the page serves it.**
It prints a JSON report and exits `1` unless all of these pass:

| Check | What it proves |
|---|---|
| `http 200` | the route exists on the board (not a 302/404/503) |
| `english served` | `Content-Language: en` — image is the English UI |
| `no %%TOKEN%% in html` | the server-side renderer resolved every `%%TOKEN%%` |
| `no %%TOKEN%% rendered` | nothing unresolved reached the DOM text |
| `assets reachable` | `/core.css` and `/auto.js?v=…` (and `config-manager.js` when used) return 200 |
| `page rendered` | DOM has text and buttons (not an empty shell / overlay) |
| `no broken images` | every `<img>` decoded (`naturalWidth > 0`) |
| `loading overlay hidden` | the page finished loading (`#loadingOverlay` hidden) |
| `no cross-origin sheet` | all stylesheets come from the board itself |
| `png width 1440` | the file matches the desktop viewport |

## 3. Inventory (desktop pages, 50 images)

Legend: **pending** = image not taken yet · **done** = image present here.
Source of truth for behaviour/flags: `PAGES/WEB_PAGES_REFERENCE.md`.

### 3.1 Common block (served by every pack)

| Page | Route | Image | Status |
|---|---|---|---|
| Home | `/` | `home.png` | **done** |
| Status | `/status` | `status.png` | pending |
| Files | `/files` | `files.png` | pending |
| SD card / storage | `/sdcard` | `sdcard.png` | pending |
| Monitor (data reception) | `/monitor` | `monitor.png` | pending |
| Serial monitor | `/serial-monitor` | `serial_monitor.png` | pending |
| Buffer viewer | `/buffer-viewer` | `buffer_viewer.png` | pending |
| Sensor charts | `/sensor-charts` | `sensor_charts.png` | pending |
| Dual file | `/dualfile` | `dualfile.png` | pending |
| Access (sessions/users) | `/access` | `access.png` | pending |
| OTA | `/ota` | `ota.png` | pending |
| API reference | `/api-reference` | `api_reference.png` | pending |
| Networks overview | `/networks` | `networks.png` | pending |
| Wi-Fi | `/wifi` | `wifi.png` | pending |
| Ethernet | `/ethernet` | `ethernet.png` | pending |
| E-mail (SMTP) | `/email` | `email.png` | pending |
| Recipients | `/recipients` | `recipients.png` | pending |
| Alerts config | `/alerts-config` | `alerts.png` | pending |
| FTP upload | `/ftp` | `ftp.png` | pending |
| Google Apps Script | `/google-script` | `google_script.png` | pending |
| Commands (interval) | `/commands` | `commands.png` | pending |
| Commands (date & time) | `/commands-by-datetime` | `commands_datetime.png` | pending |
| Structures (fixtures) | `/structures` | `structures.png` | pending |
| Printer | `/printer` | `printer.png` | pending |
| Buzzer | `/buzzer` | `buzzer.png` | pending |
| Digital outputs | `/do` | `do.png` | pending |

### 3.2 Industrial I/O & OEE (Waveshare 8DI8DO)

| Page | Route | Image | Status |
|---|---|---|---|
| Proximity config + OEE | `/proximity` | `proximity.png` | pending |
| OEE dashboard | `/proximity-dashboard` | `proximity_dashboard.png` | pending |

### 3.3 CNC (P4-CNC / GcodeSender-S3)

| Page | Route | Image | Status |
|---|---|---|---|
| GRBL controller (`?desk=1`) | `/grbl-controller` | `grbl_controller.png` | pending |

### 3.4 Camera (Cam-P4 / Cam-M)

| Page | Route | Image | Status |
|---|---|---|---|
| Camera live/playback (`?desk=1`) | `/camera` | `camera.png` | pending |
| Camera hub | `/hub` | `hub.png` | pending |

### 3.5 Rambox32 clock (Pigeon)

| Page | Route | Image | Status |
|---|---|---|---|
| Rambox unified page (`?desk=1`) | `/rambox` | `rambox.png` | pending |
| RS232 | `/rs232` | `rs232.png` | pending |
| LPT / DB25 | `/lpt-db25` | `lpt_db25.png` | pending |

### 3.6 Modbus / CAN

| Page | Route | Image | Status |
|---|---|---|---|
| Modbus RTU | `/modbus-rtu` | `modbus_rtu.png` | pending |
| Modbus TCP | `/modbus-tcp` | `modbus_tcp.png` | pending |
| Modbus live data | `/modbus-live` | `modbus_live.png` | pending |
| CAN bus | `/can` | `can.png` | pending |

### 3.7 Sensors (HomeE32)

| Page | Route | Image | Status |
|---|---|---|---|
| Sensor channels/caps | `/sensor-config` | `sensor_config.png` | pending |
| Air quality MQ135 | `/mq135-config` | `mq135_config.png` | pending |
| Weight HX711 | `/hx711` | `hx711.png` | pending |
| Current/power INA226 | `/ina226` | `ina226.png` | pending |
| Sound level | `/sound` | `sound.png` | pending |
| RTD MAX31865 | `/max31865` | `max31865.png` | pending |

### 3.8 Audio / radio (Harmonica Box, Touch7)

| Page | Route | Image | Status |
|---|---|---|---|
| Radio / MP3 player | `/radio-ui` | `radio_ui.png` | pending |
| UDA1334A DAC test | `/uda1334a` | `uda1334a.png` | pending |
| Voice/peers | `/falas` | `falas.png` | pending |
| Touch7 screen mirror | `/ecra` | `ecra.png` | pending |

### 3.9 Laser / USB server

| Page | Route | Image | Status |
|---|---|---|---|
| IPG laser RS232 | `/ipg-laser` | `ipg_laser.png` | pending |
| USB/IP server | `/usb-server` | `usb_server.png` | pending |

### 3.10 Deliberately **not** in this folder

| Route | Why |
|---|---|
| `/m` (every pack) | phone page — images live in `PAGES/mobile/` |
| `/unikon`, `/bricon` | 302 → `/rambox?dev=…` (no page of their own) |
| `/cam/snapshot`, `/cam/stream`, `/cam/raw`, `/cam/playback`, `/cam/motion/diff` | binary JPEG/MJPEG streams, not pages |
| `/healthz`, `/favicon.ico` | non-page endpoints |

## 4. Capture log (proof per image)

| Image | Board / pack | Firmware · ETag | Viewport · size | Verified |
|---|---|---|---|---|
| `home.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `…\|c1\|s1\|w1` | 1440 × 1572 · 182 303 B | HTTP 200 + English + 0 unresolved tokens + `/core.css` 200 (13 114 B) + `/auto.js?v=v1.120` 200 (65 551 B) + 5 sections / 15 buttons rendered + no broken images + overlay hidden + PNG 1440 wide → **10/10 checks pass** |

Reproduce the proof (no file written):

```bash
cd tests/web && node desktop_shot.js --host 192.168.1.29 --path / --verify-only
```

## 5. Adding an image (checklist)

1. Capture with the harness at 1440×900, English UI, board/mock in a realistic state.
2. Save as `PAGES/desktop/<page>.png` (exactly one file for that page).
3. Add a row to §4 (board, firmware/ETag, size, checks) — the proof travels with the image.
4. Reference it in `PAGES/<page>.md` (first line of the doc, like the mobile docs).
5. Mark the row **done** in §3; if the page is new, add a row.
6. Same commit as the page change that made the image necessary.
