# Google Script — `/google-script`

> One-stop reference for the **Google Apps Script email page**: purpose, layout,
> every button/input, client logic, API endpoints, persistence and the firmware-side
> Google-email engine. Written for both humans and AI agents.

---

## 1. Objective

Configure the lightweight **Google Apps Script email transport** as an alternative to
direct SMTP: pick the send method (`smtp` / `google`), set the destination email,
paste the deployed Web App URL, download the server-side script template, and send a
test email. Saving writes to the shared `email` section (`smtp_email` stem); sending
is done by `google_email.cpp` via HTTPS POST (`?action=simple|start|chunk|finish|cancel`)
against the user-deployed script (`google_apps_script_asset.cpp`).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /google-script` |
| Feature flag | `ENABLE_PAGE_GOOGLE_SCRIPT` (`chatgpt/src/config_defaults.h:279-280`, default `0`) — gates WiFi route, Ethernet mirror and home button; opt-in via `chatgpt/user_config.h:246` (commented example) |
| WiFi server | `chatgpt/src/webpages/routes/web_routes_network.cpp:227-248` — `#if ENABLE_PAGE_GOOGLE_SCRIPT==1`, streams via `streamGoogleScriptPageToPrint()` |
| Ethernet (W5500) | `chatgpt/src/ethernet_bootstrap_routes.h:314-320` — `#if ENABLE_PAGE_GOOGLE_SCRIPT==1` → `streamSharedGoogleScriptPage()` (`chatgpt/src/web_shared_pages.cpp:132-133`); else `404 not found` |
| Home button | `chatgpt/src/webpages/renderers/home_page_renderer.h:362-366` — `#if ENABLE_PAGE_GOOGLE_SCRIPT==1`, label `homeGoogleScript` ("🌐 Google App Script") |
| API (GET) | `GET /api/config?section=email` (`chatgpt/src/webpages/handlers_unified/config_handler.cpp:143-204`) |
| API (POST) | `POST /api/config` with `section=email` (`config_handler.cpp:208-486`, stem `smtp_email` at `:452`) |
| Extra APIs | `POST /api/email-test` (`chatgpt/src/webpages/handlers_unified/email_handler.cpp:51-57`); `GET /GoogleAppsScript_EmailHandler.js` download (`chatgpt/src/webpages/handlers/email_handlers.cpp:8-27`; Ethernet mirror `ethernet_bootstrap_routes.h:456-457`) |
| Route registry | `{"GET", "/google-script"}` (`chatgpt/src/webpages/routes/route_registry_api.cpp:231`); self-test manifest has **no** `/google-script` entry (see GS-4) |
| Auth | `ensureBasicAuth` + `allowHtmlClient` (same guard block as other pages, `web_routes_network.cpp:229-231`) |
| Caching | `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` (`web_routes_network.cpp:240-241`) |

Renderer order: shell (`streamPageShellStart`, title `"&#127760; Google App Script"`) → `PAGE_GOOGLE_SCRIPT` (HTML + inline JS) → `/config-manager.js` → auto.js footer (`chatgpt/src/webpages/renderers/google_script_page_renderer.cpp:6-16`).

---

## 3. Layout

```
div.page.google-script > div.wrap
 ├ div.actions.nav-inicio > a.btn[href='/']            ← Home, first child of .wrap
 ├ H1 "🌐 Google App Script" (data-i18n='gasPageTitle')
 ├ form#gas-config-form[action='/api/config'][method='POST']
 │   ├ input[type=hidden][name=_html][value=1]
 │   ├ div (blue card, General Settings)
 │   │   ├ H2 "⚙️ ..." (data-i18n='emailGeneralTitle')
 │   │   ├ .section > H3 (emailMethodLbl) + .section-desc (emailMethodDesc)
 │   │   │   └ select#email_method[name=email_method] (smtp|google)
 │   │   └ .section > H3 (emailDestLbl) + .section-desc (emailDestDesc)
 │   │       └ input#email_dest[name=email_dest][placeholder='destino@email.com']
 │   ├ div (amber card, GAS)
 │   │   ├ H2 (emailGoogleTitle) + info box (emailGoogleAdvInfo, data-i18n-html)
 │   │   ├ .section > H3 (emailGasUrlLbl) + .section-desc (emailGasUrlDesc)
 │   │   │   └ input#google_script_url[name=google_script_url][placeholder='https://script.google.com/macros/s/...']
 │   │   └ .section > H3 (emailGasCodeTitle)
 │   │       ├ a.btn[href='/GoogleAppsScript_EmailHandler.js'][download] (emailGoogleCodeDownload)
 │   │       └ info box (emailGoogleInstructions, data-i18n-html)
 │   └ div (centered) > button[type=submit][form=gas-config-form].btn (btnSave "💾 Guardar")
 └ div (purple card, test)
     ├ H2 (emailTestTitle) + desc (emailTestDesc)
     └ button#btn-test-email-gas[type=button].btn (emailTestBtn "✉️ Enviar Email de Teste")
```

Element IDs: `gas-config-form`, `email_method`, `email_dest`, `google_script_url`, `btn-test-email-gas` (`chatgpt/src/webpages/assets/page_google_script_asset.cpp:9-56`).

---

## 4. Inputs

| Input | ID / name | Type / range | Notes |
|---|---|---|---|
| Send method | `email_method` / `email_method` | select `smtp` / `google` | persisted via `setEmailMethod`; no client-side validation |
| Destination email | `email_dest` / `email_dest` | text (email address) | alias of `email_destination`; handler accepts either key (`config_handler.cpp:330-332`) |
| GAS Web App URL | `google_script_url` / `google_script_url` | text (https URL) | persisted via `setGoogleScriptUrl`; server accepts any non-empty string (see GS-2/GS-3) |
| HTML marker | `_html` = `1` | hidden | legacy marker; JS submit overrides with `section=email` + urlencoded body |

---

## 5. Buttons

| Button | Behaviour |
|---|---|
| 🏠 Home (`btnHome` "🏠 Início" fallback) | link `/` — first child of `.wrap` |
| 💾 Guardar (`btnSave`) | submits `gas-config-form` via JS `fetch` → `POST /api/config` (`section=email`) → `customAlert('✅ ' + alertSaved)` + `showSavedCard()` on success |
| 📥 Baixar Código (`emailGoogleCodeDownload`) | link `GET /GoogleAppsScript_EmailHandler.js` (`target=_blank`, `download`) — served chunked with `Content-Disposition: attachment` |
| ✉️ Enviar Email de Teste (`emailTestBtn`, `#btn-test-email-gas`) | `testEmailFromGas()` → `POST /api/email-test` (empty JSON body) → `customAlert` with server message |

---

## 6. Client-side logic (inline JS in `page_google_script_asset.cpp:58-124`)

- **`gasText(k, fb)`** (`:59-69`) — `window._t(k, fb)` if present, else `ui_lang_cache` from `localStorage`, else fallback (or key).
- **`testEmailFromGas()`** (`:71-86`) — `disableBtn` + `showLoading()`; `fetch('/api/email-test', {method:'POST', Content-Type:'application/json'})`; `d.success` → `customAlert(d.message || gasText('emailTestStarted', ...))`, else `customAlert(d.message || gasText('emailTestStartError', ...))`; network error → `customAlert(gasText('emailConnectionErrorPrefix', ...) + err.message)`; `finally` → `hideLoading()` + `enableBtn`.
- **Config load** (`:90-98`) — `GET /api/config?section=email` (`cache:'no-store'`); fills `email_method` (`email.method || 'google'`), `email_dest` (`email.destination || ''`), `google_script_url` (`gs.google_script_url || ''`, where `gs = d.google_email || {}` — **mismatched key, see GS-1**).
- **Submit handler** (`:100-123`) — `preventDefault`; `fd.set('section','email')`; `fetch(action, {method:'POST', body: URLSearchParams(fd), Content-Type:'application/x-www-form-urlencoded'})`; success → `customAlert('✅ ' + gasText('alertSaved', 'Configuration saved!'))` + `showSavedCard()`; failure → `customAlert('❌ ' + gasText('emailErrorPrefix', 'Error: ') + (d.message || gasText('accessUnknown', 'unknown')))`.
- No arrow functions; uses `customAlert`, `disableBtn`/`enableBtn`, `showLoading`/`hideLoading` — project rules honoured.

---

## 7. Server API

Handlers in `chatgpt/src/webpages/handlers_unified/config_handler.cpp` and `email_handler.cpp`.

### `GET /api/config?section=email` (`config_handler.cpp:143-204`)

Returns `{smtp, email}` slices of `getFullConfig(true)` (`:182`); the GAS URL lives at
`smtp.google_script_url`, method/destination at `email.method` / `email.destination`.
Passwords masked (`********`). Missing/invalid `section` → 400.

### `POST /api/config` with `section=email` (`config_handler.cpp:208-486`)

Urlencoded/JSON body keys consumed: `google_script_url` → `setGoogleScriptUrl` (`:316-317`,
only when non-empty); `email_method` → `setEmailMethod` (`:319-320`); `email_destination`
or `email_dest` → `setEmailDestination` (`:330-332`); plus the shared SMTP/TLS/recipient
fields when present. Persists stem `smtp_email` (`:452`); responses: no changes → 200
`{"success":true,"message":"No changes detected"}` (`:431-434`); saved → 200
`{"success":true,"message":"Config saved"}`; save failure → 500 `{"success":false,"message":"Save failed"}`.

Ethernet legacy mirror `GET /api/config/email` returns a flat doc including
`google_script_url`, `email_destination` and nested `email{method,destination}`
(`ethernet_bootstrap_routes.h:471-486`).

### `POST /api/email-test` (`email_handler.cpp:18-57`)

Shared SMTP/GAS test (not page-specific): 409 `receiving_data` while `isReceivingData`;
429 `throttle` within 10 s (`emailTestThrottleMs`); 400 `config_incomplete` when
`isEmailConfiguredAndWifiConnected()` fails; 200 `success:true` with PT message
"⚡ Teste iniciado com sucesso! ... (~30 segundos)" via `startEmailTestAsync`; 500
`start_failed`; 503 when both transports are compiled out.

### `GET /GoogleAppsScript_EmailHandler.js` (`email_handlers.cpp:8-27`)

Chunked `application/javascript` with `Content-Disposition: attachment` and `no-cache`,
served from PROGMEM `GOOGLE_APPS_SCRIPT_EMAIL_HANDLER_JS`. Gated by
`#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1` — **not** by `ENABLE_PAGE_GOOGLE_SCRIPT`.

---

## 8. Persistence

`ConfigManager` fields: `config["smtp"]["google_script_url"]` (`ConfigManager.h:730-737`,
`setGoogleScriptUrl` → `markDirtyAndScheduleSave`), `config["email"]["method"]`
(`:740-747`) and `config["email"]["destination"]` (`:749-752`). Defaults ensured in
`ConfigManager.cpp:253` (`google_script_url=""`), `:256` (`destination=""`), `:276`
(`method="smtp"`). Saved by `saveSection("smtp_email")` → `/config/smtp_email.g<slot>.json`
via `stemPath`/`saveStem` (`ConfigManager.cpp:823-847`); SMTP password masked
(`********`), GAS URL stored in plaintext. `stems[]` includes `smtp_email`
(`ConfigManager.cpp:1034-1035`); snapshot handlers also expose `google_script_url`
(`handlers/config_snapshot_handler.cpp:68`, `handlers_unified/config_handler.cpp:510`).

---

## 9. Runtime engine (Google-email transport)

Compiled under `#if ENABLE_GOOGLE_EMAIL==1` (`chatgpt/src/google_email.cpp:6`,
default `1` per `config_defaults.h:49-50`):

- `googleEmailIsAvailable()` (`:87-99`) — non-empty URL with `http(s)://` prefix.
- `googleEmailGetScriptUrl()` (`:105-107`) — returns `configManager.getGoogleScriptUrl()`.
- `googleEmailHttpPost()` (`:139-177`) — `HTTPClient` JSON POST, `HTTPC_FORCE_FOLLOW_REDIRECTS` (redirect as GET so GAS does not run twice), timeout `GOOGLE_EMAIL_TIMEOUT_MS`, retry with backoff.
- `googleEmailSendSimple()` (`:266-293`) — `?action=simple`, `maxRetries=1` (no duplicate sends), mutex-guarded.
- Chunked attachments: `googleEmailStartSession` (`:183-206`), `googleEmailUploadChunk` (`:208-231`), `googleEmailFinishSession` (`:233-248`), `googleEmailCancelSession` (`:250-260`); `googleEmailSendWithFile` (`:299-512`) and `googleEmailSendWithFilePartial` (`:518-725`) stream SD/PSRAM files in `GOOGLE_EMAIL_CHUNK_SIZE` base64 chunks; `googleEmailSendEmailDay` (`:731-961`) batches folder sends.
- Dispatch: `runtime_email.cpp:503-526` selects SMTP vs GAS by `email.method`; email-gating in `runtime_environment.cpp:31-44` requires URL + recipient for `method=="google"`; senders include the RS232/config email path (`rs232_config_impl.h:597-603`) and the email-day queue.

---

## 10. i18n keys

Page markup (`data-i18n`): `btnHome`, `gasPageTitle`, `emailGeneralTitle`, `emailMethodLbl`,
`emailMethodDesc`, `emailMethodSmtp`, `emailMethodGoogle`, `emailDestLbl`, `emailDestDesc`,
`emailGoogleTitle`, `emailGasUrlLbl`, `emailGasUrlDesc`, `emailGasCodeTitle`,
`emailGoogleCodeDownload`, `emailTestTitle`, `emailTestDesc`, `emailTestBtn`, `btnSave`;
`data-i18n-html`: `emailGoogleAdvInfo`, `emailGoogleInstructions`; `data-i18n-title`:
`gasTipHome`, `gasTipMethod`, `gasTipDest`, `gasTipUrl`, `gasTipSave`, `gasTipTest`.
JS `gasText()` keys: `emailTestStarted`, `emailTestStartError`, `emailConnectionErrorPrefix`,
`alertSaved`, `emailErrorPrefix`, `accessUnknown`. All exist in
`chatgpt/src/languages/lang_en_ui.h` (`:7-8`, `:63`, `:486-503`, `:614`, `:873-876`,
`:957`, `:1975-1980`). Home entry uses `homeGoogleScript` (`lang_en_ui.h:30`).

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| GS-1 | **Saved URL never pre-fills.** JS reads `d.google_email.google_script_url` (`page_google_script_asset.cpp:94,97`) but `GET /api/config?section=email` returns `{smtp, email}` (`config_handler.cpp:182`) with the URL at `smtp.google_script_url` (`ConfigManager.h:730-732`). `gs` is always `{}` so the field renders blank even after saving. | High | Read `d.smtp.google_script_url` (keep `d.google_email` as fallback) and add a web-test assert on the load mapping. |
| GS-2 | **URL cannot be cleared.** POST applies `google_script_url` only `if (v.length())` (`config_handler.cpp:316-317`); sending an empty field keeps the old URL. Same pattern for `email_method` (`:319-320`). | Medium | Accept present-but-empty as clear (or add an explicit "clear URL" affordance). |
| GS-3 | **No server-side URL validation.** Any non-empty string is stored; only `googleEmailIsAvailable()` checks the `http(s)://` prefix (`google_email.cpp:93-96`), so a typo saves silently and fails later at test/send time. | Low | Reject non-`http(s)` URLs (or warn) in `handleConfigPost` before persisting. |
| GS-4 | **Page missing from self-test manifest; JS default contradicts firmware default.** `test_manifest_handler.cpp:254-293` lists `/email` but no `/google-script` entry (route exists at `route_registry_api.cpp:231`); JS defaults `email.method` to `'google'` (`page_google_script_asset.cpp:95`) while firmware default is `'smtp'` (`ConfigManager.cpp:276`, `ConfigManager.h:740-741`). | Low | Add `PAGE("/google-script", ENABLE_PAGE_GOOGLE_SCRIPT)` and default the select to the server value (or `smtp`). |
| GS-5 | **PT fallback text in markup.** Hardcoded fallbacks are Portuguese ("Início", "Configurações Gerais", "Método de Envio", "Baixar Código", "Guardar", "Enviar Email de Teste" — `page_google_script_asset.cpp:6-53`) while the project rule is English HTML fallback text. | Low | Replace fallbacks with the English `lang_en_ui.h` strings (keys already exist). |
| GS-6 | **Web test does not cover load/save mapping.** `tests/web/test_google_script.js:1-34` only exercises `gasText` and `testEmailFromGas` success/failure; nothing asserts the `GET /api/config?section=email` → field mapping, so GS-1 is not caught. Fixture `tests/web/pages/google_script.html:40,231` is also a stale copy (missing `title`/`data-i18n-title` attrs, same wrong `d.google_email` key). | Info | Add load-mapping + submit-body assertions; regenerate the fixture from the PROGMEM asset. |

---

## 12. Source map

| Concern | File |
|---|---|
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_google_script_asset.{h,cpp}` |
| GAS server script template (PROGMEM) | `chatgpt/src/webpages/assets/google_apps_script_asset.{h,cpp}` |
| Renderer (shell + asset stream) | `chatgpt/src/webpages/renderers/google_script_page_renderer.{h,cpp}` |
| Shared/Ethernet page stream | `chatgpt/src/web_shared_pages.{h,cpp:132-133}` |
| Routes (WiFi page + registry) | `chatgpt/src/webpages/routes/web_routes_network.cpp:227-248`, `route_registry_api.cpp:230-231` |
| Routes (Ethernet page + email mirror + JS download) | `chatgpt/src/ethernet_bootstrap_routes.h:314-320,456-457,471-486` |
| Download route (WiFi) | `chatgpt/src/webpages/handlers/email_handlers.{h,cpp}` |
| Config GET/POST handlers | `chatgpt/src/webpages/handlers_unified/config_handler.cpp:143-204,208-486` |
| Email-test handler | `chatgpt/src/webpages/handlers_unified/email_handler.{h,cpp}` |
| Config storage | `chatgpt/src/ConfigManager.{h:730-752,1909-1930,cpp:253-276,823-847}` |
| Runtime transport | `chatgpt/src/google_email.{h,cpp}`, dispatch `chatgpt/src/runtime_email.cpp:503-526`, gating `chatgpt/src/runtime_environment.cpp:31-44` |
| Flags | `chatgpt/src/config_defaults.h:46-50,279-280`, `chatgpt/user_config.h:246,259`, relay `chatgpt/src/feature_flags.h:1-8` |
| i18n | `chatgpt/src/languages/lang_en_ui.h` (keys `:7-8,30,63,486-503,614,873-876,957,1975-1980`) |
| Tests | `tests/web/test_google_script.js`, fixture `tests/web/pages/google_script.html`; native stub `tests/native/config_manager_stub.cpp:253` |
