# Modbus — `/modbus-rtu` · `/modbus-tcp` · `/modbus-live`

> Reference for the three Modbus pages: RTU master config + slave scan, TCP client
> register map, and the live values dashboard. Written for both humans and AI agents.

---

## 1. Objective

Configure and operate the **Modbus master**:

- `/modbus-rtu` — RS485/RTU global polling settings, up to 4 devices (slave id, function,
  address, type), plus a **bus scan** that auto-detects slaves.
- `/modbus-tcp` — Modbus/TCP client: server IP/port/unit-id and a register map (up to 16
  typed registers with byte-order selection).
- `/modbus-live` — read-only dashboard polling current values of all configured devices.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URLs | `GET /modbus-rtu`, `GET /modbus-tcp`, `GET /modbus-live` |
| Entry point | `/modbus-tcp` button on the **home page**, Networks section, right below **FTP Upload** (`%%FTP_BUTTON%%` handler, `home_page_renderer.h`, gated `ENABLE_MODBUS_TCP`); `/networks` (Outras Redes) is Telnet-only |
| Feature flags | `ENABLE_MODBUS_RTU` / `ENABLE_MODBUS_TCP` / `ENABLE_MODBUS`; live additionally requires section active |
| WiFi routes | `web_routes_modbus.cpp:174` (RTU), `:211` (TCP), `:246` (live) → `streamModbusRtuPageToPrint()` etc. (`modbus_renderer.cpp`) |
| Ethernet mirrors | `ethernet_bootstrap_routes.h:252/259/266` (gate `ENABLE_MODBUS`) |
| Active guard | RTU page redirects to `/` unless `modbus.enabled && modbus.rtu_enabled`; live requires `enabled && (rtu_enabled \|\| tcp_enabled)` (`web_routes_modbus.cpp:249-255`) |
| Live streaming | live page served as **chunked PROGMEM** (~25 KB) via `buildPageFrame` + `serveThreeSegments` |
| Auth / caching | `ensureBasicAuth` + ETag `FIRMWARE_BUILD`, `private, max-age=15, must-revalidate` |
| Session exemption | all three pages ALWAYS exempt (local processing) — unconditional flag, never kicked by time |

---

## 3. Layout

### `/modbus-rtu`

```
main.page > div.wrap
├ nav home link · H1 "🔗 Modbus RTU" [modbusRtuTitle]
├ ℹ️ info card
├ ⚙️ Card "Configuração Global" [modbusGlobalTitle]   ← tab 1
│  ├ ☑ Enable Modbus RTU (#mb_enabled)
│  ├ Slave ID (#mb_slave_id 1–247) · Poll interval ms (#mb_poll_ms, def 1000)
│  ├ Timeout ms (#mb_timeout_ms, def 1000) · Retries (#mb_retries, def 2)
├ 🖥️ Card "Dispositivos" [modbusDevicesTitle]          ← tab 2
│  ├ div#dev-list device cards (function select 03/04…, address, count/type, name, remove)
│  └ ➕ button#add-dev "Adicionar Dispositivo" (client cap: alert at 4 devices)
├ 🔍 Card "Scan RTU — Detetar Escravos"                ← tab 3
│  ├ ▶ Start / ⏹ Stop scan buttons (#scan-start/#scan-stop)
│  ├ progress bar #scan-progress + status line
│  └ results table #scan-results (slave addr, best function, count) + per-row add button
└ 💾 "Guardar RTU" submit → POST /api/modbus/config (single JSON with all sections)
```

### `/modbus-tcp`

Same tab pattern; global card holds `tcp_enabled`, server IP, port (default 502), unit id;
devices card is a **register map**: function/address/type select with string options
`INT16, UINT16, INT32, UINT32, FLOAT32, INT64, UINT64, DOUBLE64` and byte-order select
`ABCD/BADC/CBAD/DCBA` (`page_modbus_asset.cpp:514-525`). Client caps registers at 16.

### `/modbus-live`

Standalone chunked document; `initModbusLivePage()` polls `GET /api/modbus/live`,
renders per-device cards with last value, timestamp, error state; auto-refresh toggle.

---

## 4. Inputs (RTU page)

| Input | ID | Notes |
|---|---|---|
| Enable RTU | `mb_enabled` | checkbox |
| Slave ID | `mb_slave_id` | 1–247 |
| Poll interval | `mb_poll_ms` | default 1000 |
| Timeout | `mb_timeout_ms` | default 1000 |
| Retries | `mb_retries` | default 2 |
| Device rows | dynamic | function code, start address, quantity/type, label |
| Scan range | scan form | optional start/end slave addresses for the bus scan |

TCP page adds: `tcp_ip`, `tcp_port`, `unit_id`, register `type` (**strings**) +
register `order` (**strings**) — see HIGH finding in §11.

---

## 5. Buttons

| Button | Handler | Behaviour |
|---|---|---|
| 🏠 Home | link `/` | |
| 💾 Guardar RTU | form submit | collects global+devices(+scan additions) into one object → `POST /api/modbus/config`; success alert + `showSavedCard()` |
| ➕ Adicionar Dispositivo | `#add-dev` | appends empty device row; refuses beyond 4 |
| ▶/⏹ Scan | `#scan-start`/`#scan-stop` | POST `/api/modbus/scan-rtu`, poll `/api/modbus/scan-status` every ~1 s |
| *(row)* Add from scan | per result | creates device named `Escravo N` from detected slave |
| Tab buttons | injected | labels hardcoded PT (see findings) |
| Auto-refresh | live page | toggles polling of `/api/modbus/live` |

---

## 6. Client-side logic

- Tab engine (`MODBUS_RTU_TABS_JS` / `MODBUS_TCP_TABS_JS`) locates sections by matching
  **h2 text content lowercased** against PT strings (`'configuração global'`,
  `'dispositivos'`, `'scan rtu…'`) — translation would silently break tabs.
- Devices rendered as DOM nodes from a JS array; add/remove updates array then re-renders.
- Save serializes the whole model (global + devices) into JSON body.
- Scan flow: start → poll status → render found slaves → "add" converts a result row into
  a device entry.
- Live page: interval fetch, value formatting via `toFixed(4)` on numeric payloads.

---

## 7. Server API

| Endpoint | Method | Handler | Purpose / shape |
|---|---|---|---|
| `/api/modbus/config` | GET | `modbus_handler.cpp` (`handleModbusConfigGet`) | full section incl. devices/registers arrays |
| `/api/modbus/config` | POST | `handleModbusConfigPost` (registry `web_routes_modbus.cpp:102-107`) | replaces section; saves stem `modbus`. **No server-side device-count validation** (see findings); register `type` coerced via `r["type"] \| (uint8_t)0` (`modbus_handler.cpp:271-272`) |
| `/api/modbus/scan-rtu` | POST | wrapper `web_routes_modbus.cpp:14-91` | starts background scan task (core 0, stack 4096/6144) |
| `/api/modbus/scan-status` | GET | registry | `{running,progress,found:[{slave,count,best_fn,…}]}` capped `MB_SCAN_MAX_FOUND=32` |
| `/api/modbus/scan-stop` | POST | registry | aborts scan task |
| `/api/modbus/live` | GET | registry | snapshot of last polled values per device/register |
| `/api/modbus/stats` | GET · `/reset-stats` POST | registry | poll counters/errors |

Limits live in `modbus_core.h:76-77`: `MODBUS_MAX_DEVICES = 4`,
`MODBUS_MAX_REGISTERS = 16` — enforced by UI only; runtime drops extras silently
(`modbus_runtime.cpp:32,60`).

---

## 8. Persistence

| Data | Where |
|---|---|
| Whole `modbus` section (global + devices/registers) | LittleFS `/config/modbus.json` (stem `MODBUS`) via `saveSection("modbus")` |
| Runtime cache | rebuilt from section at boot / after save (`modbus_runtime.*`) |

---

## 9. Runtime engine

- RTU master task polls configured devices round-robin honoring poll interval, timeout and
  retries; results stored per device for `/api/modbus/live`.
- Scan runs as its own pinned task (core 0) probing slave ids/functions.
- TCP client connects per poll cycle when `tcp_enabled`.
- Saving re-initializes the runtime cache; pages redirect to `/` while feature inactive.

---

## 10. i18n keys

**Firmware-side i18n (also added):** the API messages above are no longer hardcoded Portuguese in the handler — they now come from the firmware pack (`LangStrings`, `L(activeLang->…)`) and follow `system_language`. Page `<title>` and tab labels follow the same rule.

**Added (full PT/EN coverage, `lang_en_ui.h` + `lang_pt_ui.h`):** 9 key(s) — `mbBaudLabel`, `mbColAction`, `mbEngUnitHint`, `mbGlobalTitle`, `mbParityHint`, `mbParityOdd`, `mbSlaveAddrLabel`, `mbSlaveAutoHint`, `mbStartIdLabel`. HTML fallback text is English; PT is served from the packs.

Page headers/buttons use `data-i18n` keys (`modbusRtuTitle`, `modbusGlobalTitle`,
`modbusDevicesTitle`, scan labels, save button, …). However **most inline JS strings are
hardcoded PT** ("Guardar RTU", "Adicionar Dispositivo", "A varrer…", "A iniciar scan…",
"Remover dispositivo…", "Máximo 4 dispositivos", "Escravo N", …) and the raw HTML
fallbacks are PT as well — EN-fallback rule violated throughout (see findings).

---

## 11. Review findings & improvement ideas

| # | Finding | Pri | Suggestion |
|---|---|---|---|
| 1 | **HIGH — TCP register types corrupted on save:** page sends `type`/`order` as strings (`"FLOAT32"`, `"ABCD"`); handler coerces `r["type"] \| (uint8_t)0` → always `0`; runtime parses uint8 (`modbus_runtime.cpp:56`). Any save from `/modbus-tcp` silently collapses every register to UINT16/ABCD. | High | Map string→enum both sides (or send numerics). |
| 2 | `POST /api/modbus/config` accepts unbounded devices/registers; runtime clamps at 4/16 by silently dropping extras. | Medium | Validate counts in handler, return explicit error. |
| 3 | Hardcoded PT across HTML fallbacks, alerts/confirms and tab labels; tab engine matches h2 text (`'configuração global'`…) so translating headers breaks tab wiring. | Medium | English fallbacks + match sections by id/class, not translated text. |
| 4 | Live endpoint formats values with fixed `dtostrf(…,1,4)`/`toFixed(4)` regardless of the `pretty` setting — decimal-heavy noise for INT registers. | Low | Honor pretty flag / format by register type. |
| 5 | `mbWrSubmit` write-dialog falls through silently if `customConfirm` is undefined (dead branch — nothing executes). | Info | Call runWrite directly as fallback path. |
| 6 | Scan results capped at 32 without UI notice; dense buses truncate silently. | Info | Surface truncation hint. |
| 7 | Scan tasks pinned core 0 with modest stacks (4096/6144) — deep frames could starve other core-0 work. | Info | Document/monitor high-water mark. |

---

## 12. Source map

| Concern | File |
|---|---|
| HTML/JS assets (PROGMEM fragments) | `chatgpt/src/webpages/assets/page_modbus_asset.cpp` (RTU/TCP/LIVE) |
| Renderer | `chatgpt/src/webpages/renderers/modbus_renderer.cpp` |
| WiFi routes | `chatgpt/src/webpages/routes/web_routes_modbus.cpp:174,211,246` (+ API wrappers `:14-107`) |
| Ethernet mirrors | `chatgpt/src/ethernet_bootstrap_routes.h:252,259,266` |
| Config handlers | `chatgpt/src/webpages/handlers_unified/modbus_handler.cpp` (748 lines) |
| Limits / enums | `chatgpt/src/modbus_core.h:76-77` |
| Runtime polling/scanning | `chatgpt/src/modbus_runtime.cpp` (cache build `:32`, reg parse `:56`, drop-extra `:60`) |
| Persistence | `ConfigManager.cpp` stem `MODBUS` → `/config/modbus.json` |
