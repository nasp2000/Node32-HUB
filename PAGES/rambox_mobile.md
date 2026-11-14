# Rambox32 mobile page — `/m`

> Phone page of the Rambox32 pack (Bricon / Unikon clock + RS232). `GET /rambox`
> on a phone redirects here (302, `?desk=1` escape); the desktop page is untouched.

## 1. Objective

See the clock, the arrivals, the loft registrations and the races, switch between
Bricon and Unikon, tune the polling and the RS232 port, and reach the shared
System screen — all from the phone.

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /m` (`web_routes_rambox.cpp`, `ENABLE_UNIKON` or `ENABLE_BRICON`) |
| Entry | `GET /rambox` + phone UA → 302 `/m` (`?desk=1` keeps the full page) |
| Renderer | `renderers/rambox_mobile_page_renderer.cpp` (common CSS → `PAGE_RAMBOX_MOBILE` → shared System → tail → common JS → system JS) |
| Pack name | `PACK_SYSTEM_NAME` → `window.PACK_NAME` |

## 3. Layout

Four vertical screens (no page scroll, content centred):

```
header  strip Status · Arrivals · Config · System + pack name + badge
S1 Status   tiles: clock, fancier, race, arrivals, RS232, last poll · SYNC CLOCK
S2 Arrivals sub-tabs Arrivals | Pombal | Races · ↻ reload · ✉ resend email · ✕ clear
S3 Config   Bricon/Unikon · polling ON/OFF · interval · Serial CLOCK|MANUAL ·
            RS232 ON/OFF · baud · parity · data/stop · SAVE
S4 System   shared block (summary / firmware / log / restart)
nav         ← · n / 4 · →
```

## 4. Client logic

- Status polled every 3 s **only** on the Status screen (clock + RS232).
- **Clock selection** — only one clock can be active (shared RS232): SAVE posts
  `enabled=1` for the chosen one and `enabled=0` for the other, plus the interval.
- **Polling** — `enabled` + `interval` on `/api/<clock>-config`.
- **Serial source** — each clock owns its profile (Bricon 9600 8N2, Unikon
  9600 8N1) and the RS232 fields are locked to it; **MANUAL** posts
  `manual=1` to the clock and saves the RS232 section so the module runs on our
  own framing (`manual=0` returns to the clock profile).
- **RS232** — `POST /api/config?section=rs232` with `rs232_enabled`, `baudrate`,
  `parity`, `data_bits`, `stop_bits` (only in MANUAL).
- **Resend email** — `POST /api/<clock>-email-pombal`; the device already emails
  automatically, this only asks for it again (Unikon only today).
- **Pombal / Races** — `/api/<clock>-pombal` (`registrations[]`) and
  `/api/<clock>-contests` (`contests[]`); both fall back to Unikon when Bricon is
  not compiled in.
- **Clear** — `/api/<clock>-reset` behind a confirmation.
- Shared 250 ms tap guard, swipe and strip come from `MOBILE_COMMON_JS`.

## 5. Server API

`/api/bricon-status` · `/api/bricon-arrivals` · `/api/bricon-pombal` ·
`/api/bricon-contests` · `/api/bricon-config` · `/api/bricon-poll` ·
`/api/bricon-reset` · `/api/bricon-sync-calendar` (and the `unikon-` twins) ·
`/api/rs232-status` · `/api/config?section=rs232`
+ the shared System endpoints.

## 6. Review findings

| # | Finding | Status |
|---|---|---|
| 1 | No command/MDI screen on the phone (removed by request) — RS232 commands stay on the desktop page | by design |
| 5 | Bricon has no email-resend endpoint: the button reports it instead of pretending | by design |
| 2 | Switching clock disables the other one (shared RS232) and asks for confirmation | by design |
| 3 | Only status/interval/RS232 basics are exposed; baud tables, templates and battery stay on the desktop page | by design |
| 4 | Unikon fallback keeps a Unikon-only build usable without a second page | implemented |

## 7. Source map

| Piece | File |
|---|---|
| Status/Arrivals/Config screens | `chatgpt/src/webpages/assets/page_rambox_mobile_asset.cpp/.h` |
| Shared System + common CSS/JS | `mobile_system_asset.*`, `mobile_common_asset.*` |
| Renderer | `chatgpt/src/webpages/renderers/rambox_mobile_page_renderer.cpp/.h` |
| Routes | `chatgpt/src/webpages/routes/web_routes_rambox.cpp` (`/m`, `/rambox` redirect) |
| Tests | `tests/web/test_m_rambox.js` (10), fixture `tests/web/build_m_rambox.py` |
| Review shots | `SHARE/Tarefas/anexos/Rambox32/` |
