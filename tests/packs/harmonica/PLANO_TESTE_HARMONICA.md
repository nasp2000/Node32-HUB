# Plano de Teste Real — Pack Harmonica Box / RADIO (fw .30)

> Dono: Joana (execução) | Autonomia: Nuno
> Board: ESP32-S3 N16R8 "Harmonica Box", IP `<board-ip>`.
> Regras-base (anexo `issues (2).txt`): testar TUDO por api/curl/persistência/save/reboots,
> todos os botões e lógica de cada página, com reboots pelo meio e saves em config.
> **EXCEÇÃO:** NUNCA mexer/apagar config de **Wi-Fi** (perde-se acesso IP).
> Este pack NÃO tem Email/SMTP (logo não há exceção email — nem página existe).
> Ao terminar: repor valores por omissão (os que estavam antes) em TODOS os outros campos.
> Falhas → não resolver sozinha; escrever o que precisa de correção. Divergências/ideias → documentar.

## 0. Setup de execução
- `export BOARD_AUTH="<user>:<pass>"` (lido de onde o Nuno indicar; NÃO hardcoded aqui).
- Base curl: `curl -s -u "$BOARD_AUTH" http://<board-ip>` (WiFi-only; sem Ethernet neste pack).
- **Snap de defaults:** antes de tocar em algo, `GET` de cada secção e guardar originais
  em `HARMONICA_DEFAULTS_<data>.json` (local). Repor no fim.
- ⚠️ Diferente do Waveshare: **não há `/monitor`** (route removida no pack) — só `/serial-monitor`.
  Confirmar que `GET /monitor` → 404 e `/serial-monitor` funciona.

### Flags ativas neste pack (de `pack_radio.h`)
WIFI, WIFI_WEB_SERVER, NTP, OTA, HTTP_BASIC_AUTH, FILESYSTEM, SDCARD, PAGE_MONITOR,
PAGE_FILES, UDA1334A (I2S DAC). **OFF:** ETHERNET, SMTP/GOOGLE_EMAIL, PAGE_ACCESS,
PAGE_SDCARD, PAGE_COMMANDS, PAGE_RECIPIENTS, PAGE_ALERTS, PROXIMITY, RS232/DB25/MODBUS/CAN,
sensors, CAMERA, TELNET, SCHEDULER, LCD_SCREEN.

---

## 1. Página — Radio UI (`/radio-ui`)  [CENTRAL deste pack]
**Ativa?** Sim (`ENABLE_UDA1334A` + pack==RADIO). Fonte: `PAGES/radio_ui.md`.

### Endpoints (curl) — todos `/api/radio/*`
- `GET /api/radio/status` (poll 5 s) → estado completo.
- `GET/POST /api/radio/queue` (GET cap 120, POST até 256).
- `GET/POST /api/radio/favorites` + `/add` `/remove` `/play`.
- `POST /api/radio/stop` `/next`.
- `POST /api/radio/volume` (0–100) `/seek` (0.0–1.0, SD só).
- `POST /api/radio/play-url` `/play-sd`.
- `GET /api/radio/artwork` (binário).
- `GET /api/radio/search` (SD, ≤50) · `GET /api/radio/library` + `/library/art`.
- `GET /api/radio/stations/search` (radio-browser proxy).
- `GET/POST /api/radio/playlists` + `/playlist/save` `/load` `/delete`.
- `POST /api/radio/shuffle` `/repeat` `/sleep` `/eq`.
- `GET/POST /api/radio/resume`.
- `GET/POST /api/radio/schedule` (agenda, slots até 24).
- `POST /api/radio/upnp/discover` `/servers` `/browse`.

### Botões / lógica (9 painéis: Lib/Rad/Rec/Fav/Play/Pl/Disc/Agd/Upnp)
- Play/pause, prev/stop/next, shuffle/repeat/sleep/EQ, volume, seek, play-url.
- Library: search (300 ms debounce), folder walk, play-ordered/random/add-all.
- Favorites: ★/☆ toggle. Playlists: save/load/delete (.m3u).
- Discover: genre/country/search + paging. Agenda: editor de slots (7 dias, horas/min).
- UPnP: discover → servers → browse (recursivo, max 2000/depth 8/conc 3).
- Context menu (right-click / 550 ms hold). Idle nav 30 s → Play. Portrait-lock no touch.

### Persistência (Radio NÃO usa ConfigManager — ficheiros LittleFS próprios, .tmp atómico)
- `/config/radio_queue.json` (cap 256; seeds default stations)
- `/config/radio_favorites.json` (cap 64; seeds 90s90s+80s80s)
- `/config/radio_state.json` (volume default 30, auto_resume, eq, sleep, last mode)
- `/config/radio_schedule.json` (slots[{id,days,type,src,name,vol,enabled}])
- `/playlists/*.m3u` em SD · Recent = `localStorage["radio_recent"]` (browser, max 50)

### Reboot pelo meio
- Tocar uma estação (play-url), ajustar volume, criar favorito, reboot → confirmar:
  volume persistiu (radio_state), favorito persistiu (radio_favorites), queue persistiu.
- Agenda: criar 1 slot, reboot, confirmar que persistiu e dispara no dia/hora certos.

### Divergências conhecidas (do doc — NÃO corrigir, só documentar)
- RADIO-2: `STATIONS` hardcoded no cliente vs `/api/radio/stations/default` (endpoint morto).
- RADIO-3: endpoints mortos (`/toggle`,`/random`,`/i2s-poc`,`/mp3-test`,`/stations/tags|countries|default`, GET `/eq`).
- RADIO-4: handlers Ethernet mortos (pack tem ETHERNET=0 anyway).
- RADIO-6: volume/shuffle/repeat/seek swipe erros silenciosamente (.catch vazio).
- RADIO-8: GET queue cap 120 vs POST 256 (drift). RADIO-10 já fixo (minutes playing monotonic).
- ⚠️ **Teste de integração dependente de rede:** play-url precisa de internet; se a box
  estiver offline, testar só SD-local (precisa de cartão SD com música). Documentar se não testável.

---

## 2. Página — Files (`/files`)
**Ativa?** Sim (`ENABLE_PAGE_FILES` + SDCARD). Fonte: `PAGES/files.md` (igual ao Waveshare).

### Endpoints / botões / lógica / persistência
- Idêntico ao pack Waveshare secção 6 (navegação sd/flash/psram, CRUD, mount/unmount).
- **Diferença:** neste pack NÃO há `PAGE_SDCARD` (config page), mas `/files` funciona com SD.
- Session guard max 3 (503 "Explorer Busy").

### Reboot pelo meio
- Criar pasta de teste em SD, reboot, confirmar persiste. **Repor:** apagar no fim.

---

## 3. Página — Wi-Fi (`/wifi`)
**Ativa?** Sim (`ENABLE_WIFI`) — é a ÚNICA interface de rede deste pack (sem Ethernet).
### ⚠️ EXCEÇÃO DO ANEXO: NÃO modificar/apagar config de Wi-Fi.
- Teste permitido: `GET /api/config?section=wifi` (confirmar intacta no fim),
  `GET /api/wifi-scan` (listar, não altera), `GET /api/wifi-runtime-status`.
- NUNCA `POST /api/config` section=wifi com novo SSID/pass (perde-se o acesso IP da box).
- Se a box só tem WiFi, mexer na config wifi = perder acesso total. **Máximo cuidado.**

---

## 4. Página — Serial Monitor (`/serial-monitor`)  [e NÃO `/monitor`]
**Ativa?** Sim (`ENABLE_PAGE_MONITOR`). Fonte: `PAGES/monitor.md`.
- `/serial-monitor`: search/filter/pause/clear/copy/export, auto-poll 3 s, send cmd.
  Endpoints: `GET /api/serial-monitor`, `POST /api/monitor-open/close`, `GET /api/received-text`,
  `POST /api/clear-buffer`, `POST /api/system-command`.
- ⚠️ **`/monitor` route REMOVIDA no pack** → `GET /monitor` deve dar **404** (confirmar).
  O Data Reception buffer só está acessível via `/serial-monitor` neste pack.

---

## 5. Página — Status (`/status`)
**Ativa?** Sim. Fonte: `PAGES/status.md`.
- `GET /api/status` (429/500 ms). Confirmar features do pack: WIFI on, ETHERNET off,
  SMTP off, UDA1334A on, PAGE_ACCESS/SDCARD/ALERTS/PROXIMITY off. firmware=.30.

---

## 6. Reboots — procedimento
- Reboot via endpoint (`/api/restart` se existir; `/api/reboot` é NO-OP em alguns packs —
  confirmar qual). Ou desligar/ligar tomada.
- Após reboot: aguardar ~10 s, `GET /api/status`, re-testar secção.

## 7. Relatório final (entregar ao Nuno)
Por página: endpoints ✓/✗, botões/lógica ✓/✗, persistência ✓/✗, reboots feitos,
defaults repostos, divergências/falhas (listadas, não corrigidas), ideias.

## 8. Fora de escopo
- Waveshare (.29) e P4 CNC (.7) → planos próprios.
- Build/OTA → só se o Nuno pedir.
