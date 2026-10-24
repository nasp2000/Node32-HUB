# Email — `/email`

> Reference for the **E-mail (SMTP) configuration page**: direct-SMTP settings, test-send
> button and the persistence/security model. Written for both humans and AI agents.

---

## 1. Objective

Configure the **direct SMTP client** used by all notification/report e-mails (recipients,
daily reports, alerts), plus a one-click **test send** that runs in a background task.
Google-Apps-Script transport has its own page; this one covers only Direct SMTP (as its
info banner states).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /email` |
| Feature flag | `ENABLE_SMTP==1 \|\| ENABLE_GOOGLE_EMAIL==1` |
| WiFi server | `web_routes_config.cpp:75` → `streamEmailPageToPrint()` (`email_page_renderer.cpp:6`) |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:307` → `streamSharedEmailPage` (`web_shared_pages.cpp:128`) |
| Auth / caching | `ensureBasicAuth` + `allowHtmlClient`; `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` |
| Renderer shell | `streamPageShellStart` + fragment + `/config-manager.js` + auto.js footer |

---

## 3. Layout

```
main.page > div.wrap
├ nav.actions.nav-inicio: 🏠 Home link (/)
├ H1 "✉️ E-mail"                       [emailPageTitle]
├ ℹ️ Information card (blue border)
│  └ "This page configures only Direct SMTP and allows sending a test." [emailSmtpInfo]
└ form#send-config-form  action=/api/config  hidden input _html=1
   ├ 📡 Card "Configuração SMTP Direto" [emailSmtpTitle] (green border)
   │  ├ Servidor SMTP [emailSmtpServerLbl] + hint → input#smtp_server placeholder smtp.gmail.com
   │  ├ Porta [emailSmtpPortLbl]      + hint → input#smtp_port type=number placeholder 465
   │  ├ Utilizador [emailSmtpUserLbl] + hint → input#smtp_user
   │  ├ Email Destino [emailSmtpDestLbl] title="Sender email address" → input#email_destination
   │  ├ Senha/Password [emailSmtpPassLbl] + hint → input#smtp_pass type=password
   │  ├ ☑ TLS Inseguro permitido [emailTlsInsecureLbl] → input#smtp_tls_insecure checkbox **checked by default**
   │  └ 💾 Save (submit, form attr)
   └ 🚀 Card "Testar Envio" [emailTestTitle] (purple border)
      └ button #btn-test-email onclick=testEmail() "Enviar Email de Teste" [emailTestBtn]
```

---

## 4. Inputs

| Input | ID / name | Type | Notes |
|---|---|---|---|
| Server | `smtp_server` | text | pre-filled from `d.smtp.server`; factory default `smtp.gmail.com` |
| Port | `smtp_port` | number | factory default `465`; no min/max client-side (see findings) |
| Username | `smtp_user` | text | from `d.smtp.username`; **empty by default** (account is site-specific) |
| Destination | `email_destination` | text | from `d.email.destination` |
| Password | `smtp_pass` | password | never pre-filled; server masks as `********`, POST of the mask is skipped server-side |
| TLS insecure | `smtp_tls_insecure` | checkbox | checked = allow self-signed certs; JS explicitly sends `'1'/'0'` because unchecked checkboxes don't serialize |

---

## 5. Buttons

| Button | ID / handler | Behaviour |
|---|---|---|
| 🏠 Home | link `/` | first child of `.wrap` |
| 💾 Save | submit of `#send-config-form` | JS handler below |
| Enviar Email de Teste | `#btn-test-email` → `testEmail()` | `POST /api/email-test`, alerts `⚡ Teste iniciado...` style message |

---

## 6. Client-side logic

Inline `<script>` (auto.js supplies `customAlert`, `showLoading`, etc.; `_t()` from shell):

- `et(k, fb)` — i18n helper with fallback.
- `loadEmailConfig()` — `GET /api/config?section=email`; fills fields; TLS checkbox =
  `smtp.tls_insecure !== false` (**defaults to true** when key absent).
- `testEmail()` — `POST /api/email-test` (no body); success → `customAlert(d.message)`;
  failure → `d.message` or generic error.
- Form submit: `FormData` → sets `section=email`, forces explicit
  `smtp_tls_insecure = '1'/'0'`, converts to `URLSearchParams`, `POST /api/config`;
  on `d.success` → saved alert + `showSavedCard()`, else error alert.

---

## 7. Server API

| Endpoint | Method | Handler | Purpose / shape |
|---|---|---|---|
| `/api/config?section=email` | GET | `config_handler.cpp:143` | `{smtp:{server,port,username,password:"********",tls_insecure}, email:{destination,…}}` |
| `/api/config` | POST | `config_handler.cpp:206` | accepts `smtp_*` + `email_destination`; skips masked password; saves stem `smtp_email` |
| `/api/email-test` | POST | `email_handler.cpp:20` (`handleEmailTestPost`) | state machine: 409 while `receiving_data`; **429 throttle 10 s** with remaining seconds; 400 `config_incomplete` (+ reason via `emailTransportUnavailableReason()`); else starts async test → 200; compile-disabled build → 503 |
| `/api/email-reset` | POST | `email_handler.cpp` (`handleEmailResetPost`, SMTP builds only) | manual release of a stuck EMAILDAY send → 200 (reports whether anything was stuck) |

All `/api/email-test` JSON `message` strings are **hardcoded Portuguese** (see findings).

### Test flow (server side)

`handleEmailTestPost` → `startEmailTestAsync(...)` (`runtime_email.cpp:798`) spawns a
background task that builds subject/body from `L(emailTestSubject)`/`L(emailTestBody)`
(info table) and sends via the configured transport; result appears only in serial log /
status page — the web UI never learns the outcome.

---

## 8. Persistence

| Data | Where |
|---|---|
| Full `smtp_email` section JSON (incl. destination, tls_insecure) | LittleFS `/config/smtp_email.json` (stem `SMTP_EMAIL`) |
| SMTP password real value | **NVS** namespace `esp32_config`, key `cfg_smtp_pwd` (`NVS_KEY_SMTP_PWD`, `ConfigManager.h:134`) — disk copy stores the literal mask `"********"` |
| Load path | NVS password merged over file section at boot (`ConfigManager.cpp:1444,1463`) |

---

## 9. Runtime engine

- Transport selection: Direct SMTP when `ENABLE_SMTP=1`; availability check
  `isEmailConfiguredAndWifiConnected()` gates queued mail.
- `smtp_tls_insecure=true` permits skipping certificate verification (needed for some
  self-hosted servers; default ON).
- Outgoing mail is produced by recipients/reports/alerts modules and drained by the
  background sender; this page only seeds credentials + destination defaults.
- **EMAILDAY stuck-send fallback**: `emailDayStartMs` marks send start;
  `emailDayWatchdogTick()` (main loop) and auto-recovery in `startEmailDayAsync()`
  release sends wedged past 10 min via `emailDayForceCleanup()` (clears flag/handle +
  idempotent freeze/priority/snapshot cleanup, never kills a live task);
  `POST /api/email-reset` does the same on demand.
- **Failure diagnosis (`err=`)**: every SMTP transport failure records a short
  single-line reason (`smtpErrorSet()` in `smtp_core_impl.h`: stage + server reply,
  e.g. `auth 535 5.7.8`, `connect`, `rcpt 550 5.1.1`, `stream sd-mutex`) via
  `smtpLastErrorText()`; the `EMAIL_PIPELINE_FAIL` call sites persist it as `err=`
  (`email_event_fields.h`, byte-identical legacy format when empty). GAS failures
  pass no `err` (no transport reply exists). Diagnosing a dead card after a reboot
  no longer depends on the volatile serial monitor.
- **Recipient normalization**: `emailNormalizeRecipientList()`
  (`chatgpt/src/email_recipients.h`, unit-tested) trims, drops empties/junk, strips
  `<>` and dedupes case-insensitively (first spelling wins, rejoin with `,`). Applied
  once at the transport entry (`smtpSendEmail*`, so RCPT TO + `To:` match) and at the
  `emailday-batch` envelope build — the extras list may repeat the primary
  destination, which previously produced `to=a@x.com, a@x.com,b@y.com` in the
  persisted event plus a duplicate RCPT TO.

---

## 10. i18n keys

**Firmware-side i18n (also added):** the API messages above are no longer hardcoded Portuguese in the handler — they now come from the firmware pack (`LangStrings`, `L(activeLang->…)`) and follow `system_language`. Page `<title>` and tab labels follow the same rule.

`emailPageTitle`, `emailSmtpInfo`, `emailSmtpTitle`, `emailSmtpDesc`,
`emailSmtpServerLbl`, `emailTipServer`, `emailSmtpPortLbl`, `emailTipPort`,
`emailSmtpUserLbl`, `emailTipUser`, `emailDestinationLbl`, `emailTipDestination`,
`emailSmtpPassLbl`, `emailTipPass`, `emailTlsInsecureLbl`, `emailTipTls`,
`btnSave`, `btnHome`, `emailTestBtn`, `emailTipTest`, `emailTestStarted`,
`emailTestError`, `alertSaved`, `alertSaveError`.
All `data-i18n` keys verified present in both `lang_en_ui.h` and `lang_pt_ui.h`.

---

## 11. Review findings & improvement ideas

| # | Finding | Pri | Suggestion |
|---|---|---|---|
| 1 | Entire HTML fallback body is **Portuguese** (entities like `In&iacute;cio`, `Servidor SMTP`, `Guardar`, …) — violates the EN-fallback rule for raw PROGMEM text. | Medium | Replace fallbacks with English equivalents (runtime `_t()` already localizes). |
| 2 | `/api/email-test` responses hardcoded PT server-side (`⚠️ Teste bloqueado…`, `⏱️ Aguarde N segundos…`, `❌ Configuração incompleta`, `⚡ Teste iniciado…`, 503 `Transporte desativado em compilação`). | Medium | Route through `L()` translation keys like other handlers. |
| 3 | Inline JS fallbacks PT too: `et('emailTestStarted','Teste iniciado com sucesso!')`, `'✅ Configuração guardada!'`, `'❌ Erro: '`. | Low | English fallbacks. |
| 4 | `smtp_tls_insecure` defaults to **checked** (insecure allowed) when unset. | Info | Document; consider default-off with explicit opt-in hint. |
| 5 | Port field: no min/max client-side; server accepts any int (`v.toInt()`) — `0`/`99999` stored silently and mail just fails later. | Low | Clamp 1–65535 both sides. |
| 6 | `email_destination` tooltip says *"Sender email address"* but the field is the default **recipient**/destination — label/key naming mismatch (`emailTipSender`). | Low | Fix wording. |
| 7 | Test result is fire-and-forget: UI reports "started", outcome only visible in logs/status. | Info | Optional: expose last test result in `/api/config?section=email`. |

---

## 12. Source map

| Concern | File |
|---|---|
| HTML/JS asset (PROGMEM fragment) | `chatgpt/src/webpages/assets/page_email_asset.cpp` |
| Renderer | `chatgpt/src/webpages/renderers/email_page_renderer.cpp` |
| WiFi page route | `chatgpt/src/webpages/routes/web_routes_config.cpp:75` |
| Ethernet mirror | `chatgpt/src/ethernet_bootstrap_routes.h:307`, `web_shared_pages.cpp:128` |
| `/api/email-test` handler | `chatgpt/src/webpages/handlers_unified/email_handler.cpp:20` |
| Async test task | `chatgpt/src/runtime_email.cpp:798` (`startEmailTestAsync`) |
| Config GET/POST (section=email) | `config_handler.cpp:143,206` |
| Persistence | `ConfigManager.cpp` stem `SMTP_EMAIL`; NVS `cfg_smtp_pwd` (`ConfigManager.h:134`) |
