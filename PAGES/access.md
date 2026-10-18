# Access — `/access`

> One-stop reference for the **Access / user sessions page**: purpose, layout, tabs,
> every button/input, client logic, API endpoints, persistence and the firmware-side
> session/authorisation engine. Written for both humans and AI agents.

---

## 1. Objective

Manage web **access control**: how many browsers can connect simultaneously, session
lifetimes (inactivity + absolute timeouts), admin (password-protected) login, a read-only
**viewer** account, local-processing-page exemption and home-page monitor visibility. All
settings take effect **without reboot** (read at request time).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /access` |
| Feature flag | `ENABLE_PAGE_ACCESS` (`config_defaults.h:276`, default `1`) — gates the page in manifests; routes are registered unconditionally (see ACC-4) |
| WiFi server | `web_routes_access.cpp:47` — streams via `streamAccessPageToPrint()` |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:214` → `streamSharedAccessPage` (`web_shared_pages.cpp:156`) |
| API (GET) | `GET /api/access-config` (`registerApiRoute`; Ethernet mirror `ethernet_bootstrap_handlers.h:515`) |
| API (POST) | `POST /api/access-config` — manual route with `HeavyApiRequestGuard` (3 s) on WiFi (`web_routes_access.cpp:26`); Ethernet `ethernet_bootstrap_routes.h:733` |
| Extra APIs | `GET /api/session-history`, `POST /api/access-kick-all` (registered; **no UI** on this page — see ACC-1) |
| Auth | Basic auth (`ensureBasicAuth`) + HTML clients only (`allowHtmlClient`) — the page itself is subject to the session limits it configures |
| Caching | `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` |

Renderer order: shell → `ACCESS_TABS_CSS` → `PAGE_ACCESS` (HTML + inline JS) →
`/config-manager.js` → `/auto.js`.

---

## 3. Layout

```
main.page.access > div.wrap
 ├ [🏠 Home]                          ← always first child of .wrap (project rule)
 ├ H1 "🔒 Access"
 ├ #accTabBar                          ← pill tab bar (built client-side)
 └ form#access-form
    ├ TAB "Web Sessions" (panel data-tab='tabAccessSessions')
    │   ├ input  #web_max_sessions        (number 1–5)
    │   ├ select #web_idle_timeout_min    (5–60 min)
    │   └ select #web_abs_timeout_min     (15–120 min)
    ├ TAB "Local Processing Pages" (data-tab='tabAccessCharts')
    │   └ checkbox #charts_exempt  + hidden charts_exempt_present=1
    ├ TAB "Monitoring" (data-tab='tabAccessMonitor')
    │   └ checkbox #serial_monitor_enabled + hidden serial_monitor_enabled_present=1
    ├ TAB "Users" (data-tab='tabAccessUsers', data-tab-default='1')   ← default tab
    │   ├ Administrator section
    │   │   ├ checkbox #auth_enabled  + hidden auth_enabled_present=1
    │   │   ├ input #web_username
    │   │   ├ input #web_password_new
    │   │   ├ input #web_password_confirm
    │   │   └ #pwd-match-msg  (error line, hidden by default)
    │   └ Viewer section
    │       ├ checkbox #viewer_enabled  + hidden viewer_enabled_present=1
    │       ├ #viewer_fields → input #viewer_username, #viewer_password
    │       └ routes hint: /, /status, /sensor-charts, /monitor, /files (read-only)
    └ [💾 Save]  (submit button)
```

Tabs are injected by `accInitTabs()` — a `<button class='acc-tab-btn'>` per panel with a
`data-tab` attribute; labels come from `_t(key, key)`. Panels toggle via
`.acc-panel.active`.

---

## 4. Inputs

| Input | ID / name | Type / range | Notes |
|---|---|---|---|
| Max simultaneous clients | `web_max_sessions` | number, 1–5 | server validates 1–5; counts **distinct client IPs** (not tabs/pages) with a 15 s TTL; a new IP when full gets HTTP 429; multiple tabs from the same PC count as **one** client |
| Inactivity timeout | `web_idle_timeout_min` | select 5/10/15/20/30/60 | default 10; server accepts 1–120 (wider than UI) |
| Absolute timeout | `web_abs_timeout_min` | select 15/20/30/60/120 | default 20; server accepts 5–240 |
| Local pages never expire | `charts_exempt` | checkbox | skips timeout kick on `/sensor-charts`/monitor/camera; still counts toward session limit |
| Show Serial Monitor button | `serial_monitor_enabled` | checkbox | shows the 📺 Serial Monitor button in the home Monitoring section |
| Require login | `auth_enabled` | checkbox | master HTTP Basic auth switch (off = open, no login) |
| Admin username | `web_username` | text | only applied when non-empty and changed |
| Admin new password | `web_password_new` | password | empty = keep current; must match confirm |
| Confirm password | `web_password_confirm` | password | client-side match check |
| Enable viewer | `viewer_enabled` | checkbox | enables read-only viewer account |
| Viewer username | `viewer_username` | text | default `viewer` |
| Viewer password | `viewer_password` | password | empty = keep current |

Checkboxes are protected by hidden `*_present` fields so the server can distinguish
"untouched" from "false": `charts_exempt_present`, `serial_monitor_enabled_present`,
`auth_enabled_present`, `viewer_enabled_present` (all `value=1`).

---

## 5. Buttons

| Button | Behaviour |
|---|---|
| 🏠 Home | link `/` — first child of `.wrap` |
| 💾 Save (submit) | validates password match → `disableBtn` + loading overlay → `POST /api/access-config` → `customAlert` + `showSavedCard()` |

Tab buttons (`#accTabBar` children) switch panels; no server round-trip.

---

## 6. Client-side logic (inline JS in `page_access_asset.h`)

- **`accInitTabs()`** — builds the pill bar from `.acc-panel[data-tab]`; if fewer than 2
  panels → no bar. Default active = the panel with `data-tab-default='1'` (Users).
- **`loadAccessConfig()`** (on window load) — sets password placeholders (`(unchanged)` /
  `Repeat password`), then `GET /api/access-config` and fills `web_max_sessions` (3),
  `web_idle_timeout_min` (10), `web_abs_timeout_min` (20), `charts_exempt`,
  `serial_monitor_enabled`, `web_username`, `auth_enabled`, `viewer_enabled`,
  `viewer_username` (viewer password is never returned/set — kept blank on purpose).
- **Submit handler** (`form#access-form`):
  1. `preventDefault`; read `web_password_new` vs `web_password_confirm`; if new password
     non-empty and mismatch → show `#pwd-match-msg`, abort;
  2. `disableBtn` + `showLoading()`;
  3. `new FormData(this)` → `URLSearchParams` → `POST /api/access-config`
     (`application/x-www-form-urlencoded`);
  4. `d.success` → `customAlert('✓ ' + message)` + `showSavedCard()`; else
     `customAlert('✗ Error: ' + message)`; network error → `customAlert` with err.message;
  5. `finally` → `hideLoading()` + `enableBtn(btn)`.
- Uses `_t(key, fallback)` (auto.js `window._t`), `customAlert`, no arrow functions —
  project rules honoured. Server responds with `d.success` (matches the `d.success` rule).

---

## 7. Server API

Handlers in `handlers_unified/config_handler.cpp`.

### `GET /api/access-config` (`:525`)

```json
{
  "web": {
    "max_sessions": 3, "idle_timeout_min": 10, "abs_timeout_min": 20,
    "username": "", "auth_enabled": false,
    "viewer_enabled": false, "viewer_username": "viewer"
  },
  "charts_exempt": false,
  "serial_monitor_enabled": false
}
```

Passwords are **never** returned.

### `POST /api/access-config` (`:547`)

Reads urlencoded form fields (WiFi route reconstructs body from POST params). Each field is
applied only when present and valid; `getInt` returns `-1` and **skips** out-of-range/absent
values silently:

| Field | Range | Setter |
|---|---|---|
| `web_max_sessions` | 1–5 | `setWebMaxSessions` |
| `web_idle_timeout_min` | 1–120 | `setWebIdleTimeoutMin` |
| `web_abs_timeout_min` | 5–240 | `setWebAbsTimeoutMin` |
| `web_username` | non-empty & changed | `setWebUsername` |
| `auth_enabled` (guarded by `auth_enabled_present`) | truthy | `setWebAuthEnabled` |
| `web_password_new` | non-empty, != `(não alterado)`/`(nao alterado)` | `setWebPassword` |
| `viewer_enabled` (guarded by `viewer_enabled_present`) | truthy | `setWebViewerEnabled` |
| `viewer_username` | non-empty & changed | `setWebViewerUsername` |
| `viewer_password` | non-empty, != PT sentinels | `setWebViewerPassword` |
| `charts_exempt` (guarded by `charts_exempt_present`) | truthy | `setChartsExemptSessions` |
| `serial_monitor_enabled` (guarded by `serial_monitor_enabled_present`) | truthy | `setSerialMonitorEnabled` |

> The PT sentinel strings `(não alterado)`/`(nao alterado)` are a **dead defensive check** —
> the client placeholder is English `(unchanged)` and an empty field is already skipped
> (see ACC-3).

Responses: no changes → 200 `{"success":true,"message":"No changes detected"}`; saved →
200 `{"success":true,"message":"Access settings saved"}`; LittleFS failure → 500
`{"success":false,"message":"Save error"}`.

### `GET /api/session-history` (`:614`)

`buildSessionHistoryJson()` (`webpage_state.cpp:76`) — ring buffer (`SESSION_HIST_MAX`)
of events `{ts, ip, type: connect|kick|expired, when}`. Registered but **not surfaced**
by any page UI (see ACC-1).

### `POST /api/access-kick-all` (`:622`)

`webclientForceReset()` + `sendVirtualMessage("Web sessions force-reset via /api/access-kick-all", INFO)`
→ 200 `{"success":true,"message":"All sessions terminated"}`. Registered but **no UI button**
calls it (see ACC-1).

---

## 8. Persistence

`configManager.saveSection("access")` → **`/config/access.json`** (8 KB budget). Content is
built from two sources (`ConfigManager.cpp:680-699`):

```json
{
  "web": {
    "enabled": true, "username": "", "password": "********", "auth_enabled": false,
    "single_client_only": false, "max_sessions": 3, "idle_timeout_min": 10,
    "abs_timeout_min": 20, "viewer_enabled": false, "viewer_username": "viewer",
    "viewer_password": "********"
  },
  "system": { "charts_page_enabled": true, "charts_exempt_sessions": false,
              "serial_monitor_enabled": false }
}
```

- Only keys in `isKnownWebAccessKey()` (`ConfigManager.cpp:10`) are persisted — junk/bloat
  is filtered on load and save, keeping the file small.
- **Passwords are stored masked (`********`)** in the JSON file; the real values live in the
  in-memory config and are applied at runtime.
- In-memory getters read `config["web"][...]` with defaults (3 / 10 / 20 / `""` / false /
  `viewer`); `charts_exempt_sessions` and `serial_monitor_enabled` live under
  `config["system"]`. Setter paths in `ConfigManager.h:1386-1394` + `:910-925`.

---

## 9. Runtime engine (session / authorisation enforcement)

All enforcement reads config at request time → changes apply immediately.

- **`cfgMaxSessions()` / `cfgIdleMs()` / `cfgAbsMs()`** (`webpage_state.cpp:217-219`) —
  wrappers over `getWebMaxSessions()`, `getWebIdleTimeoutMin()`, `getWebAbsTimeoutMin()`.
- **`allowHtmlClient()`** (`:236`) — two modes:
  - *Multi-client* (default): unique-**IP** tracker (`multiTrack`, 8 slots, 15 s TTL);
    unknown client when at `max_sessions` → HTTP **429** "Too many viewers"; an already-seen
    IP is always admitted regardless of tab count (so opening N tabs on one PC never trips
    the limit); first contact records a `connect` history event; activity timestamped each request.
  - *Single-client* (`single_client_only`): idle/absolute expiry → HTTP **503** with
    "Session expired… Please reload"; second client → **429**; `onDisconnect` resets state.
  - Viewer restriction: if the request authenticates with the **viewer** account, only
    `/`, `/status`, `/sensor-charts`, `/monitor`, `/files` are allowed (else 403).
  - `charts_exempt` short-circuits the idle/absolute expiry checks (local pages never kick).
- **`webclientCleanup()`** (`:340`) — periodic: prunes stale multi-client tracker (30 s
  no-activity reset) and expires single-client sessions, recording an `expired` history event.
- **`webclientForceReset()`** (`:377`) — records `kick-all` event, clears all session state.
- **`ensureBasicAuth()`** (`:390`) — master switch `auth_enabled`; if off → open. No
  credentials configured → open (lockout guard). Otherwise authenticates admin **or** viewer.
- **`allowHtmlClient`** viewer check mirrors `isViewerRequest()` (`:222`) — compares the
  request credentials against `viewer_username`/`viewer_password` only when viewer is enabled
  and has a password.
- **`serial_monitor_enabled`** gates the Serial Monitor button on the home page
  (`home_page_renderer.h`); **`charts_exempt`** also used by charts/dashboard/GRBL/files
  handlers (`sensor_handler.cpp:56,242`, `files_page_handler.cpp:52`,
  `proximity_dashboard_renderer.cpp:15`, `grbl_controller_page_renderer.cpp:14`).

---

## 10. i18n keys

**Firmware-side i18n (also added):** the API messages above are no longer hardcoded Portuguese in the handler — they now come from the firmware pack (`LangStrings`, `L(activeLang->…)`) and follow `system_language`. Page `<title>` and tab labels follow the same rule.

`accessPageTitle`, `accessTipHome`, `accessSessionsTitle/Desc`, `accessMaxClientsLbl/Hint`,
`accessTipMaxClients`, `accessIdleTimeoutLbl/Hint`, `accessTipIdleTimeout`,
`accessAbsTimeoutLbl/Hint`, `accessTipAbsTimeout`, `accessLocalPagesTitle/Desc`,
`accessTipLocalPages`, `accessLocalPagesNeverExpireLbl/Hint`, `accessMonitorTitle/Desc`,
`accessTipMonitorBtn`, `accessMonitorBtnLbl/Hint`, `accessUsersTitle/Desc`,
`accessAdminTitle/Desc`, `accessAuthEnableLbl/Hint`, `accessTipAuthEnable`,
`accessUsernameLbl`, `accessTipAdminUser`, `accessNewPasswordLbl`, `accessTipAdminPwd`,
`accessConfirmPasswordLbl`, `accessPwdMismatch`, `accessPwdNoChangePlaceholder`,
`accessPwdRepeatPlaceholder`, `accessViewerTitle/Desc`, `accessViewerEnableLbl`,
`accessTipViewerEnable`, `accessViewerUsernameLbl`, `accessTipViewerUser`,
`accessViewerPasswordLbl`, `accessTipViewerPwd`, `accessViewerRoutesHint`,
`accessErrorPrefix`, `accessUnknown`, `btnSave`, `accessTipSave`.
Tab labels use the tab keys themselves (`tabAccessSessions`, `tabAccessCharts`,
`tabAccessMonitor`, `tabAccessUsers`) as `_t` lookups.

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| 1 | **Kick-all and session-history have no UI.** `POST /api/access-kick-all` and `GET /api/session-history` are registered and documented in `/api-reference`, but the access page has no "terminate all sessions" button and no session list — admins can't use them from the UI. | Medium | Add a "Terminate all sessions" button (Sessions tab) and optionally a small history list fed by `/api/session-history`. |
| 2 | **Invalid/absent values are silently ignored.** `getInt` returns `-1` → the field is skipped with no error; a form that posts an out-of-range value just keeps the old one. Silent failure makes debugging hard. | Low | Report an error for each out-of-range field (or clamp instead of skip). |
| 3 | **Dead PT password sentinel.** Handler compares `web_password_new`/`viewer_password` against `"(não alterado)"`/`"(nao alterado)"` while the client placeholder is English `(unchanged)`. The client never sends those strings (empty = skipped earlier), so the check is dead and its language is PT. | Low | Remove the sentinel comparison (empty field already means "keep current"). |
| 4 | **Page routes not gated by `ENABLE_PAGE_ACCESS`.** `web_routes_access.cpp` registers page + APIs unconditionally (unlike `/sdcard` which is `#if ENABLE_PAGE_SDCARD==1`); the flag only gates the home button and self-test manifest. Setting the flag off still serves the page/APIs. | Low | Wrap page registration in `#if ENABLE_PAGE_ACCESS==1` (APIs may stay). |
| 5 | UI ranges are narrower than the API accepts (`idle` UI 5–60 vs server 1–120; `abs` UI 15–120 vs server 5–240) — silent mismatch between what the user can pick and what is valid. | Low | Align ranges (UI ≤ API is acceptable, but pick one documented limit). |
| 6 | Viewer section is fully visible even when `viewer_enabled` is off — fields could be collapsed/hidden until the checkbox is checked. | Info | Toggle `#viewer_fields` visibility from the checkbox. |
| 7 | **"Maximum simultaneous clients" limits distinct client IPs, not open tabs/pages** (owner decision: keep it this way). The page hint says "new browsers receive HTTP 429", which can read as a per-tab limit. Confirmed working: `allowHtmlClient()` multi-client path counts `multiTrack` unique IPs (8 slots, 15 s TTL) and returns 429 for a *new* IP when full; an already-tracked IP is admitted regardless of how many tabs it opens. Single-client mode adds a second cap (429 for any other IP) plus idle/absolute expiry (503). | Info | Documented here; no code change (owner chose per-IP). Optionally reword `accessMaxClientsHint` (EN+PT) to say "unique devices (IP)". |

---

## 12. Source map

| Concern | File |
|---|---|
| Renderer + tabs CSS | `chatgpt/src/webpages/renderers/access_page_renderer.{h,cpp}` |
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_access_asset.h` |
| Routes (WiFi + Ethernet) | `chatgpt/src/webpages/routes/web_routes_access.cpp`, `ethernet_bootstrap_routes.h:214` |
| HTTP handlers | `chatgpt/src/webpages/handlers_unified/config_handler.cpp` (`handleAccessConfigGet/Post`, `handleSessionHistoryGet`, `handleAccessKickAllPost`) |
| Ethernet API mirrors | `chatgpt/src/ethernet_bootstrap_handlers.h:515-541`, `ethernet_bootstrap_routes.h:733` |
| Config storage | `chatgpt/src/ConfigManager.{h,cpp}` (`web` + `system` sections; `access.json` stem) |
| Session engine | `chatgpt/src/webpages/webpage_state.cpp` (session history, `webclientForceReset`, `allowHtmlClient`) |
| Auth | `ensureBasicAuth`/`isViewerRequest` in `webpage_state.cpp:222,390` |
| Self-test manifest | `test_manifest_handler.cpp:265`, `selftest_handler.cpp:134` |