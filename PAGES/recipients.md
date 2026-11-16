# Recipients — `/recipients`

> One-stop reference for the **Recipients / email rules page**: purpose, tabs, layout,
> every input/button, client logic, API endpoints, persistence and the firmware-side
> recipient-matching engine. Written for both humans and AI agents.

> Screenshot: `desktop/recipients.png` (1440 × 2210, live **HomeE32** board `.24` — the
> Waveshare pack compiles this page out, `GET /recipients` → 404, so the reference comes from
> the pack that has it enabled). Captured on the **Rules** tab (How-to + Operators card and
> the 10-row Recipients List; the default Sender tab is nearly empty) with the harness
> `--click`, and `--anon` masked the DOM values (`recipients:null` on this board, so the rows
> are empty). Index and proof in `desktop/README.md`).

---

## 1. Objective

Define **who receives emails when the data stream matches a rule**. Each rule (slot) has a
keyword/threshold matcher, a comparison operator, a recipient list, an alert-frequency cap
and an on/off switch. The page also holds the global **"don't copy the sender"** option and
the **Day Command** destination lists (`EMAILDAY` / `LOGDAY` / `SENSORDAY`).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /recipients` |
| Feature flag | `ENABLE_PAGE_RECIPIENTS` (`config_defaults.h:261`, default `1`) |
| WiFi server | `web_routes_config.cpp:120` — streams via `streamRecipientsPageToPrint()` |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:211` → `streamSharedRecipientsPage` (`web_shared_pages.cpp:136`) |
| Page headers | `Cache-Control: no-store`, `Content-Language: pt` (see RECIP-2) |
| Home entry | Home page "📧 Destinatários" button (shown when flag on) |
| Auth | Basic auth (`ensureBasicAuth`) + HTML clients only (`allowHtmlClient`) |

The renderer assembles the page from 7 PROGMEM fragments with server-side value injection:
`PAGE_RECIPIENTS_HEAD` → `checked` attr (dont-copy) → `PAGE_RECIPIENTS_AFTER_DONT_COPY`
(How-To + rules table) → `PAGE_RECIPIENTS_DAY_CMDS_A…D` (day commands, emails injected) →
`/config-manager.js` → `/auto.js` → inline `PAGE_RECIPIENTS_JS` + footer.

---

## 3. Layout

```
<body class='page recipients'> > main > div.wrap
 ├ [🏠 Home]                          ← always first child of .wrap (project rule)
 ├ H1 "📧 Recipients"
 ├ #accTabBar  ← pill tab bar injected by JS (3 tabs), groups bordered sections:
 │   TAB "✉️ Remetente" (sender)       → Sender card only
 │   TAB "📋 Regras" (rules)           → How-To card + Recipients List card
 │   TAB "📨 Comandos" (cmds)          → Day Command Sends card
 └ sections (in DOM order, each `border:2px solid #3b82f6`):
    [0] "✉️ Email Sender"  — 🚫 don't copy sender checkbox
    [1] "📋 How to Configure a Rule"  — keyword formats + operators help
    [2] "📋 Recipients List" — rules table #rec_body + [💾 Save All]
    [3] "📨 Day Command Sends" — EMAILDAY/LOGDAY/SENSORDAY inputs + [💾 Save Sends]
```

- The table uses `table-layout:fixed`, `min-width:700px` → 950 px; horizontal scroll on
  small screens via `.table-wrapper`.
- Tab bar is inserted **between the home button and the H1** by the inline JS.

---

## 4. Inputs

### Global option (Sender tab)

| Input | ID | Type | Effect |
|---|---|---|---|
| Don't send automatic copy to the sender | `email_dont_copy_sender` | checkbox (20×20) | `checked` attribute injected server-side from `getEmailDontCopySender()`; saves instantly via `POST /api/config` on change. A failed save is reported with `customAlert` and the checkbox reverts to the stored state (RECIP-3) |

### Rules table — per-row inputs (row `i`, generated client-side by `rowHTML(i)`)

| Column | ID pattern | Type / constraints | Server field | Default |
|---|---|---|---|---|
| # | (row index) | text, read-only | — | 1..10 |
| Name / Keyword | `r_name_<i>` | text, `maxlength=50` | `name` | empty |
| OP | `r_op_<i>` | select, 13 ops | `op` | `I` |
| Emails | `r_emails_<i>` | text, `maxlength=256` | `emails` | empty |
| Frequency | `r_freq_<i>` | select, 13 options | `freq` | `0` (3m default; see note) |
| Active | `r_active_<i>` | select `1`/`0` (Yes/No) | `active` | `1` |
| Save | `button[data-save='<i>']` | 💾 | — | — |

Operators (`r_op_<i>` options): `I` equal, `D` different, `+` greater, `-` less, `G` greater-or-equal, `L` less-or-equal, `R` range (inside), `DR` out-of-range, `V` variation, `C` count, `P` presence (watchdog), `Cw` count-in-window, `Hy` hysteresis.

Frequency options (seconds → label): `0` (3m default — the server default; the runtime
floors any value below 180 s to 180 s, so 0 and 180 behave the same), `180` (3m),
`300` (5m), `600` (10m), `1200` (20m), `1800` (30m), `2700` (45m), `3600` (1h),
`7200` (2h), `14400` (4h), `21600` (6h), `43200` (12h), `86400` (24h). A slot stored
with `freq:0` renders as 0 and is saved back as 0 (RECIP-5).

### Day Command Sends (Comandos tab)

| Input | ID | Server target | Persistence |
|---|---|---|---|
| EMAILDAY extra emails | `day_emailday_emails` | `POST /api/emailday-extra-emails` | `sdcard` section |
| LOGDAY extra emails | `day_logday_emails` | `POST /api/logday-extra-emails` | `sdcard` section |
| SENSORDAY extra emails | `day_sensorday_emails` | `POST /api/sensorday-extra-emails` | `sdcard` section |

Values are **prefilled server-side** by the renderer (`getEmaildayExtraEmails()` etc.),
not fetched.

---

## 5. Buttons

| Button | ID / handler | Behaviour |
|---|---|---|
| 🏠 Home | link `/` | first child of `.wrap` |
| 💾 per-row | `button[data-save='<i>']` → `saveSlot(i, btn)` | posts one slot → `POST /api/recipient-slot` |
| 🗑️ per-row | `button[data-del='<i>']` → `removeRow(i)` | confirm → rebuild table without that slot (contiguous indices) + `preload()` |
| ➕ Add Rule | `#add_rule` → `addRow()` | appends a new row up to `MAX=50`; alert at the cap |
| 💾 Save All | `#save_all` → `saveAll()` | posts one row at a time (`postSlot(i)`, 120 ms apart); stops at the first failure and reports it once; silent on full success (RECIP-4) |
| 💾 Save Sends | `#save_day_emails` (inline IIFE) | posts the 3 day-command email fields in parallel (`POST /api/{emailday,logday,sensorday}-extra-emails`), waits for all 3 |

---

## 6. Client-side logic (inline `PAGE_RECIPIENTS_JS`)

`rt(k, fb)` i18n helper (window._t → localStorage `ui_lang_cache` → fallback).

1. **`init()`** — creates **10 rows** (`addRow()`), `attachSaveAll()`, `preload()`.
   `MAX=20` exists but is unreachable (no Add-Row button; rows are never removed) — RECIP-1.
2. **`addRow()`** — appends `rowHTML(i)`, binds per-row save + `input`/`change` → `updateNameHint`.
3. **`updateNameHint(tr, i)`** — live syntax checker under the Name field (green = valid,
   orange ⚠ = op needs range / uses numeric op with text, blue = exact format).
4. **`saveSlot(i, btn)`** — builds FormData `{i, name, op, emails, freq, active}` →
   `POST /api/recipient-slot` (urlencoded). `status==='ok'` → `customAlert('✅ …')` +
   `showSavedCard()`, else error / network alert. `disableBtn`+`showLoading` around request.
5. **`attachSaveAll()`** — binds `#save_all` → `saveAll()` (and the sender-copy toggle →
   `saveDontCopy()`). **`saveAll()`** posts rows 1..`currentCount` one at a time via
   `postSlot(i)` (no per-row alerts, 120 ms apart), disables the button for the run and
   **stops at the first failure**: it re-enables the button and raises one
   `customAlert('❌ recipSaveAllFail <n>/<total> — <reason>. recipSaveAllPartial')`
   (network vs server rejection distinguished). A fully successful run is silent (RECIP-4).
6. **`saveDontCopy()`** — `POST /api/config` `{section:recipients, email_dont_copy_sender:1|0}`;
   parses the response (`r.ok` + `success`) and only alerts on failure: it restores the
   checkbox to the previous value and raises `customAlert('❌ ' + recipDontCopySaveFail)`
   (network failures append `recipErrNetwork`). Success stays silent — the checkbox already
   shows the saved state (RECIP-3).
7. **`preload()`** — `GET /api/recipient-slots` → fills rows 1..min(currentCount, slots);
   `active` mapped `false/0 → '0'` else `'1'`; calls `updateNameHint` after fill.
8. **Tab system** — standalone IIFE: finds `.wrap` children with
   `border:2px solid #3b82f6` style, groups them into 3 tabs (sender / rules / cmds),
   builds `.acc-tab-bar` (`.acc-tab-btn`, `.acc-panel.active`), inserts before H1, wraps
   each group in `.acc-panel` divs.

Project rules honoured: `customAlert`/`customConfirm`, no arrow functions, `rt()`/`_t()`
i18n, `.key`/`preventDefault` usage.

---

## 7. Server API

WiFi: `handlers_unified/recipients_handler.cpp` (+ page-level `GET /api/recipients` and
`GET /api/recipient-slots` in `handlers/recipients_handler.cpp`). Ethernet mirrors:
`ethernet_bootstrap_handlers.h:3030` (`ethernetSendRecipientsList`),
`:3083` (`ethernetSendRecipientSlots`), `:3110` (slot get). All slot APIs 1-based `1..50`.

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/recipient-slot?i=` | GET | one slot `{status,index,name,op,emails,freq,active}`; 400 if `i` outside 1..50 (`"indice invalido"` PT); `DF→DR`; invalid op → `I` |
| `/api/recipient-slot` | POST | upsert one slot `{i,name,op,emails,freq,active}` (body or query); validates `i∈1..50`, `name≤50`, `emails` trimmed to 256 by `ConfigManager` (single limit — RECIP-8), op whitelist (`DF↔DR`); `updateRecipientSlotWithFreq` → `resetRecipientThrottleSlot(idx)` → `saveSection("recipients")` |
| `/api/recipient-slots` | GET | up to 50 slots array `{slots:[{name,op,emails,freq,active}]}` (breaks on first missing) |
| `/api/recipients?limit&offset|page` | GET | paged list `{total,offset,limit,items:[{name,op,emails,freq,active}]}` — same slot shape as `/api/recipient-slots` (`recipient_slot_json.h`, RECIP-6) |
| `/api/emailday-extra-emails` | GET/POST | `{emails}` → `setEmaildayExtraEmails` + `saveSection("sdcard")` |
| `/api/logday-extra-emails` | GET/POST | `{emails}` → `setLogdayExtraEmails` + `saveSection("sdcard")` |
| `/api/sensorday-extra-emails` | GET/POST | `{emails}` → `setSensordayExtraEmails` + `saveSection("sdcard")` |
| `/api/config` | POST | global config; `email_dont_copy_sender` handled at `config_handler.cpp:364` under section `recipients` |

Validation style: server is lenient (clamps/truncates, unknown op → `I`); errors are
**PT strings** (`"indice"`, `"nao gravado"`, `"Erro ao guardar destinatario"`).

---

## 8. Persistence

| File / section | Content | Writer |
|---|---|---|
| `/config/recipients.json` (stem `recipients`, 4 KB) | `recipients_slots` array `{name,op,emails,freq,enabled}` + derived `numeric_rules` (legacy compat) | `updateRecipientSlotWithFreq` → `rebuildRecipientsDerivedFromSlots` → `saveSection("recipients")` |
| `sdcard` section (part of `/config/sdcard.json`) | `emailday_extra_emails`, `logday_extra_emails`, `sensorday_extra_emails` | day-command POST handlers |
| `email` section | `email_dont_copy_sender` | `/api/config` POST (`config_handler.cpp:364`) |

`recipDoc` (`SpiRamJsonDocument(4096)`, `ConfigManager.h:49`) holds slots in RAM.
`resizeRecipientSlots()` auto-extends the array (defaults `name=""`, `op="I"`, `enabled=true`).
`rebuildRecipientsDerivedFromSlots()` mirrors simple `I` slots into the legacy
`config["recipients"]` map and numeric ops into `numeric_rules` (op→int mapping
`+=1, -=0, D=2, G=4, L=5, R=6, DF/DR=7, V=8, C=9, P=10, Cw=11, Hy=12`).
`freq` is clamped to `0..86400` on write; `0` = always.

> Cross-page note: saving the **microSD page** (`/sdcard`) currently wipes
> `emailday_extra_emails`/`logday_extra_emails`/`sensorday_extra_emails` back to empty —
> see `PAGES/microsd_card.md` finding SD-1 (they live in the `sdcard` section).

---

## 9. Runtime engine (firmware side)

- **Matching** — `recipient_matching_impl.h` (`findRecipientsInLowerText`, used from
  `manage_email_impl.h`, `email_queues_impl.h`, `diagnostics_impl.h`,
  `data_sd_pipeline_impl.h`): scans incoming RS232/LPT/sensor lines in lowercase; matches
  simple text (`I`), `key, threshold`, `colN, threshold`, `any, threshold`, ranges
  `min..max`, exact formats; numeric rules evaluated on extracted numbers; `P` = presence
  watchdog (alert if keyword absent for N s); `C`/`Cw` counters; `Hy` hysteresis
  (fire ≥ high, re-arm < low); `V` variation (delta vs previous value).
- **Frequency throttling** — per-slot timestamp array guarded by `recipientThrottleMutex`
  (`chatgpt_includes.h:27`); min interval floor `180 s`; `getRecipientSlotFrequency()`
  reads `freq`; `resetRecipientThrottleSlot(idx)` clears on save and is also called by
  `coordinator_hooks.h`.
- **Periodic re-check** — a 5-second loop pass re-evaluates rules and fires pending emails
  respecting `freq` (`recipient_matching_impl.h:522`).
- **Sender-copy suppression** — `manage_email_impl.h:1268,1506`:
  `copySenderEnabled = (!sdCardActive && !configManager.getEmailDontCopySender())` — when
  SD saving is active or dont-copy is on, the sender is not copied on emails.
- **Day commands** — `command_executor.cpp:550` merges `getEmaildayExtraEmails()` into the
  EMAILDAY recipient set (LOGDAY/SENSORDAY analogous).

---

## 10. i18n keys

**Firmware-side i18n (also added):** the API messages above are no longer hardcoded Portuguese in the handler — they now come from the firmware pack (`LangStrings`, `L(activeLang->…)`) and follow `system_language`. Page `<title>` and tab labels follow the same rule.

`recipPageTitle`, `btnHome`, `recipTipHome`, `recipTabSender/Rules/Cmds`, `recipSenderTitle`,
`recipDontCopyLbl`, `recipDontCopyDesc`, `recipHowToTitle`, `recipKeywordTitle`,
`recipKeywordDesc`, `recipOpsTitle`, `recipOpsDesc`, `recipListTitle`, `recipTableNameOp`,
`recipTableOp`, `recipTableEmails`, `recipTableFreq`, `recipTableActive`, `btnSaveAll`,
`recipTipSaveAll`, `recipTipName`, `recipTipRuleOp`, `recipTipEmails`, `recipTipFreq`,
`recipTipActive`, `recipTipSaveRow`, `opEqual/Diff/Gt/Lt/Gte/Lte/Range/OutRange/Variation/
Count/Presence/Window/Hysteresis`, `freqDefault3m`, `freq3m…freq24h`, `opYes/opNo`, `recipSaved`,
`recipError`, `recipNetError`, `recipDontCopySaveFail`, `recipErrNetwork`, `recipSaveAllFail`, `recipSaveAllPartial`, `recipTipDayEmailday/Logday/Sensorday`, `recipTipDaySave`.

Known inconsistency: the **Day Command Sends card uses hardcoded PT text** (`Emails adicionais…`,
`Envie estes comandos pela página de comandos ou Telnet…`) — no `data-i18n` attributes
(RECIP-2); the page also sends `Content-Language: pt`.

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Status |
|---|---|---|---|
| 1 | **UI now supports up to 50 rows.** `MAX` raised to 50 (matches backend), a **➕ Add Rule** button appends rows, and each row has a **🗑️ remove** button that rebuilds the table with contiguous indices. Slots 11–50 are now editable/creatable. | High | **fixed** |
| 2 | **Day Command Sends card text is hardcoded PT (no `data-i18n`)**, and the page sends `Content-Language: pt` — violates the EN-fallback rule. Text converted to English; `Content-Language: pt` removed. | Medium | **fixed** (text + header) |
| 3 | `saveDontCopy()` was fire-and-forget (`.catch(function(){})`), so a failed save of the sender-copy option was silent. **Fixed (RECIP-3):** the response is parsed (`r.ok` + `success:false`), a failure reverts the checkbox to the stored state and shows `customAlert('❌ ' + recipDontCopySaveFail)` (network errors append `recipErrNetwork`); success stays silent. Tests: `test_recipients_dont_copy` (4 cases / 23 assertions) + `test_recipients.js` (success silent, rejected save visible + revert, network failure visible). | Medium | **fixed** |
| 4 | "Save All" was non-atomic: N sequential 120 ms-apart POSTs, each row raising its own alert, and a mid-loop failure left a partially saved table with the error buried. **Fixed (RECIP-4):** one `saveAll()` posts rows via `postSlot()` (no per-row UI), stops at the first failure and reports it once — `recipSaveAllFail <n>/<total> — <reason>. recipSaveAllPartial`, network vs server rejection distinguished; full success is silent and the button is disabled for the run. Tests: `test_recipients_save_all` (3 cases / 23 assertions) + `test_recipients.js` (success silent, mid-loop failure visible + stops at row 3, network failure visible). | Low | **fixed** |
| 5 | `freq` gap: server default is `0` but the UI list started at `180`; a stored `freq:0` rendered as 600 and any save rewrote it to 600. **Fixed (RECIP-5):** the list now includes `0` as the default option, `preload()` applies `freq` even when it is 0 (`!==undefined && !==null`, not truthiness), `removeRow`/`saveSlot` no longer use a 600 fallback, and new rows start on 0 (the server default). Note: the runtime floors `freq < 180` to 180 s, so `0` is labelled **3m (default)** rather than "always". Tests: `test_recipients_freq` (4 cases / 16 assertions) + `test_recipients.js` (stored 0 renders as 0; saving keeps freq=0). | Medium | **fixed** |
| 6 | `/api/recipients` omitted `active` (only `/api/recipient-slots` had it), so a consumer could not tell a disabled rule from an enabled one. **Fixed (RECIP-6):** both routes (WiFi + Ethernet) fill each slot through the shared `recipientSlotFillJson()`/`recipientSlotFillJsonFromStored()` (`recipient_slot_json.h`), which always emits `{name, op, emails, freq, active}` with the same defaults (`op I`, `freq 0`, `enabled true`). Tests: `test_recipient_slot_json` (5 cases / 28 assertions) + `test_recipients.js` (a disabled and an enabled slot render Active No/Yes). | Low | **fixed** |
| 7 | `saveSlot` fallbacks used `freq:'0'` when the select was missing, and `0` was not in the option list. | Low | **fixed by RECIP-5** (294f70e), verified with no code change: `slotForm()`/`removeRow()` fall back to `0`, which is now the first/default option (`3m (default)`) and the server default; no `'600'` fallback remains (RECIP-7). |
| 8 | `handleRecipientSlotPost` truncated emails to 800 and `updateRecipientSlotWithFreq` then to 256 — the 800 cap was dead code. **Fixed (RECIP-8):** the handler cap is gone; the single effective limit is `ConfigManager`'s 256. Tests: `test_recipient_email_cap` (drives the real ConfigManager: a 900-char list is stored as 256; source check that `remove(800)` is not reintroduced). | Info | **fixed** |

---

## 12. Source map

| Concern | File |
|---|---|
| Renderer | `chatgpt/src/webpages/renderers/recipients_page_renderer.{h,cpp}` |
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_recipients_asset.cpp` |
| WiFi page route | `chatgpt/src/webpages/routes/web_routes_config.cpp:120` |
| Slot/day-command handlers | `chatgpt/src/webpages/handlers_unified/recipients_handler.cpp` |
| Paged list + slots (WiFi) | `chatgpt/src/webpages/handlers/recipients_handler.cpp` |
| Ethernet mirrors | `chatgpt/src/ethernet_bootstrap_handlers.h:3030-3130`, `ethernet_bootstrap_routes.h:211` |
| Config storage | `chatgpt/src/ConfigManager.{h,cpp}` (`recipDoc`, `recipients_slots`, `numeric_rules`, `email_dont_copy_sender`) |
| Matching / throttle engine | `chatgpt/src/recipient_matching_impl.h`, `chatgpt/src/coordinator_hooks.h` |
| Sender-copy suppression | `chatgpt/src/manage_email_impl.h:1268,1506` |
| Day commands | `chatgpt/src/command_executor.cpp:550` |
| Self-test manifest | `test_manifest_handler.cpp` (`PAGE("/recipients", ENABLE_PAGE_RECIPIENTS)`), `selftest_handler.cpp` |

## 13. Alterações 2026-11-10 (i18n das abas)

- **Abas traduzidas**: `Remetente | Regras | Comandos` apareciam em PT numa UI EN porque as
  etiquetas eram construídas antes de `/api/lang` chegar. As abas passam a guardar
  `data-i18n-key`/`data-i18n-fb` e a ser re-etiquetadas quando o dicionário carrega
  (`MutationObserver` em `data-i18n-ready` + retry a 2,5 s). Verificado em .24: `✉️ Sender | 📋 Rules | 📨 Commands`.
