# RAMBOX — `/rambox`

> Single page for the **Rambox clocks** (Unikon RAMBOX + Bricon loft modules).
> One device selector, the same five tabs for every clock, one shared poll
> interval. `/unikon` and `/bricon` now **redirect** here (`?dev=unikon` /
> `?dev=bricon`). Written for both humans and AI agents.

---

## 1. Objective

**Só leitura (2026-10-25): o hub não escreve nada nos relógios.** O caminho de escrita
do treino/pombal do Unikon (`IC0`+I-frame+S1/S2/S3+`M ZP`) apagou o pombal de um RAMBOX
num teste de bancada e foi **apagado** do firmware, com as rotas
`/api/unikon-send-training`, `/api/unikon-clear-training`, `/api/bricon-send-training` e
`/api/bricon-clear-training` e os respectivos botões. Tudo o resto (polls, chegadas,
pombal, provas, email) é leitura.

Configure and monitor the **clock connected to the hub's RS232 port** — Unikon
RAMBOX or Bricon — without a second page per family: module enable, shared poll
interval, serial/framing options, arrivals, loft pigeons, races/calendar,
training upload and the full status dump. Only **one clock may be active at a
time** (exclusive rule); enabling one disables the other and applies its serial
profile (Unikon **9600 8N1**, Bricon **9600 8N2**).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /rambox` (`?dev=unikon\|bricon` pre-selects, if none is active) |
| Feature flag | `ENABLE_UNIKON==1 \|\| ENABLE_BRICON==1` (both `1` in `pack_rambox.h`) |
| Handler | `web_routes_rambox.cpp` → chunked `buildPageFrame` + `PAGE_RAMBOX` + `/config-manager.js` |
| Legacy URLs | `GET /unikon` → `302 /rambox?dev=unikon`; `GET /bricon` → `302 /rambox?dev=bricon` |
| Auth / caching | `ensureBasicAuth` + `allowHtmlClient` + ETag `FIRMWARE_BUILD`, `private, max-age=15, must-revalidate` |
| Ethernet (W5500) | NOT mirrored (WiFi-only build in this pack) |

---

## 3. Layout

```
main.page > div.wrap
├ nav.actions.nav-inicio: 🏠 Home (/)
├ H1 "🐦 RAMBOX" [rmbTitle]
├ div.rm-bar: label Aparelho + select#rmDev (unikon|bricon) + span#rmBadge
├ p.rm-muted "Só um relógio pode estar ligado/activo em cada ESP32."
├ div.rm-tabs (5 buttons: Config · Chegadas · Pombos · Prova · Estado)
├ section#rmp-config
│  └ div.rm-card
│     ├ ☑ #cfg_enabled (#rmbActive) + select#cfg_interval (1/3/5/10/15/30/60 min)
│     ├ #cfgdev_unikon: ☑ #cfg_official + email destino (#rmEmailDest) + nota 9600 8N1
│     └ #cfgdev_bricon: baud/data/parity/stop/country + BA/club/voo + nota 9600 8N2
├ section#rmp-arrivals: Actualizar · Poll agora · Sincronizar pombal ·
│                        Sincronizar calendário + #rmStats + table#tblArr + #arrmsg
├ section#rmp-pombal:   Actualizar · Ler do relógio · Email do pombal + table#tblPom + #pommsg
├ section#rmp-race:     Actualizar · Ler provas + table#tblRace (só leitura)
└ section#rmp-status:   Actualizar + table#tblStatus (todos os campos do status)
div.actions (sticky-ish): 💾 Guardar configuração + #cfgmsg
```

Tabs are plain buttons (`rmTab(name)`) that toggle `.rm-pane` + persist the choice
in `localStorage['rm_tab']`; the device choice persists in `localStorage['rm_dev']`.

---

## 4. Inputs

| Input | ID / name | Notes |
|---|---|---|
| Aparelho | `#rmDev` | `unikon` / `bricon`; the pane switches with it. **On the first status load the selector adopts `activeModule`** (the enabled clock) — the remembered/URL choice only wins when no clock is active |
| Relógio activo | `#cfg_enabled` | per-module enable; enabling one disables the other (firmware rule) |
| Intervalo de poll | `#cfg_interval` | select in **minutes**: 1, 3, 5, 10, 15, 30, 60 — **default 3 min**. Sent as `interval=<min*60>` (the firmware stores seconds) |
| Usar a configuração da RS232 | `#cfg_manual` | **bench mode** (`manual_serial`): the module stops applying its own serial profile and uses the `/rs232` saved settings instead. The module's own profile is **never** overwritten and comes back when the box is cleared |
| Formato oficial | `#cfg_official` | Unikon only |
| Baud / Dados / Paridade / Stop | `#cfg_baud`, `#cfg_data`, `#cfg_parity`, `#cfg_stop` | Bricon only; profile hint 9600 8N2 |
| País / BA / Clube / Voo | `#cfg_country`, `#cfg_ba`, `#cfg_club`, `#cfg_flight` | Bricon only (layout, bus address, `GetPigeon` codes) |

A stored interval outside the list snaps to the **next larger** step
(`120 s → 3 min`, `7200 s → 60 min`); `0`/absent shows the 3 min default.

---

## 5. Buttons

| Button | Behaviour |
|---|---|
| 🏠 Home | link `/` |
| 🔄 Actualizar | `rmRefresh()` — re-reads status/arrivals/pombal/contests of the selected clock |
| 💾 Guardar configuração | `POST` the selected clock's config endpoint (`enabled`, `interval`, + device fields) |
| 📡 Poll agora | `POST <dev>-poll` (schedules the poll on the main loop) |
| 🐦 Sincronizar pombal / 🏁 Sincronizar calendário | `POST <dev>-sync-pombal` / `<dev>-sync-calendar` |
| ✉ Email do pombal | Unikon only; `POST /api/unikon-email-pombal` (`pombal=current`) |
| ↩ Perfil do módulo | `rmRestoreProfile()` → `POST <dev>-config` with **only** `manual=0`. It is a profile action, **not** a form save: the interval/format/baud fields on screen are not stored (unsaved edits stay unsaved), nothing else in the config changes — the firmware keeps the in-memory values, persists the file (SetConfig always writes) and, with the module enabled, re-applies its own stored serial profile. Reply: "Perfil do módulo re-aplicado" (`apiProfileRestored`) |

---

## 6. Client-side logic

Inline `<script>` (no arrow functions; `customAlert`/`customConfirm` available but the
page only writes inline messages):

- `RM_DEV` — device registry: label + status/arrivals/pombal/contests/config/poll/
  sync/email/training/push endpoints, `trainingMode`, column counts. A new clock
  family = one registry entry.
- `rmd()` — current registry entry; `T(key, fallback)` → `window._t`.
- `rmEsc` / `rmPick` / `rmTime` / `rmDate` — escaping, tolerant field lookup
  (`intervalSec|interval`, `ring|anilha`, …) and time/date formatting.
- `rmIntervalOption(sec)` / `rmIntervalSec()` — seconds ↔ the fixed minute steps
  (`RM_INTERVAL_MIN = [1,3,5,10,15,30,60]`, default 3). Only place that converts.
- `rmAdoptActive(d)` — **first** status load adopts `d.activeModule`
  (`rambox`→unikon, `bricon`→bricon) so the Config pane shows the clock that is
  really enabled; later manual selections are respected.
- `rmSelect()` — applies the pane visibility (unikon/bricon config, training mode,
  push, loft email) and refreshes.
- `rmRefresh()` — 4 GETs in parallel; auto-refresh every 5 s **only** on the
  arrivals/status tabs.
- `rmStatus(d)` — badge (label + ligado/desligado + `activeModule`), fills the config
  fields, renders the raw status table.
- `rmArrivals` / `rmPombal` / `rmContests` — tables + arrival counters; the pigeon
  tab also builds the training checklist (one checkbox per 11-char ring).
- `rmSaveCfg()` — builds the urlencoded body, `interval` = minutes × 60, `manual` = bench mode.
- `rmRestoreProfile()` — unchecks `#cfg_manual` and posts `manual=0` so the module re-applies
  its own profile without touching the rest of the form.

---

## 7. Server API

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/unikon-status` | GET | `connected`, `enabled`, **`intervalSec`**, **`activeModule`**, counts, `officialFormat`, `emailDest`, push state… |
| `/api/bricon-status` | GET | `connected`, `enabled`, **`intervalSec`**, **`activeModule`**, baud/data/parity/stop, country, BA, club/flight, RX counters |
| `/api/<dev>-arrivals` · `-pombal` · `-contests` | GET | live PSRAM data |
| `/api/unikon-config` · `/api/bricon-config` | POST | `enabled`, `interval`, `manual` (bench mode), (+ bricon `baud/data/parity/stop/country/ba/club/flight`) |
| `/api/<dev>-poll` · `-sync-pombal` · `-sync-calendar` | POST | schedule a readout on the main loop |
| `/api/unikon-email-pombal` | POST | loft email. *No clock writes: the training and push (P1/P0) routes were removed (2026-10-25)* |

Status payloads are additive: unknown keys are ignored by the page (`rmPick`).

---

## 8. Persistence

| Data | Where |
|---|---|
| Unikon config (`enabled`, `interval`, `official_format`, `manual_serial`, …) | `/config/unikon.json` (LittleFS, atomic write) |
| Bricon config (`enabled`, `interval`, `manual_serial`, `baud`, `data`, `parity`, `stop`, `country`, `ba`, `club`, `flight`) | `/config/bricon.json` (LittleFS, atomic write) |
| Selected tab / device | `localStorage['rm_tab']`, `localStorage['rm_dev']` (browser only) |

`interval` is stored in **seconds**; both modules default to `180` (3 min) when the
key is missing. An explicit `0` stays 0 (manual only — not offered by the selector).

---

## 9. Runtime engine

- Enabling a clock calls its `SetEnabled` → disables the other one, applies the
  serial profile through `_pendingRS232Apply` (applied on the main loop) and
  (re)starts the interval polling when `interval > 0`.
- Polling and all device I/O run on the main loop; the POST endpoints only set
  flags and answer immediately.
- While a clock is enabled the `/rs232` page always shows the configuration **in force
  at that moment** (it reads the live runtime values, module profile included) and stays
  editable; saving there never touches the module's own profile (see `PAGES/rs232.md`).
- **Bench mode (`manual_serial`)** — the RS232 page and the module profile stay
  independent: the profile is stored only in `/config/unikon.json` /
  `/config/bricon.json` and is **never written** by the RS232 page. With bench mode on,
  `ApplySerialProfile()` loads the RS232 page values into the runtime globals
  (`syncGlobalVariables(true)` + `_pendingRS232Apply`) and leaves the stored profile
  untouched; clearing the box (or the **↩ Perfil do módulo** button) re-applies the
  stored profile immediately.
- **Arrival email table (phone-friendly)**: the ASCII table of the arrival email carries
  only **No / Ring / Chip / Time**. Date, Race, Pos and Eval were removed (2026-10-24):
  with 8 columns the table wrapped on a smartphone and became unreadable; the race is
  already named in the header lines of the email. Both clocks use the same 4-column
  layout (`appendArrivalSectionEmail` / `appendBriconArrivalSection`), widths computed
  from the values so every line keeps the same length.
- **OLED status line 3** (`screen.cpp`, 0.96" 3-line layout) names the module that
  owns the port and its arrival counters: `Unikon: +<new> total <n> reg <m>` or
  `Bricon: +<new> total <n> reg <m>` (`Bricon: offline total <n>` before the first
  read). It switches automatically with `briconActiveModuleName()`; the rotation
  entry is the only line-3 card in this pack (`line3UnikonOnly`).

---

## 10. i18n keys

All page text uses `rmb*` keys in `lang_en_ui.h` / `lang_pt_ui.h` (`rmbTitle`,
`rmbDevice`, `rmbOneOnly`, `rmbTabConfig/Arrivals/Pombal/Race/Status(+Tip)`,
`rmbActive(+Tip)`, `rmbInterval(+Tip)`, `rmbManual(+Tip)`, `rmbRestoreProfile(+Tip)`, `rmbOfficial`, `rmbSerialFixed`,
`rmbBaud/Data/Parity/Stop/Country/Ba/Club/Flight(+Tip)`, `rmbSerialHint`,
`rmbEmailLabel`, `rmbSave(+Tip)`, `rmbRefresh(+Tip)`, `rmbPoll(+Tip)`,
`rmbSyncPombal(+Tip)`, `rmbSyncCalendar(+Tip)`,
`rmbReadDevice(+Tip)`, `rmbEmailPombal(+Tip)`,
`rmbEmpty`, `rmbOnline/Offline`, `rmbNo`, `rmbRing`,
`rmbChip`, `rmbTime`, `rmbDate`, `rmbRace`, `rmbPos`, `rmbEval`, `rmbArrivals`,
`rmbNew`, `rmbNoPoll`, `rmbPollAgo`). Dynamic strings go through `T()`/`window._t`.

---

## 11. Review findings & improvement ideas

| # | Finding | Pri | Suggestion |
|---|---|---|---|
| RMB-1 | No Ethernet (W5500) mirror — page and APIs are WiFi-only. | Low | Add the dispatcher entries if a Rambox build ever ships Ethernet. |
| RMB-2 | Auto-refresh only runs on arrivals/status (5 s); the config tab never re-reads → a change made elsewhere needs a reload. | Low | Poll status on the config tab too, or refresh on tab focus. |
| RMB-3 | Status table renders every key raw (no labels, no ordering). | Info | Curate the fields with i18n labels. |
| RMB-4 | Templates delete / poll actions re-render the whole page block on every response (no diffing); fine at this size. | Info | — |

---

## 12. Source map

| Concern | File |
|---|---|
| Page asset (PROGMEM HTML/CSS/JS) | `chatgpt/src/webpages/assets/page_rambox_asset.h` |
| Route | `chatgpt/src/webpages/routes/web_routes_rambox.{h,cpp}` |
| Redirects (`/unikon`, `/bricon`) | `web_routes_unikon.cpp`, `web_routes_bricon.cpp` |
| Unikon API + persistence | `handlers_unified/unikon_handler.{h,cpp}`, `unikon_protocol.{h,cpp}` (`/config/unikon.json`) |
| Bricon API + persistence | `handlers_unified/bricon_handler.{h,cpp}`, `bricon_protocol.{h,cpp}` (`/config/bricon.json`) |
| Home link | `renderers/home_page_renderer.h` (`/rambox`) |
| Feature flags | `chatgpt/packs/pack_rambox.h` (`ENABLE_UNIKON=1`, `ENABLE_BRICON=1`) |
| Web tests | `tests/web/test_rambox.js`, `tests/web/pages/rambox_asset.html` |
| Native tests | `tests/native/test_bricon_mode.cpp` |
