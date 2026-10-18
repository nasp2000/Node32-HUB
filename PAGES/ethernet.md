# Ethernet — `/ethernet`

> Reference for the **Ethernet (W5500) configuration page**: DHCP/static setup, MAC
> override, live status polling and the WiFi-runtime on/off card. Written for both humans
> and AI agents.

---

## 1. Objective

Configure the wired **Ethernet interface** (DHCP vs static IP/gateway/DNS, optional MAC
override) and manage the **WiFi runtime switch** (disable WiFi to free ~25–35 KB of driver
heap on Ethernet-only deployments). Status is polled live; changes require a restart.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /ethernet` |
| Feature flag | `ENABLE_ETHERNET` (page gating in manifests); route compiled when web is up |
| WiFi server | `web_routes_network.cpp:203` → `streamEthernetPageToPrint()` (`ethernet_page_renderer.cpp:6`) |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:166` → `streamSharedEthernetPage` (`web_shared_pages.cpp:87`) — primary audience |
| Auth / caching | `ensureBasicAuth` + `allowHtmlClient`; `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` |
| Renderer shell | `streamPageShellStart` + fragment + auto.js footer |

---

## 3. Layout

```
main.page > div.wrap
├ nav.actions.nav-inicio: 🏠 Home link (/)
├ H1 "🌐 Ethernet"                     [ethPageTitle]
├ ℹ️ Card "Estado" [ethStatusTitle] (blue border)
│  └ div#eth_info mono — live poll output (IP/MAC/link/mode)
├ ⚙️ Card "Configuração" [ethConfigTitle]
│  ├ select#eth_mode  dhcp | static     [ethModeLbl, ethModeDhcp, ethModeStatic]
│  ├ input#eth_ip   maxlength=15        [ethIpLbl]   (disabled unless static)
│  ├ input#eth_gw   maxlength=15        [ethGwLbl]   (disabled unless static)
│  ├ input#eth_dns  maxlength=15        [ethDnsLbl]  (disabled unless static)
│  ├ input#eth_mac  maxlength=17 placeholder DE:AD:BE:EF:FE:ED  [ethMacLbl]
│  ├ div#eth_msg (feedback line)
│  └ 💾 button#eth_save                 [btnSave]
└ 📶 Card "WiFi Runtime" [wifiRuntimeTitle] (amber border) #wifi_rt_card
   ├ info lines: disabling frees heap / re-enable needs restart / persists reboot
   │                                    [wifiDisableInfo1..3]
   ├ div#wifi_rt_status (poll state)
   ├ ⛔ button#btn_wifi_off (red)       [wifiDisableBtn]
   ├ ✅ button#btn_wifi_on  (green)     [wifiEnableBtn]
   └ div#wifi_rt_msg (feedback line)
```

---

## 4. Inputs

| Input | ID | Notes |
|---|---|---|
| Mode | `eth_mode` | `dhcp`/`static`; toggling clears and disables ip/gw/dns when leaving static |
| IP / Gateway / DNS | `eth_ip` `eth_gw` `eth_dns` | maxlength 15, only sent in static mode |
| MAC override | `eth_mac` | optional; format-validated server-side (`sharedWebValidMac`) |

---

## 5. Buttons

| Button | ID / handler | Behaviour |
|---|---|---|
| 🏠 Home | link `/` | first child of `.wrap` |
| 💾 Save | `#eth_save` | POST `/api/eth-config`, success → "Saved. Restart to apply." + `showSavedCard()` |
| ⛔ Disable WiFi | `#btn_wifi_off` | `customConfirm` → POST `/api/wifi-disable` |
| ✅ Enable WiFi | `#btn_wifi_on` | POST `/api/wifi-enable`, response reminds restart needed |

---

## 6. Client-side logic

Inline `<script>` (auto.js supplies `customAlert`/`customConfirm`/`showSavedCard`):

- `t(k)` — translation lookup via `window._t`.
- `toggleStatic()` — enables/disables the three static fields per selected mode.
- `poll()` — every **6 s**: `GET /api/eth-status` → fills `#eth_info`
  (`mode / ip / mac / dhcp_gw / dhcp_dns / mac_override`); first poll may fail while the
  bootstrap is single-threaded busy — retried silently.
- `pollWifi()` — every **8 s**: `GET /api/wifi-runtime-status` → `{disabled, connected,
  ssid}` drives the amber card's status line.
- Save click: collects `_html=1`, `mode`, plus `ip/gw/dns` (static only) and `mac`;
  `POST /api/eth-config`; `j.status==='ok'` → success message + saved card; else shows
  error code from server.
- WiFi buttons: disable asks for confirmation then POSTs; enable POSTs directly.

---

## 7. Server API

| Endpoint | Method | Handler | Purpose / shape |
|---|---|---|---|
| `/api/eth-status` | GET | `network_handler.cpp:480` (`handleEthStatusGet`) | `{status:"ok", mode, ip, mac, gateway, dns, mac_override, dhcp, dhcp_gw, dhcp_dns, usbip_*}` |
| `/api/eth-config` | POST | `network_handler.cpp:543` → `sharedWebApplyEthernetConfig` (`web_shared_logic.cpp:386`) | validates `dhcp\|static`, IPv4 fields in static mode, optional MAC format; persists stem `ethernet`; errors as `{status:"erro", msg:"modo_invalido\|ipv4_invalido\|mac_invalido"}`. A missing/empty `mac` (partial round-trip POST) is **preserved** — it never wipes the stored `mac_override` |
| `/api/wifi-runtime-status` | GET | `network_handler.cpp:180` | `{disabled,connected,ip,ssid}` |
| `/api/wifi-disable` | POST | legacy `server.on` at `web_routes_network.cpp:151` (persist flag + delayed radio-off timer); Ethernet-side equivalent documented at `ethernet_bootstrap_handlers.h:491` | sets `wifi_disabled=true`, saves NVS section `wifi`, frees driver heap (~25–35 KB) |
| `/api/wifi-enable` | POST | `network_handler.cpp:285` | clears flag + saves NVS; message says restart required |

Error/status strings from these endpoints are partly **Portuguese** (`erro`,
`modo_invalido`, …) — see findings.

---

## 8. Persistence

| Data | Where |
|---|---|
| Ethernet section (mode, static ip/gw/dns, mac_override, enabled) | LittleFS `/config/ethernet.json` (stem `ETHERNET`) via `saveSection("ethernet")` |
| WiFi runtime disabled flag | inside NVS blob `cfg_wifi` (section `wifi.runtime_disabled`) |

Static values are cleared by the handler when switching to DHCP; applied at boot by the
Ethernet bootstrap (restart required after any change).

---

## 9. Runtime engine

- W5500 brought up during `ethernet_bootstrap`: DHCP or static per config; link/IP shown
  on the status card.
- Disabling WiFi powers down the radio immediately (platform adapter) and persists the
  flag across reboots; re-enabling requires restart because the driver is not re-inited at
  runtime.
- On Ethernet boards the same shared renderer serves the page over the W5500 transport
  (`streamSharedEthernetPage`), so configuration is possible even with WiFi off — that is
  the intended recovery path.

---

## 10. i18n keys

`ethPageTitle`, `ethStatusTitle`, `ethInfoInitial`, `ethConfigTitle`, `ethModeLbl`,
`ethModeDhcp`, `ethModeStatic`, `ethIpLbl`, `ethGwLbl`, `ethDnsLbl`, `ethMacLbl`,
`btnSave`, `alertSaved`, `alertSaveError`, `wifiRuntimeTitle`, `wifiDisableInfo1`,
`wifiDisableInfo2`, `wifiDisableInfo3`, `wifiDisableBtn`, `wifiEnableBtn`,
`wifiRtDisabled`, `wifiRtConnected`, `wifiRtConnecting`, `wifiConfirmDisable`,
`wifiMsgDisabled`, `wifiMsgEnabled`, `wifiMsgRestart`.

---

## 11. Review findings & improvement ideas

| # | Finding | Pri | Suggestion |
|---|---|---|---|
| 1 | Arrow functions used in poll/save handlers (`.then(r=>r.json())`, `.catch(()=>…)`) — violates the no-arrow-function rule used elsewhere in the project. | Low | Rewrite with `function(){}`. |
| 2 | Server error strings PT: `{status:"erro", msg:"modo_invalido"/"ipv4_invalido"/"mac_invalido"}` and wifi disable/enable messages — EN-fallback violation. | Low | Use English codes/messages or translate via `L()`. |
| 3 | `/api/wifi-disable` exists as two divergent implementations: WiFi AsyncWebServer timer-based stop vs unified persist-only registry path — behavior can differ per transport. | Medium | Consolidate into one `handleWifiDisablePost` registered via `registerApiRoute`. |
| 4 | Switching static→dhcp→save discards typed values silently (fields are cleared client-side by design) — no confirmation. | Info | Acceptable; document in tooltip. |
| 5 | Polling runs forever (6 s / 8 s) even when tab hidden. | Info | Pause on `document.hidden`. |
| 6 | MAC override accepts any valid hex-colon string but no normalization (lowercase/`:` enforcement) — mixed-case entries stored verbatim. | Info | Normalize before save. |

---

## 12. Source map

| Concern | File |
|---|---|
| HTML/JS asset (PROGMEM fragment) | `chatgpt/src/webpages/assets/page_ethernet_asset.cpp` |
| Renderer | `chatgpt/src/webpages/renderers/ethernet_page_renderer.cpp` |
| WiFi page route | `chatgpt/src/webpages/routes/web_routes_network.cpp:203` |
| Ethernet mirror | `chatgpt/src/ethernet_bootstrap_routes.h:166`, `web_shared_pages.cpp:87` |
| Status/config handlers | `chatgpt/src/webpages/handlers_unified/network_handler.cpp:480,543,180,285` |
| Shared apply/validation | `chatgpt/src/webpages/handlers_unified/web_shared_logic.cpp:386` |
| Legacy wifi-disable route | `web_routes_network.cpp:151`, notes `ethernet_bootstrap_handlers.h:491` |
| Persistence | `ConfigManager.cpp` stem `ETHERNET`; NVS `cfg_wifi` |
