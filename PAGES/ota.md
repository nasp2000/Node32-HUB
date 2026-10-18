# OTA Firmware Update — `/ota`

> One-stop reference for the **OTA firmware-update page**: purpose, layout,
> client-side pre-flight validation, enable-window API, signed-upload engine,
> persistence (none) and the Arduino-`Update` + RSA runtime. Written for both
> humans and AI agents.

---

## 1. Objective

Let an authenticated browser upload a signed `.bin` firmware image and reboot
the device into it, plus expose **Restart** and **Clear Settings (factory
reset)** actions. The page performs client-side pre-flight checks (extension,
magic byte, ZIP/ELF rejection, chip ID, size bounds) and then drives a
two-step flow: `POST /api/enable-ota` (opens a short RAM-only upload window)
→ `POST /api/firmware-upload` (streams the image). All firmware writes go
through Arduino `Update` with RSA-2048/SHA-256 signature + chip-magic
verification. Nothing on this page is persisted to LittleFS/NVS.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /ota` |
| Feature flag | `ENABLE_OTA` (`config_defaults.h:18-19`, default `1`; override commented out in `user_config.h:253`; forced `1` by all packs, e.g. `pack_waveshare.h:41-42`) — gates page, APIs and Ethernet mirrors; `feature_flags.h:5-6` is a pure relay, never edited |
| WiFi server | `web_routes_ota.cpp:14-33` — `ensureBasicAuth` + `allowHtmlClient`, streams via `streamOtaPageToPrint(..., WifiAsync)` (`:28`) |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:321-327` → `streamSharedOtaPage(chunked, true)` (`web_shared_pages.cpp:256-260`) |
| API (POST) | `POST /api/enable-ota` via `registerApiRoute` (`web_routes_ota.cpp:11`; registry `route_registry_api.cpp:287`; Ethernet reaches it through the generic `findApiRouteHandler` dispatch) |
| Upload (POST) | `POST /api/firmware-upload` — WiFi `server.on` with upload handler (`ota_admin_handler.cpp:18-189`); Ethernet `ethernet_bootstrap_routes.h:774-780` → `ethernetHandleFirmwareUpload` |
| Extra APIs used | `POST /api/restart` (`web_routes_core.cpp:58-67`), `POST /api/factory-reset` (`web_routes_core.cpp:86-95`) |
| Registry entries | `GET /ota` (`route_registry_api.cpp:245`), `POST /api/enable-ota` (`:287`), `POST /api/firmware-upload` (`:290`), `POST /api/restart` (`:355`), `POST /api/factory-reset` (`:289`) |
| Home entry | System-section OTA button (`home_page_renderer.h:384-386`, label `navOtaBtn`) |
| Auth | Basic auth + HTML-client session gate on the page (WiFi); upload handlers re-check auth per chunk (WiFi) |
| Caching | `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` (`web_routes_ota.cpp:26-27`) |
| Self-test/manifest | `PAGE("/ota", ENABLE_OTA)` in `test_manifest_handler.cpp:288` and `selftest_handler.cpp:157` |

Renderer order: `PAGE_OTA_HEAD_PREFIX` → `FIRMWARE_VERSION` → `PAGE_OTA_HEAD_MIDDLE`
→ `FIRMWARE_BUILD_DATETIME` → `PAGE_OTA_HEAD_SUFFIX` → `PAGE_OTA_SCRIPT_WIFI` **or**
`PAGE_OTA_SCRIPT_ETHERNET` → footer `/auto.js?v=<VERSION>&b=<BUILD>`. No
`config-manager.js`; styling is inline plus `/core.css`.

---

## 3. Layout

```
main.page > div.wrap
 ├ div.actions.nav-inicio > a.btn[href='/'] "🏠 Home"   ← always first child of .wrap
 ├ H1 "🔄 Update System"                                (otaPageTitle)
 ├ version card
 │   ├ H2 "ℹ️ Current Version"                          (otaCurrentTitle)
 │   └ "Version: <FIRMWARE_VERSION> (Build: <FIRMWARE_BUILD_DATETIME>)"
 ├ firmware card
 │   ├ H2 "🚀 Firmware Update"                          (otaFwTitle)
 │   ├ div "Select the .bin file..."                    (otaFwDesc)
 │   ├ div > strong "✅ Automatic Checks"               (otaAutoChecks, heading only — see OTA-1)
 │   ├ label "Firmware File (.bin)"                     (otaFileLabel)
 │   │   ├ button "Choose File" → clicks hidden input   (otaChooseFileBtn)
 │   │   ├ span#fw_name "No file selected"              (otaNoFile)
 │   │   └ input#fw[type=file][accept='.bin'][hidden]
 │   └ button#upBtn[onclick='up()'] > span#upText "🚀 Update" + div#upBar (progress fill)
 └ actions card
     ├ H2 "⚙️ System Actions"                           (otaActionsTitle)
     ├ div (otaActionsDesc, data-i18n-html: Restart / Clear Settings explanations)
     ├ button[onclick='rst()'] "🔄 Restart"             (otaRestartBtn)
     └ button[onclick='reset()'] "🧼 Clear Settings"    (otaFactoryBtn, red)
```

---

## 4. Inputs / buttons

| Element | ID | Notes |
|---|---|---|
| Firmware file picker | `input#fw` (`accept='.bin'`, hidden) | opened by the "Choose File" button; `onchange` updates `#fw_name` (`page_ota_asset.cpp:7`) |
| Selected-file label | `span#fw_name` | shows file name, or literal `"No file selected"` on cancel — bypasses i18n (see OTA-6) |
| Update button | `button#upBtn` → `up()` | disabled during validation/upload; `span#upText` shows phase, `div#upBar` is the progress bar |
| Choose-file button | anonymous `button.btn` | `onclick='document.getElementById("fw").click()'` |
| 🏠 Home | link `/` | first child of `.wrap` (project rule) |
| 🔄 Restart | `rst()` | `customConfirm` → `POST /api/restart` → redirect `/` after 10 s |
| 🧼 Clear Settings | `reset()` | double `customConfirm` → `POST /api/factory-reset` → redirect `/` after 12 s |

No text/number/select inputs and no Save semantics — this page uploads a file,
so Inputs and Buttons are documented together.

---

## 5. Client-side logic (inline JS in `page_ota_asset.cpp:9` WiFi / `:11` Ethernet)

- **`up()`** — guards: no file → `otaChooseFile`; name not ending `.bin` →
  `otaBinOnly`. Then `FileReader.readAsArrayBuffer(f.slice(0,64))` and header
  checks on the first bytes: `<32` → `otaFileTooSmallHint`; `50 4B` →
  `otaZipDetected`; `7F 45 4C 46` → `otaElfDetected`; byte 0 `!== 0xE9` →
  `otaMagicInvalid` + `otaMagicInvalidSuffix`; byte 12 (chip ID) not in
  `{9, 2, 18}` → `customConfirm(otaChipWarning + id + otaChipWarningSuffix)`,
  abort on decline.
- **`_continueUpload()`** — size bounds: `<131072` (128 KB) → `otaFileTooSmall`;
  `>8388608` (8 MB) → `otaFileTooBig`; else `otaSizeOk` + `20%` bar.
- **`_continueOTA()`** — `POST /api/enable-ota` (expects `d.ttl_ms`); then
  uploads: WiFi builds `FormData(file, filesize)` → `POST /api/firmware-upload`
  via `XMLHttpRequest` with `upload.onprogress` driving `#upText`/`#upBar`;
  Ethernet instead sends the raw file with `Content-Type: application/octet-stream`
  plus `X-Filename` / `X-Filesize` headers (`:11`). Success (`d2.success`) →
  `otaRebooting`, `100%` bar, then polls `HEAD /` every 1 s (max 30 attempts)
  and redirects to `/`; failure → `otaErrFail` + `customAlert(otaErrOta + msg)`.
- **`rst()` / `reset()`** — as listed in §4; `reset()` requires two confirms
  (`otaConfirmFactory`, `otaConfirmFactory2`).
- Uses `customAlert`/`customConfirm` and `_t(key, fallback)` throughout; mixes
  `function(){}` with arrow callbacks (`e=>`, `()=>` — see OTA-2).

---

## 6. Server API

### `POST /api/enable-ota` (`handlers_unified/ota_handler.cpp:23-39`)

Optional `ttl_ms` in query string or body (default `300000`, i.e. 5 min; any
`v > 0` accepted, no upper clamp — see OTA-5). Extends `otaEnabledUntil`
(`otaEnabledUntil < millis() ? millis() + ttl : otaEnabledUntil + ttl`).

```json
// 200
{"status":"ok","enabled_until":123456789,"ttl_ms":300000}
```

Note the key is `status`, not `success` — the client only checks `d.ttl_ms`.
With `ENABLE_OTA==0` the stub returns 503
`{"success":false,"message":"OTA nao compilado (ENABLE_OTA=0)"}` (`:42-47`,
sic — unaccented "nao").

### `POST /api/firmware-upload`

- WiFi (`ota_admin_handler.cpp:24-189`): multipart upload handler. Rejects with
  403 if the enable window expired (`:27-30`); rejects non-`.bin` names,
  `totalSize > ESP.getFlashChipSize()`, `totalSize < 260`; optional 32-char
  `expected_md5` param → `Update.setMD5`. Signed layout assumed:
  `fwSize = totalSize - 260`, streams through `otaVerifyFeed`, writes only the
  first `fwSize` bytes, verifies with `otaVerifyEnd`, then `Update.end(true)` →
  200 `{"success":true,"message":"Atualização concluída. Reiniciando..."}` and
  `safeReboot("OTA Success")` (`:156-164`); errors → 400 with `{"success":false,
  "message":"<detail>"}`.
- Ethernet (`ethernet_bootstrap_handlers.h:1056-1201`): same policy, raw body
  read in a lazily allocated 4 KB PSRAM/internal buffer (`:1107-1117`), 30 s
  receive deadline (`:1122`), identical verify/finish/reboot sequence.

### `POST /api/restart`, `POST /api/factory-reset`

Shared core routes (`web_routes_core.cpp:58-67`, `:86-95`) with
`ensureBasicAuth`; restart runs `performRestartActions()` inline, factory reset
runs `performFactoryResetActions()` on disconnect.

---

## 7. Persistence

Stateless / session-only — **no `ConfigManager` section, no `/config/*.json`
stem, no NVS keys.** The only state is the RAM global `otaEnabledUntil`
(`diagnostics_impl.h:4`, helper `otaEnableFor` at `:7-10`), initialised `0`
(refuse uploads) and lost on every reboot. The `enable-ota` window is therefore
a per-boot, millis-based capability, not a setting.

---

## 8. Runtime engine

- **Flash writer:** Arduino `Update` (`Update.h`): `Update.begin(fwSize)` (or
  argument-less legacy path when no `filesize` is supplied,
  `ota_admin_handler.cpp:93-104`), `Update.write`, `Update.end(true)`, with
  `Update.abort()` on any error/timeout.
- **Signature scheme** (`ota_security.h`, `ota_security.cpp:21-93`): signed
  image = `[firmware][4-byte chip magic][256-byte RSA-2048 signature]`
  (260 bytes overhead). `otaVerifyInit(totalSize)` sets hash/buffer windows;
  `otaVerifyFeed` hashes everything except the trailing 256 signature bytes and
  buffers the last 260; `otaVerifyEnd` checks `OTA_CHIP_MAGIC`
  (`S3__` vs `P4__`, selected by `TARGET_BOARD_P4` at `ota_security.h:10-14`)
  then RSA PKCS#1 v1.5/SHA-256 against `OTA_PUBKEY_DER` (`ota_pubkey.h:4-7`).
  Ethernet feeds only when `totalSize > 0` (`:1145-1147`); WiFi always feeds.
- **Rollback:** `otaMarkBootValid()` → `esp_ota_mark_app_valid_cancel_rollback()`
  (`ota_security.cpp:95-97`), called at end of setup (`setup_impl.h:325`); a
  separate stale-comment block in `diagnostics_impl.h:306-322` describes an
  ArduinoOTA on-demand strategy that is **not** what this page implements (the
  page uses `Update`, not ArduinoOTA).
- **Post-success:** `directLogPrintln` byte count, optional SD flush
  (`flushSdcardNowBlocking("OTA_REBOOT", 5000)`), `safeReboot("OTA Success")`.

---

## 9. i18n keys

**Firmware-side i18n (also added):** the API messages above are no longer hardcoded Portuguese in the handler — they now come from the firmware pack (`LangStrings`, `L(activeLang->…)`) and follow `system_language`. Page `<title>` and tab labels follow the same rule.

All confirmed present in `lang_en_ui.h` (PT mirrors in `lang_pt_ui.h`):

- Static (`data-i18n` / `-title` / `-html` in `page_ota_asset.cpp:3-7`):
  `btnHome`, `loading`, `otaTipHome`, `otaPageTitle`, `otaCurrentTitle`,
  `otaVersionLabel`, `otaBuildLabel`, `otaFwTitle`, `otaFwDesc`, `otaAutoChecks`,
  `otaFileLabel`, `otaTipChoose`, `otaChooseFileBtn`, `otaNoFile`, `otaTipUpdate`,
  `otaUpdateBtn`, `otaActionsTitle`, `otaActionsDesc`, `otaRestartBtn`,
  `otaFactoryBtn`.
- Dynamic (`_t()` in `page_ota_asset.cpp:9`, `:11`): `otaChooseFile`,
  `otaBinOnly`, `otaValidating`, `otaUpdateBtn`, `otaFileTooSmallHint`,
  `otaZipDetected`, `otaElfDetected`, `otaMagicInvalid`, `otaMagicInvalidSuffix`,
  `otaChipWarning`, `otaChipWarningSuffix`, `otaValidated`, `otaFileTooSmall`,
  `otaFileTooSmallSuffix`, `otaFileTooBig`, `otaFileTooBigSuffix`, `otaSizeOk`,
  `otaActivating`, `otaErrOta`, `otaStarting`, `otaRebooting`, `otaTimeout`,
  `otaUnknownFail`, `otaErrFail`, `otaErrException`, `otaErrAnalysis`,
  `otaErrReadFile`, `otaConfirmRestart`, `otaConfirmFactory`, `otaConfirmFactory2`.

---

## 10. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| OTA-1 | **"✅ Automatic Checks" panel is a heading with no content.** The div after the `otaAutoChecks` strong tag is empty (`page_ota_asset.cpp:7`) — users are promised checks that are never listed. | Medium | List the real client checks (magic `0xE9`, ZIP/ELF rejection, chip ID 9/2/18, 128 KB–8 MB size, RSA signature server-side) or drop the heading. |
| OTA-2 | **Arrow functions violate the project "no arrow functions" rule** (ESP32 browser compat). `x.upload.onprogress=e=>`, `x.onload=()=>`, `x.onerror=()=>`, `.catch(e=>`/`.catch(()=>` in `page_ota_asset.cpp:9` and `:11`. | Low | Rewrite as `function(e){...}` callbacks like the rest of the file. |
| OTA-3 | **Dead store, WiFi/Ethernet divergence:** WiFi script sets `window._fileArrayBuffer=fr.result` (`page_ota_asset.cpp:9`) but nothing ever reads it (single grep hit); the Ethernet script (`:11`) omits it. | Low | Remove the assignment (or use it and skip the second `slice` read). |
| OTA-4 | **PT strings and mojibake in user-facing messages.** 403 says "Use the 'Atualizar' button…" (`ota_admin_handler.cpp:28`, `ethernet_bootstrap_handlers.h:1058`); error literals are PT (`ota_admin_handler.cpp:50,63,67,87,98,124,138,151`); Ethernet copies are encoding-corrupted: `"inv�lido"` (`:1071`), `"dispon�vel"` (`:1083`), `"Atualiza��o conclu�da"` (`:1193`). | Medium | Route messages through i18n keys and save the handler file as UTF-8. |
| OTA-5 | **`ttl_ms` unbounded and window accumulates.** Any `v > 0` is accepted and `otaEnabledUntil + enabledFor` extends the window indefinitely (`ota_handler.cpp:23-33`). | Low | Clamp `ttl_ms` (e.g. max 30 min) and set `max(millis()+ttl, …)` instead of accumulating. |
| OTA-6 | **Cancel-file fallback bypasses i18n.** `onchange` resets `#fw_name` to literal `"No file selected"` (`page_ota_asset.cpp:7`) instead of `_t('otaNoFile',…)` already used for the initial label. | Low | Use `_t('otaNoFile','No file selected')` in the `onchange` expression. |

---

## 11. Source map

| Concern | File |
|---|---|
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_ota_asset.cpp` (+ `page_ota_asset.h`) |
| Renderer (version/build injection, WiFi vs Ethernet script) | `chatgpt/src/webpages/renderers/ota_page_renderer.{h,cpp}` |
| Shared stream wrapper | `chatgpt/src/web_shared_pages.cpp:256-260` (`web_shared_pages.h:68`) |
| Routes WiFi (page + enable API) | `chatgpt/src/webpages/routes/web_routes_ota.cpp` |
| Upload handler WiFi | `chatgpt/src/webpages/handlers/ota_admin_handler.{h,cpp}` |
| Enable-window API | `chatgpt/src/webpages/handlers_unified/ota_handler.{h,cpp}` |
| Upload handler Ethernet | `chatgpt/src/ethernet_bootstrap_handlers.h:1056-1201` |
| Routes + page mirror Ethernet | `chatgpt/src/ethernet_bootstrap_routes.h:321-327`, `:774-780` |
| API registry | `chatgpt/src/webpages/routes/route_registry_api.cpp:245,287,289-290,355` |
| Enable-window state | `chatgpt/src/diagnostics_impl.h:3-10` |
| Signature/rollback engine | `chatgpt/src/ota_security.{h,cpp}`, `chatgpt/src/ota_pubkey.h`, `setup_impl.h:325` |
| Restart / factory-reset APIs | `chatgpt/src/webpages/routes/web_routes_core.cpp:58-67,86-95` |
| Flag default | `chatgpt/src/config_defaults.h:18-19`, `chatgpt/user_config.h:253` |
| Tests | `tests/web/test_ota.js` (standalone reimplementation of magic/chip/extension checks); `test_manifest_handler.cpp:288`, `selftest_handler.cpp:157`; flag covered by `tests/native/test_config_defaults.cpp:20,92` |
