# Wi-Fi — `/wifi`

> Reference for the **Wi-Fi configuration page**: SSID/password form, network scanner and
> the hybrid AP/STA runtime rules. Written for both humans and AI agents.

---

## 1. Objective

Configure the **station (client) Wi-Fi connection**: enter SSID + password (or pick a
network from a live scan), save, and let the firmware reconnect/disconnect automatically.
The page also documents the AP lifecycle rules (internal Access Point disabled after first
successful client connection; reactivated on reboot when no client network is saved).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /wifi` |
| Feature flag | `ENABLE_WIFI` (page gating in manifests); route always compiled when web is up |
| WiFi server | `web_routes_network.cpp:174` → `streamWifiPageToPrint()` (`wifi_page_renderer.cpp:6`) |
| **AP-only special case** | `WiFi.getMode()==WIFI_AP && !ethernetHasIp()` → serves self-contained `PAGE_WIFI_AP_MINIMAL` (`renderers/wifi_ap_minimal.h`) with no external assets (core.css/auto.js unavailable in AP-only) |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:202` → `streamSharedWifiPage` (`web_shared_pages.cpp:83`) — note: Ethernet boards can still configure the (disabled) WiFi radio |
| Auth / caching | `ensureBasicAuth` + `allowHtmlClient`; `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` |
| Renderer shell | `streamPageShellStart` + fragment + `/config-manager.js` + auto.js footer |

---

## 3. Layout

```
main.page > div.wrap
├ nav.actions.nav-inicio: 🏠 Home link (/)
├ H1 "📶 Wi-Fi"                      [wifiPageTitle]
├ ℹ️ Information card (blue border)
│  └ #wifiInfoDesc [data-i18n-html=wifiInfoDesc]
│     • AP disabled after first successful connection (interference)
│     • Scan + tap a network to fill the form
│     • Reboot reactivates AP if no client network saved
├ ⚙️ Configuration card
│  └ form#send-config-form  action=/api/config  hidden input _html=1
│     ├ label 📡 SSID [wifiSsidLbl] + hint [wifiSsidHint] → input#ssid name=ssid
│     ├ label 🔐 Password [wifiPasswordLbl] + hint [wifiPasswordHint] → input#password type=password
│     └ 💾 Save button (submit) [btnSave] title wifiTipSave
└ 📡 Available Networks card
   ├ center: 🔍 Scan button (#sb, onclick=scan()) [wifiScanBtn]
   └ div#nets → scan results (.net rows)
```

---

## 4. Inputs

| Input | ID | Type | Notes |
|---|---|---|---|
| SSID | `ssid` | text | pre-filled from `GET /api/config?section=wifi` (`d.wifi.ssid`); placeholder set by i18n |
| Password | `password` | password | never pre-filled (server masks as `********`); hidden entirely when an Open network is selected |

---

## 5. Buttons

| Button | ID / handler | Behaviour |
|---|---|---|
| 🏠 Home | link `/` | first child of `.wrap` |
| 💾 Save | submit of `#send-config-form` | JS handler below |
| 🔍 Scan | `#sb` → `scan()` | GET `/api/wifi-scan`, renders top-10 networks |
| *(row click)* | `.net` onclick=`sel(ssid,enc,ev)` | fills SSID, hides/clears password for Open networks, focuses password, highlights row |

---

## 6. Client-side logic

Inline `<script>` in the asset (auto.js supplies `customAlert`, `disableBtn`, `showLoading`,
`hideLoading`, `enableBtn`, `showSavedCard`, `window._t`):

- `wt(k,fb)` — i18n helper: `window._t()` else `localStorage.ui_lang_cache` else fallback.
- `applyWifiTexts()` — sets localized placeholders for ssid/password.
- `loadWifiConfig()` — `GET /api/config?section=wifi`; fills `#ssid` only (password masked server-side).
- `scan()` — disables button, "Scanning...", `GET /api/wifi-scan`; renders each network as
  `<div class="net" onclick="sel('${n.ssid}','${n.encryption}',event)">🔓/🔒 ${n.ssid} 📶…</div>`
  (bars: RSSI > −50 → 3, > −70 → 2, else 1). Empty list → "No networks". Auto-runs 1 s after load.
- `sel(ssid,enc,ev)` — fills form; Open → clears + hides password; else shows + focuses it.
- Form submit:
  1. `preventDefault`, `disableBtn(btn)` + `showLoading()`
  2. `FormData` → `URLSearchParams`, forces `section=wifi`
  3. `POST /api/config`; on `d.success` → alert saved + `showSavedCard()`;
     on failure → `d.message` or generic error.
- Note: after saving a new SSID the device disconnects and re-associates (see §8) — the
  browser session drops mid-request; the response may not arrive.

---

## 7. Server API

| Endpoint | Method | Handler | Purpose / shape |
|---|---|---|---|
| `/api/config?section=wifi` | GET | `config_handler.cpp:143` (`handleConfigGet`) | `{wifi:{ssid,password:"********",runtime_disabled,…}}` — passwords masked by `getFullConfig(true)` |
| `/api/config` | POST | `config_handler.cpp:206` (`handleConfigPost`) | accepts `ssid`,`password` (skips `"********"`); adds to saved networks; saves stem `wifi` (NVS); triggers reconnect when SSID differs |
| `/api/wifi-scan` | GET | `network_handler.cpp:97` (`handleWifiScanGet`) — WiFi: `web_routes_network.cpp:62` (direct `server.on`); Ethernet: `ethernet_bootstrap_handlers.h:2993` wrapper | synchronous scan (~300 ms/channel), returns top **10** by RSSI: `{networks:[{ssid,rssi,channel,encryption:"Open"\|"Encrypted",authType}]}` |
| `/api/wifi-runtime-status` | GET | `network_handler.cpp:180` | `{disabled,connected,ip,ssid}` (used by /ethernet page) |
| `/api/wifi-enable` | POST | `network_handler.cpp:285` | clears `wifi_disabled` flag, saves NVS, asks for restart |
| `/api/wifi-disable` | POST | legacy `server.on` at `web_routes_network.cpp:151` (persist + delayed radio-off timer); Ethernet variant documented at `ethernet_bootstrap_handlers.h:491` | persists `wifi_disabled=true`, frees driver heap |
| `/api/wifi-reconnect` | POST | `network_handler.cpp:192` | switches to best available saved network |
| `/api/config/saved_networks` | GET/POST/DELETE | `web_routes_network.cpp:83-101` → `handleSavedNetworks*` (`network_handler.cpp:403-477`) | list (masked)/add/remove entries; used by `/networks` page |
| `/api/wifi-intelligent-status`, `/api/wifi-force-ap`, `/api/wifi-disable-ap`, `/api/clear-ap-lock` | various | `network_handler.cpp:146-305` | AP/hybrid diagnostics (registered via `registerApiRoute`, `web_routes_network.cpp:43-53`) |

Validation (POST `/api/config` section=wifi): empty/masked values ignored — no length or
format checks on SSID/password beyond truthiness.

---

## 8. Persistence

| Data | Where |
|---|---|
| `wifi.ssid`, `wifi.password`, `wifi.runtime_disabled` | **NVS** namespace `esp32_config`, key `cfg_wifi` (JSON blob) — boot-critical, per AGENTS.md rule (`ConfigManager.cpp:1877-1896`) |
| `saved_networks[]` | NVS key `cfg_saved_net` |
| Web password (unified) | NVS `cfg_web_pwd` (saved alongside when present) |

`saveSection("wifi")` writes **only** to NVS — no LittleFS file. Load path restores from
NVS at boot (`ConfigManager.cpp:1444,1463`).

---

## 9. Runtime engine

- Hybrid manager states: `BOOT_AP_STA(0)` → `STA_ONLY(1)` after client connect /
  `RECOVERY_AP_STA(2)` on failure (`network_hybrid_manager.*`).
- On save with new SSID: `addWifiNetwork()`, then if current association differs —
  `WiFi.disconnect()`, `currentHybridState = HYBRID_BOOT_AP_STA`, 60 s boot window,
  `WiFi.mode(WIFI_AP_STA)` + `WiFi.begin(ssid,pass)` (`config_handler.cpp:455-465`).
- First successful client connection stops the internal AP (reduces RF interference);
  reboot with no saved client network reactivates AP.
- `wifi_disabled` runtime flag: platform adapter powers down the radio; re-enabling
  requires restart (driver re-init).

---

## 10. i18n keys

**Firmware-side i18n (also added):** the API messages above are no longer hardcoded Portuguese in the handler — they now come from the firmware pack (`LangStrings`, `L(activeLang->…)`) and follow `system_language`. Page `<title>` and tab labels follow the same rule.

`wifiPageTitle`, `infoTitle`, `wifiInfoDesc`, `wifiConfigTitle`, `wifiSsidLbl`,
`wifiSsidHint`, `wifiTipSsid`, `wifiSsidPlaceholder`, `wifiPasswordLbl`, `wifiPasswordHint`,
`wifiTipPassword`, `wifiPasswordPlaceholder`, `btnSave`, `wifiTipSave`,
`wifiNetworksTitle`, `wifiScanBtn`, `wifiTipScan`, `wifiSearching`, `wifiNoNetworks`,
`wifiGenericError`, `wifiErrorPrefix`, `alertSaved`, `alertSaveError`, `btnHome`.
Runtime fallbacks used inline: `'Scanning...'`, `'No networks'`, `'Network name'`,
`'Password (if any)'` — plus **PT leftovers** (see findings).

---

## 11. Review findings & improvement ideas

| # | Finding | Pri | Suggestion |
|---|---|---|---|
| 1 | Inline JS hardcodes PT fallbacks: `'✅ Configuração guardada!'`, `'❌ Erro ao guardar'`, `'Erro: '` — violates EN-fallback rule (all other fallbacks are English). | Medium | Switch to English strings (keys `alertSaved`/`alertSaveError` already exist). |
| 2 | Arrow functions used throughout (`r=>r.json()`, `.catch(()=>{})`, `.finally(...)`) — violates the no-arrow-function ESP32-browser-compat rule. | Low | Rewrite with `function(){}` like the RS232/Ethernet pages. |
| 3 | `scan()` interpolates raw SSIDs into an inline `onclick="sel('${n.ssid}',…)"` — quotes/backslashes in SSID break the handler or inject attributes (SSID is attacker-controlled within radio range). | Low | Build rows via DOM APIs (`textContent`, `addEventListener`). |
| 4 | `/api/wifi-scan` performs a **synchronous** scan on the async_tcp thread — blocks other requests for the scan duration; UI also auto-scans on every page load. | Info | Consider async scan + cached results endpoint. |
| 5 | Scan results capped at top-10 by RSSI server-side — UI gives no hint that more networks may exist. | Info | Show "(top 10)" hint. |
| 6 | Saving a new SSID drops the browser's own connection; success alert often never displays. | Low | Warn user before submit ("you will be disconnected"). |
| 7 | No client validation (SSID ≤32 chars, WPA password ≥8) — errors only surface as silent reconnect failures. | Low | Add minimal length hints. |

---

## 12. Source map

| Concern | File |
|---|---|
| HTML/JS asset (PROGMEM) | `chatgpt/src/webpages/assets/page_wifi_asset.cpp` |
| Renderer | `chatgpt/src/webpages/renderers/wifi_page_renderer.cpp` |
| AP-minimal fallback page | `chatgpt/src/webpages/renderers/wifi_ap_minimal.h` |
| WiFi page route | `chatgpt/src/webpages/routes/web_routes_network.cpp:174` |
| Network API routes (registry) | `web_routes_network.cpp:43-53` |
| Scan/status/reconnect/saved-networks handlers | `chatgpt/src/webpages/handlers_unified/network_handler.cpp` |
| Config GET/POST (section=wifi) | `chatgpt/src/webpages/handlers_unified/config_handler.cpp:143,206` |
| NVS persistence | `chatgpt/src/ConfigManager.cpp:1877-1896`, keys `ConfigManager.h:134` |
| Hybrid/AP runtime | `chatgpt/src/network_hybrid_manager.*`, `network_access_point.*` |
| Ethernet bootstrap mirror | `chatgpt/src/ethernet_bootstrap_routes.h:202`, `ethernet_bootstrap_handlers.h:2993` |
