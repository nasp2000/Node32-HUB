# Plano de Teste Real — Pack Waveshare 8DI8DO (fw .29)

> Dono: Joana (execução) | Autonomia: Nuno
> Regras-base (anexo `issues (2).txt`, 2026-08-28): testar TUDO por api/curl/persistência/save/reboots,
> todos os botões e lógica de cada página, com **reboots pelo meio** e saves em config.
> **EXCEÇÕES:** NUNCA mexer/apagar config de **Wi-Fi** nem de **Email** (perde-se acesso IP / notificações).
> Ao terminar: repor valores por omissão (os que estavam antes) em TODOS os outros campos.
> Falhas → não resolver sozinha; escrever o que precisa de correção. Divergências/ideias → documentar.

## 0. Setup de execução (não é teste, é preparação)

- Board alvo: Waveshare-POE-ETH-8DI-8DO, IP `192.168.1.29`, fw `.29`.
- Auth: Basic Auth (utilizador/pass da board). **NÃO hardcoded aqui** — usar env:
  `export BOARD_AUTH="<user>:<pass>"` (lido de onde o Nuno indicar; não escrever no repo/SHARE).
- Base curl: `curl -s -u "$BOARD_AUTH" http://192.168.1.29` (ou `localhost:4096` via tunnel se a LAN isolar).
- Rate-limit conhecido: `/api/status` = 1 req / 500 ms → 429 (espaçar pedidos).
- Credenciais OpenCode Web (Nasp/Magic) são do servidor opencode, **não** da board — não confundir.
- Antes de cada página: `GET /api/status` para confirmar board viva (respeitar 500 ms).
- **Snapshot de defaults:** antes de tocar em algo, correr `GET` de cada secção de config e
  guardar os valores originais num ficheiro `WAVESHARE_DEFAULTS_<data>.json` (local, não no repo)
  para repor no fim. EXCETO wifi/email (nem ler para alterar — só confirmar que continuam intactos no fim).

---

## 1. Página — Access (`/access`)
**Ativa?** Sim (`ENABLE_PAGE_ACCESS`). Fonte: `PAGES/access.md`.

### Endpoints (curl)
- `GET /api/access-config` → ler valores (max_sessions, timeouts, auth, viewer).
- `POST /api/access-config` (form urlencoded) → testar cada campo:
  - `web_max_sessions` (1–5): testar 1, 3, 5.
  - `web_idle_timeout_min` (1–120), `web_abs_timeout_min` (5–240): testar limites + fora-do-limite (deve ser ignorado silenciosamente — finding ACC-2).
  - `auth_enabled` + `auth_enabled_present=1`: ligar/desligar.
  - `web_username`, `web_password_new`/`web_password_confirm`: alterar, confirmar match/mismatch (mismatch → erro cliente).
  - `viewer_enabled` + `viewer_enabled_present=1`, `viewer_username`, `viewer_password`.
  - `charts_exempt` + `_present=1`, `serial_monitor_enabled` + `_present=1`.
- `GET /api/session-history` e `POST /api/access-kick-all` → testar (sem UI, finding ACC-1).

### Botões / lógica
- 💾 Save: validação password match, loading overlay, `customAlert` + `showSavedCard`.
- Tabs (Web Sessions / Local Processing / Monitoring / Users): troca sem round-trip.
- Viewer: login com viewer → só `/`, `/status`, `/sensor-charts`, `/monitor`, `/files` (403 nos resto).

### Persistência
- `saveSection("access")` → `/config/access.json`. Confirmar que o ficheiro existe e passwords estão masked (`********`).

### Reboot pelo meio
- Alterar `web_max_sessions=1`, reboot (ver secção "Reboots" abaixo), confirmar que persistiu = 1.

### Divergências conhecidas (do doc, não bugs meus)
- ACC-1: kick-all/session-history sem UI. ACC-2: valores inválidos ignorados silenciosamente.
- ACC-3: dead PT sentinel em password. ACC-4: rotas não gateadas por flag.

---

## 2. Página — Ethernet (`/ethernet`)
**Ativa?** Sim (`ENABLE_ETHERNET`). Fonte: `PAGES/ethernet.md`.

### Endpoints (curl)
- `GET /api/eth-status` → modo/ip/mac/dhcp.
- `POST /api/eth-config` → dhcp/static, ip/gw/dns (só static), mac_override (validar formato).
- `GET /api/wifi-runtime-status` → disabled/connected/ssid.
- `POST /api/wifi-disable` / `POST /api/wifi-enable` → **CUIDADO:** desligar wifi liberta heap; reativar pede reboot.
  ⚠️ **NÃO é exceção proibida** (exceção é a *config* wifi, não o runtime switch), mas testar com cuidado:
  ao desligar wifi a board fica só em ethernet (ok, é o caso normal). Ao reativar, confirmar que pede reboot e não quebrar acesso.

### Botões / lógica
- 💾 Save: success "Saved. Restart to apply."
- ⛔ Disable WiFi (customConfirm) / ✅ Enable WiFi.
- Poll live a cada 6 s/8 s (verificar que o status atualiza).

### Persistência
- `/config/ethernet.json` (stem ETHERNET) + flag `wifi_disabled` em NVS `cfg_wifi`.

### Reboot pelo meio
- Static IP → reboot → confirmar que IP static persistiu e foi aplicado (pode precisar de reboot para aplicar).

---

## 3. Página — Proximity (`/proximity`) + Proximity Dashboard (`/proximity-dashboard`)
**Ativa?** Sim (`ENABLE_PROXIMITY`, 8 canais). Fontes: `PAGES/proximity_sensors.md`, `proximity_dashboard.md`.

### Endpoints (curl)
- `GET /api/proximity`, `GET /api/proximity/config` (dgi/fixedPins/channels).
- `POST /api/proximity/save-all` (JSON unificado: channels+shift+emails) — testar save completo.
- `POST /api/proximity/reset?ch=`, `reset-all`, `reset-total?ch=`.
- `POST /api/proximity/oee-reset?ch=`, `GET /api/proximity/oee`.
- `POST /api/proximity/test-email` (verifica recipients+email+wifi+SD+NTP).
- `GET /api/proximity/shift-config`, `/shiftday-emails`, `/shift-history?days=`.
- `GET /api/proximity-chart-data?date=DD-MM-YYYY&mode=day`.
- ⚠️ `GET /api/proximity/events` = **stub vazio** (finding prox-2) — documentar, não "corrigir".

### Botões / lógica
- Tabs: Status · Config · Shifts · Shiftday · OEE.
- Presets (encoder/presence/speed/batch) em DI1; toggle restaura.
- Reset por row (customConfirm), Send Test Email, Reset All OEE, Apply/Defaults OEE, 💾 Save.
- Dashboard: read-only, poll 2 s, gráfico 24h canvas, `loadHistoricalData` 3 s após load.

### Persistência
- `/config/sensor_proximity.json` (config+shift+tca), `/config/sensor_prox_counters.json` (counters/OEE).
- CSVs em SD: `/SDNAME/PROXIMITY_DD-MM-YYYY/...csv` (Waveshare: dia alinhado ao último shift, não 00:00).

### Reboot pelo meio
- Configurar canal DI1 (mode COUNTER, goal), contar pulsos (simular?), reboot, confirmar counters persistem.
- Shift engine: verificar que transição de shift dispara CSV + email.

### Divergências conhecidas
- prox-1: `resetAll()` sem botão. prox-2: `/events` stub. prox-3: erro PT "JSON invalido".
- prox-4: log INFO a spamar monitor. prox-5: save-all 400 se shift/emails ausentes.
- **Crítico já fixo (verificar ainda assim):** hourly chart 22H=0 (fix 2026-08-25), `/chart-data` timeout (fix).

---

## 4. Página — Inactivity Alerts (`/alerts-config`)
**Ativa?** Sim (`ENABLE_PAGE_ALERTS`). Fonte: `PAGES/inactivity_alerts.md`.

### Endpoints (curl)
- `POST /api/inactivity-alert-config` (6 slots: enabled/timeout/max/emails).
- `POST /api/inactivity-alert-test` (slot i + emails → envia teste).

### Botões / lógica
- 💾 Save Alert Configuration: validação cliente (email obrigatório + `@` quando ativo).
- 6 slots × (enable/timeout 5min–24h/max-per-day/emails).
- **Regra Waveshare:** só **proximity counts** resetam o timer (RS232/LPT não existem no pack).

### Persistência
- `/config/alerts.json` (stem alerts) — atomic LittleFS.

### Reboot pelo meio
- Configurar slot 0 (timeout 5 min, email válido), reboot, confirmar persistiu.
- Testar que alerta dispara quando proximity para (pode demorar — teste de integração longo).

### Divergências conhecidas
- ALERTS-2: validação só `indexOf('@')` ("a@" passa). ALERTS-4: UI cap 60/day vs API 1–100.
- ALERTS-5: sem feedback live de contadores. ALERTS-6: disabled slots sobrepostos no save.

---

## 5. Página — Monitor / Serial Monitor (`/monitor`, `/serial-monitor`, `/buffer-viewer`)
**Ativa?** Sim (`ENABLE_PAGE_MONITOR`). Fonte: `PAGES/monitor.md`.

### Endpoints (curl)
- `POST /api/monitor-open` / `POST /api/monitor-close`.
- `POST /api/clear-buffer`.
- `GET /api/monitor-status` (buffer_seq/usage), `GET /api/received-text?limit=100`.
- `GET /api/serial-monitor?limit_msgs=200&since=`.
- `POST /api/system-command` (JSON `{command}`) — testar comando `status`/`help`.
  (RS232/Modbus commands = 503 ou no-op no Waveshare, flags off.)

### Botões / lógica
- /monitor: Refresh, Clear, Open RX Viewer, Send Command.
- /serial-monitor: search/filter/pause/clear/copy/export, auto-refresh 3 s, send cmd.

### Persistência
- Nenhuma (runtime RAM/PSRAM). Reboot limpa logs/buffer.

### Reboot pelo meio
- Abrir /serial-monitor, gerar logs, reboot, confirmar que buffer/log limpam (esperado).

---

## 6. Página — Files (`/files`)
**Ativa?** Sim (`ENABLE_PAGE_FILES`, SDCARD on). Fonte: `PAGES/files.md`.

### Endpoints (curl)
- `GET /files?storage=sd|flash|psram&path=&sort=&p=` (navegação server-rendered).
- `GET /api/sd/download?storage=&path=` (download).
- `POST /api/sd/create-file`, `/create-dir`, `/rename`, `/delete`, `/delete-multiple` (JSON).
- `POST /api/sd/mount`, `/unmount`.

### Botões / lógica
- Filter, storage selector, select all/clear, sort, refresh, back, delete, new folder, rename.
- Keyboard: Delete/Ctrl+A/Esc. Context menu (download/open/rename/delete/move).

### Persistência
- Nenhuma config; lê/escreve ficheiros em storage ativo. Session guard max 3 (503 "Explorer Busy").

### Reboot pelo meio
- Criar pasta de teste em SD, reboot, confirmar pasta persiste (SD não-volátil).
- **Repor:** apagar pasta de teste no fim.

### Divergências conhecidas
- FIL-2: download 2 s client-guard só. FIL-3: sortByType morto. FIL-4: arrow functions.
- FIL-1 já fixo (fallbacks PT→EN).

---

## 7. Página — microSD / PSRAM (`/sdcard`)
**Ativa?** Sim (`ENABLE_PAGE_SDCARD`). Fonte: `PAGES/microsd_card.md`.

### Endpoints (curl)
- `GET /api/sd-status`, `GET /api/sdcard-config`.
- `POST /api/sdcard-config-save` (flush_interval/format/email/auto_cleanup/threshold/keep_days).
- `POST /api/sd-action` (mount/unmount/refresh).

### Botões / lógica
- Mount/Unmount/Refresh, 💾 Save (selects só).
- Campos: buffer write 2s–60m, active, format txt/csv/bin, email, auto_cleanup, threshold %, keep_days.

### Persistência
- `/config/sdcard.json` (stem sdcard). `keep_days=0` = ∞.

### Reboot pelo meio
- Alterar flush_interval + format, reboot, confirmar persistiu e aplicou.

### Divergências conhecidas
- SD-2: dead duplicate initSdcardPage. SD-3: Commands card nunca renderizado.
- SD-4: `#sd_msg` em falta + branch `j.disabled` morta. SD-1/5/6 já fixos.

---

## 8. Página — Email (`/email`)
**Ativa?** Sim (`ENABLE_SMTP`). Fonte: `PAGES/email.md`.
### ⚠️ EXCEÇÃO DO ANEXO: NÃO modificar/apagar config de Email.
- Teste permitido: `POST /api/email-test` (envia teste com config EXISTENTE — não altera nada).
- `GET /api/config?section=email` → só LER para confirmar que continua intacta no fim.
- NUNCA `POST /api/config` section=email com valores novos. Se o teste falhar → documentar, não corrigir.

---

## 9. Página — Wi-Fi (`/wifi`)
**Ativa?** Sim (`ENABLE_WIFI`). Fonte: `PAGES/wifi.md`.
### ⚠️ EXCEÇÃO DO ANEXO: NÃO modificar/apagar config de Wi-Fi.
- Teste permitido: `GET /api/config?section=wifi` (confirmar intacta no fim).
- `GET /api/wifi-scan` → listar redes (não altera config).
- `GET /api/wifi-runtime-status` → disabled/connected.
- NUNCA `POST /api/config` section=wifi com novo SSID/pass. (O runtime switch disable/enable
  está coberto na secção Ethernet, com cuidado — não altera a *config* guardada.)

---

## 10. Página — Status (`/status`)
**Ativa?** Sim (sempre, via PAGE_MONITOR). Fonte: `PAGES/status.md`.

### Endpoints (curl)
- `GET /api/status` (respeitar 429 / 500 ms). Verificar campos: firmware, chip, heap/psram,
  SD, NVS, wifi_*, ethernet_*, email_destination, feature_* flags (confirmar Waveshare = ETH+PROX+SMTP+SDCARD+ACCESS+FILES+ALERTS, sem RS232/MODBUS/CAN/sensors).

### Botões / lógica
- 🔄 Refresh (`#r`). Read-only.

### Persistência
- Nenhuma (live view).

### Reboot pelo meio
- Reboot, `GET /api/status` → confirmar uptime resetou, firmware = .29, features corretas.

---

## 11. Reboots — procedimento transversal
- Reboot via endpoint se existir (ex: `/api/restart` — verificar em core_admin_handler),
  ou desligar/ligar pela tomada/relé se não houver API.
- **IMPORTANTE:** `/api/reboot` no Waveshare é **NO-OP** (doc memory: reinicia via `/api/restart`).
  Confirmar qual o endpoint real antes de usar.
- Após cada reboot: aguardar ~10 s, `GET /api/status` para confirmar board de volta,
  depois re-testar a secção em causa.
- Contar reboots por página no relatório final.

---

## 12. Relatório final (a entregar ao Nuno)
Para cada página:
- [ ] Endpoints testados (✓/✗) e resposta.
- [ ] Botões/lógica testados (✓/✗).
- [ ] Persistência confirmada (ficheiro LittleFS/NVS existe e correto).
- [ ] Reboots feitos e sobreviveu a config.
- [ ] Defaults repostos (exceto wifi/email, que ficam intactos).
- [ ] Divergências/falhas encontradas (listadas, NÃO corrigidas pela Joana).
- [ ] Ideias de melhoria (se houver).

## 13. Fora de escopo (neste pack)
- Harmonica/RADIO (.30), P4 CNC (.7, board off) → planos próprios, ainda por fazer.
- Build/OTA → só se o Nuno pedir explicitamente; por agora é documentação de teste.
