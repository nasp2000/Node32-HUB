# Desktop pages — reference screenshots

Same convention as `PAGES/mobile/`, applied to the **desktop** pages:
**exactly one image per page, always the most recent**, stored here and versioned.

> Status: **inventory raised (50 desktop pages); sixteen images captured** —
> the common block (`home`, `status`, `files`, `sdcard`, `monitor`, `serial_monitor`,
> `buffer_viewer`, `access`, `ota`, `wifi`, `ethernet`, `email`), the Waveshare pack
> (`proximity`, `proximity_dashboard`) and the camera pack (`camera`, `hub`). Every other
> row is **pending**.

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

Optional: `--auth user:pass` (default `admin:admin`), `--w` / `--h` (default 1440×900),
`--wait ms` (settle time before the screenshot, default 8000 — raise it for slow first
polls, e.g. `--wait 95000` for `/serial-monitor`), `--reload` (load, wait 4 s, reload, then
wait again — for pages that build their labels once at parse time, before `/api/lang`
arrives, e.g. `/access`), `--localstorage "k=v; k2=v2"` (seed **client-side** view prefs before
the load, e.g. `prox_visCh=3` / `prox_dash_opts={"title":"ch"}` — nothing is written to the
board; the keys used are printed as `localstorage`), `--allow-empty "reason"` (accept a page
that is genuinely an empty state — the reason is printed as `allow_empty` and must be spelled
out in §4; a blank or half-rendered page still fails), `--anon` (neutralise
operator-identifying values, see below).

`--anon` keeps the capture publishable: the HTML/CSS/JS are still the board's own, but the
JSON the page renders is rewritten in-page (before the first page script) so SSID, MAC, IPs,
e-mail addresses, SMTP user, subject and credentials become `NODE32-BENCH`,
`02:00:00:00:00:00`, `192.168.1.10`, `operator@example.com`, `Node32-HUB bench` (list entries
keep their position — masked `ssid`s become `NODE32-BENCH 1`, `NODE32-BENCH 2`, … so a scan
list stays readable); **unset addresses are never masked** — an interface with no link keeps
`0.0.0.0`, because that is state, not identity. `/api/lang`
is never touched (UI labels stay real) and every masked key is printed as `anon_masked`, so
§4 can state exactly what was neutralised. Right before the screenshot the same pass also
redacts **free text** (log/terminal pages): MACs, IPv4 addresses and e-mail addresses inside
DOM text nodes **and in `input`/`textarea`/`option` values and placeholders** become the same
placeholders — the count is printed as `anon_text_nodes`.

**The harness refuses to write an image that does not prove the page serves it.**
It prints a JSON report and exits `1` unless all of these pass:

| Check | What it proves |
|---|---|
| `http 200` | the route exists on the board (not a 302/404/503) |
| `english ui` | page `Content-Language: en`, or (routes that do not stamp it, e.g. `/status`) `GET /api/lang` in `en` — the strings the page actually renders |
| `lang api 200 + en` | `/api/lang` answers 200 with `Content-Language: en` + `X-UI-Lang: en`. This proves the *language source*, not every literal: hardcoded PT fallbacks still slip through (see `files.png` / FIL-7), so read the image before marking a row done |
| `no %%TOKEN%% in html` | the server-side renderer resolved every `%%TOKEN%%` |
| `no %%TOKEN%% rendered` | nothing unresolved reached the DOM text |
| `assets reachable` | `/core.css` and `/auto.js?v=…` (and `config-manager.js` when used) return 200 |
| `page rendered` | DOM has text and interactive controls (`a.btn`, `button`, `input[type=submit|button]`) — or, on button-less terminal pages, substantial content (not an empty shell / "Loading…" overlay) |
| `no broken images` | every `<img>` **that has a `src`** decoded (`naturalWidth > 0`); stream placeholders with no `src` yet are ignored |
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
| Status | `/status` | `status.png` | **done** |
| Files | `/files` | `files.png` | **done** |
| SD card / storage | `/sdcard` | `sdcard.png` | **done** |
| Monitor (data reception) | `/monitor` | `monitor.png` | **done** |
| Serial monitor | `/serial-monitor` | `serial_monitor.png` | **done** |
| Buffer viewer | `/buffer-viewer` | `buffer_viewer.png` | **done** |
| Sensor charts | `/sensor-charts` | `sensor_charts.png` | pending |
| Dual file | `/dualfile` | `dualfile.png` | pending |
| Access (sessions/users) | `/access` | `access.png` | **done** |
| OTA | `/ota` | `ota.png` | **done** |
| API reference | `/api-reference` | `api_reference.png` | pending |
| Networks overview | `/networks` | `networks.png` | pending |
| Wi-Fi | `/wifi` | `wifi.png` | **done** |
| `ethernet.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `v1.120 - Nov 15 2026 19:16:43 WET` | 1440 × 1405 · 101 491 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 666 chars / 5 buttons rendered (Current Status, Configuration form, WiFi Runtime with Disable/Enable) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; `--anon` masked 10 JSON keys + 5 DOM values (ethernet `mac`/`mac_override`, WiFi runtime `ssid`/`ip`, the form's MAC/placeholder hints). `--wait 20000` used because the WiFi Runtime line needs the runtime-status fetch (`Fetching WiFi status…` at 8 s). **No W5500 on this board**: the card honestly reads `IP: 0.0.0.0 | Mode: DHCP` (the unset address is deliberately **not** masked) — `PAGES/BACKLOG.md` ETH-5 |
| `email.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `v1.120 - Nov 15 2026 19:16:43 WET` | 1440 × 1530 · 106 022 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 786 chars / 4 buttons rendered (Information, Direct SMTP form, Save, Email Test) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; **`--anon` mandatory here**: 14 JSON keys masked (SMTP `username`/`password`, `email.destination`/`subject`) + 2 DOM values, so the image shows `operator` / `operator@example.com` / password `(saved)` — no credential or real address in clear
| Ethernet | `/ethernet` | `ethernet.png` | **done** |
| E-mail (SMTP) | `/email` | `email.png` | **done** |
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
| Proximity config + OEE | `/proximity` | `proximity.png` | **done** |
| OEE dashboard | `/proximity-dashboard` | `proximity_dashboard.png` | **done** |

### 3.3 CNC (P4-CNC / GcodeSender-S3)

| Page | Route | Image | Status |
|---|---|---|---|
| GRBL controller (`?desk=1`) | `/grbl-controller` | `grbl_controller.png` | pending |

> **Blocked on hardware, not on the harness** (2026-11-16): `GET /grbl-controller?desk=1` on the
> Waveshare board `.29` answers **404** — that build has `ENABLE_GRBL=0`, so the route is not
> compiled at all. The page needs a CNC pack (P4-CNC `.7`, GcodeSender-S3 `.38`) and both were
> offline when checked; the camera page (next row) was captured instead. Re-run
> `node desktop_shot.js --host <cnc-ip> --path '/grbl-controller?desk=1' --out ../../PAGES/desktop/grbl_controller.png`
> as soon as one is powered.

### 3.4 Camera (Cam-P4 / Cam-M)

| Page | Route | Image | Status |
|---|---|---|---|
| Camera live/playback (`?desk=1`) | `/camera` | `camera.png` | **done** |
| Camera hub | `/hub` | `hub.png` | **done** |

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
| `status.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `v1.120 - Nov 15 2026 19:16:43 WET` | 1440 × 2639 · 311 368 B | HTTP 200 + English (`/api/lang` `en`; this route stamps no `Content-Language`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 2 728 chars / 2 buttons / heap table rendered + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; captured with `--anon`, 9 keys neutralised (`ip_address`, `wifi_ip`, `wifi_ssid`, `wifi_mac`, `email.{smtp_username,destination,subject}`, `email_destination`, `recent_ips.0.ip`) — no operator e-mail/SSID/MAC/IP in the file |
| `files.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag absent (dynamic listing) | 1440 × 1055 · 70 938 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120&b=…` 200 + 415 chars / 9 buttons / SD space card + 2 folder rows rendered + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; no `--anon` needed (folder names `Equip15`/`LOGs`, sizes and dates only — no operator data). **Known gap, not the harness:** the table headers say `Tipo`/`Nome`/`Tamanho` (hardcoded PT, no `data-i18n`) → `PAGES/BACKLOG.md` FIL-7; the image keeps exactly what the board serves |
| `sdcard.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `v1.120 - Nov 15 2026 19:16:43 WET` | 1440 × 1690 · 207 874 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 2 087 chars / 5 buttons rendered (storage bar `Mounted=Yes`, buffer/cleanup selects, command hints) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; no `--anon` needed (SD totals, formats and the `SystemName_DD-MM-YYYY` rule — no operator values) |
| `monitor.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `v1.120 - Nov 15 2026 19:16:43 WET` | 1440 × 1038 · 136 791 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 4 242 chars / 5 buttons rendered (Actions, Send Command, live RX buffer with proximity telemetry) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; no `--anon` needed (counter/ppm telemetry only). Live tail — a re-capture shows the newest lines, the frame is fixed |
| `serial_monitor.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `v1.120 - Nov 15 2026 19:16:43 WET` | 1440 × 900 · 196 676 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/auto.js?v=1.120` 200 + 30 295 chars rendered (500/500 msgs, heap + ETH diagnostics) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; captured with `--wait 95000` because the page's **first poll asks 500 messages and takes ≈90 s** (`/api/serial-monitor?limit_msgs=500&since=0` → 89 591 ms / 56 109 B; 200 → 7.5 s; 100 → 134 ms; 50 → 66 ms — see `PAGES/BACKLOG.md` MON-7), and with `--anon` (11 DOM text nodes redacted: device MAC `02:00:00:00:00:00`, IPs, e-mails) |
| `buffer_viewer.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `v1.120 - Nov 15 2026 19:16:43 WET` | 1440 × 900 · 193 349 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/auto.js` 200 (65 551 B; the page ships its own inline CSS, no `/core.css`) + 3 996 chars rendered (toolbar, stats `Lines: 10 · Bytes: 3725`, 10 live proximity lines) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; no `--anon` needed (counter/ppm telemetry only). Live view (auto-refresh 5 s) — a re-capture shows newer lines |
| `access.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `v1.120 - Nov 15 2026 19:16:43 WET` | 1440 × 981 · 79 625 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/config-manager.js` 200 + `/auto.js?v=1.120` 200 + 616 chars / 4 buttons rendered (4 tabs, Administrator + Viewer panels) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; captured with `--anon` (39 JSON keys masked — every `events[*].ip` in the session history) and `--reload`, because on a cold browser the tab labels stay as raw i18n keys (first visit builds them before `/api/lang` lands) — `PAGES/BACKLOG.md` ACC-8 |
| `ota.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `v1.120 - Nov 15 2026 19:16:43 WET` | 1440 × 948 · 77 517 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 469 chars / 5 buttons rendered (Current Version `v1.120 (Node32-OEE)`, Firmware Update with file picker, System Actions Restart/Factory Reset) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; no `--anon` (version/build only, no IPs) and no `--reload` (labels render correctly first time). Note: the Factory-Reset button reads `Factory Reset` (pack) while the HTML fallback and the explanation block say `Clear Settings` — `PAGES/BACKLOG.md` OTA-1 |
| `proximity.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `v1.120 - Nov 15 2026 19:16:43 WET` | 1440 × 951 · 74 755 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 660 chars / 4 buttons rendered (tabs Status/Config/Shifts/Shiftday/OEE, 8-channel Real-Time Status, `Show: 8 channels` + Save) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; no `--anon` (channel names + counters), no `--reload` (labels correct first time). The status table scrolls inside `.table-wrapper` (890 px visible vs 1020 px table), so the last column `Elapsed` stays behind the horizontal scroll at 1440 — `PAGES/BACKLOG.md` PX-4 |
| `proximity_dashboard.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `v1.120 - Nov 15 2026 19:16:43 WET` | 1440 × 1745 · 251 654 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 957 chars / 6 sections rendered (DAY/LAST HOUR counters, OEE donut + A/P/Q tiles, 3 hourly charts with Gantt strips) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; no `--anon` (counters only). Captured with `--localstorage "prox_visCh=3; prox_dash_opts={\"title\":\"ch\"}"` (client-side prefs only): without them the page stacks 8 charts all titled `Equip15` — `PAGES/BACKLOG.md` PD-6. Shifts show `--` because shift tracking is disabled on this board (`shift-config` → `en:false`), not an error |
| `camera.png` | Cam-P4 (`Cam-P4`), `192.168.1.18` | `v1.120 - Nov 15 2026 16:09:06 WET`, ETag absent | 1440 × 900 · 463 728 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/auto.js?v=1.120` 200 + 521 chars / 26 controls rendered (top toolbar, Camera Control panel, side tabs Camera/Pictures/Recording/Motion/Vision, `Waiting for camera…` placeholder) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; no `--anon` (`Source USB — 1b3f:1167` is the device id) and no `--reload`. Captured **with the stream stopped** (the page's own initial state): starting it would publish a live view of the premises. The `<title>` still reads `📷 Câmara` (hardcoded PT literal, not a `pageTitle*` LangString) — `PAGES/BACKLOG.md` CAM-70 |
| `hub.png` | Cam-P4 (`Cam-P4`), `192.168.1.18` | `v1.120 - Nov 15 2026 16:09:06 WET`, ETag `v1.120 - Nov 15 2026 16:09:06 WET` | 1440 × 900 · 65 563 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 192 chars / 3 controls rendered (topbar Home/Refresh/Clear, IP field + Add, empty-state line and the 10 s refresh hint) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass** with `--allow-empty` (the camera list lives in the browser's `localStorage['camHub']`, so a fresh profile legitimately shows the empty state; adding an entry would pull a live snapshot of the premises) and `--anon` (1 value masked: the `192.168.1.18` prefilled in the IP field → `192.168.1.10`) |
| `wifi.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `v1.120 - Nov 15 2026 19:16:43 WET` | 1440 × 1169 · 83 011 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 577 chars / 4 buttons rendered (Information, Configuration SSID + Password + Save, Available Networks with Search and 6 scanned rows) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; `--anon` masked 14 JSON keys (configured `wifi.ssid`/`wifi.password`, the 6 `networks[*].ssid` — as `NODE32-BENCH 1…6` so the scan stays legible — plus `smtp.username/password` and `email.destination/subject` carried in the page payload). No `--reload` needed |

Reproduce the proof (no file written):

```bash
cd tests/web
node desktop_shot.js --host 192.168.1.29 --path / --verify-only
node desktop_shot.js --host 192.168.1.29 --path /status --anon --verify-only
node desktop_shot.js --host 192.168.1.29 --path /files --verify-only
node desktop_shot.js --host 192.168.1.29 --path /sdcard --verify-only
node desktop_shot.js --host 192.168.1.29 --path /monitor --verify-only
node desktop_shot.js --host 192.168.1.29 --path /serial-monitor --anon --wait 95000 --verify-only
node desktop_shot.js --host 192.168.1.29 --path /buffer-viewer --verify-only
node desktop_shot.js --host 192.168.1.29 --path /access --anon --reload --verify-only
node desktop_shot.js --host 192.168.1.29 --path /ota --verify-only
node desktop_shot.js --host 192.168.1.29 --path /proximity --verify-only
node desktop_shot.js --host 192.168.1.29 --path /proximity-dashboard --localstorage 'prox_visCh=3; prox_dash_opts={"title":"ch"}' --verify-only
node desktop_shot.js --host 192.168.1.18 --path '/camera?desk=1' --verify-only
node desktop_shot.js --host 192.168.1.18 --path /hub --anon --allow-empty "browser-side camera list" --verify-only
node desktop_shot.js --host 192.168.1.29 --path /wifi --anon --verify-only
node desktop_shot.js --host 192.168.1.29 --path /ethernet --anon --wait 20000 --verify-only
node desktop_shot.js --host 192.168.1.29 --path /email --anon --verify-only
```

## 5. Adding an image (checklist)

1. Capture with the harness at 1440×900, English UI, board/mock in a realistic state; add
   `--anon` whenever the page shows operator config (SSID, IP, e-mail, credentials).
2. Save as `PAGES/desktop/<page>.png` (exactly one file for that page).
3. Add a row to §4 (board, firmware/ETag, size, checks) — the proof travels with the image.
4. Reference it in `PAGES/<page>.md` (first line of the doc, like the mobile docs).
5. Mark the row **done** in §3; if the page is new, add a row.
6. Same commit as the page change that made the image necessary.
