# Desktop pages — reference screenshots

Same convention as `PAGES/mobile/`, applied to the **desktop** pages:
**exactly one image per page, always the most recent**, stored here and versioned.

> Status: **inventory raised (50 desktop pages); thirty images captured** —
> the common block (`home`, `status`, `files`, `sdcard`, `monitor`, `serial_monitor`,
> `buffer_viewer`, `access`, `ota`, `wifi`, `ethernet`, `email`, `recipients`, `alerts`,
> `ftp`, `commands`, `commands_datetime`, `printer`, `do`, `rs232`), the Waveshare pack
> (`proximity`, `proximity_dashboard`), the camera pack (`camera`, `hub`, `modbus_tcp`) and
> the environment pack (`sensor_config`, `mq135_config`, `sensor_charts`), plus `dualfile`
> and `api_reference` rendered from the shipped asset (§3.12). The remaining 20 rows are
> blocked by the build/config on every live board (§3.11) and are being covered one by one as
> smoke captures.

Two provenances, marked in the Status column:

- **`done`** — captured from a **live board** over HTTP with `desktop_shot.js` (the default and
  the strongest proof: real firmware, real config, real session engine).
- **`done (smoke)`** — the route exists in the firmware but **no live board serves it** (flag
  off in every pack we can reach). The image is rendered from the **shipped asset** in the
  harness (§3.12): same HTML/CSS/JS the firmware streams, with the `fetch` calls answered by a
  local mock seed instead of the board. No board is touched, no flag is changed.

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
board; the keys used are printed as `localstorage`), `--click "selector"` (clicks one element
once before the screenshot — client-side only, for pages whose default tab is not the
interesting one, e.g. `/recipients` → Rules; the result is printed as `click`),
`--click-text "OK"` (clicks the button whose visible label matches, e.g. dismiss the
`customAlert` a page opens on load — `/sensor-config` shows "Configuration loaded"; printed as
`click_text`), `--allow-empty "reason"` (accept a page
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

**Smoke capture (routes no live board serves).** Two small tools in `tests/web/`:

```bash
# 1) build the harness copy from the shipped PROGMEM asset (nothing touches a board)
python3 build_desktop_shots.py dualfile          # -> shots/desktop_dualfile.html
# 2) render it at desktop size (1440 wide, full height) over CDP
node desktop_smoke_shot.js shots/desktop_dualfile.html ../../PAGES/desktop/dualfile.png
# pages that hide their content behind <details> can be captured expanded:
node desktop_smoke_shot.js shots/desktop_api_reference.html ../../PAGES/desktop/api_reference.png --expand
```

`build_desktop_shots.py` takes the R-string exactly as compiled, keeps its own `<style>` (it
inlines `core.css` only when the asset links it), and injects `window.__MOCK_API__` + a
no-network `fetch` shim so the page fills with realistic data. `desktop_smoke_shot.js` sets a
1440×900 desktop viewport, waits, reports `title / text_len / controls / tables / rows /
failed_imgs / overlay / overflow`, and captures the full page. Its checks are the local
equivalents of the live ones (`title present`, `page rendered`, `no broken images`,
`loading overlay hidden`, `no horizontal overflow`, `png width 1440`) — **there is no
`english ui` / `lang api` check because there is no board**: the language comes from the asset.

**Caveat — `pass` is about the document that ends up loaded.** A page that redirects itself
(`location.href='/'`) still yields `pass: true` with 10/10, because the checks run on whatever
document the tab is showing. Always read `location` and `render.title` in the JSON before
keeping a PNG: on `/sensor-charts` (chart-session lockout, `SC-9`) the run reported 10/10 while
the tab was on Home. If `location` is not the requested path, the capture is void.


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
| Sensor charts | `/sensor-charts` | `sensor_charts.png` | **done** (captured on HomeE32 `.24` once the chart-session lockout cleared — `BACKLOG.md` SC-9) |
| `dualfile.png` | **harness render** (no live board serves `/dualfile`; `ENABLE_DUALFILE` is 1 only in `pack_p4_usb_server.h`) | asset as compiled at `efb1686`, no firmware/ETag | 1440 × 900 · 64 725 B | **smoke capture** (§3.12): `python3 build_desktop_shots.py dualfile` + `desktop_smoke_shot.js` → title `Dual File Manager`, 729 chars, 7 controls, 4 tables / 42 rows, 0 broken images, overlay hidden, no horizontal overflow, PNG 1440 wide → **7/7 harness checks** (the local equivalents; `english ui` / `lang api` are not applicable without a board). Data comes from the mock seed (SD 28.0 MB free, Flash 2.8 MB free), the layout and JS are the shipped ones. **Finding: DF-6** — the right pane's storage selector is never initialised from `state[side].storage`, so it reads SD while the pane lists Flash (and stays stale after Ctrl+U swap)
| `api_reference.png` | **harness render** (no live board serves `/api-reference`; `ENABLE_API_REFERENCE_PAGE` is 0 in `user_config.h` and in every pack) | asset as compiled at `efb1686`, no firmware/ETag | 1440 × 6735 · 777 901 B | **smoke capture** (§3.12): `build_desktop_shots.py api_reference` + `desktop_smoke_shot.js --expand` → title `🔌 Referência de API`, 6 058 chars, 81 buttons, 16 tables / 283 rows, 0 broken images, overlay hidden, no horizontal overflow, PNG 1440 wide → **7/7 harness checks**. Frame reproduced byte-for-byte from `streamPageShellStart()` (`core.css` inlined, i18n marked ready because `auto.js` is not part of the harness), `_F` set to the Waveshare flags so the flag-filtering shows what that board would show. **Two findings in `PAGES/api_reference.md`: APIR-6** (the `▶` try-button loses to `core.css`'s `button` rule → full-width pill per GET row) and **APIR-7** (PT fallbacks remain in several summaries/labels, contradicting APIR-4's "fallbacks are English now")

> **History — the page was unusable for a while (`PAGES/BACKLOG.md` SC-9, still open)** (HomeE32
> `.24`): `GET /sensor-charts` was 200 but the page needs one of **two chart sessions**; with
> `charts_exempt_sessions` ON those sessions never expire, so abandoned captures held both slots
> → `POST /api/sensor-charts-session` answered `503 Max 2 concurrent chart viewers` and the page
> did `customAlert(...); location.href='/'` (Home). **Resolved for this capture:** after the
> board rebooted (`reset_reason SOFTWARE`, uptime 152 s) both slots were free
> (`POST → 200`, released → 204 twice) and the capture went through with `location` staying on
> `/sensor-charts` and the title `Monitoring Charts`. **The underlying leak is not fixed** —
> two headless captures re-wedged it immediately afterwards, so the re-capture command stays
> here for the day the firmware releases dead slots:
> `node desktop_shot.js --host <ip> --path /sensor-charts --wait 12000 --out ../../PAGES/desktop/sensor_charts.png`
| Dual file | `/dualfile` | `dualfile.png` | **done** (smoke — rendered from the shipped asset, see §3.12) |
| Access (sessions/users) | `/access` | `access.png` | **done** |
| OTA | `/ota` | `ota.png` | **done** |
| API reference | `/api-reference` | `api_reference.png` | **done** (smoke — §3.12) |

> **Blocked on the build, not on the harness** (2026-11-17): `GET /api-reference` answers **404**
> on all four reachable boards (`.29`, `.24`, `.18`, `.9`). `user_config.h:285` sets
> `ENABLE_API_REFERENCE_PAGE 0` and every pack keeps it 0 (the page is compiled out to save
> flash). The sensor/charts section was attempted next — see the note under **Sensor charts**.
| Networks overview | `/networks` | `networks.png` | pending |

> **Blocked on the build, not on the harness** (2026-11-17): `GET /networks` answers **404** on
> all four live boards (`.29`, `.24`, `.18`, `.9`). The page is registered only under
> `#if ENABLE_TELNET==1` (`web_routes_networks.cpp:15`, mirrored in
> `ethernet_bootstrap_routes.h:232-238`), and `ENABLE_TELNET` is **0** in `user_config.h:291`
> *and* in every pack (`pack_cam`, `pack_cam_p4`, `pack_environment`, `pack_p4_cnc`,
> `pack_p4_usb_server`, `pack_radio`, `pack_rambox`, `pack_rs232`, `pack_s3_cnc_usb`,
> `pack_s3_touch7`, `pack_waveshare` all `#undef`/`#define 0`) — no pack in this tree serves it,
> same class as `/google-script`. The next non-blocked row (`/rs232`) was captured instead.
| Wi-Fi | `/wifi` | `wifi.png` | **done** |
| `ethernet.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `v1.120 - Nov 15 2026 19:16:43 WET` | 1440 × 1405 · 101 491 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 666 chars / 5 buttons rendered (Current Status, Configuration form, WiFi Runtime with Disable/Enable) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; `--anon` masked 10 JSON keys + 5 DOM values (ethernet `mac`/`mac_override`, WiFi runtime `ssid`/`ip`, the form's MAC/placeholder hints). `--wait 20000` used because the WiFi Runtime line needs the runtime-status fetch (`Fetching WiFi status…` at 8 s). **No W5500 on this board**: the card honestly reads `IP: 0.0.0.0 | Mode: DHCP` (the unset address is deliberately **not** masked) — `PAGES/BACKLOG.md` ETH-5 |
| `email.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `v1.120 - Nov 15 2026 19:16:43 WET` | 1440 × 1530 · 106 022 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 786 chars / 4 buttons rendered (Information, Direct SMTP form, Save, Email Test) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; **`--anon` mandatory here**: 14 JSON keys masked (SMTP `username`/`password`, `email.destination`/`subject`) + 2 DOM values, so the image shows `operator` / `operator@example.com` / password `(saved)` — no credential or real address in clear
| `recipients.png` | HomeE32 (`ENVIRONMENT`), `192.168.1.24` | `v1.120 - Nov 15 2026 16:08:35 WET`, ETag absent | 1440 × 2210 · 279 752 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 5 875 chars / 28 controls rendered (Sender/Rules/Commands tabs, How-to + Operators card, 10-row Recipients List, Save All + Add Rule) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**. **Not from `.29`:** the Waveshare pack compiles the page out (`pack_waveshare.h:78` `ENABLE_PAGE_RECIPIENTS 0` → `GET /recipients` **404**), so the capture came from HomeE32 `.24`, the only live board with the page. `--anon` ran (6 DOM values; this board has `recipients:null`, so no address to leak) and `--click '.acc-tab-bar button:nth-child(2)'` opened the **Rules** tab, which is the page's core UI (default tab is Sender — nearly empty)
| `alerts.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag absent | 1440 × 1075 · 110 185 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 1 881 chars / 6 buttons rendered (Inactivity/OEE tabs, 6 inactivity rows with timeout + alerts/day + alert emails, Save Alert Configuration) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; `--anon` masked 9 JSON keys (alert `email.destination`/`smtp_username`/`subject`, the status payload's `ip_address`/`wifi_*`) + 13 DOM values, so no destination address appears in clear
| `ftp.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `v1.120 - Nov 15 2026 19:16:43 WET` | 1440 × 1809 · 171 991 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 1 323 chars / 5 buttons rendered (FTP Connection, Automatic upload, Save, Status + recent uploads, Test connection / Upload now) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; `--anon` masked the JSON `user`/`password` and 1 DOM value — the FTP host `192.168.1.35` was rewritten to `192.168.1.10` by the DOM/text pass (its payload key is plain `host`, which no key rule matches), so no internal server address is published
| `commands.png` | HomeE32 (`ENVIRONMENT`), `192.168.1.24` | `v1.120 - Nov 15 2026 16:08:35 WET`, ETag absent | 1440 × 1283 · 145 687 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 1 584 chars / 24 controls rendered (Information card, 10 slot rows with type/command/interval/unit/days/template/active, Save All + Add Command) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**. **Not from `.29`:** the Waveshare build compiles `/commands` out (`GET /commands` → 404, only the date/time variant exists there), so the capture came from HomeE32 `.24`, which serves both. No `--anon` needed (empty slots, no addresses) but it ran anyway. **Known gap:** the hint line under the Information card is hardcoded PT (*Deslize horizontalmente…*) — `PAGES/BACKLOG.md` CMD-9
| `commands_datetime.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag absent | 1440 × 1054 · 115 086 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 958 chars / 12 controls rendered (Daily Restart card with the two fixed `SYS: REBOOT` lines, Scheduled Commands with 3 slot rows, Save All + Add Command) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; `--anon` ran (no addresses on this page). **Two gaps visible, both in the BACKLOG:** the hint line is still hardcoded PT (CMD-9) and the Type column renders **blank** because slot 1 stores `type:"rs232"` while this Waveshare build has no RS232 option — the select ends with `selectedIndex -1`, and any save falls back to `system`, silently rewriting the slot (CMD-10)
| `printer.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag absent | 1440 × 940 · 80 308 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 791 chars / 3 buttons rendered (System Name + mDNS, Language, NTP / Time Zone, Save) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; `--anon` ran (nothing to mask — the page shows the machine name `Equip15`, a language and a timezone, no address or credential). NB: `/printer` renders the **System Settings** form (name/mDNS/language/NTP), not a printer emulation form — the inventory keeps the route-based name `printer.png`
| `do.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag absent | 1440 × 1208 · 63 022 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 454 chars / 12 buttons rendered (Output Control with All ON/All OFF + 8 relay cards, Output Configuration with 8 name/invert rows, Save Config) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; `--anon` ran (nothing to mask — relay states, names and the `/config/do.json` hint). Measured: the configuration table (1322 px) fits its panel (1362 px) at 1440, so nothing is clipped. Output states are re-read on load, so a re-capture is not byte-identical (same frame, 1440 × 1208)
| `sensor_config.png` | HomeE32 (`ENVIRONMENT`), `192.168.1.24` | `v1.120 - Nov 15 2026 16:08:35 WET`, ETag `v1.120 - Nov 15 2026 16:08:35 WET` | 1440 × 1649 · 155 450 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 1 122 chars / 4 controls rendered (Main Control, Sensor Readings table, Output Options, Statistics 24h, Save Configuration) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**. **Not from `.29`:** the Waveshare pack has no AHT20/BMP280, so `/sensor-config` is 404 there; HomeE32 `.24` is the pack that serves it. Captured with `--click-text OK` because the page opens a *Configuration loaded* alert on load (without it the modal covers the page) and `--anon` (nothing to mask)
| `mq135_config.png` | HomeE32 (`ENVIRONMENT`), `192.168.1.24` | `v1.120 - Nov 15 2026 16:08:35 WET`, ETag `v1.120 - Nov 15 2026 16:08:35 WET` | 1440 × 1690 · 172 508 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 1 211 chars / 4 controls rendered (Main Control with `📅 Last reading: 05:20:25 (17/11/2026)` + Sensor Stable, Calibration with R0 13008 Ω, ADC RAW 3524 / RS 7606.5 Ω / RS/R0 0.585, Detected Gases for the eight gases with the AQI scale (`Good (46)`), Output Options, Save Configuration) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**. **Not from `.29`:** the Waveshare pack has no MQ-135, so `/mq135-config` is 404 there; HomeE32 `.24` is the pack that serves it. Captured with `--click-text OK` (the page opens a *Configuration loaded* alert on load) and `--reload`, because on a **cold** browser the i18n pass lands after the config fetch and overwrites the timestamp element with its static text `No reading available` — the second render shows the real reading (`PAGES/BACKLOG.md` MQ-1). `--anon` ran (nothing to mask: gas ppm, offsets, R0/ADC values, no address).
| `rs232.png` | Waveshare 8DI8DO (`Node32-OEE`), `192.168.1.29` | `v1.120 - Nov 15 2026 19:16:43 WET`, ETag `v1.120 - Nov 15 2026 19:16:43 WET` | 1440 × 1431 · 131 209 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 1 156 chars / 7 controls rendered (Configuration and Advanced tabs, `Status: Active (19200 bps)`, Enable RS232, Baud 19200 / Data 8 / Parity None / Stop 1, Templates card with Save Template + Delete Selected, Save) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; `--anon` masked 6 JSON keys (`wifi.ssid`, `wifi.password`, `smtp.username`, `smtp.password`, `email.destination`, `email.subject`) and the rendered text stayed clean. **This page is also the finding:** the build has `ENABLE_RS232 0` / `ENABLE_SNIFFER 0` (its own self-test manifest expects `PAGE("/rs232", (ENABLE_RS232 || ENABLE_SNIFFER))`), yet `/rs232` answers 200, reports **Active (19200 bps)** and `GET /api/rs232-status` + `GET /api/rs232-templates` are **404** — `PAGES/BACKLOG.md` RS232-8
| `modbus_tcp.png` | Cam-P4 (`Cam-P4`), `192.168.1.18` | `v1.120 - Nov 15 2026 16:09:06 WET`, ETag `v1.120 - Nov 15 2026 16:09:06 WET` | 1440 × 1041 · 80 523 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 536 chars / 6 controls rendered (Live / RTU tabs, Global Configuration with the two global enables, the register-columns legend, TCP Devices with `+ Add TCP Device`, TCP Scan with the subnet/start/end/timeout fields, Save TCP) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; `--anon` ran (nothing to mask — subnet/start/end IPs are the plain `192.168.1` scan defaults, no credential). **Only `.18` serves it:** `ENABLE_MODBUS_TCP 1` in `pack_cam_p4.h:117-118`, 404 on `.29`/`.24`/`.9`. **Two visible defects, both in the BACKLOG:** the scan description prints the broken attribute `data-i18n='mbScanSubnet'>` as text (MODBUS-6) and the page is still half Portuguese on an English build — subtitle, both global enables, the legend summary, `Subnet (ex: 192.168.1)` and `IP fim` (MODBUS-3)
| `sensor_charts.png` | HomeE32 (`ENVIRONMENT`), `192.168.1.24` | `v1.120 - Nov 15 2026 16:08:35 WET`, ETag absent (dynamic page) | 1440 × 2185 · 337 067 B | HTTP 200 + English UI (`/api/lang` `en`) + 0 unresolved tokens + `/core.css` 200 + `/auto.js?v=1.120` 200 + 4 115 chars / 27 controls rendered (REFRESH 5 min + G1/G2/G3 sensor selectors, Live/8h/6h/24h/Dec/Realtime strip, Realtime + CSV + PNG, `Updated at 17:31:53 · Data: 6 of 6 total (stride 1)`, the Temperature / Humidity / Atmospheric Pressure charts with MIN/MAX/AVG/CURRENT stats) + no broken images + overlay hidden + PNG 1440 wide → **11/11 checks pass**; `--anon` ran (nothing to mask — indoor temperature/humidity/pressure only). `location` verified on `/sensor-charts` with the title `Monitoring Charts` (the harness `pass` alone is not enough here — see the §2 caveat and SC-9; a first attempt re-wedged the two chart sessions and had to be cleared by a reboot)
| Ethernet | `/ethernet` | `ethernet.png` | **done** |
| E-mail (SMTP) | `/email` | `email.png` | **done** |
| Recipients | `/recipients` | `recipients.png` | **done** |
| Alerts config | `/alerts-config` | `alerts.png` | **done** |
| FTP upload | `/ftp` | `ftp.png` | **done** |
| Google Apps Script | `/google-script` | `google_script.png` | pending |

> **Blocked on the build, not on the harness** (2026-11-16): `GET /google-script` answers
> **404** on every live board (`.29`, `.24`, `.18`, `.9`) — `ENABLE_PAGE_GOOGLE_SCRIPT` is
> commented out in `user_config.h` and **no pack enable**s it (the validator's prerequisite is
> only `ENABLE_GOOGLE_EMAIL`, which just `pack_rambox.h` turns on, and that board is offline).
> The next inventory row (`/commands`) was captured instead.
| Commands (interval) | `/commands` | `commands.png` | **done** |
| Commands (date & time) | `/commands-by-datetime` | `commands_datetime.png` | **done** |
| Structures (fixtures) | `/structures` | `structures.png` | pending |

> **Blocked on the build, not on the harness** (2026-11-17): `GET /structures` answers **404**
> on every live board (`.29`, `.24`, `.18`, `.9`). `user_config.h:287` sets
> `ENABLE_STRUCTURES_PAGE 0` and only `pack_rs232.h:77-78` turns it on; every pack we can reach
> (Waveshare, environment, cam, cam_p4, radio, rambox, touch7) compiles it out, and the RS232
> board is offline. The next inventory row (`/printer`) was captured instead.
| Printer | `/printer` | `printer.png` | **done** |
| Buzzer | `/buzzer` | `buzzer.png` | pending |

> **Blocked on the build, not on the harness** (2026-11-17): `GET /buzzer` answers **404** on
> all four reachable boards (`.29`, `.24`, `.18`, `.9`). The route is compiled only when
> `ENABLE_BUZZER==1` (`web_routes_buzzer.cpp:12,69`); the Waveshare pack forces it to 0
> (`pack_waveshare.h:74-75`) and the N16R8/cam builds never define it (the macro only exists
> inside one board-default block, `user_config.h:367-368`). The next inventory row (`/do`) was
> captured instead.
| Digital outputs | `/do` | `do.png` | **done** |

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
| RS232 | `/rs232` | `rs232.png` | **done** (captured on the Waveshare `.29` — no Rambox is online; see the log row and `BACKLOG.md` RS232-8) |
| LPT / DB25 | `/lpt-db25` | `lpt_db25.png` | pending |

### 3.6 Modbus / CAN

| Page | Route | Image | Status |
|---|---|---|---|
| Modbus RTU | `/modbus-rtu` | `modbus_rtu.png` | pending |
| Modbus TCP | `/modbus-tcp` | `modbus_tcp.png` | **done** (captured on **Cam-P4** `.18`, the only live board with `ENABLE_MODBUS_TCP 1`) |
| Modbus live data | `/modbus-live` | `modbus_live.png` | pending |

> **Hidden by its own guard, not by a build flag** (2026-11-17): `/modbus-live` answers **302 →
> `/`** on Cam-P4 `.18`, the only live board with `ENABLE_MODBUS_TCP 1` (`web_routes_modbus.cpp:254-262`
> redirects when `modbus.enabled` is false or neither `rtu_enabled`/`tcp_enabled` is set — on that
> board Modbus is not active, so the page hides itself). 404 on `.29`, `.24`, `.9`. Re-capture on
> a board whose Modbus config is active, reviving this page is a config write (out of scope here).
| CAN bus | `/can` | `can.png` | pending |

### 3.7 Sensors (HomeE32)

| Page | Route | Image | Status |
|---|---|---|---|
| Sensor channels/caps | `/sensor-config` | `sensor_config.png` | **done** |
| Air quality MQ135 | `/mq135-config` | `mq135_config.png` | **done** |
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

### 3.11 Blocked routes — smoke matrix

The 20 rows still **pending** have no live page to capture (two of the original 22 are
now delivered as smoke captures, §3.12). Each was probed **once** on all
four live boards (`.29` Waveshare, `.24` HomeE32, `.18` Cam-P4, `.9` Cam-M1) with

```bash
for ip in 192.168.1.29 192.168.1.24 192.168.1.18 192.168.1.9; do
  curl -s -o /dev/null -w "$ip %{http_code}\n" -u admin:admin -m 6 http://$ip<route>
done
```

and the answer plus the compile-time reason is recorded below, so the next attempt starts from
the cause instead of re-probing. **They will not change until a board with that pack is online**
(GcodeSender-S3, P4-CNC, P4 USB-server, Rambox, Harmonica, Touch7) or a flag is deliberately
turned on for a capture — which is a firmware change and out of scope for this folder.

| Route | Image | Answer (.29/.24/.18/.9) | Why there is no page |
|---|---|---|---|
| `/dualfile` | `dualfile.png` | 404 ×4 | `ENABLE_DUALFILE` — only `pack_p4_usb_server.h:68` sets 1 — **captured by smoke, §3.12** |
| `/api-reference` | `api_reference.png` | 404 ×4 | `ENABLE_API_REFERENCE_PAGE 0` (`user_config.h:285` and every pack) — **captured by smoke, §3.12** |
| `/networks` | `networks.png` | 404 ×4 | `ENABLE_TELNET 0` (`user_config.h:291` and every pack) |
| `/google-script` | `google_script.png` | 404 ×4 | `ENABLE_PAGE_GOOGLE_SCRIPT` never defined (commented out) |
| `/structures` | `structures.png` | 404 ×4 | `ENABLE_STRUCTURES_PAGE 0` — only `pack_rs232.h:77-78` sets 1 |
| `/buzzer` | `buzzer.png` | 404 ×4 | `ENABLE_BUZZER` undefined (…`:367-368`) / forced 0 in `pack_waveshare.h:74-75` |
| `/grbl-controller` | `grbl_controller.png` | 404 ×4 | `ENABLE_GRBL` — only `pack_p4_cnc.h:73` and `pack_s3_cnc_usb.h:44` |
| `/rambox` | `rambox.png` | 404 ×4 | Rambox pack — that board is offline |
| `/lpt-db25` | `lpt_db25.png` | 404 ×4 | `ENABLE_DB25 0` (`user_config.h:423`) |
| `/modbus-rtu` | `modbus_rtu.png` | 404 ×4 | `ENABLE_MODBUS_RTU 0` everywhere (e.g. `pack_cam_p4.h:115-116`) |
| `/modbus-live` | `modbus_live.png` | **302 → `/`** on `.18`, 404 ×3 | page guard: Modbus not active in config (`web_routes_modbus.cpp:254-262`) |
| `/can` | `can.png` | 404 ×4 | `ENABLE_CAN 0` in every pack |
| `/hx711` | `hx711.png` | 404 ×4 | `ENABLE_HX711 0` in every pack |
| `/ina226` | `ina226.png` | 404 ×4 | `ENABLE_INA226 0` in every pack |
| `/sound` | `sound.png` | 404 ×4 | `ENABLE_SOUND_SENSOR 0` in every pack |
| `/max31865` | `max31865.png` | 404 ×4 | `ENABLE_MAX31865 0` in every pack |
| `/radio-ui` | `radio_ui.png` | 404 ×4 | `ENABLE_UDA1334A` — only `pack_radio.h:127` sets 1 |
| `/uda1334a` | `uda1334a.png` | 404 ×4 | same flag as `/radio-ui` (`page_audio` family) |
| `/falas` | `falas.png` | 404 ×4 | Touch7 pack (`web_routes_touch7.cpp`) — that board is offline |
| `/ecra` | `ecra.png` | 404 ×4 | same Touch7 pack |
| `/ipg-laser` | `ipg_laser.png` | 404 ×4 | `ENABLE_IPG_LASER 0` in every pack |
| `/usb-server` | `usb_server.png` | 404 ×4 | `ENABLE_USBIP_SERVER` — only `pack_p4_usb_server.h:30` sets 1 |

### 3.12 Smoke captures — routes only the firmware has

For the rows in §3.11 the page exists in the build but no live board serves it. Following the
phone pages (`PAGES/mobile/`, rendered the same way from their assets), the image is produced
from the **shipped asset** with the harness, never from a hand-made mock-up:

```bash
cd tests/web
python3 build_desktop_shots.py <page>                                  # shots/desktop_<page>.html
node desktop_smoke_shot.js shots/desktop_<page>.html ../../PAGES/desktop/<page>.png
```

What the image therefore **is**: the exact `PAGE_<NAME>` R-string the firmware streams (own
`<style>`, own JS, i18n attributes), laid out by Chromium at 1440 px with `window.__MOCK_API__`
answering the `/api/...` calls. What it **is not**: a board capture — no real SD card, no live
session engine, no `/api/lang` (so the strings are the asset's own fallbacks), and `/core.css`
is inlined when the asset links it. `auto.js` is **not** part of the harness (it is the board
runtime: language fetch, session keepalive, Home/Back footer), so the builder marks
`html[data-i18n-ready='1']` itself — without that flag `core.css` hides every `[data-i18n]`
element and the page renders as empty boxes. Pages that hide their body behind `<details>` are
captured with `--expand` (all sections open); the row says so when it applies.

| Route | Image | Rendered from | Notes |
|---|---|---|---|
| `/api-reference` | `api_reference.png` | `chatgpt/src/webpages/assets/page_api_reference_asset.h` + the `_F` flag object of `streamApiReferencePage()` (Waveshare values) | 1440 × 6735, **all 16 `<details>` opened with `--expand`** (on load they are collapsed). `_F` filtering hides the six sections this pack has off (RS232, Sensores, Modbus, GRBL, Câmara, Áudio) → 10 sections / 97 rows. Shows **APIR-6**: every `▶` test button is crushed by `core.css`'s global `button` rule into a full-width pill on its own line |
| `/dualfile` | `dualfile.png` | `chatgpt/src/webpages/assets/page_dualfile_asset.h` (`ENABLE_DUALFILE`, P4 USB-server pack only) | 1440 × 900, two panes on `/` (left SD: gcode/logs/photos/reports + 3 files; right Flash: config/charts/reports + 3 files), F1–F10 bar, command line. Shows **DF-6**: the right pane lists Flash while its storage selector still reads *SD* |

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
node desktop_shot.js --host 192.168.1.24 --path /recipients --anon --click '.acc-tab-bar button:nth-child(2)' --verify-only
node desktop_shot.js --host 192.168.1.29 --path /alerts-config --anon --verify-only
node desktop_shot.js --host 192.168.1.29 --path /ftp --anon --verify-only
node desktop_shot.js --host 192.168.1.24 --path /commands --anon --verify-only
node desktop_shot.js --host 192.168.1.29 --path /commands-by-datetime --anon --verify-only
node desktop_shot.js --host 192.168.1.29 --path /printer --anon --verify-only
node desktop_shot.js --host 192.168.1.29 --path /do --anon --verify-only
node desktop_shot.js --host 192.168.1.24 --path /sensor-config --anon --click-text OK --verify-only
node desktop_shot.js --host 192.168.1.24 --path /mq135-config --anon --reload --click-text OK --verify-only
node desktop_shot.js --host 192.168.1.29 --path /rs232 --anon --verify-only
node desktop_shot.js --host 192.168.1.18 --path /modbus-tcp --anon --verify-only
node desktop_shot.js --host 192.168.1.24 --path /sensor-charts --anon --wait 12000 --verify-only
# smoke capture (no live board serves the route)
python3 build_desktop_shots.py dualfile
node desktop_smoke_shot.js shots/desktop_dualfile.html ../../PAGES/desktop/dualfile.png
# pages that hide their content behind <details> can be captured expanded:
node desktop_smoke_shot.js shots/desktop_api_reference.html ../../PAGES/desktop/api_reference.png --expand
```

## 5. Adding an image (checklist)

1. Capture with the harness at 1440×900, English UI, board/mock in a realistic state; add
   `--anon` whenever the page shows operator config (SSID, IP, e-mail, credentials).
2. Save as `PAGES/desktop/<page>.png` (exactly one file for that page).
3. Add a row to §4 (board, firmware/ETag, size, checks) — the proof travels with the image.
4. Reference it in `PAGES/<page>.md` (first line of the doc, like the mobile docs).
5. Mark the row **done** in §3; if the page is new, add a row.
6. Same commit as the page change that made the image necessary.
