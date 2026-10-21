# Status — `/status`

> One-stop reference for the **Status / system overview page**: purpose, layout, every
> button, client logic, API endpoint and the firmware-side status payload. Written for both
> humans and AI agents.

---

## 1. Objective

A single-page **read-only system status** dashboard: device identity, chip/heap/PSRAM/NVS/
flash/SD storage, network (WiFi + Ethernet), RS232, email/recipients, scheduler, and
"compiled modules" feature flags. Pulls everything from one **`GET /api/status`** payload
(`fillSystemStatusJson`) and renders it into two columns + a "Monitoring" section.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /status` |
| Feature flag | none dedicated — always served by `ENABLE_PAGE_MONITOR` block (`web_routes_monitor.cpp:446`) |
| WiFi server | `web_routes_monitor.cpp:446` — `streamStatusPageToPrint()` |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:201` → `streamSharedStatusPage` (`web_shared_pages.cpp:99`) |
| Home entry | Home page "📊 Status" button |
| Auth / caching | `ensureBasicAuth` + `allowHtmlClient`; `ETag` = `FIRMWARE_BUILD`, `private, max-age=15, must-revalidate` |
| API rate limit | `/api/status` is throttled: **1 request / 500 ms** → HTTP **429** `{"error":"too_many_requests"}` (`core_admin_handler.cpp:31`) |

Renderer (`status_page_renderer.cpp`) builds the whole page inline (HTML + JS, no separate
asset file), then auto.js footer. The page **does not** call the firmware-cache-buster
(`auto.js` skips `/status`).

---

## 3. Layout

```
main.page > div.wrap
 ├ [🏠 Home] (data-i18n='btnHome')
 ├ H1 "📊 Status"
 ├ .status-grid
 │  ├ #col1   (left column — general + storage sections)
 │  └ #col2   (right column — network + protocol sections)
 ├ #heap-section ("🧠 Loading..." → heap/PSRAM/Flash/NVS bars + counts)
 ├ .status-monitor
 │  ├ H2 "📊 Monitoring"
 │  └ #monitor (email/recipients/scheduler summary)
 └ [🔄 Refresh]  (#r, data-i18n='filesBtnRefresh')
```

Each column fills via `L.push(...)` (safe HTML lines) — sections separated with
`═ <name> ═` headers.

---

## 4. Inputs / buttons

| Element | ID | Behaviour |
|---|---|---|
| 🏠 Home | link `/` | first child of `.wrap` |
| 🔄 Refresh | `#r` | refetch `GET /api/status` and re-render (`u()`) |

No other inputs — fully read-only.

---

## 5. Client-side logic

- **`u()`** — sets both columns + monitor to `_st('statusUpdating','Updating...')`, then
  `fetch('/api/status', {cache:'no-store'})`. On `null` → "No data". Otherwise:
  - **Left column (`L`)**: SYSTEM (chip model/rev/features, cores, CPU MHz, temp,
    firmware/build, uptime, local time/timezone, reset reason, NTP sync), RESOURCES,
    CHIP ESP32, MicroSD (mounted, storage mode, format, usage bars, write counts, life,
    buffer usage, folders/files), Flash (LittleFS fs/mounted, total/used/free, write
    counts), NVS (used/total, writes, action hints), COMPILED MODULES (feature_* flags).
  - **Right column (`M`)** — used for **Monitoring**: DESTINATION EMAIL (principal +
    EMAILDAY extras), RECIPIENTS (name, op label, freq, emails), COMMANDS BY INTERVAL and
    COMMANDS BY DATE/TIME (slot, interval/date, days, destination System/RS232),
    daily reboot, and per-source live counts (RS232 RX/BPS, buffer use, RX status,
    active/inactive timeouts, printer mode, etc.).
  - **`#heap-section`** — heap/PSRAM/flash/NVS usage bars + **DMA pool** (`dma_free`, `dma_largest` — the pool WiFi/SDMMC/SPI actually need; it can be exhausted while ordinary DRAM still looks free) + fragmentation (`heap_frag_pct`) + **largest contiguous internal block** (`largest_block_internal`) — the real fragmentation signal (a large free heap with a tiny largest block means fragmentation).
  - **Internal Heap Analysis table** — one row per compiled module with **three columns: `Module | Est. KB | Meas.`**. `Est.` is the hardcoded compile-time guess (`heap_breakdown[].est_kb`); `Meas.` is the **measured** delta between boot checkpoints (`heap_breakdown[].meas_kb`, `—` when that module has no checkpoint). The footer shows `Actual used | Σ est. | Σ measured | Δ`; Δ = what remains unattributed (JSON buffers, strings, sockets, running tasks). Legend added under the footer.

  - `fmtSendFreq(v)`: `0` → "no limit"; `s`/`min`/`h` formatting.
- i18n via `_st(k,fb)` helper (window._t → localStorage cache → fallback). Fallbacks are
  English (good). No arrow functions.

---

## 6. Server API

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/status` | GET | `handleStatusGet` (`core_admin_handler.cpp:28`) → `fillSystemStatusJson(doc, payloadState)` (12 KB `SpiRamJsonDocument`); **throttled 500 ms** → 429 |

Payload covers: `firmware_version/build`, `chip_model/revision/features/cores/mhz/temp_c`,
`uptime`, `local_time`, `timezone`, `ntp_sync`, `reset_reason`, `heap_*`, `psram_*`,
`flash_*`, `nvs_*`, `sd_*` (mounted/format/usage/writes/life/file+folder counts/email),
`wifi_*`, `ethernet_*`, `eth_wifi_state`, `email_destination`, `emailday_extra_emails`,
`recipients_active`, `sched_cmd_active`, `sched_dt_active`, `daily_reboot_active`,
`rs` (RS232 state), `printer_mode`, `telnet_*`, `timeout_*`, `unikon_*`, `recent_ips`,
`feature_*` (compiled-module flags), `storage_mode`, `buffer_*`, `rx_bps`, `charset`,
`bt_*`/`api_*` extras as built by `fillSystemStatusJson` (`system_status_payload.h`).

**Heap diagnostics (read-only, no allocation):** `free_internal`, `largest_block_internal`
(`heap_caps_get_free_size` / `get_largest_free_block(MALLOC_CAP_INTERNAL)`), and the PSRAM
equivalents `free_psram`, `largest_block_psram`. Use `largest_block_internal` (not just
`free_heap`) to detect fragmentation and to measure the effect of any heap-tuning change.
`dma_free` / `dma_largest` expose the DMA-capable internal pool separately.

**Measured boot trace (2026-10-21):** `heap_trace[]` is a sequence of boot checkpoints
recorded by `heap_trace.h/.cpp` (`heapTraceMark(label)` in `setup_impl.h`,
`rs232_config_impl.h` (email), `webpage_impl.h` (webserver), `config_sync_impl.h` (wifi),
`sd_status_impl.h` (sd), `sd_tasks_impl.h` (ethernet), `loop_blocks.h` (boot_ok)). Each
entry carries `label`, `free_kb`, `dma_kb`, `largest_kb`, `psram_kb` and `delta_kb` (heap
consumed since the previous checkpoint). `heap_breakdown[]` rows map to a checkpoint via
`trace_key` and expose `meas_kb`, replacing the "Δ = dynamic heap" black box with named
measurements. Cost: 16 slots × 24 B of `.bss`, no allocations.

---

## 7. Persistence

**None.** The page is a live view of in-memory counters + persisted settings read by
`fillSystemStatusJson` (which reads ConfigManager values); no writes.

---

## 8. Runtime engine

- **Status payload** — `fillSystemStatusJson(doc, statusPayloadState)` in
  `system_status_payload.h` reads ConfigManager (config), runtime counters (heap/PSRAM/
  NVS/flash/SD), network state (WiFi/Ethernet/Telnet), RS232, email/recipients and the
  scheduler state. Built on demand (no caching beyond the 500 ms throttle).
- **429 throttle** — `statusBusy` flag + `lastStatusMs` prevent the page's poll loop (or
  external clients) from hammering the JSON build; the page retries on next refresh.

---

## 9. i18n keys

`btnHome`, `homeSince` ("since July 2025" footer line), `homeUsbServer`,
`homeEmailCfg`, `homeGoogleScript`, `filesBtnRefresh`, plus `status*` keys: `statusUpdating`, `statusNoData`,
`statusOnline`, `statusOffline`, `statusMeasuring`, `statusLoadFail`, `statusSynced`,
`statusSemLimite` ("no limit"), `statusActive/Disabled/InsertSD/RetrySD/RetrySDError/SdNone`,
`statusEthIdle/Lost/NoSTA/Recover/Stable`, `statusRssiExcellent/Fair/Poor`,
`statusSecGeneral/Net/RS232/MicroSD/NVS/Flash/Email`, `statusLbl*` (all field labels:
`statusLblMounted/Format/Space/Free/Used/Total/MaxSize/MinSize/FileCount/FolderCount/
Logging/LogWrites/LogWritesFlash/LogWritesTotal/Lifetime/BufUse/Type/Port/Baudrate/Pins/
XonXoff/Invert/MaxLines/Mode/Coord/Charset/ActiveTemplate/SavedTemplate/Dest/ExtraEmails/
EmailSend/CopySender/Server/Tls/User/Subject/Email/Port/Reconn/Canal/Rx/Buffer/...`),
`statusNvsWarning/Critical/Action1/2/3/NvsActionTitle`, `statusLblTelnet/TelnetTimeout/
Timeout/TimeoutActive/TimeoutInactive`, `statusLblFreeEst/SpaceEst`.

---

## 10. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| 1 | **Section headers/module lines hardcoded English** inside `M`/`L` pushes (not `data-i18n`) — **fixed:** 22 section headers routed through `_st` with `stSec*` i18n keys (EN+PT). Module feature lines remain English-only. | Medium | **fixed** (headers) |
| 2 | **`opMap` in the monitoring render is ad-hoc** (`{'I':'==','R':'REGEX','DR':'≠','RP':'R/≠','12':'num.','001 020..040':'prefix'}`) — doesn't match the actual operator set documented in `recipients.md` (I/D/+/−/G/L/R/DR/V/C/P/Cw/Hy); labels can be wrong. | Low | Align opMap with `isValidRecipientOp`. |
| 3 | `#r` refresh simply re-runs `u()`; a 429 (rapid refresh) shows stale data silently (the fetch still resolves with the 429 body → `j` non-null but `{error}` → renders garbage). | Low | Handle `j.error === 'too_many_requests'` gracefully. |
| 4 | Single 12 KB JSON rebuilt each request — payload is large for a "quick glance" page; consider split endpoints or ETag on the JSON. | Info | Optional perf. |

---

## 11. Source map

| Concern | File |
|---|---|
| Renderer (HTML+JS inline) | `chatgpt/src/webpages/renderers/status_page_renderer.{h,cpp}` |
| Route (WiFi) | `chatgpt/src/webpages/routes/web_routes_monitor.cpp:446` |
| Ethernet page | `ethernet_bootstrap_routes.h:201`, `web_shared_pages.cpp:99` |
| `/api/status` handler | `chatgpt/src/webpages/handlers_unified/core_admin_handler.cpp:28` |
| Status payload builder | `chatgpt/src/system_status_payload.h` (`fillSystemStatusJson`) |
| Self-test manifest | `test_manifest_handler.cpp` (`PAGE("/status", …)`), `selftest_handler.cpp` |