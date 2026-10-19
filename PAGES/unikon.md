# Unikon — `/unikon`

> One-stop reference for the **Unikon RAMBOX page**: purpose, layout, every
> button/input, client logic, API endpoints, persistence and the firmware-side
> RS-232 RAMBOX protocol engine. Written for both humans and AI agents.
>
> **TRAINING (2026-10-19):** a secção Training Session (`#uk-sec-training`) tem
> o botão `Send Training` + **3 modos** de alocação (radio `trainingMode`):
> **só a prova** (`rings=-`, sem registos de pombos — teste), **pombal todo**
> (`rings=*`, por omissão) e **só os marcados** (`#trainingPombalSelect`, que só
> aparece nesse modo). Escreve a prova fictícia `UNIKON_TRAINING_CODE` ("090" =
> literal '0' + prova 90) — os dois "F7 Transmitir dados" do programa DOS da
> deister (`UNI.EXE` V1.16: menu 1 = pombos, menu 3 = plano de vôo; ambos
> perguntam "Módulo de gravação activado?" ⇒ escrevem **no módulo**):
> `IC0` + I-frame + S1/S2/S3 em replay **+ `M` + linha do pombo por cada pombo
> alocado**. Uma prova **sem os pombos alocados não abre** ⇒ nada no polling.
> No fim o firmware faz **leitura de volta** (X) e compara
> (`read-back … MATCH/MISMATCH`). O botão só age por pedido explícito, nunca
> automático. **Não há botão de apagar** — a prova apaga-se na RAMBOX.

---

## 1. Objective

**Module exclusivity + RS232 profile (automatic):** only one Rambox module can be
active. Enabling Unikon disables Bricon **and applies the RAMBOX profile 9600 8N1**
(`unikonApplySerialProfile()`); enabling Bricon disables Unikon and applies **9600 8N2**
(Bricon uses `StopBits.Two`). While a module is enabled its framing is authoritative —
the RS232 page's saved settings are not restored over it.

Configure and monitor a **Unikon RAMBOX pigeon-racing terminal** over RS-232
(9600 8N1): connection status + club/fancier/race identity, race arrivals
(W-phase P-frames), annual contest calendar (IC0/I session), season pigeon loft
registrations (X-phase ZP-frames), training-session write/clear, and module
config (enable, official ring/chip format, auto-poll interval). All poll/sync
actions are **non-blocking web requests** — they set flags consumed by
`unikonUpdate()` in the main loop.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /unikon` |
| Feature flag | `ENABLE_UNIKON` (`config_defaults.h:234`, default `0`; enabled by `pack_rambox.h:85`) — page + all APIs gated (routes no-op when `0`) |
| WiFi server | `web_routes_unikon.cpp:30` — chunked 3-segment stream (`buildPageFrame` + `PAGE_UNIKON`), title "🦆 Unikon RAMBOX" |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:409` → `streamSharedUnikonPage` (`web_shared_pages.cpp:239`) |
| API (GET) | `/api/unikon-status`, `/api/unikon-arrivals`, `/api/unikon-registrations`, `/api/unikon-contests`, `/api/unikon-pombal` (`web_routes_unikon.cpp:14-18`) |
| API (POST) | `/api/unikon-config` (manual body-params route), `/api/unikon-sync-pombal`, `/api/unikon-email-pombal`, `/api/unikon-poll`, `/api/unikon-sync-zp`, `/api/unikon-poll-w`, `/api/unikon-sync-calendar`, `/api/unikon-reset`, `/api/unikon-send-training`, `/api/unikon-clear-training` (`:19-27`) |
| Ethernet API gap | Only `POST /api/unikon-config` mirrored (`ethernet_bootstrap_routes.h:724`); all other Unikon APIs have Ethernet handler functions but no route dispatch (see UK-2) |
| Auth | Basic auth (`ensureBasicAuth`) + HTML clients only (`allowHtmlClient`) (`web_routes_unikon.cpp:31-33`) |
| Caching | `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` (`:54-55`) |
| Session exemption | ALWAYS exempt (local processing) — unconditional `window._localPagesNeverExpire=true`, never kicked by time |

Renderer order (WiFi): `buildPageFrame(..., "<script src='/config-manager.js'>")` → `PAGE_UNIKON`.
Ethernet/shared renderer order: shell → `UNIKON_TABS_JS` → `PAGE_UNIKON` → auto.js footer → `/config-manager.js` (`unikon_page_renderer.cpp:101-112`).

---

## 3. Layout

```
main.page > div.wrap
 ├ [🏠 Home]  (div.actions.nav-inicio > a.btn, data-i18n='btnHome'; hardcoded fallback "🏠 Início")
 ├ H1 "🦆 Unikon RAMBOX" (hardcoded, no i18n key)
 ├ .uk-tab-bar  (injected by UNIKON_TABS_JS; 5 pill buttons)
 ├ #uk-sec-status      (TAB Status)
 │   ├ #unikonStatus (identity/connection block)
 │   ├ stat cards: #statTotal #statRace #statNew #statAgo
 │   ├ [🔍 Update Arrivals (W)] [🔄 Reset Count]
 │   ├ #push-state  (push automático: pedido uma vez por RAMBOX no fim do 1.º poll)
 │   └ #poll-toast (hidden)
 ├ #uk-sec-arrivals    (TAB Arrivals: table > tbody#raceArrivalsBody, cols #/Ring/Chip ETS/Time/Date/Race)
 ├ #uk-sec-contests    (TAB Contests: [Sync Calendar] + table > tbody#contestsBody + #calendar-toast)
 ├ #uk-sec-pombal      (TAB Loft: [Sync Pigeon Loft] [#btnEmailPombal] + details#pombalListToggle > tbody#pombalBody + #pombal-toast)
 ├ #uk-sec-training    (TAB Loft, grouped with pombal: [Send Training] + #training-toast — sem lista de pombos e sem botão de apagar)
 ├ #uk-sec-guide       (TAB Configuration, grouped with config: jumper pin 9→5, 9600 8N1)
 └ #uk-sec-config      (TAB Configuration: #rs232Info/#rs232InfoText + form#unikonForm + #status)
     └ form#unikonForm: checkbox #enabled, checkbox #official_format, select #interval, [Save Configuration]
```

Tab groups (`unikon_page_renderer.cpp:60-66`): Status→`[sStatus]`,
Arrivals→`[sArrivals]`, Contests→`[sContests]`, Loft→`[sPombal, sTraining]`,
Configuration→`[sGuide, sConfig]`; default tab index 0.

---

## 4. Inputs

| Input | ID / name | Type / range | Notes |
|---|---|---|---|
| Module active | `enabled` | checkbox | master enable; when off, all POST poll/sync actions return `{"success":false,"message":"Unikon module is disabled."}` |
| Official format | `official_format` | checkbox, default checked | `true` = ring `23003379958`→`23-3379958`, chip `C9933525001`→`C9933525`; also used for email body |
| Polling interval | `interval` | select (s): 0 Manual, 60, 180 (default selected), 300, 600, 900, 1200, 1800, 2700, 3600, 7200 | 0 = manual only; server silently migrates 1–59 s to 180 (see UK-4) |

---

## 5. Buttons

| Button | Behaviour |
|---|---|
| 🏠 Home | link `/` — first child of `.wrap` |
| 🔍 Update Arrivals (W) | `pollNow()` → `POST /api/unikon-poll` → toast `ukToastPollScheduled`, refresh status+arrivals after 3 s |
| 🔄 Reset Count | `resetState()` → `customConfirm(ukConfirmReset)` → `POST /api/unikon-reset` → `customAlert` + reload |
| 🗓️ Sync Calendar | `syncCalendar()` → `customConfirm(ukConfirmCalendar)` → `POST /api/unikon-sync-calendar` → toast, reload contests+status after 2 s |
| 🔄 Sync Pigeon Loft | `syncPombal()` → `customConfirm(ukConfirmPombal)` → `POST /api/unikon-sync-pombal` → toast, reload pombal+status |
| ✉️ Email Pigeon Loft (`#btnEmailPombal`) | `emailPombal()` → `customConfirm(ukConfirmEmailPombal)` → `POST /api/unikon-email-pombal`; re-entrancy guard `emailPombalPending`, button disabled while sending |
| 📡 Send Training | `sendTraining()` → `customConfirm(ukConfirmTraining + ' — ' + modo)` → `POST /api/unikon-send-training` com **3 modos** (radio `trainingMode`): **`none`** → `rings=-` (só a prova, sem registos de pombos), **`all`** (por omissão) → `rings=*` (pombal todo), **`list`** → a lista de anilhas marcadas em `#trainingPombalSelect` (a lista e os atalhos Todos/Nenhum só aparecem nesse modo). Guard `trainingPending`. Escreve a prova de treino — os dois "F7 Transmitir dados" do programa DOS: `IC0` + `I…` (a prova) e `M` + linha do pombo (os pombos) — e no fim faz **leitura de volta** e compara (`read-back … MATCH/MISMATCH`). **Não há botão de apagar** (a prova apaga-se na RAMBOX). |
| 🗑️ ~~Clear Training~~ | **Removido (2026-10-19)** — a prova apaga-se **na RAMBOX**, é mais seguro. A rota `POST /api/unikon-clear-training` mantém-se apenas como **diagnóstico manual** (sem botão na página). |
| 💾 Save Configuration | `saveUnikonConfig()` → urlencoded `POST /api/unikon-config` (`enabled`, `official_format`, `interval`) → `customAlert` + `showSavedCard()`; sets `configLoaded=false` to re-sync |

---

## 6. Client-side logic (inline JS in `page_unikon_asset.h`)

- **`t(key)`** (`:175`) — single-argument lookup from `[data-i18n]` element text, else returns the key itself. Several call sites pass a second fallback argument that is **silently ignored** (see UK-1).
- **`loadStatus()`** (`:190`) — `GET /api/unikon-status`; renders connected badge, module/club/fancier/release/race lines (all `escapeHtml`), auto-polling line, 4 stat cards; on first load only (`configLoaded` flag) syncs `enabled`, `official_format`, best-match `interval` option (rounds down), and `emailDestDisplay`.
- **`loadArrivals()`** (`:266`) — `GET /api/unikon-arrivals` (`kind` + `race` por registo); `splitArrivals()` separa `kind==='training'` de prova; a tabela mostra provas e, a seguir a um separador `ukTrainingTitle`, os treinos. Ambos os grupos usam `buildArrivalsRows()` (`:239`), que formata `HHMMSS`→`HH:MM:SS`, `DDMM`→`DD/MM`, chip `00000000000` como `ukNoChip` e mostra a coluna Prova (`ukColRace`).
- **`loadContests()`** (`:283`) / **`loadPombal()`** (`:351`) — fill `#contestsBody` (`{code,date,name}`) and `#pombalBody` (`{ring,chip}`) with empty-state fallbacks.
- **`loadRs232Info()`** (`:531`) — `GET /api/config?section=rs232`; shows `baud · 8N1` + active/inactive dot in `#rs232InfoText`, with links to `/rs232` and `/email`.
- **Boot + polling** (`:551-557`) — `loadStatus/loadArrivals/loadContests/loadRs232Info/loadPombal` on load; `setInterval(..., 8000)` refreshes status+arrivals and pombal. No arrow functions; `customConfirm`/`customAlert` used throughout.
- **Push (sem botões)** — o push é **automático**: o firmware pede-o uma vez por RAMBOX no fim do primeiro poll que o identifica. A página só mostra `#push-state` (*off* / *activo + ident* / *recusado*), actualizado pelo `loadStatus()`.
- **Web tests** (`tests/web/test_unikon.js`) cover `escapeHtml`, `buildArrivalsRows` (incl. coluna Prova), `splitArrivals`, the `sendTraining` payload (**sempre `rings=*`** — o pombal todo) e a ausência dos controlos removidos (`clearTraining` e `#trainingPombalSelect` já não existem), contra `tests/web/pages/unikon_asset.html`.

---

## 7. Server API

Handlers in `handlers_unified/unikon_handler.cpp` (WiFi, via `registerApiRoute`); Ethernet mirrors (partial) in `ethernet_bootstrap_handlers.h:2530-2733`.

### `GET /api/unikon-status` (`:18`)

```json
{"connected":true,"lastPollAgoSec":12,"totalKnown":42,"raceKnownCount":40,
 "trainingKnownCount":2,"lastNewCount":3,"registeredCount":120,"contestCount":8,
 "moduleId":"...","clubName":"...","soltaName":"...","clubCode":"...","fancierName":"...",
 "raceDate":"15.06","raceName":"CANAME","pushState":0,"pushActive":false,"pushAuto":false,
 "pushCode":0,"pushVersion":"...","enabled":true,"intervalSec":180,
 "polling":true,"passiveListen":false,"officialFormat":true,"emailDest":"..."}
```

`pushState`: 0 = off, 1 = active, 2 = rejected/unsupported. `pushCode`: 0 = ACK, 0x3f1 = NAK, 0x3ef = no answer. `pushVersion`: ident the push was negotiated with (empty until probed). Buffer widened to 1200 B for the extra fields.

`emailDest` comes from `configManager.getEmailDestination()` (`:38`); `raceKnownCount`/`trainingKnownCount` recomputed by scanning arrivals with valid time (`:23-36`).

### `GET /api/unikon-arrivals` (`:92`) / `GET /api/unikon-registrations` (`:133`) / `GET /api/unikon-pombal` (`:195`)

`{"count":N,"arrivals":[{"ring":"...","chip":"...","time":"HHMMSS","date":"DDMM","kind":"race|training","race":"..."}]}`. Arrivals/registrations honour `officialFormat`; **pombal always returns raw ring/chip** (`:211-215`). `/api/unikon-registrations` is registered but **never called by the page UI** (page uses `/api/unikon-pombal`).

### `GET /api/unikon-contests` (`:168`)

`{"count":N,"contests":[{"code":"001","date":"0703","name":"..."}]}` (stack array `UNIKON_MAX_CONTESTS=64`).

### POST actions (all `{success,message}`; poll/sync variants refuse with `"Unikon module is disabled."` when disabled)

| Endpoint | Effect |
|---|---|
| `/api/unikon-poll` | `unikonRequestWOnlyPoll()` (W arrivals only) |
| `/api/unikon-poll-w` | same as poll (alias) |
| `/api/unikon-sync-zp` | `unikonRequestPoll()` (full X+W) |
| `/api/unikon-sync-calendar` | `unikonRequestIcOnlyPoll()` (IC calendar) |
| `/api/unikon-sync-pombal` | `unikonRequestXOnlyPoll()` (X registrations) |
| `/api/unikon-email-pombal` | `unikonQueuePombalSeasonEmail(msg)` (no disabled-guard; always attempts queue) |
| `/api/unikon-reset` | `unikonResetArrivalsOnly()` (keeps contests+registrations; no disabled-guard) |
| `/api/unikon-send-training` | `unikonSetTrainingFilter(rings)` + `unikonRequestSendTraining()`. **3 modos**: `rings=-` → `UNIKON_TRAIN_MODE_NONE` (só a prova, sem registos), `rings=*` → `UNIKON_TRAIN_MODE_ALL` (pombal todo), lista de anilhas → `UNIKON_TRAIN_MODE_LIST` (só as marcadas). No fim faz **leitura de volta** (X) e compara (`read-back`). |
| `/api/unikon-clear-training` | `unikonRequestClearTraining()` (remove o concurso de treino `UNIKON_TRAINING_CODE`). **Diagnóstico manual apenas** — a página já não tem botão: a prova apaga-se **na RAMBOX**. |
| `/api/unikon-push` | **Diagnóstico/manual** (sem botão na página — o push é automático). Body `on=1` → `unikonRequestPush(true)` (envia **`P1`**), `on=0` → `unikonRequestPush(false)` (envia **`P0`**); optional `auto=1|0` → `unikonSetPushAuto()` (por omissão já ligado). O probe faz handshake e guarda o resultado (`unikonGetPushCode()`: 0 = ACK, 0x3f1 = NAK, 0x3ef = sem resposta) e o ident (`unikonGetPushVersion()`). |

### `POST /api/unikon-config` (`handlers/unikon_handler.cpp:15`)

Reads urlencoded `enabled`, `interval`, `passive`, `official_format` (only `enabled`/`interval` sent by the page); `interval` cast to `uint16_t` with **no range check** (see UK-4). Verifies `/config/unikon.json` exists after save; success/failure strings are hardcoded Portuguese (see UK-3). Ethernet twin: `ethernet_bootstrap_handlers.h:2649`.

---

## 8. Persistence

**Not** via `ConfigManager` — dedicated file **`/config/unikon.json`** (`unikon_protocol.cpp:66`), written atomically (`.tmp` → `bytes>0` check → remove original → rename, `:336-350`):

```json
{"enabled":true,"interval":180,"passive":false,"official_format":true,
 "contests":[{"code":"001","date":"0703","name":"..."}],
 "registrations":[{"ring":"...","chip":"..."}]}
```

- `loadConfig()` (`:188`) runs at `unikonBegin()`; short intervals 1–59 s migrate to 180 (`:249`); contests/registrations restored into PSRAM arrays (caps 64 / `UNIKON_MAX_ARRIVALS=500`).
- `unikonSetConfig()` (`:2583`) saves immediately; on failure sets `s_configWritePending` for deferred persist.
- Deferred reload after flash mount: `sd_tasks_impl.h:406-409` (`unikonFlushPendingConfig()` + `unikonReloadConfig()`); `unikonPersistCalendarAndPombal()` (`:2569`) auto-saves after successful IC/X polls.

---

### Training / race allocation — the real club flow

At the club the fancier brings **the portable module and the pigeons for that race**.
The club adds **the race with its pigeons** to the module, and the module **accumulates**
races: there is no "delete pigeon" in the captured protocol.

| Mode in the page | When | What it does / read-back |
|---|---|---|
| **Race + its pigeons (club)** — `list` | **the club operation** | `IC0` + `I090` + context (`S1/S2/S3`) + one `M`+`ZP` per ticked pigeon. The module **adds/refreshes** those rings and keeps its others. Read-back = **presence** of every sent ring |
| **Whole loft (fancier upload)** — `all` | at home, upload the full loft (`Concorrente/F7` in the DOS) | every registration. Read-back = **hash equality** (valid only if the module has no other pigeons) |
| **Race only (flight plan)** — `none` | program the race without pigeons (`Plano de vôo/F7`) | no pigeon records, no pigeon read-back |


## 9. Runtime engine (RAMBOX protocol driver)

`unikon_protocol.{h,cpp}` (`#if ENABLE_UNIKON==1`, `:48`–`:2599`).

- **Lifecycle**: `unikonBegin()` (`setup_impl.h:290`) allocates PSRAM storage, loads config, opens UART; `unikonUpdate()` (`loop_impl.h:29`) drives the auto-poll timer and pending-request flags (blocking up to ~10 s only while a poll runs); RS232 loop defers to polls via `unikonIsPolling()`/`unikonIsPostPollBlackout()` (`loop_impl.h:587-621`).
- **Protocol**: 9600 8N1, `STX(02)+ADDR(81)+payload+CK1+CK2+ETX(03)`, 16-bit sum checksum where **CK1 is escaped with `|0x80` when `< 0x20`** (`unikon_checksum.h`); P-frames (39 B, arrivals), ZP-frames (30 B, registrations, no time), I-frames (contests), v/S1/S2/S3/W-3 identity frames. The I-frame content is `'I' '0' RR[2] DD[2] MM[2] NAME[8] N[1]` (the `0` after `I` is a **literal**; the last byte is a digit) — see `sendIFrame()`. Writing a pigeon is a **poul-upload line**: `'M'` + `"ZP"+ring+chip` (31 B) by default, switchable to the PC-file form `'M'` + `"P"+ring+chip` (30 B) via `UNIKON_ZP_WRITE_PREFIX_Z`. The leading `M` is mandatory — `uniUploadPoulData()` in `unikonDrv.dll` prefixes **every** poul line with it (it is not a standalone command; `FUN_10005ee0` skips the command frame when `cmd[0]=='M'` and then builds `"M"+line`), and without it the RAMBOX does not store the record. By contrast the contest upload (`uniUploadLiberationPointSets`) sends `IC0` **once** and then the raw `I…` lines — which is what `sendTrainingSession()` already does.
- **Request flags** (non-blocking, consumed by `unikonUpdate`): `unikonRequestPoll/WOnlyPoll/IcOnlyPoll/XOnlyPoll/SendTraining/ClearTraining/Push` (`:2270-2290`); `unikonPollNow()` (`:1687`) is the blocking immediate variant.
- **Push (automático, por aparelho, aditivo)**: o driver do fornecedor (`uniActivatePush()`/`uniDeactivatePush()`) envia as tramas **`P1`/`P0`**; `uniSetCallBack()`/`deSendMessageOnChange()` só ligam uma mensagem Windows — no fio é apenas esse par de comandos. **Não há botões**: `unikonPollNow()`, no fim de cada poll que identificou o RAMBOX, chama `pushProbe(true)` quando o estado ainda é `UNIKON_PUSH_OFF` — logo o `P1` sai **no primeiro poll, manual ou automático**. `pushCheckDevice()` (em todos os handshakes `v`) mantém o estado **agarrado ao aparelho** (ident = versão + série): enquanto o ident for o mesmo nada mais é enviado (nem `P1` repetido, nem um `NAK` repetido); ao aparecer **outro RAMBOX** o estado volta a `OFF` e o `P1` é pedido de novo. É **aditivo**: o poll automático configurado continua a correr como garantia e as tramas empurradas são lidas por `tryPassiveListen()` (que também corre com o push activo), por isso uma chegada pode aparecer **antes** do intervalo (ex. 5 min). Resultado em `unikonGetPushCode()` (0 = ACK, 0x3f1 = NAK, 0x3ef = sem resposta) e `unikonGetPushVersion()`. Não foi exercitado pelo software do fornecedor (0 `P1`/`P0` em 35 capturas; o `UNIKON_Manager.exe` nunca chama o push) — a resposta ACK/NAK é a única forma de saber se uma unidade o suporta.
- **Training tag**: a W-phase frame `3` is classified as training when its **contest code (bytes [9..11])** equals the fixed `UNIKON_TRAINING_CODE` ("090" = literal `'0'` + race 90). The matcher also accepts the bare race number ("90") and a space-padded field (" 90"), so the tag survives whichever form the device echoes. The code is a **compiled constant** — no persistence needed, so a reboot mid-training still classifies correctly, and sending/clearing training writes nothing to flash. `arrival.race` falls back to the code when the frame carries no name, so the Prova column is never blank for short frames.
- **State accessors**: `unikonGetStatus/Arrivals/Registrations/Contests/Config`, `unikonResetState` (full) vs `unikonResetArrivalsOnly` (keeps calendar+loft), `unikonFormatDisplayRing/Chip` (official format), `unikonQueuePombalSeasonEmail()` (fixed-width ASCII table via email pipeline). Arrival emails include a `Treino` section when `isTraining` arrivals exist (tagged by matching frame-`3` race name against the programmed training contest); totals show `N pombo(s) + M treino`.
- **Cross-links**: status fields feed `/api/status` (`system_status_payload.h:431-444`) and `feature_unikon` (`:544`); `[UNIKON]` log lines are never suppressed (`logger_impl.h:89`); home-page entry gated by `ENABLE_UNIKON==1` (`home_page_renderer.h:173`).

---

## 10. i18n keys

**Firmware-side i18n (also added):** the API messages above are no longer hardcoded Portuguese in the handler — they now come from the firmware pack (`LangStrings`, `L(activeLang->…)`) and follow `system_language`. Page `<title>` and tab labels follow the same rule.

**Added (full PT/EN coverage, `lang_en_ui.h` + `lang_pt_ui.h`):** 8 key(s) — `ukBtnSendTraining`, `ukColRace`, `ukTab1`, `ukTab2`, `ukTab3`, `ukTab4`, `ukTrainingHint`, `ukTrainingTitle`. **+10 (2026-10-19, 3 modos de treino):** `ukTrainingMode/ModeNone/ModeAll/ModeList`, `ukTipTrainingModeNone/All/List`, `ukTrainingAll/None/NoRings`. **Removidas (2026-10-19):** `ukBtnClearTraining`, `ukConfirmClearTraining`, `ukTrainingClearing/Cleared/ClearError`, `ukTrainingSelect/SelectAll`, `unikonTipClearTraining`, `ukTrainingRaceOnly`, `ukTipTrainingRaceOnly`, `ukConfirmTrainingRaceOnly`. HTML fallback text is English; PT is served from the packs.
**Added 2026-10-18 (push):** 5 key(s) — `ukPushTitle`, `ukPushHint`, `ukPushOff`, `ukPushActive`, `ukPushUnsupported` (EN ≠ PT, validated by `scripts/validate_i18n.py`). O push é automático, por isso **não há chaves de botão**.

All present in `lang_en_ui.h` unless marked MISSING: `btnHome`, `tabUnikonStatus/Arrivals/Contests/Pombal/Config` (`:619-623`), `ukStatus/Arrivals/Contests/Config/Loading`, `ukStatTimed/Race/New/LastPoll`, `ukBtnPollArrivals/ResetCount/SyncCalendar/SyncPombal/EmailPombal/SaveConfig`, `ukArrivalsHint/ContestsHint/PombalHint`, `ukColRing/Chip/Time/Date/Code/Name`, `ukNoArrivals/Contests/Pigeons/Chip`, `ukPombalTitle/ListTitle`, `ukConnTitle/Jumper/Speed`, `ukRs232Active/Loading/Configure`, `ukModuleActive/OfficialFormat`, `ukEmailRegistrations/NotConfigured/Configure`, `ukPollingInterval/Manual/1min/3min/5min/10min/15min/20min/30min/45min/60min/120min` (no 25 min key — UI has none), `ukStatusConnected/Disconnected/Module/Club/ClubCode/Fancier/Release/Race/ZP/IC/AutoPolling`, `ukConfirmCalendar/Pombal/EmailPombal/Reset`, `ukAlertError/UnknownError/ModuleDisabled`, `ukToastPollScheduled/CalendarUpdating/PombalUpdating/EmailQueued/EmailError/ConfigSaved/ConfigError/ConfigSavedMemory/FileNotWritten/CountReset/CalendarScheduled/PombalScheduled/ZPScheduled/WPollScheduled/CountResetApi`, `unikonTipHome/Poll/Reset/SyncCal/SyncPombal/EmailPombal/SendTraining/ClearTraining/Enabled/Official/Interval/Save` (`:1829-1840`).
MISSING from both dictionaries: none — the 8 JS-only training keys (`ukTrainingSending/Sent/Error`, `ukTrainingClearing/Cleared/ClearError`, `ukConfirmTraining/ClearTraining`) were absent from both dicts AND unreachable via single-arg `t(key)` (no `data-i18n` anchor) so confirms/toasts showed raw keys; added 2026-10-18 together with `t(key, fb)` consulting `window._lang` (auto.js `/api/lang` dict) first. Tab JS fallbacks are Portuguese (`'Estado','Chegadas','Concursos','Pombal','Configuração'`, `unikon_page_renderer.cpp:61-65`).

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| UK-1 | ~~Training-section i18n keys do not exist~~ **RESOLVIDO 2026-10-17:** as 12 chaves (`ukTrainingTitle/Hint`, `ukBtnSendTraining/ClearTraining`, `ukTrainingSending/Sent/Error`, `ukTrainingClearing/Cleared/ClearError`, `ukConfirmTraining/ClearTraining`) foram adicionadas a `lang_en_ui.h`/`lang_pt_ui.h`. | Done | Add the 12 keys to both language files and extend local `t()` to `t(key, fallback)`. |
| UK-2 | **Route-coverage gaps.** Ethernet dispatches only `GET /unikon` and `POST /api/unikon-config` (`ethernet_bootstrap_routes.h:409,724`) — the Ethernet `ethernetHandleUnikonStatus/Arrivals/Registrations/Contests/Poll/Sync*/EmailPombal/Reset` functions (`ethernet_bootstrap_handlers.h:2530-2733`) are unreachable, so the page's auto-refresh is dead over W5500. Separately, `route_registry_api.cpp:391-398` omits `POST /api/unikon-send-training` and `/api/unikon-clear-training` from the manifest/registry. | Medium | Wire the missing Ethernet GET/POST dispatches; add the two training POSTs to `route_registry_api.cpp`. |
| UK-3 | **Hardcoded Portuguese API strings.** WiFi `POST /api/unikon-config` returns `"Ficheiro não gravado no LittleFS. Tente novamente."` (`handlers/unikon_handler.cpp:45`) and `"✅ Configuração Unikon guardada!"` (`:49`) regardless of UI language (Ethernet twin at least uses English). | Low | Return stable codes/keys and let the page resolve `ukToastConfigSaved/ukToastFileNotWritten`. |
| UK-7 | ~~**Training I-frame malformed.**~~ **RESOLVIDO 2026-10-18:** o código era `"990"`, mas o builder original é `I0%02u%02u%02u%-8s%01u` — o `0` logo a seguir a `I` é **literal**, logo o código tem de ser `'0'` + nº de prova de 2 dígitos (`"090"`), e o último byte do nome é um **dígito** (`N[1]`), não um espaço. Corrigido: `UNIKON_TRAINING_CODE "090"`, `sendIFrame()` escreve `NAME[8]` + `UNIKON_I_TAIL_RACE/CLEAR`, e o matcher aceita `"90"`/`" 90"`. | Done | — |
| UK-8 | **"Sem prova" aparece como "desligado".** `connected` = "last full poll succeeded", logo um RAMBOX presente mas **sem prova programada** (fica em silêncio total — provado em `docs/Unikon Sniffer/16.txt`: três `v`-frames sem resposta) é reportado como `Disconnected`, indistinguível de cabo/aparelho ausente. | Medium | Estado `noRace` distinto de `noDevice` (silêncio no `v`-frame vs porta/eco ausente) e mostrar isso no badge + texto `ukStatusDisconnected`. |
| UK-9 | ~~**Forma de escrita do registo de pombo por verificar.**~~ **Resolvido por REA ao `unikonDrv.dll` (2026-10-18):** o upload do pombal é `uniUploadPoulData` → **cada linha leva o prefixo `M`** (`02 81 4D <linha> CK1 CK2 03`, uma trama por pombo); o `M` **não** é comando autónomo (`FUN_10005ee0`: se `cmd[0]=='M'` salta o envio do comando e constrói `"M"+linha`). As provas vão por `IC0` uma vez + linhas `I…` cruas. | Info | `sendZPFrame()` já emite o `M`; `UNIKON_ZP_WRITE_PREFIX_Z` escolhe `ZP`/`P` na linha. Evidência: `rea-work/ucuk/artifacts/drv/COMMAND_MAP.md`. |
| UK-4 | **No interval validation + silent migration.** `interval` is cast `(uint16_t)request->...->value().toInt()` with no range check (`handlers/unikon_handler.cpp:27`, same in Ethernet `:2664`) — negative input wraps to 65k; values 1–59 s are then silently rewritten to 180 (`unikon_protocol.cpp:2586`). | Low | Clamp/validate (e.g. accept only the 11 UI values, 0 = manual) and report out-of-range instead of migrating silently. |
| UK-5 | **Status JSON built with raw `snprintf %s`.** `moduleId/clubName/soltaName/clubCode/fancierName/raceDate/raceName/emailDest` are interpolated unescaped into an 896 B buffer (`handlers_unified/unikon_handler.cpp:40-85`); a quote/backslash from a RAMBOX frame yields malformed JSON and possible truncation. | Low | JSON-escape fields or build with ArduinoJson; same for the Ethernet twin. |
| UK-6 | **Arrivals hint contradicts behaviour.** Hint says "Only records with valid time (HHMMSS) appear in this table" (`page_unikon_asset.h:39`), but the server returns every record (`handlers_unified/unikon_handler.cpp:92-131`) and the client only excludes `kind==='training'` (`:266-281`), rendering `000000`-time rows as `-`. | Info | Either filter `time==="000000"` client-side to match the hint, or reword the hint. |

---

## 12. Source map

| Concern | File |
|---|---|
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_unikon_asset.h` |
| Tabs CSS/JS + shared renderer | `chatgpt/src/webpages/renderers/unikon_page_renderer.{h,cpp}` (`streamUnikonPageToPrint`) |
| Routes (WiFi) | `chatgpt/src/webpages/routes/web_routes_unikon.{h,cpp}` (`registerUnikonRoutes`) |
| Config POST (WiFi, manual) | `chatgpt/src/webpages/handlers/unikon_handler.cpp` (`registerUnikonApiRoutes`) |
| REST handlers (WiFi) | `chatgpt/src/webpages/handlers_unified/unikon_handler.{h,cpp}` (+ 404 stubs when disabled, `:316-336`) |
| Ethernet page + partial APIs | `chatgpt/src/ethernet_bootstrap_routes.h:409,724`, `chatgpt/src/ethernet_bootstrap_handlers.h:2522-2737`, `chatgpt/src/web_shared_pages.{h,cpp}` |
| Protocol engine + persistence | `chatgpt/src/unikon_protocol.{h,cpp}` (`/config/unikon.json`, `UNIKON_CFG_PATH :66`) |
| Loop/setup hooks | `chatgpt/src/loop_impl.h:29,587-621`, `chatgpt/src/setup_impl.h:290`, `chatgpt/src/sd_tasks_impl.h:406-409`, `chatgpt/chatgpt.ino:173` |
| Config flag | `chatgpt/src/config_defaults.h:234` (default `0`), `chatgpt/user_config.h:206` (commented), `chatgpt/packs/pack_rambox.h:85` (sets `1`) |
| Manifests/self-test | `chatgpt/src/webpages/handlers_unified/test_manifest_handler.cpp:277`, `selftest_handler.cpp:76,146`, `chatgpt/src/webpages/routes/route_registry_api.cpp:201-205,260,391-398` |
| Web tests | `tests/web/test_unikon.js`, `tests/web/pages/unikon_asset.html` |
