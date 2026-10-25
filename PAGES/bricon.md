# Bricon Rambox — `/bricon`

> One-stop reference for the **Bricon module page**: purpose, layout, every
> button/input, client logic, API endpoints, persistence and the firmware-side
> engine. Twin of `/unikon`, slimmer.
>
> **SÓ LEITURA (2026-10-25):** o hub **não escreve** nada no relógio Bricon. As rotas
> `/api/bricon-send-training` e `/api/bricon-clear-training` (stubs de "driver TBD") e o
> botão *Designate* foram removidos; fica poll/sync/leituras e email.
>
> **WIRE DRIVER IMPLEMENTED (2026-10-19, from the REA capture):** framing + CK,
> `ActivateMaster`/BA scan, the `FlightsReadOut` sequence
> (`GetFancier` → `GetPigeon` chain → `GetTimer`), plus `GetFlightNames` and
> `GetBandTable`. Validated on the host against the captured byte layouts and
> against the bench simulator; **not yet exercised against a real clock**.
> The poll runs on the main loop (blocking RS-232 I/O never runs in the web
> task), so the poll endpoints schedule it and answer immediately.

---

## 1. Objective

Configure and monitor a **Bricon loft module** (Little/DUO/Speedy/Bricon+ over
RS232, **9600 8N2** — `Communication::StartComm` builds `StopBits.Two`) with the
same UX as Unikon: connection status, arrivals,
loft registrations, contests, training send/clear, and module config. Only
**one module is active at a time** (rambox XOR bricon — enabling one disables
the other and switches the RS232 profile automatically).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /bricon` → `302 /rambox?dev=bricon` (the unified RAMBOX page owns this module's UI — see `PAGES/rambox.md`) |
| Feature flag | `ENABLE_BRICON` (`config_defaults.h`, default `0`; enabled by `pack_rambox.h`) — page + all APIs gated (404 stubs when `0`) |
| WiFi server | `web_routes_bricon.cpp` — chunked 3-segment stream (`buildPageFrame` + `PAGE_BRICON`), title "🕊 Bricon Rambox" |
| Ethernet (W5500) | NOT mirrored (gap — see BRC-1) |
| API (GET) | `/api/bricon-status`, `/api/bricon-arrivals`, `/api/bricon-registrations`, `/api/bricon-contests`, `/api/bricon-pombal` (live PSRAM data; empty until first ingest) |
| API (POST) | `/api/bricon-config` (urlencoded `enabled`, `interval`, `baud`), `/api/bricon-poll`, `/api/bricon-sync-pombal`, `/api/bricon-sync-calendar`, `/api/bricon-reset` |
| Auth | Basic auth (`ensureBasicAuth`) + HTML clients only (`allowHtmlClient`) |
| Caching | `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` |
| Session exemption | Local processing — `window._localPagesNeverExpire=true` (same as unikon) |

---

## 3. Layout (same look as `/unikon`: dark cards, `.uk-tabs` / `.uk-tab-btn` / `.uk-tab-panel`, sticky `.uk-savebar`)

```
main.page > div.wrap
├ [🏠 Home]
├ H1 "🕊 Bricon Rambox"
├ .uk-tabs#briconTabBar (4 buttons, static HTML)
│   ├ [0] "📊 Status & Config"      → #bk-tab-0
│   ├ [1] "🏁 Arrivals & Contests"  → #bk-tab-1
│   ├ [2] "🕊 Pigeon Loft"          → #bk-tab-2
├ #bk-tab-0.uk-tab-panel.active
│   ├ #bk-sec-status (H3 Status + #briconStatus + 4 .stat-cards #bkStatTotal/#bkStatContests/#bkStatPombal/#bkStatAgo + [Update] [Reset] + #bricon-poll-toast)
│   └ #bk-sec-config (H3 Configuration + #bkRs232Info + form#briconForm: checkbox #briconEnabled, select #briconInterval, #briconBaud, #briconCountry, #briconBa, #briconData, #briconParity, #briconStop)
├ #bk-tab-1.uk-tab-panel
│   ├ #bk-sec-arrivals (H3 Race Arrivals + table: #, Ring, Chip ETS, Time, Date, Race, **Pos**, **Eval** — the RS-232 record fields; tbody#briconArrivalsBody)
│   └ #bk-sec-contests (H3 Calendar + [Sync Calendar → briconSyncCalendar] + table > tbody#briconContestsBody + #bricon-calendar-toast)
├ #bk-tab-2.uk-tab-panel
│   └ #bk-sec-pombal (H3 Loft + [Sync Pigeon Loft → briconSyncPombal] + table > tbody#briconPombalBody + #bricon-pombal-toast)
├ #bk-tab-3.uk-tab-panel
├ .uk-savebar ([💾 Save Configuration → saveBriconConfig], common to all tabs)
└ #bricon-config-status
```

Tab switching: `bkSwitchTab(idx)` toggles `.active` on buttons + panels, persists to `localStorage['bk_tab']`; `bkShowTab(idx)` is a kept alias; `bkBuildTabs()` restores the saved tab. Active tab button uses Bricon green (`#15803d`, not Unikon blue `#1d4ed8`).

---

## 4. Inputs

| Input | ID / name | Type / range | Notes |
|---|---|---|---|
| Module active | `enabled` | checkbox | master enable; **enabling Bricon disables Unikon** (exclusive rule) |
| Polling interval | `interval` | edited on `/rambox` as a **minute** selector: 1, 3, 5, 10, 15, 30, 60 (default **3 min** = 180 s); the firmware stores seconds and defaults a missing key to `180` | explicit `0` (manual) is still accepted by the API but no longer offered by the page |
| Baud | `baud` | select: 1200–115200, default 9600 | stored in `/config/bricon.json`; applied via deferred `_pendingRS232Apply`. 38400 only matters for the firmware-update path |
| Country (layout) | `country` | select PT/ES/FR/BE/NL/UK/HU/RO/PL, default **PT** | decides the record layout: `Utils::IsUKProtocol` / `GetPigeonCommand::HandleResponse`. PT ⇒ FedBand **14 B** (53-byte arrival) and **non-UK** fancier (104 B) |
| Clock address | `ba` | select auto-scan / 0x82 Bricon·Little·DUO / 0x81 Benzing / 0x83 Tauris / 0x84 Tipes / 0x85 Unikon / 0x10 Master / 0x00 club, default **auto** | `Adresses` enum; auto = the recovered scan list `[0x10, 0x80..0x85, 0x00]` |
| Data / Parity / Stop | `data`/`parity`/`stop` | 8 / none / **2** | REA: the link is **8N2** (Unikon is 8N1). The stop bit is now selectable so the bench can try both |
| Club / Flight code | `club`/`flight` | 4 chars each, default **0000** | carried in the `GetPigeon` request (`58\|C8 0A idx clube4 voo4`). `0000` is the wildcard the app tolerates (it substitutes the requested ids when the clock echoes `0000`) |

---

## 5. Buttons

| Button | Behaviour |
|---|---|
| 🏠 Home | link `/` |
| 🔍 GetPigeon | `briconPoll()` → `POST /api/bricon-poll` → toast + status refresh. Schedules a **full poll**: activate → `GetFancier` → `GetPigeon` chain (`58\|C8 0A idx clube4 voo4`, `idx+1` until status `0x05`) → `GetTimer` → `GetFlightNames` → `GetBandTable` |
| 🔄 Reset Count | `briconReset()` → `POST /api/bricon-reset` → status refresh. Local counters only — there is no clock reset here (the ESP32 *is* the PC master, so `ResetMaster 10 67 00` does not apply) |
| 🗓️ GetFlightNames | `briconSyncCalendar()` → `POST /api/bricon-sync-calendar`. Reads the contests with `GetFlightNames` (`FA 0C idx`, also chained) — the captured calendar read |
| 🔄 GetFancier | `briconSyncPombal()` → `POST /api/bricon-sync-pombal`. Reads the loft with the `GetFancier` chain (`51\|C1`) — the captured registration read |
| 💾 Save Configuration (savebar, all tabs) | `saveBriconConfig()` → urlencoded `POST /api/bricon-config` (`enabled`, `interval`, `baud`) → status reload |

---

## 6. Client-side logic (inline JS in `page_bricon_asset.h`, mirrors `page_unikon_asset.h`)

- **`bt(key, fallback)`** — `window._lang` (auto.js `/api/lang` dict) first, then `[data-i18n]` element text, else fallback. JS-only keys (no DOM anchor) now translate via the dict.
- **`besc(s)`** — HTML escape (null-safe).
- **`bkTabs` (4 groups) / `bkSwitchTab(idx)` / `bkShowTab(idx)` alias / `bkBuildTabs()`** — 4 tab panels (`#bk-tab-0..3`), persisted in `localStorage['bk_tab']`; same pattern as Unikon `ukSwitchTab` but with Bricon-green active button.
- **`loadBriconStatus()`** — `GET /api/bricon-status`; renders connected badge/module/pending/RX counters + stat cards (`#bkStatTotal/Contests/Pombal/Ago`); syncs `enabled`/`interval`/`baud` on first load; chains `loadBriconArrivals/Pombal/Contests/loadBkRs232Info`; auto-refresh every 8 s (same as Unikon).
- **`briconRow(a, n)` / `briconCat(c)`** — same row formatting as Unikon (HHMMSS → HH:MM:SS, DDMMYY → DD/MM) plus Bricon `category`/`speed` columns.
- **Actions** — `briconPoll/Reset/SyncCalendar/SyncPombal/saveBriconConfig` as above (no write to the clock) (`briconClearTraining` was removed — no captured equivalent); no arrow functions; no `customAlert` (toasts only).
- **Web tests** (`tests/web/test_bricon.js`) cover `besc`, `bt`, 4-tab bar, panel show/hide, all section + stat-card IDs and function existence against `tests/web/pages/bricon_asset.html`.

---

## 7. Server API

Handlers in `handlers_unified/bricon_handler.cpp` (WiFi, via `registerApiRoute`); 404 stubs when disabled (same pattern as unikon).

### `GET /api/bricon-status`

```json
{"connected":false,"hardwarePending":true,"enabled":false,"intervalSec":180,
 "baud":9600,"dataBits":8,"parity":"none","stopBits":2,"country":"PT","baAddress":0,
 "lastRxAgoSec":0,"bytesRx":0,"framesSeen":0,"activeModule":"none","manualSerial":false}
```

`connected` flips true on the first RX byte; `activeModule` is `rambox`|`bricon`|`none`;
`manualSerial` = bench mode (the RS232 page settings are used and the stored 9600 8N2
profile is left untouched).

### List endpoints

The arrival email (same layout as the Unikon one) prints a **No / Ring / Chip / Time**
table — Date, Race, Pos and Eval are not emitted (8 columns wrapped on a smartphone;
the race is already in the header lines).

Live PSRAM data (empty until first ingest):
`GET /api/bricon-arrivals|registrations|contests|pombal`. Arrivals carry
`kind`, `race`, `posArriveClub`, `posEnterClub`, **`evaluation`** (`"1"` = clocked/valid,
`"0"` = unclassified/poul letter). `sex`/`color`/`category`/`speed` come from the club
database, **not** from the RS-232 record — the page only shows wire fields; contests carry
`club`, `distance`, `pigeonCount`, `emailSent`, `webSent` (fields Bricon
provides natively and Unikon does not). Pombal returns raw ring/chip.

### POST actions

| Endpoint | Effect |
|---|---|
| `/api/bricon-config` | urlencoded `enabled`, `interval`, `baud`, **`data`, `parity`, `stop`, `country`, `ba`** → `briconSetConfig()` + `briconSetEnabled()` (enabling applies the RS232 profile + disables Unikon). Success toast follows `system_language` |
| `/api/bricon-poll` | `briconRequestWOnlyPoll()` → schedules the **arrivals-only** readout (`GetFancier` + `GetPigeon` chain + `GetTimer`) |
| `/api/bricon-sync-pombal` | `briconRequestXOnlyPoll()` → schedules the **pombal** readout (`GetBandTable`) |
| `/api/bricon-sync-calendar` | `briconRequestIcOnlyPoll()` → schedules the **calendar** readout (`GetFlightNames` chain) |
| `/api/bricon-reset` | no-op success (no data yet) |

All poll/sync/training POSTs refuse with the localized `apiBriconDisabled` message when inactive; the pending-driver message is the localized `apiBriconPending` (both follow `system_language` via `L(activeLang->…)`).

---

## 8. Persistence

Dedicated file **`/config/bricon.json`** (`bricon_protocol.cpp`), written atomically (`.tmp` → `bytes>0` check → remove original → rename):

```json
{"enabled":false,"interval":180,"manual_serial":false,"baud":9600,"parity":"none","data":8,"stop":2,"country":"PT","ba":0}
```

---

## 9. Runtime engine (skeleton)

`bricon_protocol.{h,cpp}` (`#if ENABLE_BRICON==1`).

- **Lifecycle**: `briconBegin()` (setup) loads config, enforces exclusion, requests RS232 profile; `briconUpdate()` (loop) drives poll flags + passive RX sniffer (non-blocking, throttled 500 ms hex log).
- **Exclusion + RS232 profile (automatic)**: enabling Bricon disables Unikon **and** applies the
  Bricon profile **9600 8N2**; enabling Unikon disables Bricon **and** applies **9600 8N1**.
  Each module owns its framing, so choosing the module switches the port with no manual setup
  (`briconApplySerialProfile()` / `unikonApplySerialProfile()`, applied on the main loop).
  While a module is enabled the RS232 page's saved settings are **not** restored over it. `briconActiveModuleName()` reports `rambox`|`bricon`|`none`.
- **RS232 profiles**: `briconApplySerialProfile()` writes `rs232_baudrate[_index]/_data_bits/_parity/_stop_bits` (pins untouched) + `_pendingRS232Apply`. **Bricon profile = 9600 8N2** (REA: `StopBits.Two`); Unikon is 8N1. Enabling logs the derived profile: baud/framing, country, FedBand length, arrival record size, fancier flavour and BA mode.
- **Data model**: `BriconArrival`/`BriconContest` carry the fields Bricon natively exposes (sex, colour, category, speed, antenna, club positions, distance, email/web flags) — not just Unikon's ring/chip/time. Ingestion is a single funnel (`briconIngestArrival`) shared by the future poll parser and spontaneous frames.
- **Wire layer (implemented)**: `bkSendRequest`/`bkReadReply`/`bkCommand` (framing `6C…CK` / `33…CK`, 1 ms pacing,
  3 retries, echo + CK validation), `briconActivate`/`briconFindDevice` (configured address, else the recovered
  scan order `[0x10, 0x82, 0x81, 0x83, 0x84, 0x85, 0x80, 0x00]`), `briconReadFancier`,
  `briconReadArrivals` (chained), `briconReadTimer`, `briconReadCalendar`, `briconReadPombal`,
  and `briconParsePigeon` (country-driven FedBand 14/22 → 53/61-byte records).
  `briconPoll(arrivals, calendar, pombal)` is the single entry point used by the loop.

---

## 10. i18n keys

**Added (full PT/EN coverage, `lang_en_ui.h` + `lang_pt_ui.h`):** 34 key(s) — `briconArrivals`, `briconArrivalsHint`, `briconBtnClearTraining`, `briconBtnPoll`, `briconBtnReset`, `briconBtnSave`, `briconBtnSendTraining`, `briconBtnSyncCalendar`, `briconBtnSyncPombal`, `briconConfig`, `briconContests`, `briconFrames`, `briconHardwarePending`, `briconInterval`, `briconModuleActive`, `briconNoArrivals`, `briconNoContests`, `briconNoPigeons`, `briconPombal`, `briconPombalHint`, `briconRxBytes`, `briconStatArrivals`, `briconStatContests`, `briconStatLoft`, `briconStatRx`, `briconStatus`, `briconTab1`, `briconTab2`, `briconTab3`, `briconTab4`, `briconTitle`, `briconTraining`, `briconTrainingHint`, `ukColRace` (+ shared `statusInactive` "Inactive"/"Inativo"). HTML fallback text is English; PT is served from the packs.

`briconTitle/Status/BtnPoll/BtnReset/Arrivals/Pombal/Training/BtnSendTraining/BtnClearTraining/Contests/Config/ModuleActive/BtnSave` in `lang_en_ui.h` + `lang_pt_ui.h` (after `ukColRace`).

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| BRC-1 | **No Ethernet mirror.** Only WiFi routes registered; no `/bricon` page or `/api/bricon-*` dispatch over W5500. | Medium | Mirror like unikon (page + API dispatch) once the driver lands. |
| BRC-2 | ~~Driver is structure-only~~ **RESOLVIDO 2026-10-19:** the wire layer is implemented (framing/CK, activation scan, GetFancier→GetPigeon→GetTimer, GetFlightNames, GetBandTable) and host-tested against the captured layouts. Pending: exercise it against a real clock. | Done (needs HW run) | Run the bench: simulator on the 2nd ESP32 / `fake_clock.py` on a USB-RS232 port. |
| BRC-3 | **No arrivals storage yet.** No PSRAM ring for Bricon arrivals/registrations. | Medium | Mirror unikon storage when the driver lands. |
| BRC-4 | ~~Server toast messages hardcoded (mostly PT)~~ **RESOLVIDO 2026-10-18:** handler now uses `L(activeLang->apiBriconCfgActive/CfgOff/Disabled/Pending/NothingReset)` (`lang_types.h` + `lang_active.cpp` PT/EN) so toasts follow `system_language`, same pattern as other handlers. | Done | — |

---

## 12. Source map

| Concern | File |
|---|---|
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_bricon_asset.h` |
| Routes (WiFi) | `chatgpt/src/webpages/routes/web_routes_bricon.{h,cpp}` |
| REST handlers (WiFi) | `chatgpt/src/webpages/handlers_unified/bricon_handler.{h,cpp}` (+ 404 stubs when disabled) |
| Protocol engine + persistence | `chatgpt/src/bricon_protocol.{h,cpp}` (`/config/bricon.json`) |
| Loop/setup hooks | `chatgpt/src/loop_impl.h`, `chatgpt/src/setup_impl.h`, `chatgpt/chatgpt.ino` |
| Config flag | `chatgpt/src/config_defaults.h` (default `0`), `chatgpt/packs/pack_rambox.h` (sets `1`) |
| Manifest | `chatgpt/src/webpages/routes/route_registry_api.cpp` |
| Web tests | `tests/web/test_bricon.js`, `tests/web/pages/bricon_asset.html` |
| Native tests | `tests/native/test_bricon_mode.cpp` |
