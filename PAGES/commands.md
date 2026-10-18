# Commands — `/commands` + `/commands-by-datetime`

> One-stop reference for the two **scheduled-command** pages: purpose, layout, every
> button/input, client logic, API endpoints, persistence and the firmware scheduler.
> Written for both humans and AI agents.

---

## 1. Objective

Schedule commands to run automatically:

- **`/commands`** — **Recurring interval commands**: send a command (System/RS232/Modbus)
  every N seconds/minutes/hours, restricted to selected days. Up to **30 slots**.
- **`/commands-by-datetime`** — **One-shot date/time commands** (with optional daily repeat)
  plus the **daily reboot** slots. Up to **16 date/time slots** and **2 reboot slots**.

Both pages render a per-slot table with an optional RS232 **template** attached to each
command (applies the template's framing before queueing).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URLs | `GET /commands`, `GET /commands-by-datetime` |
| Feature flag | `ENABLE_PAGE_COMMANDS` (page routes: `/commands` interval + `/commands-by-datetime` date/time) + `ENABLE_SCHEDULER` (interval/datetime API handlers/engine). `ENABLE_PAGE_COMMANDS_INTERVAL=0` strips only the interval page (`/commands` route + its 3 APIs + Home button + Ethernet + manifests) while the datetime page stays — e.g. Waveshare (`ENABLE_SCHEDULER=1`, `ENABLE_PAGE_COMMANDS=1`, `ENABLE_PAGE_COMMANDS_INTERVAL=0`). The **daily-reboot** API + executor are decoupled: they build with `ENABLE_DAILY_REBOOT` alone (schema `daily_reboot`, status `daily_reboot_active`, stem `schedule` are pack-agnostic) |
| WiFi server | `web_routes_commands.cpp:23` (`/commands`), `:45` (`/commands-by-datetime`) |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:205` (`/commands`), `:208` (`/commands-by-datetime`) |
| Shared streamers | `web_shared_pages.cpp:263,267` → `streamCommandsPageToPrint` / `streamCommandsByDatetimePageToPrint` |
| Home entry | Home page buttons (shown when flags on) |
| Auth / caching | `ensureBasicAuth` + `allowHtmlClient`; `ETag` = `FIRMWARE_BUILD`, `private, max-age=15, must-revalidate`; **`Content-Language: pt`** (see findings) |

Renderer (`commands_page_renderer.cpp`): `PAGE_COMMANDS_HTML` → `var _rs232Enabled/…` →
`PAGE_COMMANDS_JS` → `</script>` → auto.js footer. The datetime page mirrors this with its
own asset/renderer.

---

## 3. Layout — `/commands`

```
main.page > div.wrap
 ├ [🏠 Home]
 ├ H1 "📡 Comandos por Intervalo de Tempo"      (cmdsPageTitle)
 ├ ℹ️ Informação card (cmdsRecurrentDesc info box)
 ├ 📋 Comandos Recorrentes card
 │  └ table.cmd#sched_cmd_body (columns: # | Tipo | Comando | Intervalo | Unid | Dias | Template | Ativo | 💾)
 │    rows added client-side (10 on load, max 20)
 └ [💾 Save All]  (#cmd_save_all)
```

Columns (`rowHTML`, `page_commands_asset.cpp`): `#` | `type_<i>` | `cmd_<i>` |
`int_<i>` (number, min 0) | `unit_<i>` | `days_<i>` | `tpl_<i>` | `en_<i>` | per-row 💾.

---

## 4. Layout — `/commands-by-datetime`

```
main.page > div.wrap
 ├ [🏠 Home]
 ├ H1 "📡 Comandos por Data e Hora"             (cmdsDtPageTitle)
 ├ ♻️ Reinício diário card
 │  └ table#reboot_daily_body  (2 rows: # | Hora (time input) | Ativo (select) | 💾)
 ├ 📅 Comandos Agendados card
 │  └ table.sdt#sched_dt_body  (columns: # | Tipo | Comando | Data | Hora | Diário | Template | Ativo | 💾)
 │    rows added client-side (10 on load, max 20)
 └ [💾 Save All]  (#sched_dt_save_all)
```

Columns (`r(i)`, `page_commands_datetime_asset.cpp`): `#` | `sd_type_<i>` |
`sd_cmd_<i>` (maxlength 64) | `sd_date_<i>` (`<input type=date>`) | `sd_time_<i>`
(`<input type=time>`) | `sd_daily_<i>` (checkbox) | `sd_tpl_<i>` | `sd_en_<i>` | per-row 💾.

---

## 5. Inputs / buttons

### Shared row inputs

| Column | ID pattern | Type / options | Server field |
|---|---|---|---|
| Tipo | `type_<i>` / `sd_type_<i>` | select: System / RS232 / Modbus (RS232/Modbus gated by flags) | `type` |
| Comando | `cmd_<i>` / `sd_cmd_<i>` | text; dt page maxlength 64 | `command` |
| Intervalo | `int_<i>` | number ≥ 0 | `interval` |
| Unid | `unit_<i>` | select Sec / Min (default) / Hour | `unit` |
| Dias | `days_<i>` | select Everyday / Weekdays / Weekends / Mon..Sun | `days` |
| Template | `tpl_<i>` / `sd_tpl_<i>` | select from `/api/rs232-templates`; **disabled** when type is System/Modbus | `template` |
| Ativo | `en_<i>` / `sd_en_<i>` | select Yes/No (default No) | `enabled` |
| Data (dt) | `sd_date_<i>` | `date` picker | `date` |
| Hora (dt) | `sd_time_<i>` | `time` picker | `time` |
| Diário (dt) | `sd_daily_<i>` | checkbox "repeat daily" | `daily` |

Reboot rows (datetime page): `#reboot_daily_body` → time (HH:MM) + active select + 💾 per
slot (2 slots).

### Buttons

| Button | ID / handler | Behaviour |
|---|---|---|
| 🏠 Home | link `/` | first child of `.wrap` |
| 💾 per-row | `[data-save='<i>']` → `saveSlot(i)` / `ss(i)` | `POST /api/scheduled-command-slot` / `/api/scheduled-datetime-slot` |
| 🗑️ per-row | `[data-del='<i>']` → `removeRow(i)` / `rm(i)` | confirm → rebuild table without that slot (contiguous indices) + `loadAll()` / `la()` |
| ➕ Add Command | `#add_cmd` / `#add_sd` → `addRow()` | appends a new row up to `MAX` (30 / 16); alert at the cap |
| 💾 Save All | `#cmd_save_all` / `#sched_dt_save_all` | sequential `saveSlot` for rows 1..currentCount, 160 ms / 180 ms apart |

---

## 6. Client-side logic

Both pages use an identical inline IIFE pattern (`PAGE_COMMANDS_JS`,
`PAGE_COMMANDS_DATETIME_JS`):

- **`init()`** — if table empty, create **10 rows** (`MAX=20`, no add-row button);
  `attachSaveAll()`; `loadAll()`.
- **`loadAll()`/`la()`** — per row `GET /api/scheduled-command-slot?i=` /
  `/api/scheduled-datetime-slot?i=`; then `GET /api/rs232-templates` to populate the
  template `<select>` (preserving `data-cur`). Sets type/command/interval/unit/days/
  date/time/daily/template/enabled onto the row.
- **`toggleTemplate(i)`/`tt(i)`** — disables + dims the template select when type is
  System or Modbus.
- **`saveSlot(i,btn)`/`ss(i,btn)`** — FormData `{i,type,command,interval,unit,days,template,enabled}`
  (dt adds `date,time,daily`) → `POST` slot endpoint; `status==='ok'` →
  `customAlert('✅ Command saved!')` + `showSavedCard()`; error/network → alert. Loading
  overlay + disabled button around request.
- **`attachSaveAll()`** — Save All loops rows 1..currentCount calling `saveSlot` at
  160/180 ms intervals (non-atomic — findings).
- Project rules: `customAlert`, no arrow functions, `t(k,fb)` i18n, `esc()`/`e()` HTML
  escaping of values before injection.

---

## 7. Server API

Handlers: `handlers_unified/commands_handler.cpp` (guarded `ENABLE_SCHEDULER`; 404 stubs
when off). Routes in `web_routes_commands.cpp`.

| Endpoint | Method | Params | Validation |
|---|---|---|---|
| `/api/scheduled-command-slot` | GET | `i` (1..30) | 400 `{"status":"err","msg":"idx"}`; 404 `"slot"` if missing |
| `/api/scheduled-command-slot` | POST | `i,type,command,interval,unit,days,template,enabled` | 400 `"idx"`; when enabled: command non-empty (`"cmd"`), `interval>0` (`"interval"`); `unit` normalised `s/m/h ↔ seconds/minutes/hours` |
| `/api/scheduled-command-slots` | GET | — | full list (loops 1..30) `{status,slots:[{i,command,interval,unit,days,template,enabled}]}` |
| `/api/scheduled-datetime-slot` | GET | `i` (1..16) | 400 `"indice invalido"` |
| `/api/scheduled-datetime-slot` | POST | `i,type,command,date,time,daily,template,enabled` (aliases `sd_*`) | 400 `"indice"`; when enabled: command (`"command vazio"`), `date==YYYY-MM-DD` (`"data invalida"`), `time==HH:MM` (`"hora invalida"`) |
| `/api/scheduled-datetime-slots` | GET | — | loops 1..8 → `{status,slots:[...]}` (note: only 8 here, page allows 16) |
| `/api/daily-reboot` | GET | `i` (1..2) | 400 `"indice"`; reads ConfigManager, falls back to legacy NVS `cfg_drbt`. Slot 1 with OEE shifts configured additionally returns `follows_shifts:true` + `effective_time:"HH:MM"` (end of last shift + 5 min grace), and `time` already carries the effective value |
| `/api/daily-reboot` | POST | `i,time,enabled` | 400 `"indice"`; when enabled `HH:MM` + range check (`"hora"`); persists NVS too |

All save paths: `setSchedCmdSlot`/`setSchedDtSlot`/`setDailyRebootSlot` →
`resetSchedCmdSlotTimer`/`resetSchedDtSlotTimer` → `saveSection("schedule")` → 500
`{"success":false,"message":"Erro ao guardar …"}` (PT) on failure. Success →
`{"status":"ok"}`. Config changes logged via `observabilityLogConfigChange`.

> **Headless daily-reboot API:** with `ENABLE_DAILY_REBOOT=1` but `ENABLE_PAGE_COMMANDS=0`
> (e.g. Waveshare), `web_routes_commands.cpp` still registers `GET/POST /api/daily-reboot`
> (routes) and `commands_handler.cpp` builds the real daily handlers — no `/commands` UI,
> config done via the API or a generic config PUT.
>

> **Slot-count mismatch:** the interval slots endpoint lists 30 (matches), but
> `/api/scheduled-datetime-slots` only serialises **8** while the page/API allow **16**
> (finding CMD-2).

---

## 8. Persistence

All slots live in the **`schedule`** stem → `/config/schedule.json` (4 KB), written via
`saveSection("schedule")` → `schedDoc` (`ConfigManager.h:50`). Legacy `cfg_drbt` NVS key is
read/written by the daily-reboot handlers for backward compatibility. ConfigManager getters
default empty/disabled; setters call `markDirtyAndScheduleSave()`.

Slot ranges: interval 1..30, datetime 1..16, daily-reboot 1..2.

---

## 9. Runtime engine (scheduler)

`runtime_scheduler.cpp`:

- **`processSchedCmdSlots()`** (tick-driven, boot-stabilisation + throttle):
  loops 1..10; skips disabled/empty/interval≤0; computes `intervalMs` from
  `unit`; fires only when `now - lastExec >= intervalMs` and `shouldRunToday(days)`.
  System-type commands execute inline via `processLine(...)`; RS232/Modbus are queued with
  `enqueueRS232Command(cmd, tpl, "CMD_<i>", type)` (WARN log when the queue is full). Each
  execution updates `schedCmdLastExecRAM[i]` and logs an observability event.
- **`processSchedDtSlots()`** — per-minute check of date/time + `daily` flag; one-shot
  slots use the 3-layer anchor (`getSchedDtLastRunDayJson` on schedDoc + legacy NVS
  migration) to avoid double-firing after reboot; `resetSchedDtSlotTimer(i)` clears the
  in-RAM marker after a successful save.
- **`processDailyRebootSlots()`** — fires the daily reboot when enabled: default slot-1
  time `00:00` (all packs with the datetime page; user-changeable/disablable on the page).
  **Shift following:** with OEE shifts configured, slot 1 ignores its stored HH:MM and fires
  at the end of the last shift of the production day (= shift day-boundary) + 5 min grace,
  so shift-end processing and shift emails finish first; tracks shift-config changes
  automatically (API reports `follows_shifts` + `effective_time`, page locks row 1). Slot 2
  is always fully manual. A disabled slot never fires.
  **Graceful gate:** every scheduled reboot (daily slots, datetime + interval REBOOT shortcuts)
  goes through `scheduleGracefulReboot()` — while emails are sending/queued, SD writes or
  FTP uploads are busy, the reboot is armed pending (15 min max, 1/min progress log) and only
  `safeReboot()`s once idle (or at timeout, with WARN + pending queue counts). The pending
  reboot is serviced on all three scheduler ticks (daily, interval, datetime), so no
  SCHEDULER/DAILY_REBOOT flag combination can strand it; FTP busy is read via non-blocking
  `ftpIsBusy()` (zero-wait mutex peek). `resetDailyRebootSlotTimer` from the
  API resets the marker and drops an armed pending reboot. Compiled standalone
  under `ENABLE_DAILY_REBOOT` (top of `runtime_scheduler.cpp`), independent of
  `ENABLE_SCHEDULER`.
- System vs device commands distinguished by `isSystemCommand(trimmed, type, prefixLen)`
  (system commands run on the ESP32 itself; others go to the selected port).
- **Scheduled REBOOT-family commands bypass the interactive cooldown:** `REBOOT`,
  `RESTART`, `ESP.REBOOT`, `ESP.RESET` (SYS-prefixed, or bare REBOOT auto-interpreted on
  date/time slots) call `safeReboot()` **directly** in `processSchedCmdSlots()` /
  `processSchedDtSlots()` instead of going through `handleReboot`'s 1-h `REBOOT_COOLDOWN_MS`
  lockout — a scheduled restart must always fire (the slot is already marked executed first).
  **Loop-safety (3-layer anchor, all packs):** date/time slots flush
  `saveSection("schedule")` before the restart (a one-shot slot is cleared in RAM only and
  the deferred `markDirtyAndScheduleSave` would never run) and daily slots anchor their day
  key in the LittleFS truth — `daily_reboot.sN.last_run_day` (cores section, via
  `setDailyRebootLastRunDay` + `saveSection("core")`) and `sched_dt.sN.last_run_day`
  (schedule section, via `setSchedDtLastRunDayJson` + `saveSection("schedule")`). A warm
  RTC-mirror (`s_rebootAnchorRtc`, `RTC_NOINIT`) is written right before `safeReboot` as
  the zero-flash primary (covers a failed deferred flush); the legacy NVS keys
  (`drb_lrN`/`sdt_lrN`) are read-only migration for upgrades — writes to NVS ceased. The
  guard `schedDtLastRunDayKey[i] == curDayKey` now also covers one-shot slots, so a
  scheduled reboot fires at most once per day and never re-fires at boot into a reboot loop.
- `resetSchedCmdSlotTimer`/`resetSchedDtSlotTimer` exported (`runtime_scheduler.h:11-12`).

---

## 10. i18n keys

**Firmware-side i18n (also added):** the API messages above are no longer hardcoded Portuguese in the handler — they now come from the firmware pack (`LangStrings`, `L(activeLang->…)`) and follow `system_language`. Page `<title>` and tab labels follow the same rule.

`cmdsPageTitle`, `cmdsDtPageTitle`, `cmdsRecurrentTitle`, `cmdsRecurrentDesc`, `infoTitle`,
`cmdsDtRebootTitle`, `cmdsDtScheduledTitle`, `thType`, `thCommand`, `thInterval`, `thUnit`,
`thDays`, `thTemplate`, `thActive`, `thDate`, `thTime`, `thDaily`, `btnSaveAll`,
`cmdTipType`, `cmdTipCommand`, `cmdTipEnabled`, `cmdTipSave`, `cmdDtTipType`,
`cmdDtTipCommand`, `cmdDtTipDate`, `cmdDtTipTime`, `cmdDtTipDaily`, `cmdDtTipTemplate`,
`cmdDtTipEnabled`, `cmdDtTipSave`, `typeSystem`, `unitSecondShort`, `unitMinuteShort`,
`unitHourShort`, `dayEveryday`, `dayWeekdays`, `dayWeekends`, `dayMonday..Sunday`,
`opYes`, `opNo`, `cmdsPlaceholder`, `cmdsSaved`, `cmdsError`, `cmdsNetError`.

> The HTML fallbacks are **PT** (`Ativo`, `Dias`, `Unid`, `Reinício diário`, `Comandos
> Agendados`, `Comando`) — repo rule violation (finding CMD-3); both pages also send
> `Content-Language: pt`.

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Status |
|---|---|---|---|
| 1 | **Both pages now support the full slot range.** Interval page `MAX=30`, datetime `MAX=16` (matching the APIs); a **➕ Add Command** button appends rows and each row has a **🗑️ remove** button that rebuilds with contiguous indices. | Medium | **fixed** |
| 2 | **`/api/scheduled-datetime-slots` serialises only 8 slots** while GET/POST accept 1..16 — the bulk endpoint under-reports; clients can miss slots 9..16. | Medium | open |
| 3 | **HTML fallbacks hardcoded PT (`Ativo`, `Dias`, `Unid`, `Reinício diário`, …) + `Content-Language: pt`** — converted to English; headers removed. | Low | **fixed** |
| 4 | **"Save All" is non-atomic** — sequential POSTs at 160/180 ms; mid-loop failure leaves partial save with no error surfaced. | Low | open |
| 5 | Daily-reboot also writes legacy NVS `cfg_drbt` — duplicated persistence path that can drift from `/config/schedule.json`. | Low | open |
| 6 | `getParamInt` treats missing interval as 0; enabled rows without interval get 400 — inconsistent but arguably correct. | Info | open |

---

## 12. Source map

| Concern | File |
|---|---|
| Renderers | `chatgpt/src/webpages/renderers/commands_page_renderer.{h,cpp}`, `commands_datetime_page_renderer.{h,cpp}` |
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_commands_asset.{h,cpp}`, `page_commands_datetime_asset.{h,cpp}` |
| Routes (WiFi + Ethernet) | `chatgpt/src/webpages/routes/web_routes_commands.cpp`, `ethernet_bootstrap_routes.h:205,208` |
| API handlers | `chatgpt/src/webpages/handlers_unified/commands_handler.cpp` |
| Config storage | `chatgpt/src/ConfigManager.{h,cpp}` (`schedule` stem / `schedDoc`) |
| Scheduler engine | `chatgpt/src/runtime_scheduler.{h,cpp}` |
| RS232 templates (used by `tpl_` select) | `chatgpt/src/webpages/routes/web_routes_config.cpp` (`/api/rs232-templates`) |
| Self-test manifest | `test_manifest_handler.cpp` (`PAGE("/commands"...)`, `PAGE("/commands-by-datetime"...)`) |