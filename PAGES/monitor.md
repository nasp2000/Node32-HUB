# Monitor / Serial Monitor — `/monitor` + `/serial-monitor`

> One-stop reference for the **Data Reception (RX Monitor)** and **Serial Monitor (log
> terminal)** pages: purpose, layout, every button/input, client logic, API endpoints and
> the firmware-side buffers/log pipeline they surface. Written for both humans and AI agents.

---

## 1. Objective

Two companion pages under `ENABLE_PAGE_MONITOR`:

- **`/monitor`** — "📥 Data Reception": live view of the **received data buffer** (last 100
  lines), plus a quick command sender (System / RS232 / Modbus). It opens the same buffer in
  a full-screen **Buffer Viewer** (`/buffer-viewer`) for local processing on the PC.
- **`/serial-monitor`** — "Serial Monitor": a full-screen debug terminal that tails the
  **virtual log** (all `sendVirtualMessage` output, module-tagged), with search, filters,
  pause, copy/export and an inline command box. This is the web counterpart of
  `logger_impl.h` / `pushLogToPsram`.

All three count as **concurrent web sessions** (`allowHtmlClient`) but are
**ALWAYS exempt** — local-processing pages never kick by time, Access option not needed
(flag in `<head>` before `auto.js`, `web_monitor_pages.cpp`).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URLs | `GET /monitor`, `GET /serial-monitor`, `GET /buffer-viewer` |
| Feature flag | `ENABLE_PAGE_MONITOR` (page + routes compiled out when `0`; both share the flag) |
| WiFi server | `web_routes_monitor.cpp:467` (`/monitor`), `:526` (`/serial-monitor`), `:508` (`/buffer-viewer`) |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:183` (`/monitor`), `:196` (`/serial-monitor`) |
| Shared streamer | `web_monitor_pages.cpp` — `streamSharedMonitorPage` (`:7`), `streamSharedSerialMonitorPage` (`:451`), `streamSharedBufferViewerPage` (`:443`) |
| Home entry | Home page "📊 Data Reception" + "📺 Serial Monitor" buttons (`home_page_renderer.h`) |
| Auth / session | `ensureBasicAuth` + `allowHtmlClient`; `ETag` + `private, max-age=15, must-revalidate` |
| Session exemption | ALWAYS exempt (local processing) — never kicked by time, no option needed |

---

## 3. Layout — `/monitor`

```
main.page > div.wrap
 ├ [🏠 Home]                     (#rx_btn_home)
 ├ H1 "📥 Data Reception"        (#rx_page_heading)
 ├ ⚙️ Actions card
 │  ├ info box (Refresh / Clear / Open RX Viewer explanations)
 │  └ buttons: #btn_refresh | #btn_clear | #btn_open_viewer
 ├ 📤 Send Command card
 │  ├ info box (select destination, type, Enter or Send)
 │  ├ select #cmd_dest (options: System=sys [RS232|Modbus if flags on])
 │  ├ input #cmd            (text, "Enter command")
 │  └ button #btn_send
 └ 📊 RX Buffer Content card
    ├ #rx_buffer_desc  ("Real-time view of received data (last 100 lines)")
    └ pre #rx_text      (monospace, max-height auto-sized to line count)
```

No `data-i18n` attributes — the page applies its own `applyMonitorTexts()` i18n via the
inline JS (`_rxT(key, pt, en)` helper). Page opens with a fire-and-forget
`POST /api/monitor-open` and closes with `POST /api/monitor-close` (`sendBeacon`) on unload.

---

## 4. Layout — `/serial-monitor`

```
body (monospace, full-height, no scroll on body)
 ├ #toolbar
 │  ├ #srch-wrap → input #srch (search) + #srch-x (clear)
 │  ├ select #ftype (All/INFO/WARN/ERROR/DEBUG/CRITICAL_EVENT/COMMAND_RESPONSE/TELNET_EVENT)
 │  ├ select #ftag (All modules / [TAG] (count) — dynamic)
 │  ├ buttons: #btn-pause, #btn-clear, #btn-copy, #btn-export
 ├ #optbar
 │  ├ checkboxes #optAuto (auto-refresh 3 s), #optScroll (auto-scroll), #optWrap (wrap), #optRegex (regex search)
 │  └ select #ctype (System/RS232/Modbus) + input #cmd + button "📤 Send"
 ├ #statbar
 │  ├ #sMsgs (filtered count), #sTotal (total), #sPaused (visible when paused),
 │  ├ #sHeap (free heap KB), #sTime (now), #sStatus (coloured status dot)
 └ #buf  (message list: timestamp [TYPE] [tag] text; empty-state "No messages found")
```

Message rendering: colored left border per type (`typeColors`), module tag colored from a
14-color palette (`modColor`), `<mark>` highlight for search terms, click on a tag filters
by that module (`filterTag`).

---

## 5. Inputs / buttons

### `/monitor`

| Element | ID | Behaviour |
|---|---|---|
| Refresh | `#btn_refresh` | `loadRxText(100)` — refetch buffer |
| Clear | `#btn_clear` | `POST /api/clear-buffer` then refresh; disabled + dimmed while running |
| Open RX Viewer | `#btn_open_viewer` | `window.open('/buffer-viewer', '_blank')` |
| Destination | `#cmd_dest` | `sys` (default) / `rs232` / `modbus` (options gated by flags) |
| Command | `#cmd` | text; Enter or `#btn_send` triggers `send()` |
| Send | `#btn_send` | `send()` → route per target (see §7) |

### `/serial-monitor`

| Element | ID / fn | Behaviour |
|---|---|---|
| Search | `#srch` | filters `allMsgs` (plain or regex via `#optRegex`) |
| Clear search | `#srch-x` / `clearSearch()` | reset term |
| Type filter | `#ftype` | exact `m.type` match |
| Module filter | `#ftag` | exact `[tag]` match; rebuilt from seen tags |
| Pause/Resume | `#btn-pause` → `togglePause()` | stops polling; button turns red `tb-paused`; shows `#sPaused` |
| Clear | `#btn-clear` → `clearFilter()` | clears search + both filters |
| Copy all | `#btn-copy` → `copyAll()` | clipboard (`navigator.clipboard` w/ fallback), lines `ts [TYPE] text` |
| Export | `#btn-export` → `exportTxt()` | downloads `serial_monitor_<ts>.txt` |
| Auto-refresh | `#optAuto` | `setInterval(load, 3000)` |
| Auto-scroll | `#optScroll` | scrolls `#buf` to bottom on render |
| Wrap | `#optWrap` | `.wr` vs `.nw` class on messages |
| Regex | `#optRegex` | search term treated as regex (case-insensitive) |
| Command type | `#ctype` | system / rs232 / modbus |
| Command box | `#cmd` | Enter or Send → `sendCmd()` |

---

## 6. Client-side logic

### `/monitor` (`web_monitor_pages.cpp:37-435`)

- **Poll**: `setInterval(_pollRxActivity, 900)` → `GET /api/monitor-status`; if
  `buffer_seq` changed → schedule a throttled refresh (min 3 s apart,
  `_scheduleIdleRefresh`). Skipped when `document.hidden`.
- **`loadRxText(limit=100)`**: `GET /api/received-text?limit=100&t=<now>`, collapses
  blank lines, sets `#rx_text` and auto-sizes max-height via `_setRxHeightForLines`.
- **`send()`**: trims `#cmd`, picks URL from `#cmd_dest` (`/api/system-command`,
  `/api/rs232-command`, `/api/modbus-command`), `POST` JSON `{command}`; on success shows
  `✅ <response>` in the placeholder (3 s), error `❌ <response>` (4 s); clears input,
  re-enables, refocuses. Uses `e.key==='Enter'` (project rule).
- i18n via `_rxT(key, pt, en)` / `applyMonitorTexts()` — hardcoded EN+PT pairs (see
  findings MON-3).

### `/serial-monitor` (`web_monitor_pages.cpp:471-681`)

- **State**: `allMsgs[]` (capped 500), `_msgCount` (last server total), `isLoading`,
  `isPaused`, `knownTags{}` (module counts).
- **`load()`**: `GET /api/serial-monitor?limit_msgs=200&since=<count>&t=<now>` (first load
  `limit_msgs=500`). If server `total < _msgCount` (log cleared) → reset. Appends new msgs,
  updates tag filter, heap/time/status bars, then `renderMsgs()`. 429 → retry up to 2×
  (1.2 s backoff). Auto-poll every 3 s when `#optAuto` on and not paused.
- **`renderMsgs()`**: applies type/module/search filters, builds HTML rows, shows filtered
  vs total counts, highlights matches.
- **`sendCmd()`**: `POST` `{command}` JSON to `/api/system-command`|`/api/rs232-command`|
  `/api/modbus-command`; appends a local `COMMAND_RESPONSE` line `➜ cmd → resp` unless
  `streamed`; re-schedules a load.
- Full-screen terminal (no nav bar), keybind-friendly.

---

## 7. Server API

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/monitor-open` | POST | increments the monitor session counter (`monitor_handler.cpp:5`) → `{status,override:true}` |
| `/api/monitor-close` | POST | decrements → `{status,override:false}` |
| `/api/clear-buffer` | POST | `sharedWebClearReceivedBuffer`; 503 `{status:busy}` if locked |
| `/api/monitor-status` | GET | `sharedWebBuildMonitorStatusJson` (`web_shared_logic.cpp:533`): `{status,isReceiving,bufferUsage,bufferSize,buffer_seq,last_rx_ms,now_ms}` |
| `/api/received-text` | GET | `sharedWebBuildReceivedText` (`web_shared_logic.cpp:566`): params `limit` (100), `max_bytes` (4096), `include_partial`, `download`; streamed with `HeavyApiRequestGuard` 400 ms |
| `/api/serial-monitor` | GET | `sharedWebStreamSerialMonitorJson` — params `limit_msgs`, `since`, `no_cmds`, `last_seq`, `max_bytes`, `limit_lines`, `debug`; 500 ms guard. WiFi handler opens the response stream with a **16 KB initial buffer** (up from 1460 B) and the serializer feeds `esp_task_wdt_reset()` every 32 messages / 512 raw buffer bytes — prevents a **task-WDT reboot** when `/serial-monitor` is the first page opened right after boot (WiFi `async_tcp` is subscribed to TWDT and used to spend >5 s building the JSON, see `PAGES/BACKLOG.md` MON-6) |
| `/api/system-command` | POST | `command_api_handler.cpp:270` — form or JSON `{command}`; `status`/`help` inline, else queued via `enqueueRS232Command` |
| `/api/rs232-command` | POST | same file `:303` — form or JSON `{command, template?}` → RS232 queue |
| `/api/modbus-command` | POST | same file — sends command to Modbus engine |

Routes: `/api/monitor-open|close|clear-buffer` via `registerApiRoute` in
`web_routes_monitor.cpp:436-438`; `/api/monitor-status|received-text|serial-monitor` via
`monitor_buffer_handler.cpp:36,50,78`; command endpoints via `registerCommandApiRoutes`.

---

## 8. Persistence

None of these pages write persistent config. They are read-only views of runtime state:

- **RX buffer**: in-RAM `bufferDados` (captured RS232/LPT/sensor lines), protected by
  `bufferDadosSpinlock`; capacity/size/seq exposed by `/api/monitor-status`.
- **Virtual log**: in-RAM PSRAM ring (module-tagged `sendVirtualMessage` entries); cleared
  on reboot; `/api/serial-monitor` reads with sequence-based delta (`since`).

---

## 9. Runtime engine

- **Buffer capture** — every received line is timestamped and appended to `bufferDados`
  (with `buffer_seq` monotonic counter); the monitor page only reads a window of the tail.
- **Log pipeline** — `logger_impl.h`: `sendVirtualMessage(F("..."), LEVEL, bypassDedup)`
  → Serial + Telnet + **Web Serial Monitor** (pushed to PSRAM ring). Type tags come from the
  message level; module tags from the `[TAG]` prefix. The `/api/system-command` endpoint can
  return a real reply (buffered reply in `logger_impl.h:116`).
- **Session accounting** — `/monitor` increments a counter (`monitor_session_state.h`) so
  the device knows a human is watching; used for `monitor-open/close` override semantics.

---

## 10. i18n keys

`/monitor` uses inline `_rxT(key, pt, en)` lookups — keys: `loading`, `btnHome`,
`homeDataRecv`, plus hardcoded EN/PT strings for actions/send/buffer sections. The shell
loading overlay uses `data-i18n='loading'` ("A processar..." / "Processing...").
`/serial-monitor` has **no i18n** — all strings hardcoded English.

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| 1 | `/monitor` i18n is done by `_rxT(key, pt, en)` with **PT fallback selected by `_rxLangCode()`** (a browser-language heuristic), bypassing `data-i18n` and `/api/lang` — inconsistent with every other page and can show PT to EN browsers before the user cached a language. | Medium | Use standard `data-i18n` + `window._t`; drop the ad-hoc PT/EN switch. |
| 2 | `/monitor` opens/closes a session via `POST /api/monitor-open|close` but the counters are only used for `override` — unclear if they actually gate anything (see `monitor_session_state.h`). Dead-ish state. | Low | Verify or remove. |
| 3 | `/serial-monitor` message cap is 500 and auto-poll 3 s regardless of activity; a long-running high-volume log churns the whole list and re-renders 500 rows each poll. | Low | Batch render (document fragment) + adaptive interval when `buffer_seq` stalls. |
| 4 | `sendCmd`/`send()` rely on `r.json()` — the command endpoints can return non-JSON (e.g. 503 text) and the `.catch` then masks the real cause as a network error. | Low | Read as text and fall back on parse failure (monitor page already does). |
| 5 | `#srch-x` and search rely on `document.getElementById('srch').value` at render time; the input has no `input` listener, so the list only re-filters on the next poll. | Low | Add `oninput` → `renderMsgs()`. |

---

## 12. Source map

| Concern | File |
|---|---|
| Shared HTML/JS (both pages) | `chatgpt/src/web_monitor_pages.{h,cpp}` |
| Routes (WiFi) | `chatgpt/src/webpages/routes/web_routes_monitor.cpp` |
| Buffer/status/serial handlers | `chatgpt/src/webpages/handlers/monitor_buffer_handler.cpp` |
| Open/close/clear handlers | `chatgpt/src/webpages/handlers_unified/monitor_handler.cpp` |
| Command send handlers | `chatgpt/src/webpages/handlers/command_api_handler.cpp` |
| Buffer + log JSON builders | `chatgpt/src/web_shared_logic.cpp` (`:533`,`:566`) |
| Session state | `chatgpt/src/monitor_session_state.{h,cpp}` |
| Log pipeline (source of serial-monitor data) | `chatgpt/src/logger_impl.h` |
| Ethernet routes | `ethernet_bootstrap_routes.h:183,196` |
| Self-test manifest | `test_manifest_handler.cpp` (`PAGE("/monitor"...)`), `selftest_handler.cpp` |