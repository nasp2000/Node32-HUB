# Teste API Pack P4 CNC (GcodeSender-P4) — plano + relatório

> Objetivo: testar TODAS as paginas/opcoes/botoes do pack P4 CNC via API/curl, encontrar
> problemas ou coisas que nao funcionem conforme a logica. NAO corrigir — so relatar.
> Board alvo: 192.168.1.7 (P4 CNC) — ATENCAO: board pode estar DESLIGADA; confirmar
> por mDNS ou /api/status antes. Se desligada, nao ha teste em campo (so revisao de codigo).
> Autenticacao: Basic admin:admin em todos os POST.
> Nota: paginas deste pack sao MUITO GRANDES (grbl-controller tem ~50 handlers).
> Foco: testar os ENDPOINTS reais, nao abrir a pagina toda.

## Paginas do pack (confirmadas em pack_p4_cnc.h — flags ON)
- `/grbl-controller` (ENABLE_GRBL) — pagina gigante: G-code sender, ~50 Web API handlers
- `/` (home) — GcodeSender button (ENABLE_WIFI_WEB_SERVER default)
- `/monitor` (ENABLE_PAGE_MONITOR default 1) — Web Serial Monitor
- `/serial-monitor` (alias)
- USB Host pages (ENABLE_USB_HOST) — status/config/devices
- Self-test — `/selftest` (manifest de paginas)

Features OFF (NAO testar): telnet, page_commands, page_recipients, page_alerts,
page_sdcard, smtp, google_email, lcd, ethernet(nota), wifi-web (sim).

## Metodologia
1. Para cada grupo: GET de estado + POST de acao (valido + invalido).
2. GRBL tem ~50 endpoints — testar todos, agrupados por funcao.
3. Verificar /api/grbl/status e /api/status apos acoes.
4. NAO mexer em codigo. So relatar aqui.

---

## 1. PAGINA /grbl-controller (G-code sender)
### Estado / transporte (GET)
- [ ] GET /api/grbl/status — estado completo (streaming, fila, posicao)
- [ ] GET /api/grbl/debug — debug info
- [ ] GET /api/grbl/control-token — token de controlo (seguranca)
- [ ] GET /api/grbl/events — eventos
- [ ] GET /api/grbl/list-gcode — lista de G-code carregado
- [ ] GET /api/grbl/frame-bbox — bounding box do frame
- [ ] GET /api/grbl/psram-file — ficheiro em PSRAM
- [ ] GET /api/grbl/psram-read-text — ler texto de PSRAM
- [ ] GET /api/grbl/probe-grid — grelha de probe
- [ ] GET /api/grbl/height-map — height map

### Acoes (POST)
- [ ] POST /api/grbl/send — enviar linha G-code
- [ ] POST /api/grbl/stream — stream de ficheiro
- [ ] POST /api/grbl/psram-stream — stream de PSRAM
- [ ] POST /api/grbl/queue-start — iniciar fila
- [ ] POST /api/grbl/queue-stop — parar fila
- [ ] POST /api/grbl/pause — pausa
- [ ] POST /api/grbl/resume — retoma
- [ ] POST /api/grbl/cancel — cancela
- [ ] POST /api/grbl/override — override (feed/rapid)
- [ ] POST /api/grbl/transport — transporte (USB-CDC)
- [ ] POST /api/grbl/auto-detect-baud — detetar baud
- [ ] POST /api/grbl/usb-line-coding — line coding USB
- [ ] POST /api/grbl/usb-restart — restart USB
- [ ] POST /api/grbl/poll-interval — intervalo de poll
- [ ] POST /api/grbl/probe-grid — probe grid
- [ ] POST /api/grbl/probe-grid-cancel — cancel probe
- [ ] POST /api/grbl/height-map-enable — enable height map
- [ ] POST /api/grbl/height-map-clear — clear height map
- [ ] POST /api/grbl/psram-clear — limpar PSRAM
- [ ] POST /api/grbl/create-dir — criar dir
- [ ] POST /api/grbl/delete-path — apagar path

### Testes de logica (foco nos bugs conhecidos)
- [ ] Paragens no INICIO do stream (bug conhecido: micro-stalls GRBL) — reproduzir?
- [ ] Streaming grande (12MB PSRAM / 30MB SD) — mantem sem parar?
- [ ] Control-token: POST sem token valido e' rejeitado? (seguranca)

## 2. PAGINA / (home)
- [ ] GET / (home 200?)
- [ ] Botao GcodeSender aponta para /grbl-controller?

## 3. PAGINA /monitor + /serial-monitor
- [ ] GET /monitor (200?)
- [ ] GET /serial-monitor (alias)
- [ ] GET /api/monitor-status — responde?
- [ ] Streaming de logs (DEBUG_LOGS=1 neste pack)

## 4. USB HOST
- [ ] GET /api/usb-server-status — estado do USB host
- [ ] GET /api/usb-server-config — config
- [ ] GET /api/usb-server-devices — dispositivos detetados (ex.: GRBL por USB-CDC)
- [ ] POST /api/usb-server-config — alterar config

## 5. SD CARD (via /api/sd*, NOT page_sdcard que esta OFF)
- [ ] GET /api/sd-status — SD montado?
- [ ] GET /api/sdcard-config — config
- [ ] POST /api/sd/mount — montar
- [ ] POST /api/sd/unmount — desmontar
- [ ] POST /api/sd/create-dir — criar dir
- [ ] POST /api/sd/create-file — criar ficheiro
- [ ] POST /api/sd/rename — renomear
- [ ] POST /api/sd/delete — apagar
- [ ] POST /api/sd/reset-write-counter — reset contador
- [ ] POST /api/sdcard-config-save — gravar config

## 6. SELF-TEST
- [ ] GET /selftest — manifest (grbl-controller, monitor aparecem PASS?)

---

## 7. DISCO SD + PSRAM (persistencia)
### SD
- [ ] GET /api/sd-status — montado? espaco?
- [ ] G-code upload via /api/grbl/upload-gcode-stream (SD, ate 50MB) — testar upload
- [ ] G-code upload via /api/grbl/psram-upload (PSRAM, ate 12MB) — testar upload
- [ ] Download/leitura do ficheiro carregado (GET /api/grbl/psram-read-text, list-gcode)

### PSRAM (volatil — perde no reboot)
- [ ] GET /api/grbl/psram-file apos upload — conteudo presente?
- [ ] Apos reboot: PSRAM limpo? (esperado, e volatil)

### LittleFS config
- [ ] GET /api/usb-server-config — /config/usb_server*.json
- [ ] GET /api/sdcard-config — /config/sdcard*.json
- [ ] Apos POST, refazer GET e confirmar persistencia

---

## Problemas encontrados (preencher durante teste)
| # | Pagina / Endpoint / Botao | Esperado | Real | Severidade | Notas |
|---|---|---|---|---|---|
|   |   |   |   |   |   |
| 1 | /api/usb-server-* (status/config/devices) | 200 (P4 tem USB_HOST) | 302 redirect | BAIXA/N/A | Endpoints guardados por ENABLE_USBIP_SERVER (pack P4 tem USBIP_SERVER=0). P4 usa USB Host p/ GRBL, nao USBIP Server. Pagina /usb-server tambem 302. Esperado p/ este pack. |
| 2 | /api/grbl/send,/pause,/resume (sem token) | 200 | 400 auth_token_invalid | N/A | Endpoints exigem sid+token (seguranca). Sem dispositivo GRBL USB ligado ao P4, nao da p/ testar fundo. |
| 3 | GET /api/grbl/status | 200 | 200 mas connected:None, slot:None | N/A | Sem maquina GRBL ligada ao USB do P4. Endpoints OK, so nao ha alvo. |
| 4 | POST /api/sd/create-dir, /api/sd/delete | JSON {status:ok} | 302 redirect → /files | MÉDIA | Bug F5 (corrigido no Waveshare fd1f928) NÃO aplicado ao P4 CNC. Board .7 tem v1.105_0759 (sem F1-F5). Requer rebuild P4 com fd1f928 + OTA. Ver RELATORIO_TESTE_P4_CNC_20260828.md P1. |
| 5 | GET /selftest | manifest JSON (páginas PASS?) | 302 redirect → / | BAIXA | Rota /selftest não registada ou retorna 302 no P4 CNC. Ver RELATORIO P2. |

## Notas de logica (divergencias conhecidas)
- Paragens no inicio do stream GRBL (micro-stalls) — CORRIGIDO em d01e5ca. Testar com maquina GRBL ligada (nao disponivel agora).
- Control-token: seguranca respeitada (POSTs perigosos exigem token). OK.
- Upload SD 50MB / PSRAM 12MB (GRBL_STREAM_PSRAM_MAX_BYTES) — testar upload quando possivel.
- P4 tem USB_HOST=1 mas USBIP_SERVER=0 => pagina /usb-server + endpoints usb-server NAO existem (intencional).


## Notas de logica (divergencias conhecidas)
- Paragens no inicio do stream GRBL (micro-stalls) — CORRIGIDO em d01e5ca (big-pickle),
  testar se persiste.
- Upload: SD 50MB / PSRAM 12MB (GRBL_STREAM_PSRAM_MAX_BYTES=12MB).
- Control-token: seguranca para POSTs perigosos — verificar se e respeitado.
- Board .7 pode estar desligada — testar so quando ligada.
