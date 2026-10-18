# System Settings — form on `/printer`

> One-stop reference for the **System Settings** part of the printer page: device
> name, mDNS, language, timezone/NTP. It posts to `/api/config` with
> `section=system` and is persisted in `/config/system.json`.

## 1. Objective

Device-level settings central to the hub identity and scheduling, editable from
one form: network name (`system_name`), mDNS/Bonjour advertisement
(`mdns_enabled`), UI/email language (`system_language`), local timezone
(`timezone`) and NTP server (`ntp_server`). All values must survive a reboot.

## 2. Access & routing

- Rendered as part of the **Printer** page: route `GET /printer`
  (`web_routes_core.cpp:229` → `printer_page_renderer.h`).
- The form HTML lives in `page_printer_asset.cpp` (`#printer-form`). It POSTs to
  `/api/config` with a hidden `section=system`.
- Read-back: `GET /api/config?section=system` (also present in the page's
  `loadCurrentConfig()` JS).
- Handler: `chatgpt/src/webpages/handlers_unified/config_handler.cpp`
  (`handleConfigPost` + the `section == "system"` GET branch).

## 3. Layout

Single form (`page_printer_asset.cpp:10`):

| Field | Control |
|---|---|
| `system_name` | text input (`id=system_name`), placeholder `Node32-HUB`, 100% width |
| `mdns_enabled` | checkbox `id=mdns_enabled` + hidden `mdns_enabled_present=1` |
| `system_language` | select `id=system_language` (`pt`/`en`) |
| `timezone` | select `id=timezone_select` |
| `ntp_server` | select `id=ntp_server` |

Followed by the printer-emulation section (out of scope here).

## 4. Inputs

- `system_name` — free text, server strips control chars.
- `mdns_enabled` — checkbox value `1` when checked (absent when unchecked).
- `system_language` — `pt` or `en`; on change the page calls `setupLanguage()`.
- `timezone` — POSIX timezone string (e.g. `WET0WEST,M3.5.0/1,M10.5.0`).
- `ntp_server` — hostname (e.g. `pool.ntp.org`).

## 5. Buttons

One **Save** action via the form submit. The form JS sets
`fd.set('section','system')` before POSTing.

## 6. Client-side logic (inline JS in `page_printer_asset.cpp`)

- `loadCurrentConfig()`: `GET /api/config?section=system` fills
  `timezone_select`, `ntp_server`, `system_name`, `mdns_enabled` checkbox and
  `system_language`. mDNS read default: `true`.
- `updateMdnsAddr(name)`: live gadget mapping `system_name` → `.local` address.

## 7. Server API — `/api/config`

### `GET /api/config?section=system`

`config_handler.cpp:184-187` returns:
`system_name`, `system_language`, `mdns_enabled` (from `getMdnsEnabled()`,
default `true` when unset) and an `ntp` object with `server`/`timezone`
(read from the NtpManager in-memory values).

### `POST /api/config` (`section=system`)

Applies (only fields present in the body):

| Field | Setter | Notes / fixes |
|---|---|---|
| `system_name` | `setSystemName()` | control chars stripped |
| `mdns_enabled` | `setMdnsEnabled()` | **was gated on hidden marker `mdns_enabled_present`** — direct API calls without the marker silently lost the value (reboot → default `true`). **Fixed 2026-08-30:** the key `mdns_enabled` itself (or the legacy marker) triggers the branch; a marker-only body with no value no longer disables mDNS. `config_handler.cpp:245-255` |
| `system_language` | `setSystemLanguage()` + `setupLanguage()` | |
| `timezone`/`ntp_server` | `setNtpTimezone()`/`setNtpServer()` → `ntpManager.set…` | mark the NtpManager dirty only |
| `emulation|tput|…` | printer fields | |

After applying, the handler:
- calls `configTzTime(timezone, server)` when NTP was touched, and now also
  **`ntpManager.save()`** — before this fix no code ever called `save()`
  outside `begin()`, so timezone/server reverted to defaults after reboot.
- `saveSection("system")` for the section.
- if `telnet`/`unified_password` fields were present, also saves `telnet`/`wifi`.

## 8. Persistence (ConfigManager)

`/config/system.json` — written atomically by `saveSection("system")`, restored
in `loadFromLittleFS()` from `systemPageDoc` (kept in the final config-import
zone alongside the alerts import — see the pool-relocation note in
`inactivity_alerts.md`).

File format (top level):

```json
{
  "printer": { … },
  "system_name": "NASP-HUB",
  "system_language": "pt",
  "mdns_enabled": false,
  "system": {
    "serial_monitor_enabled": false,
    "heartbeat_enabled": true,
    "charts_page_enabled": true,
    …
  }
}
```

**Before 2026-08-30** `saveSection("system")` wrote only
`{printer, system_name, system_language}` and the load restored only those, so:
- `mdns_enabled` was never persisted (the `.29` failure `system.mdns_enabled`);
- the `config["system"]` subtree (`serial_monitor_enabled`, `heartbeat_*`,
  `charts_page_enabled`, `charts_exempt_sessions`) was only partially restored
  from `access.json`, so the system page fields did not survive.

**Fixed:** the save now adds `mdns_enabled` and a full `system` object
(filtered by `isSafeConfigJsonKey`), and the load restores both. Buffers for
this file follow the 2× worst-case rule: `sysDoc` and read-back
`systemPageDoc` are now 8192.

Registers/DBs: getters read top-level `config["system_name"]`,
`config["mdns_enabled"]` (default `true`), `config["system_language"]`,
and the `config["system"]` subtree.

## 9. Runtime engine

- mDNS: `setupMdns()` registers `<system_name>.local` when `mdns_enabled`
  (default on).
- Timezone/NTP: `setupNtp()` on boot; `ntpManager.begin()` seeds
  `/config/ntp.json` on first boot; `NtpManager::save()` writes the same file
  atomically (`.tmp` → validate bytes>0 → rename) so page edits persist now.
- Language: `setupLanguage()` reloads the active language pack.

## 10. i18n keys used on this page

**Added (full PT/EN coverage, `lang_en_ui.h` + `lang_pt_ui.h`):** 1 key(s) — `settSysNameTitle`. HTML fallback text is English; PT is served from the packs.

UI (`data-i18n`): `printerTipSysName`, `printerTipLang`, `printerTipTimezone`,
`printerTipNtp`, plus the shared `btnHome`/`loading`/save strings of the printer
page. Runtime strings: `cfgSaved` family from `config_handler.cpp`.

## 11. Review findings & improvement ideas

| # | Finding | Severity | Status |
|---|---|---|---|
| 1 | **mDNS toggle lost on direct API saves (`.29`).** Handler only read `mdns_enabled` when the hidden marker `mdns_enabled_present` was present; harness/direct calls without it lost the value. **Fixed:** key presence (or legacy marker) triggers the branch; value guarded so marker-only bodies don't force mDNS off. | High | **fixed** |
| 2 | **`mdns_enabled` never written to disk.** `saveSection("system")` wrote only printer/name/language; default `true` reappeared after reboot. **Fixed:** save+load now cover `mdns_enabled` + the full `config["system"]` subtree. | High | **fixed** |
| 3 | **Timezone/NTP never persisted (`.29`).** Setters only dirty the NtpManager; `ntpManager.save()` was never called outside `begin()`. **Fixed:** `handleConfigPost` calls `ntpManager.save()` when NTP touched; round-trip proven by `test_ntp_persist_real`. | High | **fixed** |
| 4 | **Save buffers < 2× worst case.** `sysDoc`/`systemPageDoc` were 4096 (now 8192); `recipDoc` 4096→8192, `schedDoc` 2048→4096, `saveSection("recipients"/"schedule")` write cap 4096→8192. | Medium | **fixed** |
| 5 | Checkbox loads mDNS read-only from the GET payload; a reboot with mDNS off and no POST leaves `mdns_enabled` unset → GET shows `true`. Expected as long as the toggle default is on. | Low | open |

## 12. Source map

| Concern | File |
|---|---|
| Page form + JS | `chatgpt/src/webpages/assets/page_printer_asset.cpp` |
| Page renderer | `chatgpt/src/webpages/renderers/printer_page_renderer.{h,cpp}` |
| Config handlers | `chatgpt/src/webpages/handlers_unified/config_handler.cpp` |
| Routes | `web_routes_core.cpp` (`/printer`), shared `/api/config` registration |
| Config storage | `chatgpt/src/ConfigManager.{h,cpp}` (`saveSection("system")`, system import in `loadFromLittleFS`) |
| NTP persistence | `chatgpt/src/NtpManager.{h,cpp}` (`/config/ntp.json`) |
| Native tests | `tests/native/test_system_persist_real.cpp`, `tests/native/test_ntp_persist_real.cpp` |