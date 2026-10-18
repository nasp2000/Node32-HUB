# Radio UI — `/radio-ui`

> One-stop reference for the **Harmonica Box radio/audio page**: purpose, layout, panels,
> every button/input, client logic, API endpoints, persistence and the firmware audio
> engine. Written for both humans and AI agents.

---

## 1. Objective

Full-screen touch-oriented **internet radio / local music player** (I2S DAC): streams
internet stations, plays SD music library, manages queue/favorites/playlists, live station
**discovery** (radio-browser.info proxy), a **day-of-week schedule (agenda)** and **UPnP**
media browsing. Board-locked to the **RADIO pack** ("Harmonica Box", ESP32-S3 N16R8).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /radio-ui` |
| Feature flags | `ENABLE_UDA1334A` (outer gate, default `0`; enabled by `pack_radio.h`) + runtime gate `SELECTED_PACK == PACK_RADIO` |
| WiFi server | `web_routes_audio.cpp:269` — chunked 3-segment stream (`serveThreeSegments`) |
| Ethernet | page only: `ethernet_bootstrap_routes.h:395`; radio **APIs on Ethernet go through the RouteRegistry fallback** (`:806`) + 3 explicit body-accum routes (`:638-643`) |
| Home entry | `home_page_renderer.h:146` (target=_blank); file-browser bridge link |
| Auth | `ensureBasicAuth` + `allowHtmlClient` |
| Caching | `ETag`/`Cache-Control` per stream; page has no `data-i18n` (all hardcoded EN) |

Renderer (`radio_ui_page_renderer.cpp`): shell + single PROGMEM blob `PAGE_RADIO_UI`
(`page_radio_ui_asset.cpp`, 1054 lines) + auto.js footer.

---

## 3. Layout

```
div#app (grid rows auto 1fr auto)
 ├ .pg-hdr  → H1 "🎵 Harmonica Box"
 ├ .ps      → scrollable area (9 panels .pn, one visible — pnPlay on load)
 ├ .bn      → bottom nav: #nbPlay (▶) + #nbArrow (rotate nav window) + #nbWin (3 rotating buttons)
 ├ #toast   → notifications
 └ #rotOv   → portrait-lock overlay
```

Panels: `pnLib` (SD library) · `pnRad` (stations) · `pnRec` (recent) · `pnFav` (favorites) ·
`pnPlay` (now playing, default) · `pnPl` (playlists) · `pnDisc` (discover) · `pnAgd`
(agenda) · `pnUpnp` (UPnP).

### Now playing (`pnPlay`)

- `#ab` (vinyl disc) + `#plBtn` (▶/⏸) → `playToggle()`; ⏮ `radioPrev()`; ⏹ `radioStop()`;
  ⏭ `radioNext()`.
- `#shBtn` shuffle · `#rpBtn` repeat (0→1→All) · `#slBtn` sleep (0/15/30/60/90/120 min) ·
  `#eqBtn` EQ preset cycle (5-band: 80/400/1k/4k/12k).
- `#artImg`/`#artPh`/`#abWrap` artwork · `#pbSt`/`#pbTi`/`#pbAr`/`#pbMeta` (STREAMING|LOCAL
  FILE) · `#viz` fake equalizer · `#prTr`/`#tCur`/`#tTot`/`#prFl` progress · `#volBar`/
  `#vLbl`/`#volFill` volume · `#urlInp` + Play → `playUrl()`.

### Other panels (key elements)

- **Library**: `#sInp` (SD search, 300 ms debounce → `/api/radio/search`), `#libSearch`,
  `#libSort`, `#libList`, `#libNoAvail` ("Insert SD card…"), `#libCount`.
- **Stations**: `#stList`, `#stCount`.
- **Recent**: `#recList`, `#recClear` (browser `localStorage["radio_recent"]`, max 50).
- **Favorites**: `#favList` (★/☆ inline toggle), `#favCount`.
- **Playlist**: `#plNInp` name, Save → `savePl()`, `#plList`.
- **Discover**: `#dsInp`, `#dsGenre`, `#dsCountry`, `#dsPrev`/`#dsNext` paging, `#dsList`.
- **Agenda**: `#agdList`, `#agdNext` (next slot), `#agdCount`, "+ Add slot", editor
  `#agdEdit` with `agdSh/agdSm/agdEh/agdEm` (hours/minutes), `#agdType` (url/folder/playlist),
  `#agdSrc`, `#agdUrlBox`, `#agdName`, `#agdVolBar`, 7 day buttons Su..Sa, `agdSave`/
  `agdCancel`/`agdDelEdit`.
- **UPnP**: `#upnpDisc` (re-scan), `#upnpPath` breadcrumb, `#upnpSrvList`, `#upnpItemList`,
  `#upnpBack`, `#upnpPrevBtn`/`#upnpNextBtn`.
- **Context menu** `#ctxMenu` (right-click or 550 ms hold) with per-`data-kind` items.
- Stats: `#qCount` (queue), `#agdCount`, etc.

---

## 4. Inputs / buttons summary

All transport controls POST to `/api/radio/*`; server is the single source of truth; local
`P`/`C`/`IP`/`SH`/`RP`/`SRC` are mirrors.

| Button / input | Behaviour |
|---|---|
| `#ab` / `#plBtn` | play/pause (label swapped by `updUI()`) |
| ⏮ / ⏹ / ⏭ | prev / stop / next (client-side cycle of stations/recent/fav/library/UPnP; server `/api/radio/next` only for SD queue) |
| `#shBtn` / `#rpBtn` / `#slBtn` / `#eqBtn` | shuffle / repeat / sleep / EQ (`/api/radio/shuffle`, `/repeat`, `/sleep`, `/eq`) |
| `#volBar` / vol ± | `setVol()` → `/api/radio/volume` (3 s poll suppression) |
| `#prTr` | `seek()` → `/api/radio/seek` (SD only) |
| `#urlInp` + Play | `playUrl()` → `/api/radio/play-url` |
| Library folder items | `play-sd` (ordered/random/add-all via `queue=1`); search box |
| `#resumeToggle` / `#resumeTarget` | auto-resume on/off + Resume Last/Playlist (`/api/radio/resume`) |
| Playlist Save / items | `savePl` / load / delete (`/api/radio/playlist/*`); SD `.m3u` |
| `favToggle` | add/remove/play favorite (`/api/radio/favorites/*`) |
| Discover search | `dsSearch()` → `/api/radio/stations/search`; paging; genre/country selects |
| Agenda editor | `agdSave` POSTs full `{"slots":[...]}` as text/plain → `/api/radio/schedule` |
| UPnP | discover → poll servers → browse/paginate; recursive URL collect (max 2000, depth 8, concurrency 3) |

---

## 5. Client-side logic

- **Poll**: `setInterval(loadSt, 5000)` → `GET /api/radio/status`; repaints volume, title,
  artist, active/shuffle/repeat/queue-index/sleep/EQ/art/progress/library availability.
- **Boot**: `renderNav(); sw('Play'); dsFillSelects(); renderStations(); renderRecent();
  loadQ(); loadPls(); loadFav(); loadLib(); loadResume(); loadSt(); agdBind(); agdLoad();`
- **Queue sync**: `syncQ()` POSTs `{items,index}` JSON to `/api/radio/queue` after every
  local mutation (`qAdd/qUp/qDown/qPlay/rmQ`).
- **Library**: `loadLib()` lists `/api/radio/library?path=`; folder walk via `libEnter`/
  `libUp`; folder context menu play-ordered/random/add-all (`queue=1`).
- **Discover**: 300 ms debounce, sequence guard (`DS_SEQ`); format gate `chkSup()` only on
  `dsPlay` (whitelist `.mp3/.flac/.aac/.m4a/.wav`).
- **Agenda**: `agdLoad()` GETs `/api/radio/schedule`; editor posts the whole slot array.
- **UPnP**: discover (POST) → poll servers; browse with `start/count` paging + breadcrumb
  stack; recursive collect.
- **Idle nav**: 30 s inactivity auto-returns to Play tab; **portrait-only** on touch
  devices (`rotLock`). No WebSocket — all REST `fetch()`.

---

## 6. Server API (all `/api/radio/*`)

Handlers: `handlers_unified/audio_handler.cpp` (gated `ENABLE_UDA1334A` + `_isRadioPackActive()`,
all others → 400 `"PACK_RADIO only"`). Routes: `web_routes_audio.cpp:136-172` + body-accum
`server.on` (`:175-235`). See subagent report §4 for full table; highlights:

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/radio/status` | GET | full playback state (5 s poll) |
| `/api/radio/queue` | GET/POST | queue + index (GET caps at 120, POST up to 256) |
| `/api/radio/favorites` + `/add` `/remove` `/play` | GET/POST | favorites CRUD/play |
| `/api/radio/stop` `/next` | POST | transport |
| `/api/radio/volume` `/seek` | POST | volume 0–100 / seek 0.0–1.0 |
| `/api/radio/play-url` `/play-sd` | POST | start stream / local file |
| `/api/radio/artwork` | GET | album art binary |
| `/api/radio/search` | GET | SD recursive filename search (≤50) |
| `/api/radio/stations/search` | GET | radio-browser.info proxy (limit 50) |
| `/api/radio/library` + `/library/art` | GET | folder listing / cover art |
| `/api/radio/playlists` + `/playlist/save` `/load` `/delete` | GET/POST | `.m3u` management |
| `/api/radio/shuffle` `/repeat` `/sleep` `/eq` | POST | transport modes |
| `/api/radio/resume` | GET/POST | auto-resume prefs |
| `/api/radio/schedule` | GET/POST | agenda slots |
| `/api/radio/upnp/discover` `/servers` `/browse` | POST/GET | UPnP discovery/browse |

Registered-but-unused by this page: `/api/radio/toggle`, `/random`, `/i2s-poc`, `/mp3-test`,
`/stations/tags|countries|default`, GET `/api/radio/eq`.

---

## 7. Persistence

**ConfigManager is not involved.** Radio uses its own LittleFS files with atomic `.tmp`
writes (debounced in `radioAudioRuntimeLoop`, bitmask `s_persistDirty`):

| Data | Path | Notes |
|---|---|---|
| Queue | `/config/radio_queue.json` | `{items[], index}`; cap 256; seeded default stations |
| Favorites | `/config/radio_favorites.json` | `{name,url,favicon}`; cap 64; seeds 90s90s+80s80s |
| State | `/config/radio_state.json` | volume (default 30), auto_resume/resume_first, station_favicon, eq gains, sleep_end/min, last_mode/source/active |
| Schedule | `/config/radio_schedule.json` | `slots[{id,days,type,src,name,vol,enabled}]`; days bit0=Sun..bit6=Sat; max 24 |
| Playlists | `/playlists/*.m3u` on SD | `#EXTM3U`; load sets queue + plays first |
| Recent | `localStorage["radio_recent"]` (browser) | max 50 |

---

## 8. Runtime engine

- `radio_audio_runtime.cpp` — decode (mp3/flac/aac/m4a/wav/m4b), queue auto-advance,
  artwork fetch/cache, EQ, volume, sleep timer (epoch-based, survives reboot), persist loop.
  - **Streaming elapsed (`active_ms`)** is now **monotonic**: `s_streamPlayedMs` gathers the
    played time whenever a stream segment is torn down (`radioAudioCleanupSource`), and the
    status reports `s_streamPlayedMs + current segment` for streams — so a reconnect no
    longer resets the "minutes playing" back to 0:00. A fresh user-initiated play starts a
    new session (counter reset); SD files still report per-track elapsed. These counters
    only feed the clock display and never touch the decode/I2S path.
- `radio_schedule.cpp` — agenda evaluation (day-of-week bits, start/end time) → play slot.
- `upnp_client.cpp` — SSDP discovery + DLNA browse.
- `audio_handler.cpp` — REST translation layer; `_isRadioPackActive()` runtime gate.

---

## 9. i18n keys

**None** — the page has no `data-i18n` and no lang keys; all strings are hardcoded English.
The only i18n element is the shared shell loading overlay `data-i18n='loading'` with PT
fallback.

---

## 10. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| 1 | **Hardcoded PT in shared shell / bridge / API-reference**: `loading` overlay (`A processar...`), file-browser bridge ("UI Rádio", "Erro", "falha", "Selecione ficheiros .mp3", "Sem fila ativa") and `/api/radio/random` doc row — **fixed:** all English. | Medium | **fixed** |
| 2 | **Duplicated station lists / data drift**: page hardcodes `STATIONS` (PT Antenas/Comercial/M80) while server's `/api/radio/stations/default` returns a different set; `renderStations()` never calls the endpoint → endpoint effectively dead. | Medium | Use the endpoint or remove it; keep one source of truth. |
| 3 | **Dead server endpoints**: `/api/radio/toggle`, `/random`, `/i2s-poc`, `/mp3-test`, `/stations/tags|countries|default`, GET `/api/radio/eq` — registered, no UI caller (page hardcodes genres/countries). | Low | Remove or wire. |
| 4 | **Dead Ethernet handlers**: `ethernetHandleRadio*/Upnp*` are never dispatched (Ethernet radio APIs run via RouteRegistry fallback); target pack has `ENABLE_ETHERNET 0` anyway. | Low | Delete the dead handlers. |
| 5 | **Route-registry table gaps** cause wrong 405-vs-404 for many radio paths. | Low | Extend the legacy table (or rely on registry only). |
| 6 | **Silent failures**: volume/shuffle/repeat/seek POSTs swallow errors (`.catch(function(){})`); seeking a stream returns 400 with no UI feedback. | Medium | Surface errors via `#toast`. |
| 7 | **Format gate only on Discover**: manual URL / stations / recents bypass `chkSup()` (`.m4b` playable server-side but rejected by the page whitelist). | Low | Apply the gate everywhere or remove it. |
| 8 | **Queue API truncation**: GET caps at 120 vs POST 256 → the page can show a shorter queue than real (`status.queue_count` is accurate). | Low | Align caps. |
| 9 | Client/server queue mirror can drift while the server auto-advances; reload restores from server (acceptable). | Info | Re-sync on visibilitychange. |
| 10 | **Streaming "minutes playing" stuck at 0:00** — `active_ms` was `millis()` since `s_streamStartMs`, which resets to ~0 on every stream reconnect; radios showed 0:00 while SD (single start) looked fine. **fixed:** accumulated `s_streamPlayedMs` + current segment, monotonic across reconnects. | Medium | **fixed** |

---

## 11. Source map

| Concern | File |
|---|---|
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_radio_ui_asset.{h,cpp}` |
| Renderer | `chatgpt/src/webpages/renderers/radio_ui_page_renderer.{h,cpp}` |
| Routes (WiFi) | `chatgpt/src/webpages/routes/web_routes_audio.cpp` |
| HTTP handlers | `chatgpt/src/webpages/handlers_unified/audio_handler.{h,cpp}` |
| Audio runtime | `chatgpt/src/radio_audio_runtime.{h,cpp}` |
| Stream elapsed logic test | `tests/native/test_radio_elapsed.cpp` |
| Agenda | `chatgpt/src/radio_schedule.{h,cpp}` |
| UPnP | `chatgpt/src/upnp_client.{h,cpp}` |
| Ethernet | `ethernet_bootstrap_routes.h:395,638,806`, `ethernet_bootstrap_handlers.h` (dead handlers) |
| Pack / flags | `chatgpt/packs/pack_radio.h`, `chatgpt/src/config_defaults.h`, `user_config.h` |
| Self-test manifest | `test_manifest_handler.cpp` (`PAGE("/radio-ui", ENABLE_UDA1334A)`) |