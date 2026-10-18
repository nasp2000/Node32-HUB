# Teste API Pack Waveshare (8DI8DO) — plano + relatório

> Objetivo: testar TODAS as paginas/opcoes/botoes do pack Waveshare via API/curl, encontrar
> problemas ou coisas que nao funcionem conforme a logica. NAO corrigir — so relatar.
> Board alvo: <board-ip> (Waveshare 8DI8DO). Confirmar por mDNS ou /api/status antes.
> Autenticacao: HTTP Basic (via TEST_AUTH) em todos os POST.

## Paginas do pack (confirmadas em pack_waveshare.h — flags ON)
- `/` (home) — Proximity + Dashboard, Ethernet, SD Card, Monitor (ENABLE_WIFI_WEB_SERVER)
- `/proximity` (ENABLE_PROXIMITY) — config 8ch + shift management (custom pages)
- `/alerts` (ENABLE_PAGE_ALERTS) — inactivity alerts (a pagina que estamos a corrigir)
- `/monitor` + `/serial-monitor` (ENABLE_PAGE_MONITOR) — Web Serial Monitor
- `/files` (ENABLE_PAGE_FILES) — file browser
- `/sdcard` (ENABLE_PAGE_SDCARD) — SD card page (mount/unmount/config)
- `/access` (ENABLE_PAGE_ACCESS) — access control
- `/ota` (ENABLE_OTA) — firmware update
- `/email` (ENABLE_SMTP) — email/SMTP config
- `/ethernet` (ENABLE_ETHERNET) — ethernet config
- Self-test — `/selftest` (manifest de paginas)

Features OFF neste pack (NAO testar): recipients, mdns, google_email, db25, sniffer,
modbus*, can, unikon, ipg, camera, uda1334a, lcd, telnet, debug, scheduler, commands,
structures, api-reference, async_websocket, sensors, buzzer, rs232, usbip, grbl.

## Metodologia
1. Para cada pagina: GET da pagina (200?) + GET dos endpoints de estado.
2. Para cada botao/acao da UI: mapear para endpoint e testar POST (valido + invalido).
3. Verificar /api/status apos acoes.
4. NAO mexer em codigo. So relatar aqui.

## PATHS CORRETOS (descobertos em teste — plano inicial tinha erros)
- Pagina alertas = **/alerts-config** (NAO /alerts)
- Self-test = **/api/self-test** (com hifen; NAO /selftest nem /api/selftest)
- GET config alertas = NAO existe; ler via /api/status (campo inactivity_alerts)
- POST alertas = /api/inactivity-alert-config (POST only; GET da 302)
- A maioria dos GETs de config sao POST-only ou lidos via /api/status

## ESTADO DOS TESTES (Waveshare .29, v1.103)
- [x] Paginas 200: /, /proximity, /monitor, /files, /sdcard, /access, /email, /ethernet, /ota, /alerts-config
- [x] /api/self-test 200 (manifest OK: proximity, alerts-config, files, sdcard, access, email, ethernet, ota enabled)
- [x] /api/proximity 200; /api/proximity/oee 200 (dados presentes)
- [x] /api/access-config 200; /api/sd-status 200
- [x] POST /api/inactivity-alert-config 200 (grava slot0; status mostra valido — SEM corrupcao)
- [ ] FALHA no plano: /alerts e /selftest (302) — erro de mapeamento, NAO bug firmware

---

## 1. PAGINA / (home)
- [ ] GET / (home 200?)
- [ ] Verificar paineis: Proximity, Dashboard, Ethernet, SD Card, Monitor aparecem?
- [ ] Links apontam para /proximity, /ethernet, /sdcard, /monitor?

## 2. PAGINA /proximity (8ch + shift)
### GET estado
- [ ] GET /api/proximity — estado dos 8 canais
- [ ] GET /api/proximity/config — config
- [ ] GET /api/proximity/shift-config — config de turno
- [ ] GET /api/proximity/shiftday-emails — emails do dia de turno
- [ ] GET /api/proximity/events — eventos
- [ ] GET /api/proximity/oee — OEE (A%? problema conhecido do A=0%)
- [ ] GET /api/proximity/shift-history — historico de turnos
- [ ] GET /api/proximity-chart-files — ficheiros de grafico
- [ ] GET /api/proximity-chart-cols — colunas
- [ ] GET /api/proximity-chart-data — dados
- [ ] GET /api/proximity/tca9554 — I2C expander
### POST (acoes)
- [ ] POST /api/proximity/config — alterar config (valido + invalido)
- [ ] POST /api/proximity/shift-config — alterar turno
- [ ] POST /api/proximity/shift-reset — reset de turno (se existir)
- [ ] Verificar se OEE A% calcula certo (era bug conhecido)

## 3. PAGINA /alerts (inactivity alerts) — EM FOCO (correcao alerts.json)
- [ ] GET /alerts (pagina 200?)
- [ ] GET /api/inactivity-alert-config — config atual (6 slots)
- [ ] POST /api/inactivity-alert-config — gravar slot (enabled/timeout/max/emails)
- [ ] POST /api/inactivity-alert-test — teste de alerta
- [ ] **TESTE CRITICO**: gravar sem alterar -> /config/alerts.json NAO corrupto?
      (validar via GET /api/inactivity-alert-config apos gravar, ou reler estado)
- [ ] Verificar se emails com control chars sao sanatizados (correcao ConfigManager)

## 4. PAGINA /monitor + /serial-monitor
- [ ] GET /monitor (200?)
- [ ] GET /serial-monitor (alias)
- [ ] GET /api/monitor-status — responde?
- [ ] Streaming de logs em tempo real?

## 5. PAGINA /files (file browser)
- [ ] GET /files (200?)
- [ ] GET /api/files ou listagem SD
- [ ] Download: GET /api/sd/download?path=...

## 6. PAGINA /sdcard (SD card mount/unmount)
- [ ] GET /sdcard (200?)
- [ ] GET /api/sd-status — SD montado?
- [ ] GET /api/sdcard-config — config
- [ ] POST /api/sdcard-config-save — mount/unmount/config (valido + invalido)

## 7. PAGINA /access (access control)
- [ ] GET /access (200?)
- [ ] GET /api/access-config — config
- [ ] POST /api/access-config — alterar (se existir endpoint)

## 8. PAGINA /email (SMTP)
- [ ] GET /email (200?)
- [ ] GET /api/config?section=email — config SMTP
- [ ] POST /api/config (section=email) — alterar SMTP (valido + invalido)
- [ ] Verificar que password nao vem em claro no GET

## 9. PAGINA /ethernet
- [ ] GET /ethernet (200?)
- [ ] GET /api/config?section=ethernet ou /api/ethernet-status — config
- [ ] POST /api/config (section=ethernet) — alterar

## 10. PAGINA /ota
- [ ] GET /ota (200?)
- [ ] POST /api/enable-ota — ativa?
- [ ] POST /api/firmware-upload — upload de bin (NAO fazer OTA real da .29 sem ordem)

## 11. SELF-TEST
- [ ] GET /selftest — manifest de paginas (proximity, alerts, monitor, files, sdcard, access, ota aparecem PASS?)

---

## 12. DISCO SD + LITTLEFS (persistencia)
### SD
- [ ] GET /api/sd-status — montado?
- [ ] GET /api/sdcard-config
- [ ] GET /api/sd/download?path=... — conteudo bruto
- [ ] Ficheiro SD do dia (rotacao por turno — correcao recente): ver se pasta/data correta

### LittleFS config
- [ ] GET /api/inactivity-alert-config — /config/alerts.json valido apos gravar?
- [ ] GET /api/proximity/config — /config/proximity*.json
- [ ] GET /api/proximity/shift-config — turno
- [ ] GET /api/config?section=email — /config/smtp_email*.json
- [ ] GET /api/config?section=ethernet — /config/ethernet*.json
- [ ] GET /api/access-config — /config/access*.json
- [ ] Apos POST, refazer GET e confirmar persistencia (ficheiro reescrito)

---

## Problemas encontrados (preencher durante teste)
| # | Pagina / Endpoint / Botao | Esperado | Real | Severidade | Notas |
|---|---|---|---|---|---|
| 1 | /api/config?section=ethernet (GET+POST) | aceita secao ethernet (pack tem ENABLE_ETHERNET=1, pagina /ethernet existe) | 400 "Invalid section. Use wifi|email|rs232|system|recipients|scheduler" | ALTA | config_handler.cpp linha 151 so valida wifi/email/rs232/etc — falta case "ethernet". Pagina /ethernet nao consegue gravar/ler config. BUG FIRMWARE. |
| 3 | GET /api/proximity-chart-data | devolve dados de grafico (com file=) | 000 TIMEOUT (mesmo com file=test) | MEDIA | endpoint bloqueia/loopa sem ficheiro valido ou path errado. So afeta graficos proximity. |
| 4 | (paths plano) /alerts, /selftest | 200 | 302 redirect / | N/A | erro de mapeamento no plano — paths reais sao /alerts-config e /api/self-test. |
|   |   |   |   |   |   |

## Notas de logica (divergencias conhecidas)
- OEE A% = 0 (bug conhecido, pode estar corrigido — verificar)
- alerts.json corrompe ao gravar (correcao em curso em ConfigManager.cpp)
- SD ficheiro do dia na ultima hora de turno (correcao recente big-pickle)

---

## EXECUÇÃO REAL — 2026-08-28 (Joana, harness automatizado)
Harness executável: `teste_waveshare_api.py` (corre via `python3 teste_waveshare_api.py`,
alvo <board-ip>, auth user:pass). Resultados em `teste_waveshare_api_results.json`.

Board: Waveshare 8DI8DO @ <board-ip>, **fw v1.105** (doc anterior era v1.103).
eth_link=False (wifi only), wifi RSSI ~-48/-52.

### Resultado dos testes (REAIS, contra o board vivo)
- [PASS] Todas as paginas/endpoints GET: /, /alerts-config, /api/self-test, /api/proximity,
  /api/proximity/oee, /api/sd-status, /api/access-config, /api/config?section=email,
  /api/config?section=ethernet, /api/inactivity-alert-config, /api/proximity-chart-data?file=test.
- [PASS] PERSISTENCIA: POST /api/inactivity-alert-config (echo do que se le no /api/status)
  devolve 200 e a config user (enabled/timeout/max/emails por slot) mantem-se igual apos
  re-leitura. Campos volateis de runtime (alerts_sent_today) sao resetados no save — esperado,
  NAO e corrupcao.
- [PASS] REBOOT: POST /api/restart reinicia o board e este recupera em ~5s (uptime resetou
  10.8h -> 32s; 82s -> 9s em 2 runs de teste). Recuperacao confirmada.

### Bugs do doc ANTERIOR — RE-TESTADOS em v1.105
- [RESOLVIDO] #1 Ethernet: GET /api/config?section=ethernet agora devolve 200 (era 400).
  A pagina /ethernet consegue ler/gravar config. (Corrigido entregue no v1.105.)
- [RESOLVIDO] #3 proximity-chart-data: GET /api/proximity-chart-data?file=test devolve 200
  com dados validos (count 170). Era TIMEOUT.
- [N/A] #4 paths: /alerts e /selftest davam 302 — era erro de mapeamento do PLANO, nao bug
  firmware. Paths reais: /alerts-config (pagina) e /api/self-test (manifest).
- [RESOLVIDO] OEE A%: availability agora ~35% (era bug A=0%). Ver /api/proximity/oee.

### Divergencias / melhorias encontradas (para seguimento)
- [MEDIA] `/api/reboot` e um NO-OP: devolve 200 "Reiniciando..." mas NAO reinicia
  (handleRestartPost nao chama performRestartActions). O endpoint que reinicia e
  /api/restart (web_routes_core.cpp:58 -> performRestartActions -> safeReboot). Se a UI/doc
  referenciam /api/reboot, devem apontar para /api/restart. (Nao corrigido — fora do escopo
  do teste; a indicar ao Nuno/big-pickle.)
- [BAIXA] /api/status tem rate-limit de 1 chamada / 500ms (core_admin_handler.cpp:31; 429 se
  mais rapido). O harness espa as chamadas 600ms. Documentar para quem faz testes manuais.
- [INFO] inactivity-alert-config POST normaliza campos de runtime — o echo perfeito do JSON
  nao e estavel por causa de alerts_sent_today; comparar so os campos de config do user.

### Proximos passos (mandato do Nuno: teste real por pack e por pagina)
- Harmonica/RADIO (.30): criar harness equivalente (TESTE_API_RADIO.md ja existe).
- P4 CNC (.7): board DESLIGADO — ligar e testar (TESTE_API_P4_CNC.md ja existe).
- Expandir o harness Waveshare a TODOS os botoes/acoes POST de cada pagina (nao so echo).
