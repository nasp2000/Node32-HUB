# Plano de Teste Real — Pack GcodeSender-P4 / CNC (fw .7)

> Dono: Joana (execução) | Autonomia: Nuno
> Board: ESP32-P4 (Waveshare ESP32-P4-Module-DEV-KIT), IP `192.168.1.7`, fw `.7`.
> ⚠️ **ESTADO DA BOARD: OFF** (conforme memory: ".7 (OFF, ver antes OTA)").
> Regras-base (anexo `issues (2).txt`): testar TUDO por api/curl/persistência/save/reboots,
> todos os botões e lógica de cada página, com reboots pelo meio.
> **EXCEÇÃO:** NUNCA mexer/apagar config de **Wi-Fi** (se o pack tiver wifi/email).
> Este pack NÃO tem SMTP/Email nem PAGE_ACCESS — logo essas exceções não se aplicam.
> Falhas → não resolver sozinha; escrever o que precisa de correção. Divergências → documentar.

## 0. Setup de execução
- `export BOARD_AUTH="<user>:<pass>"` (NÃO hardcoded aqui).
- Base curl: `curl -s -u "$BOARD_AUTH" http://192.168.1.7`.
- ⚠️ **BOARD OFF:** este plano está documentado mas a EXECUÇÃO depende de ligar a board
  (tomada/relé) e confirmar que sobe (ping / `GET /api/status`). Antes de OTA, confirmar
  alvo por mDNS ou `GET /api/status` (regra memory: "SEMPRE confirmar alvo antes de OTA").
- **Snap de defaults:** `GET` de cada secção → `P4_DEFAULTS_<data>.json`; repor no fim.
- ⚠️ Flag WIFI: `pack_p4_cnc.h` NÃO define `ENABLE_WIFI` explicitamente → herda default do
  build. Confirmar na página `/status` (`feature_wifi` on/off) antes de assumir acesso IP.
  Se não houver wifi nem ethernet, a board só é acessível via USB/serial (teste limitado).

### Flags ativas neste pack (de `pack_p4_cnc.h`)
USB_HOST (OTG20 HS via CH334F → Type-A), GRBL, SDCARD, ASYNC_WEBSOCKET (WS status ~100 ms),
DEBUG_LOGS. **OFF:** TELNET, PAGE_COMMANDS, PAGE_RECIPIENTS, PAGE_ALERTS, PAGE_SDCARD,
SMTP/GOOGLE_EMAIL, LCD_SCREEN.
Storage: SD presente (SDMMC4) → upload até 50 MB/file; PSRAM fallback (16 MB VFS, 12 MB
G-code max) quando sem SD. PSRAM VFS é volátil (perde no reboot).

---

## 1. Página — GRBL Controller (`/grbl-controller`)  [CENTRAL deste pack]
**Ativa?** Sim (`ENABLE_GRBL`, route/API gate para packs CNC). Fonte: `PAGES/grbl_controller.md`.

### Endpoints (curl) — todos `/api/grbl/*`
- `GET /api/grbl/status` (WS ~100 ms se disponível, senão REST poll ~500 ms).
- `GET /api/grbl/debug`, `GET /api/grbl/control-token` (session gate), `GET /api/grbl/events` (SSE, não usado).
- `GET /api/grbl/list-gcode` · `frame-bbox` · `psram-file` · `psram-read-text` · `read-gcode-text` (≤50 MB).
- `POST /api/grbl/usb-restart` · `transport` · `usb-line-coding` · `auto-detect-baud` (morto).
- `POST /api/grbl/send` (comando G-code/macro) · `override` (feed/spindle).
- `POST /api/grbl/stream` (stream file SD/PSRAM) · `pause` `/resume` `/cancel`.
- `POST /api/grbl/create-dir` `/delete-path` (SD fs) · `psram-stream` `/psram-clear` (PSRAM fs).
- `POST /api/grbl/upload-gcode-stream` (**SD upload** ≤50 MB, overwrite guard) ·
  `psram-upload` (**PSRAM** ≤12 MB).
- `GET/POST /api/grbl/probe-grid` · `probe-grid-cancel` · `GET /api/grbl/height-map` ·
  `POST /api/grbl/height-map-enable` `/height-map-clear`.
- `POST /api/grbl/queue-start` `/queue-stop` (mortos) · `poll-interval` (morto, sem UI).
- WebSocket: `ws://.../api/grbl/ws` (via `initWsServer`, `ws_handler.h`).

### Botões / lógica (widgets dockable, dark theme)
- Transport selector (USB/serial), Baud select, Jog pad + step/distance, Feed override.
- Pause / Resume / Cancel, Probe auto-zero, Probe grid + height map.
- SD panel: list/upload/load/stream; Command box (raw G-code/macro); Macro templates.
- Sim scrub / play / stop (timeline através do preview parseado).
- Preview 2D (top/iso/side) + 3D + contour/light; widget dock/undock (localStorage por browser).

### Persistência
- Widget layout/dock → **browser localStorage** só.
- Board settings → ConfigManager `config["grbl"]` (ex: `log_enabled`) + SD auto-cleanup.
- **Nenhuma escrita page-triggered a NVS/LittleFS.** Uploads persistem em **SD** (ou PSRAM VFS para o path PSRAM — volátil).

### Reboot pelo meio
- Upload de um ficheiro G-code para SD (≤50 MB), reboot → confirmar que o ficheiro persiste em SD.
- ⚠️ Se testar PSRAM upload (sem SD): o ficheiro **perde-se no reboot** (esperado, volátil) — documentar.
- GRBL state (machine position, settings) persiste em NVS da GRBL, não no nosso config — verificar que sobrevive.

### Divergências conhecidas (do doc — NÃO corrigir, só documentar)
- GRBL-1: mojibake fallbacks (`âœ…`/`â€œ`) em `wtSafe`/`showAlert`.
- GRBL-2: `\u{…}` escapes literais em HTML (emoji rotate-warning sem data-i18n).
- GRBL-3: 28 i18n keys em falta em ambos os lang files.
- GRBL-4: DOM refs órfãos (`grbl_poll_ms`, `btn_apply_poll`, `grbl_step`, backplot controls).
- GRBL-5: APIs registadas-mortas (`queue-start/stop`, `events` SSE, `auto-detect-baud`, `probe-grid` GET, `height-map` GET, `debug`).
- GRBL-6: `setCommandMode('psram-stream')` não está no mode map → badge fica MANUAL.
- GRBL-7: `poll-interval` endpoint órfão (sem input UI).
- GRBL-8: em packs não-CNC com ENABLE_GRBL, página serve mas `/api/grbl/*` → 404 (pack-gated).

---

## 2. Página — Status (`/status`)
**Ativa?** Sim (sempre, via PAGE_MONITOR). Fonte: `PAGES/status.md`.
- `GET /api/status` (429/500 ms). Confirmar features: USB_HOST on, GRBL on, SDCARD on,
  ASYNC_WEBSOCKET on, DEBUG_LOGS on; SMTP/PAGE_ACCESS/ALERTS/RECIPIENTS off; firmware=.7.
- ⚠️ Confirmar flag WIFI/ETHERNET (ver secção 0).

---

## 3. Página — Serial Monitor (`/serial-monitor`)
**Ativa?** Sim (`ENABLE_PAGE_MONITOR`, DEBUG_LOGS on → logs verbose úteis aqui).
- `GET /api/serial-monitor`, `POST /api/monitor-open/close`, `GET /api/received-text`,
  `POST /api/clear-buffer`, `POST /api/system-command`.

---

## 4. Reboots — procedimento
- Board OFF → ligar primeiro. Reboot via endpoint (confirmar `/api/restart` vs `/api/reboot` NO-OP)
  ou power-cycle. Após reboot: aguardar, `GET /api/status`, re-testar.
- **USB Host:** ao ligar um dispositivo GRBL (ex: Arduino Nano via CH340G) na Type-A atrás do
  CH334F hub, confirmar que o `usb-host` deteta e GRBL comunica (OTG20 HS UTMI).
  ⚠️ Requer hardware ligado — teste de integração, não só curl.

## 5. Relatório final (entregar ao Nuno)
Por página: endpoints ✓/✗, botões/lógica ✓/✗, persistência ✓/✗, reboots feitos,
defaults repostos, divergências/falhas (listadas, não corrigidas), ideias.
⚠️ Se a board continuar OFF durante o teste, o relatório diz explicitamente "não executado —
board off" e lista o que falta fazer.

## 6. Fora de escopo
- Waveshare (.29) e Harmonica/RADIO (.30) → planos próprios.
- Build/OTA → só se o Nuno pedir explicitamente (e sempre confirmar alvo antes de OTA).
