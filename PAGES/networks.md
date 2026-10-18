# Other Networks — `/networks`

> One-stop reference for the **Other Networks / Telnet page**: purpose, layout,
> inputs/buttons, client logic, API endpoints, persistence and the firmware-side
> Telnet server engine. Written for both humans and AI agents.
> This is NOT the `/wifi` page (see `PAGES/wifi.md`).

---

## 1. Objective

Enable/disable the **Telnet remote console** (debugging and monitoring) and rotate
its password. Changes take effect **without reboot**: enabling calls
`startTelnetServer()` immediately, disabling calls `stopTelnetServer()`
(`network_handler.cpp:630-635`). Modbus TCP has its own entry on the home page
(Networks section, below FTP Upload) — see `PAGES/modbus.md`; this page is
Telnet-only.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /networks` |
| Feature flag | `ENABLE_TELNET` (`config_defaults.h:54-55`, default `1`; `user_config.h:256` sets `0` in this checkout; every pack forces `0`, e.g. `pack_waveshare.h:135-136`) — gates the page AND `POST /api/telnet-config` (`web_routes_networks.cpp:14`) |
| Home entry | `%%NETWORKS_BUTTON%%` ("Outras Redes"), gated on `ENABLE_TELNET==1` (`home_page_renderer.h`) |
| WiFi server | `web_routes_networks.cpp:13` (`registerNetworksRoutes`) — chunked 3-segment PROGMEM serve via `serveThreeSegments()` |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:224-230` → `streamSharedNetworksPage` (`web_shared_pages.cpp:160-162` → `streamNetworksPageToPrint` in `networks_page_renderer.cpp:3-13`) |
| API (GET) | `GET /api/telnet-status` (`web_routes_network.cpp:49`, unconditional — handler returns 501 when disabled) |
| API (POST) | `POST /api/telnet-config` (`web_routes_networks.cpp:16`, inside `#if ENABLE_TELNET==1`); `POST /api/telnet-toggle` (`web_routes_network.cpp:50`, unconditional, **no UI** — see NET-5) |
| Auth | Basic auth (`ensureBasicAuth`) + HTML clients only (`allowHtmlClient`) (`web_routes_networks.cpp:20-22`) |
| Caching | `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` (`web_routes_networks.cpp:43-44`) |
| Manifests | `PAGE("/networks", ENABLE_TELNET)` in `test_manifest_handler.cpp:311` and `selftest_handler.cpp:162` |

Renderer order (WiFi): shell via `buildPageFrame(..., "&#127760; Outras Redes", ..., "<script src='/config-manager.js'></script>")` → `PAGE_NETWORKS` (HTML + inline JS) → `/auto.js` (shell suffix). Shared/Ethernet renderer: `streamPageShellStart(out, "&#127760; Outras Redes")` → `PAGE_NETWORKS` → `/config-manager.js` → `streamAutoJsFooter()` → `streamPageShellEnd()` (`networks_page_renderer.cpp:3-13`).

---

## 3. Layout

```
div.page.networks > div.wrap
 ├ div.actions.nav-inicio
 │   └ [🏠 Home]  (link `/`, data-i18n='btnHome')
 ├ H1 "🌐 Other Networks"  (data-i18n='netTelnetPageTitle')
 └ Telnet card #netTelnetCard (blue border #3b82f6, dark #0f172a)
     ├ H2 "💻 Telnet"  (data-i18n='netTelnetTitle')
     ├ div "Remote console access for debugging and monitoring."  (data-i18n='netTelnetDesc')
     └ form#telnet-form
         ├ hidden telnet_enabled_present=1
         ├ checkbox #telnet_enabled_chk + span "Enable Telnet"  (data-i18n='netTelnetEnable')
         ├ span "Telnet Password" (data-i18n='netTelnetPwdLbl') + hint "(leave empty to keep current)" (data-i18n='netTelnetPwdHint')
         ├ input#telnet_password_new (password, autocomplete='new-password', placeholder='(unchanged)')
         └ [💾 Save]  (submit, data-i18n='btnSave')
```

Single card, no tabs. Home link is the first child block of `.wrap` (project rule).
The card renders only when its backend exists (JS gating, §6) — e.g. CAM_P4
(no Telnet) shows nothing on this page; its Modbus entry is on the home page.

---

## 4. Inputs

| Input | ID / name | Type | Notes |
|---|---|---|---|
| Enable Telnet | `telnet_enabled` / `#telnet_enabled_chk` | checkbox | state loaded from `GET /api/telnet-status`; applied immediately on save (server starts/stops) |
| Guard | `telnet_enabled_present` | hidden, `value=1` | distinguishes "untouched" from "false" (`network_handler.cpp:620-621`) |
| Telnet password | `telnet_password_new` / `#telnet_password_new` | password | empty = keep current; never returned by any API; placeholder set from `netTelnetPwdPh` (`page_networks_asset.cpp:43-44`) |

No port/username fields — port is fixed at 23 (`telnet_server.cpp:24`) and the username is fixed `root` (see §9).

---

## 5. Buttons

| Button | Behaviour |
|---|---|
| 🏠 Home | link `/` — first block of `.wrap` |
| 💾 Save (submit) | `disableBtn` + `showLoading()` → `POST /api/telnet-config` (urlencoded) → `d.success` → `customAlert(message)` + `showSavedCard()`; else `customAlert(Error + message)`; network error → `customAlert(err.message)`; `finally` → `hideLoading()` + `enableBtn(btn)` |

---

## 6. Client-side logic (inline JS in `page_networks_asset.cpp:40-66`)

- **`t(k, fb)`** (`:41`) — `window._t` → `ui_lang_cache` → fallback; same helper shape as other pages.
- **`loadTelnetState()`** (on window `load`) — sets the password placeholder via `t('netTelnetPwdPh', '(unchanged)')`, then `GET /api/telnet-status` shows `#netTelnetCard` only when `d.enabled` exists (501 `{"status":"disabled"}` otherwise → hidden). Failures swallowed.
- **Submit handler** — `preventDefault`; `new FormData(this)` → `URLSearchParams` → `POST /api/telnet-config` as `application/x-www-form-urlencoded`; success key is `d.success` (project rule honoured); no arrow functions (project rule honoured). Unreachable when the card is hidden.
- The form never sends or receives the current password — by design.

---

## 7. Server API

Handlers in `handlers_unified/network_handler.cpp:584-663` (all internally `#if ENABLE_TELNET==1`-guarded, 501 `{"status":"disabled"}` otherwise).

### `GET /api/telnet-status` (`:585-594`)

```json
{"enabled": true}
```

Reads `configManager.getTelnetEnabled()` only.

### `POST /api/telnet-config` (`:614-663`)

Parses urlencoded `body` (or `query` fallback, `:619`):

| Field | Handling |
|---|---|
| `telnet_enabled_present` + `telnet_enabled` | only when present-marker found; accepts `1`/`true`/`on` (`:620-624`); on change → `setTelnetEnabled` + immediate `startTelnetServer()` (or `"Telnet pending: no active network"` WARN) / `stopTelnetServer()`, each with a `sendVirtualMessage` (`:626-637`) |
| `telnet_password_new` | applied when non-empty and not `(não alterado)`/`(nao alterado)` (`:640-645`) → `setTelnetPassword` (see NET-3) |

Responses (all **Portuguese**, see NET-4): no changes → 200 `{"success":true,"message":"Nenhuma alteração detectada"}`; saved → 200 `{"success":true,"message":"Configuração Telnet guardada"}` (`:655`); save failure → 500 `{"success":false,"message":"Erro ao guardar"}`. Persists via `markDirtyAndScheduleSave()` + `saveSection("telnet")` (`:652-653`).

### `POST /api/telnet-toggle` (`:596-612`)

Flips the current state, saves, returns `{"success":true,"enabled":<bool>}`. Registered but **never called by this page's UI** (see NET-5).

---

## 8. Persistence

`configManager.saveSection("telnet")` → **`/config/telnet.json`** (4 KB budget, `ConfigManager.cpp:859-867` via `STEM_TELNET`).

- Defaults (`ConfigManager.cpp:139-142`, backfilled `:296-300`): `{username:"root", password:"root", port:23, enabled:false}`.
- Getters `ConfigManager.h:982-997` (`getTelnetUsername/Password/Port/Enabled`); setters `:1510-1512` (`setTelnetUsername/Password/Port`, each `markDirtyAndScheduleSave()` — note `setTelnetEnabled` at `:996-997` only writes, the caller schedules the save).
- The **real password additionally lives in NVS** (`cfg_tlnt_pwd`, `ConfigManager.h:138`; load `:1352`/`:1677`, save `:1870`); JSON dumps mask it as `********` (`ConfigManager.cpp:716-717`, `:2111`; `ConfigManager.h:1925`).
- The generic `POST /api/config` handler also accepts `telnet_enabled` and saves the `telnet` section (`config_handler.cpp:280-284,462-464`).

---

## 9. Runtime engine (Telnet server)

Implemented in `chatgpt/src/telnet_server.{h,cpp}`, pumped from the main loop.

- **Transport**: `TELNET_PORT = 23` (`telnet_server.cpp:24`); lazily allocated `WiFiServer` and (when `ENABLE_ETHERNET==1`) `EthernetServer`; single active `UnifiedTelnetClient` (`telnet_server.h:20-35`).
- **Lifecycle**: `startTelnetServer()` binds whichever transports have an IP (`:87-163`); `stopTelnetServer()` drops the client and deletes listeners (`:66-84`); `syncTelnetNetworkBindings()` re-binds on network change and stops everything when disabled (`:178-242`).
- **Scheduling**: post-boot start when enabled (`loop_blocks.h:358-366`, `STARTUP_TELNET_PENDING`); per-loop pump `handleTelnet()` only after `STARTUP_COMPLETE` and only when enabled (`loop_blocks.h:398-406`, via `loop_impl.h:576` → `handleTelnetClient`).
- **Auth**: `Username:`/`Password:` prompt against `telnetUser`/`telnetPass` (defaults `root`/`root`, `:22-23`, synced from config at WiFi bring-up in `wifi_config_impl.h:172-174`); 60 s auth timeout (`TELNET_AUTH_TIMEOUT_MS`, `:33`); optional `ENABLE_TELNET_AUTH` (`config_defaults.h:57-58`).
- **Integration**: all `sendVirtualMessage` output mirrors to an authenticated Telnet client (`logger_impl.h:535-556`); console commands `TELNET ON`/`TELNET OFF` persist the flag (`process_line_impl.h:350-366`); `system_status_payload.h:525` reports `telnet_active`.

---

## 10. i18n keys

**Firmware-side i18n (also added):** the API messages above are no longer hardcoded Portuguese in the handler — they now come from the firmware pack (`LangStrings`, `L(activeLang->…)`) and follow `system_language`. Page `<title>` and tab labels follow the same rule.

All used keys exist in `lang_en_ui.h` (PT mirrors in `lang_pt_ui.h`): `btnHome` (`:7`), `btnSave` (`:8`), `netConfigSaved`/`netErrSaving`/`netUnknown` (`:1668-1670`), `netTipHome`, `netTipTelnetEnable`, `netTipTelnetPwd`, `netTipSave`, `netTelnetPageTitle` (`"🌐 Other Networks"`), `netTelnetTitle`, `netTelnetDesc`, `netTelnetEnable`, `netTelnetPwdLbl`, `netTelnetPwdHint`, `netTelnetPwdPh` (`:2201-2206`). HTML fallback text is English and matches the EN values verbatim. JS-gating covered by `tests/web/test_networks.js`; `tests/web/audit_i18n.py:13` maps this asset for the i18n audit.

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| NET-1 | **Page loads on Ethernet but its APIs don't exist there.** `GET /networks` is served on W5500 (`ethernet_bootstrap_routes.h:224-230`) but no Ethernet dispatch exists for `/api/telnet-status`, `/api/telnet-toggle` or `/api/telnet-config`; helpers `ethernetSendTelnetStatus` / `ethernetHandleTelnetTogglePost` (`ethernet_bootstrap_handlers.h:1011-1023`) have zero call sites, and the comment at `:1025` promises a `POST /api/telnet-config` handler that was never written. Over Ethernet the checkbox never initialises (error swallowed, `page_networks_asset.cpp:51`) and Save fails. | Medium | Wire the three endpoints into the Ethernet bootstrap dispatcher (shared handlers already exist) or return 404 for `/networks` on Ethernet until then. |
| NET-2 | **Hardcoded Portuguese chrome around translated content.** Frame title `"Outras Redes"` is hardcoded in both render paths (`web_routes_networks.cpp:28`, `networks_page_renderer.cpp:5`) while the H1 is i18n `"🌐 Other Networks"` (`netTelnetPageTitle`); the home-page button `"Outras Redes"` likewise has no `data-i18n` (`home_page_renderer.h:355`). Under EN locale the tab/title/button stay PT. | Low | Add e.g. `netPageTitle`/`navNetworks` keys and use them in all three spots. |
| NET-3 | **Dead PT password sentinel.** `handleTelnetConfigPost` skips `telnet_password_new` when it equals `"(não alterado)"`/`"(nao alterado)"` (`network_handler.cpp:641`), but the client placeholder is English `(unchanged)` and an empty field returns earlier — the check is unreachable and PT (same pattern as ACC-3 on `/access`). | Low | Drop the sentinel comparison; empty-means-keep is sufficient. |
| NET-4 | **Server messages in PT on an EN-first page.** `POST /api/telnet-config` / toggle responses (`"Nenhuma alteração detectada"`, `"Configuração Telnet guardada"`, `"Erro ao guardar"`, `network_handler.cpp:604,648,655,657`) are shown verbatim by `customAlert`, so EN users see PT confirmations. | Low | Return stable codes and let the client render `netConfigSaved`-style keys, or at least mirror the client's locale. |
| NET-5 | **Toggle endpoint with no caller + stale docs/comments.** `POST /api/telnet-toggle` is registered (`web_routes_network.cpp:50`) but no UI calls it (the form uses `/api/telnet-config`); the API-reference page documents `toggle` but not `telnet-config` (`page_api_reference_asset.h:103-104`); header comment `web_routes_networks.h:5` claims `"all /api/networks-* endpoints"` though no such endpoints exist (asset comments also say `Varias Redes`, `page_networks_asset.cpp:2`). | Info | Either add a toggle button to the page or remove the endpoint; fix the reference page and comments to name `/api/telnet-*`. |

---

## 12. Source map

| Concern | File |
|---|---|
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_networks_asset.{cpp,h}` |
| WiFi routes (page + `telnet-config`) | `chatgpt/src/webpages/routes/web_routes_networks.{cpp,h}` |
| WiFi API routes (`telnet-status`, `telnet-toggle`) | `chatgpt/src/webpages/routes/web_routes_network.cpp:49-50` |
| HTTP handlers | `chatgpt/src/webpages/handlers_unified/network_handler.cpp:584-663` (`.h:41-43`) |
| Shared/Ethernet renderer | `chatgpt/src/webpages/renderers/networks_page_renderer.cpp`, `chatgpt/src/web_shared_pages.cpp:160-162` |
| Ethernet page route (APIs missing — NET-1) | `chatgpt/src/ethernet_bootstrap_routes.h:224-230`, unwired helpers in `chatgpt/src/ethernet_bootstrap_handlers.h:1011-1025` |
| Config storage | `chatgpt/src/ConfigManager.{h,cpp}` (`telnet` section → `/config/telnet.json` + NVS `cfg_tlnt_pwd`) |
| Telnet engine | `chatgpt/src/telnet_server.{h,cpp}` (loop pump `loop_blocks.h:398-406` via `loop_impl.h:576`; boot start `:358-366`) |
| Tests | `tests/web/test_networks.js` (card presence + JS gating + route guards); pack-shot fixtures `tests/web/pages/pack_networks.html`, `tests/web/shots/pack_pack_networks.html` (`batch_pack_shots.py:16`), i18n audit (`audit_i18n.py:13`) |
| Manifests | `test_manifest_handler.cpp:289`, `selftest_handler.cpp:158` |
