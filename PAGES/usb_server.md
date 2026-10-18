# USB Server — `/usb-server`

> One-stop reference for the **USB Server (USB/IP) page**: purpose, layout, every button/input, client logic, API endpoints, persistence and the firmware-side USB/IP engine. Written for both humans and AI agents.

---

## 1. Objective

Expose local USB-CDC devices (via the USB Host stack) to network clients using the USB/IP protocol on TCP (default port 3240). The page shows live server **status** (state, port, clients, RX/TX bytes), the list of connected **USB devices**, and a small **configuration** form (enabled, port, RX timeout, IN-wait timeout). Applies immediately, no reboot. P4-only feature; on other boards the page and APIs are compiled out.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /usb-server` |
| Feature flag | `ENABLE_USBIP_SERVER` (`config_defaults.h:249`, default `0`; `user_config.h:212` documents it as P4-only, requires `ENABLE_USB_HOST=1`) — gates page, all 4 APIs, persistence and engine |
| WiFi server | `web_routes_network.cpp:250-271` — guarded by `#if ENABLE_USBIP_SERVER==1` (`:250`), streams via `streamUsbServerPageToPrint()` (`:265`) |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:169-175` — serves `streamSharedUsbServerPage()` when enabled, else `404 not found` |
| API (GET) | `GET /api/usb-server-status`, `GET /api/usb-server-config`, `GET /api/usb-server-devices` (`web_routes_network.cpp:55-57`); manifest `route_registry_api.cpp:206-208` |
| API (POST) | `POST /api/usb-server-config` (`web_routes_network.cpp:58`; manifest `route_registry_api.cpp:399`) |
| Home link | `home_page_renderer.h:160-162` — `🧩 USB Server (USB/IP)` button, emitted only when `ENABLE_USBIP_SERVER==1` |
| Auth / caching | `ensureBasicAuth` + `allowHtmlClient` + ETag/`private, max-age=15, must-revalidate`, same pattern as other pages (`web_routes_network.cpp:252-264`) |
| Session exemption | ALWAYS exempt (local processing) — unconditional `window._localPagesNeverExpire=true`, never kicked by time |

Renderer order: shell (`streamPageShellStart(out, "🧩 USB Server")`) → `PAGE_USB_SERVER` (HTML + inline JS) → `/config-manager.js` → auto.js footer (`usb_server_page_renderer.cpp:6-15`).

---

## 3. Layout

```
div.page.usb-server > div.wrap
 ├ [🏠 Home] (.actions.nav-inicio, first child of .wrap)
 ├ H1 "🧩 USB Server (USB/IP)"
 ├ section Status
 │   └ #usb_server_status.mono ("Getting status...")
 ├ section USB Devices
 │   └ #usb_server_devices.mono ("Getting devices...")
 └ section Configuration
     ├ info box (data-i18n-html='usbServerInfoDesc', 4 rows)
     ├ form#usb_server_form
     │   ├ checkbox #usb_server_enabled (checked) "Enable USB/IP server"
     │   ├ input #usb_server_port (number 1–65535, 3240)
     │   ├ input #usb_server_recv_timeout (number 100–60000, 1200)
     │   ├ input #usb_server_in_wait (number 20–5000, 220)
     │   └ #usb_server_msg.desc (save feedback line)
     └ [💾 Save] (#usb_server_save, OUTSIDE the form)
```

Asset: `page_usb_server_asset.cpp:4-51`.

---

## 4. Inputs

| Input | ID | Type / range | Notes |
|---|---|---|---|
| Enable USB/IP server | `usb_server_enabled` | checkbox, default checked | sent as `enabled=1/0`; server parses with `bodyTruthy` (`network_handler.cpp:689`) |
| TCP Port | `usb_server_port` | number, 1–65535, default 3240 | server rejects out-of-range with 400 `porta_invalida` |
| Timeout RX (ms) | `usb_server_recv_timeout` | number, 100–60000, default 1200 | socket receive timeout; 400 `recv_timeout_invalido` |
| Timeout IN (ms) | `usb_server_in_wait` | number, 20–5000, default 220 | max wait for RX data on IN requests; 400 `in_wait_timeout_invalido` |

Client sends `parseInt(value || default)` fallbacks, so empty fields silently become defaults (`page_usb_server_asset.cpp:168-170`).

---

## 5. Buttons

| Button | Behaviour |
|---|---|
| 🏠 Home | link `/` — first child of `.wrap` |
| 💾 Save (`#usb_server_save`) | `disableBtn` + `showLoading()` → `POST /api/usb-server-config` (FormData + `_html=1`) → success: green `usbServerSaveOk` line + `customAlert(alertSaved)` + `showSavedCard()` + refresh status/devices; failure: red `usbServerSaveErr` line + `customAlert(alertSaveError)`; network error: `usbServerRequestFail` + `emailErrorPrefix + message` (`page_usb_server_asset.cpp:163-191`) |

No other buttons. Status/devices refresh on load and every 6 s via `setInterval` (`:193-200`).

---

## 6. Client-side logic (inline JS in `page_usb_server_asset.cpp:53-202`)

- **`t(k, fb)`** (`:63-68`) — `window._t` wrapper with fallback; no arrow functions, project rules honoured.
- **`loadConfig()`** (`:74-84`) — `GET /api/usb-server-config`, fills checkbox + 3 numeric fields; silently returns unless `j.success === true` (no error path).
- **`loadStatus()`** (`:86-104`) — `GET /api/usb-server-status`, renders one line: `State: <Running/Stopped> | <PortLbl>: <port> | <ClientsLbl>: <n> | RX: <n> B | TX: <n> B`. Failure sets `usbServerNoData` / `usbServerStatusError`.
- **`hex2` / `hex4` / `speedText()`** (`:106-126`) — format VID:PID/BCD as uppercase hex, map speed 1/2/3/5 to Low/Full/High/Super else Unknown.
- **`loadDevices()`** (`:128-161`) — `GET /api/usb-server-devices`, one text line per device: `Slot <s> | Addr <a> | Speed <t> | VID:PID <hhhh:hhhh> | BCD 0x<hhhh> | IF <c>/<s>/<p> | IN 0x<hh> OUT 0x<hh> | RX <n> B TX <n> B`. Empty list → `usbServerDevicesNone`. Note: the server also sends `description` per device but the JS never renders it (see USB-3).
- **Save handler** (`:163-191`) — as in section 5; uses `d.success`-style `j.success` check.
- **Boot + poll** (`:193-200`) — `loadConfig().then(loadStatus + loadDevices)`; then 6 s interval for status + devices (no visibility check, no `.catch` on the initial chain — see USB-4).

---

## 7. Server API

Handlers in `handlers_unified/network_handler.cpp:665-728`, thin wrappers over `web_shared_logic.cpp` builders. All return 501 `{"status":"disabled"}` when compiled out.

### `GET /api/usb-server-config` (`:676`)

```json
{"success":true,"enabled":true,"port":3240,"recv_timeout_ms":1200,"in_wait_timeout_ms":220}
```

Built from `config["usb_server"]` with `| true / 3240 / 1200 / 220` defaults (`web_shared_logic.cpp:296-311`).

### `POST /api/usb-server-config` (`:686`)

Parses `enabled` (truthy), `port`, `recv_timeout_ms`, `in_wait_timeout_ms` via `bodyValue`/`bodyTruthy` (form or JSON). Range validation (`:694-705`):

| Field | Range | Error |
|---|---|---|
| `port` | 1–65535 | 400 `porta_invalida` |
| `recv_timeout_ms` | 100–60000 | 400 `recv_timeout_invalido` |
| `in_wait_timeout_ms` | 20–5000 | 400 `in_wait_timeout_invalido` |

On success delegates to `sharedWebApplyUsbServerConfig()` → 200 `{"success":true,"message":"saved"}`; LittleFS failure → 400 `erro_guardar_littlefs` (`web_shared_logic.cpp:361-396`).

### `GET /api/usb-server-status` (`:666`)

```json
{"success":true,"compiled":true,"running":true,"enabled":true,"port":3240,
 "recv_timeout_ms":1200,"in_wait_timeout_ms":220,
 "active_clients":0,"accepted_clients":3,"rx_bytes":0,"tx_bytes":0}
```

Mixes live stats (`usbipServerGetStats()`) with runtime config (`usbipServerGetRuntimeConfig()`) (`web_shared_logic.cpp:262-294`).

### `GET /api/usb-server-devices` (`:720`)

```json
{"success":true,"devices":[{"slot":0,"dev_addr":2,"description":"...","speed":2,
 "vid":1027,"pid":24577,"bcd_device":0,"iface_class":10,"iface_subclass":0,
 "iface_protocol":0,"in_ep_addr":129,"out_ep_addr":1,"rx_bytes":0,"tx_bytes":0}]}
```

Iterates `USB_HOST_MAX_CDC_DEVICES` slots via `usbHostGetDevice()`, skips disconnected (`web_shared_logic.cpp:313-359`).

Related read-only surface: `GET /api/eth-status` embeds `usbip_enabled/running/port/clients/accepted/rx/tx` when enabled (`network_handler.cpp:517-533`); same fields in the WiFi status builder (`web_shared_logic.cpp:240-257`).

---

## 8. Persistence

`configManager.saveSection("usb_server")` → **`/config/usb_server.json`** (4 KB budget, `ConfigManager.cpp:808-821`).

- Defaults seeded at boot: `enabled=true, port=3240, recv_timeout_ms=1200, in_wait_timeout_ms=220` (`ConfigManager.cpp:210-213`, re-ensured `:433-437`).
- Load merges `/config/usb_server.json` → `config["usb_server"]` (`:1085-1103`, `:1231-1234`); atomic write via `writeJsonAtomic` (`:2203-2208`).
- Save path: `markDirtyAndScheduleSave()` + `saveSection("usb_server")`; on success the runtime config is pushed live via `usbipServerSetRuntimeConfig()` (restarts listener if running) (`web_shared_logic.cpp:381-392`).
- Self-test tracks the `usb_server` stem (`selftest_handler.cpp:26`) and the `usbip_server` flag (`:89`).

---

## 9. Runtime engine

Implemented in `usbip_server.cpp` (guarded `#if ENABLE_USBIP_SERVER == 1`, `:4`), types in `usbip_server.h:12-26`.

- **Boot**: `setup_impl.h:309-320` loads `config["usb_server"]` into `UsbIpRuntimeConfig` via `usbipServerSetRuntimeConfig()`, then `usbipServerBegin()`. Loop calls the (currently no-op) `usbipServerUpdate()` + health ping (`loop_blocks.h:692-695`).
- **Begin/stop** (`usbip_server.cpp:826-881`): allocates a 16 KB RX bridge buffer in PSRAM (`MALLOC_CAP_SPIRAM`, `:831`), hooks USB RX mirror (`usbHostSetRxHook(usbHostRxMirrorCb)`), spawns `usbip_srv` task (6 KB stack, prio 4). `usbipServerSetRuntimeConfig()` (`:895-911`) clamps bad values to defaults, stops the server if running, restarts if `enabled`.
- **Protocol** (`:721-822`): TCP listener (backlog 2), max 4 clients (`USBIP_MAX_CLIENTS`, `:26`); per-client task handles `OP_REQ_DEVLIST` (lists connected CDC devices as bus `1-<devaddr>`) and `OP_REQ_IMPORT` (binds one device; single import session owns the RX bridge via mutex, `:672-680`). URB subset: EP0 SET_LINE_CODING / SET_CONTROL_LINE_STATE forwarded to `usbHostSetLineCoding` / `usbHostSetDtrRts` (`:425-484`); bulk OUT forwarded via `usbHostSend` (`:403-423`); bulk IN served from the RX mirror ring (drops oldest on overflow, `:260-283`), capped at 1024 B per reply (`:627-642`).
- **Compile guards**: `ENABLE_USBIP_SERVER=1` requires `ENABLE_USB_HOST=1` (`pack_validator.h:34-36`; auto-downgrade in `user_config.h:393-398`), P4 target only (`user_config.h:400-408`), and PSRAM (`user_config.h:503-505`). Only pack enabling it is `pack_p4_usb_server.h:25-29` (`ENABLE_USB_HOST=1`, `ENABLE_USBIP_SERVER=1`).
- **Stubs**: when disabled, all API is inline no-op/false (`usbip_server.h:65-71`).

---

## 10. i18n keys

Page-specific (all confirmed in `lang_en_ui.h:824-854` + `:1841-1846`): `usbServerPageTitle` ("🧩 USB Server (USB/IP)"), `usbServerStatusTitle`, `usbServerLoading`, `usbServerConfigTitle`, `usbServerEnabledLbl`, `usbServerPortLbl` ("TCP Port:"), `usbServerRecvTimeoutLbl`, `usbServerInWaitLbl`, `usbServerInfoDesc`, `usbServerNoData`, `usbServerStateLbl`, `usbServerClientsLbl`, `usbServerRunning` ("Running"), `usbServerStopped` ("Stopped"), `usbServerStatusError`, `usbServerDevicesTitle`, `usbServerDevicesLoading`, `usbServerDevicesNone`, `usbServerDevicesError`, `usbServerDevSlot`, `usbServerDevAddr`, `usbServerDevSpeed`, `usbServerDevBcd`, `usbSpeedLow/Full/High/Super/Unknown`, `usbServerSaveOk`, `usbServerSaveErr`, `usbServerRequestFail`, `usbTipHome`, `usbTipEnable`, `usbTipPort`, `usbTipRxTimeout`, `usbTipInTimeout`, `usbTipSave`, plus `homeUsbServer` (`:28`). Shared keys reused: `btnHome` (`:7`), `btnSave` (`:8`), `alertSaved` (`:957`), `alertSaveError` (`:958`), `emailErrorPrefix` (`:503`). PT mirrors exist in `lang_pt_ui.h:824-854` + `:1841-1846`.

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| USB-1 | **FIXED** — PT fallback in status line. Changed fallback from `'Porta TCP'` to `'TCP Port'`. | Low | Fixed. |
| USB-2 | **FIXED** — Server error codes now reach the user. POST handler maps `j.message` codes (`porta_invalida`, `recv_timeout_invalido`, `in_wait_timeout_invalido`, `erro_guardar_littlefs`) to i18n keys (`usbServerSaveErrPort`, `usbServerSaveErrRecv`, `usbServerSaveErrInWait`, `usbServerSaveErrLittleFS`). | Low | Fixed. |
| USB-3 | **FIXED** — `description` now rendered in device line (appended after RX/TX). | Low | Fixed. |
| USB-4 | **FIXED** — `.catch` added to initial `loadConfig().then(...)` chain; on failure shows error in status and devices divs. | Low | Fixed. |
| USB-5 | **FIXED** — Save button moved inside `<form id='usb_server_form'>`. | Info | Fixed. |
| USB-6 | **FIXED** — Added `usbServerRxLbl`/`usbServerTxLbl` i18n keys and `usbServerThroughput` for B/s display. Status line now uses `t('usbServerRxLbl','RX')` and `t('usbServerTxLbl','TX')`. | Info | Fixed. |

---

## 12. Source map

| Concern | File |
|---|---|
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_usb_server_asset.cpp` (+ `.h:5`) |
| Renderer (shell + asset stream) | `chatgpt/src/webpages/renderers/usb_server_page_renderer.{h,cpp}` |
| WiFi routes (page + 4 APIs) | `chatgpt/src/webpages/routes/web_routes_network.cpp:54-59,250-271` |
| Ethernet page mirror | `chatgpt/src/ethernet_bootstrap_routes.h:169-175` |
| Shared page stream | `chatgpt/src/web_shared_pages.cpp:91-93` (`streamSharedUsbServerPage`) |
| HTTP handlers | `chatgpt/src/webpages/handlers_unified/network_handler.{cpp:665-728,h:46-49}` |
| Shared JSON builders/apply | `chatgpt/src/web_shared_logic.{cpp:262-396,h:88-106}` |
| USB/IP engine | `chatgpt/src/usbip_server.{cpp,h}` |
| Config storage | `chatgpt/src/ConfigManager.cpp` (defaults `:210-213`, ensure `:433-437`, save `:808-821`, load `:1085-1103,:1231-1234`, atomic `:2203-2208`) |
| Boot wiring | `chatgpt/src/setup_impl.h:309-320`; loop `chatgpt/src/loop_blocks.h:692-695` |
| Flags / guards | `chatgpt/src/config_defaults.h:249-250`, `chatgpt/user_config.h:212-213,393-408,503-505`, `chatgpt/packs/pack_validator.h:34-36`, `chatgpt/packs/pack_p4_usb_server.h:25-29` |
| Home button | `chatgpt/src/webpages/renderers/home_page_renderer.h:160-162` |
| Route manifest | `chatgpt/src/webpages/routes/route_registry_api.cpp:206-208,261,399` |
| i18n | `chatgpt/src/languages/lang_en_ui.h:824-854,1841-1846` (+ `lang_pt_ui.h` mirrors) |
| Tests | none — `tests/web/` has no `test_usb*` file (verified dir listing); self-test only lists flag/stem (`selftest_handler.cpp:26,89`) |
