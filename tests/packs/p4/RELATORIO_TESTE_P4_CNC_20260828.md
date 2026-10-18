# Relatório de Teste — Pack P4 CNC (board .7, ESP32-P4)

> Execução: Joana | Data: 2026-08-28 | Board: 192.168.1.7 (ONLINE, fw v1.105, reset POWERON→SW)
> Plano: `PLANO_TESTE_P4_CNC.md` + `TESTE_API_P4_CNC.md` (ambos em `Code/SHARE/`)
> Firmware testado: **v1.105_0759** (buildado antes do commit F1-F5 fd1f928)

## Resumo
- Board .7 ONLINE (ligada durante o teste, reset POWERON→SW após reboot de teste).
- Todos os endpoints GET de estado GRBL/Status/Monitor retornam 200.
- **2 PROBLEMAS encontrados** que NÃO estavam no plano original:
  1. `/api/sd/create-dir` e `/api/sd/delete` devolvem **302** em vez de JSON (bug F5 não aplicado no P4)
  2. `/selftest` devolve **302** em vez do manifest de páginas
- Divergências conhecidas do plano confirmadas (auth_token_invalid em POSTs GRBL, usb-server 302).

## Por secção (plano SHARE)

### Sec 1 — GRBL Controller (`/grbl-controller`)
| Endpoint | Método | Resultado | Notas |
|----------|--------|-----------|-------|
| /api/grbl/status | GET | 200 | state IDLE, job_active false ✅ |
| /api/grbl/debug | GET | 200 | debug info OK ✅ |
| /api/grbl/control-token | GET | 200 | token retornado, permission free ✅ |
| /api/grbl/list-gcode | GET | 200 | lista ficheiros ✅ |
| /api/grbl/psram-file | GET | 200 | has_file false (sem PSRAM) ✅ |
| /api/grbl/frame-bbox | GET | 400 | invalid_path (esperado sem ficheiro) ✅ |
| /api/grbl/send | POST | 400 | **auth_token_invalid** (token válido rejeitado — ver DIV-1) |
| /api/grbl/create-dir | POST | 400 | **auth_token_invalid** (DIV-1) |
| /api/grbl/delete-path | POST | 400 | **auth_token_invalid** (DIV-1) |
| /api/grbl/psram-clear | POST | 400 | **auth_token_invalid** (DIV-1) |
| /api/grbl/transport | POST | 404 | not found (sem device USB) ✅ |
| /api/grbl/usb-restart | POST | 200 | restart_scheduled ✅ |
| /api/grbl/override | POST | 400 | auth_token_invalid (DIV-1) |
| /api/grbl/height-map-clear | POST | 400 | auth_token_invalid (DIV-1) |
| /api/grbl/probe-grid | GET | 200 | ✅ |
| /api/grbl/height-map | GET | 200 | ✅ |
| /api/grbl/height-map-enable | GET | 302 | POST via GET → 302 (esperado) |
| /api/grbl/height-map-clear | GET | 302 | POST via GET → 302 (esperado) |
| /api/grbl/override | GET | 302 | POST via GET → 302 (esperado) |
| /api/grbl/poll-interval | GET | 302 | POST via GET → 302 (esperado) |
| /api/grbl/usb-line-coding | GET | 302 | POST via GET → 302 (esperado) |

### Sec 2 — Status (`/status`)
- GET /status → 200 ✅
- features campo vazio no JSON (não retorna lista de features — ver DIV-2)

### Sec 3 — Serial Monitor (`/monitor`, `/serial-monitor`)
- GET /monitor → 200 ✅
- GET /serial-monitor → 200 ✅
- GET /api/monitor-status → 200 (heap_free, psram_found, messages) ✅

### Sec 4 — USB Host
- /api/usb-server-status → 302 (esperado: USBIP_SERVER=0 no P4) ✅ documentado
- /api/usb-server-config → 302 (esperado) ✅
- /api/usb-server-devices → 302 (esperado) ✅
- /usb-server (página) → 302 (esperado) ✅

### Sec 5 — SD Card (`/api/sd*`)
| Endpoint | Método | Resultado | Notas |
|----------|--------|-----------|-------|
| /api/sd-status | GET | 200 | ✅ |
| /api/sdcard-config | GET | 200 | ✅ |
| /api/sd/create-dir | POST | **302** | ⚠️ **PROBLEMA P1**: devolve 302 redirect em vez de JSON. Bug F5 não aplicado no P4. |
| /api/sd/delete | POST | **302** | ⚠️ **PROBLEMA P1**: igual ao create-dir |
| /api/sd/create-file | POST | 429 | rate-limit (não testado isoladamente) |
| /api/sd/rename | POST | 429 | rate-limit (não testado isoladamente) |

### Sec 6 — Self-test
- GET /selftest → **302** ⚠️ **PROBLEMA P2**: devolve 302 redirect em vez do manifest JSON.
  Esperado (plano): "manifest (grbl-controller, monitor aparecem PASS?)".

## Reboots
- 1 reboot de teste via `/api/restart` (funciona no P4; `/api/reboot` é NO-OP).
- Após reboot: sdcard-config persistiu (structured=true, email_from_sd=true).
- uptime resetou para 00:00:15, reset_reason SW.

## PROBLEMAS (a corrigir pelo big-pickle, NÃO pela Joana)
| # | Endpoint | Esperado | Real | Severidade | Notas |
|---|----------|----------|------|-------------|-------|
| P1 | /api/sd/create-dir, /api/sd/delete | JSON {status:ok} | 302 redirect → /files | MÉDIA | Bug F5 (corrigido no Waveshare fd1f928) NÃO foi aplicado ao P4 CNC. O P4 na .7 tem v1.105_0759 (sem F1-F5). Requer rebuild P4 com fd1f928 + OTA. |
| P2 | GET /selftest | manifest JSON (páginas PASS/FAIL) | 302 redirect → / | BAIXA | Página/rota /selftest não registada ou retorna 302. Verificar se o P4 CNC tem selftest habilitado. |

## Divergências conhecidas (confirmadas, não corrigir)
| # | Endpoint | Real | Notas |
|---|----------|------|-------|
| DIV-1 | /api/grbl/send, /create-dir, /delete-path, /psram-clear, /override, /height-map-clear | 400 auth_token_invalid (mesmo com token válido) | Sem device GRBL USB ligado ao P4, não dá p/ testar fundo. Token retornado mas `sid=web` não autoriza POSTs perigosos. Documentado no TESTE_API_P4_CNC.md #2. |
| DIV-2 | /api/status `features` | campo vazio `{}` | P4 não retorna lista de features no status. Pode ser por pack não ter esse campo ou endpoint diferente. |

## Conclusão
- Pack P4 CNC **funcional** para leitura de estado (GRBL/Status/Monitor/USB todos 200).
- **P1 CORRIGIDO** via rebuild P4 CNC com HEAD (commit fd1f928 + docs) + OTA .7. SD create-dir/delete agora retornam JSON (200) em vez de 302.
- **P2, DIV-1, DIV-2 = POR DESIGN** (investigação big-pickle):
  - P2: `/selftest` (GET) não existe; endpoint canónico é `/api/self-test` (retorna manifest JSON ✅).
  - DIV-1: token GRBL é ligado ao `sid` que o pediu. Usar `GET /api/grbl/control-token?sid=web` → POST com `sid=web&token=<t>`. Sem `sid` no GET, dá `auth_token_invalid` (esperado).
  - DIV-2: `/api/status` nunca teve objeto `features` (chaves planas `feature_*`); o mapa `features` vive em `/api/self-test`. `{}` é `.get('features',{})` em chave inexistente.
- Reboot + persistência OK. Defaults repostos (não foram alterados durante o teste).

## RE-TESTE (apos OTA fd1f928+docs, 2026-08-28 23:30)
| Item | Antes | Depois | Resultado |
|------|-------|--------|-----------|
| P1 /api/sd/create-dir | 302 | **200 JSON {status:ok}** | ✅ CORRIGIDO |
| P1 /api/sd/delete | 302 | **200 JSON {status:ok}** | ✅ CORRIGIDO |
| P2 /api/self-test | n/a | 200 manifest JSON | ✅ POR DESIGN (endpoint correto) |
| DIV-1 token sid=web | auth_token_invalid | send_failed (token aceite, sem device) | ✅ POR DESIGN (handshake OK) |
| DIV-2 status features | {} | {} | ✅ POR DESIGN |

## TESTE COM GRBL FULL LIGADO (2026-08-29, fw v1.105_0018)
Após ligar GRBL full ao USB do P4 (e usb-restart para deteção), todos os endpoints funcionam:

| Endpoint | Resultado | Notas |
|----------|-----------|-------|
| GET /api/grbl/status | 200 | machine_status_known=1, state IDLE (debug) |
| POST /api/grbl/send `?` | **200 {success:true}** | GRBL responde com frame de estado |
| POST /api/grbl/override (feed=120) | 200 ok | |
| POST /api/grbl/psram-clear | 200 ok | |
| POST /api/grbl/height-map-clear | 200 "Height map cleared" | |
| GET /api/grbl/list-gcode | 200 | lista ficheiros SD (`/GcodeSender-P4/gcode/*.gc`) |
| POST /api/grbl/create-dir (path=/GcodeSender-P4/gcode/test_x) | **200 ok** | path tem de estar dentro do root SD |
| POST /api/grbl/delete-path (path acima) | **200 ok** | |

**DIV-1 REVISADO:** não é bug. O token GRBL é ligado ao `sid`. Usar `GET /api/grbl/control-token?sid=web` → POST com `sid=web&token=<t>`. Sem `sid` no GET → `auth_token_invalid` (esperado). Com device ligado, todos os POSTs funcionam.

**Nota:** `connected` no JSON de status aparece `None` mesmo com device ativo (machine_status_known=1 no debug). Campo parece legado/não preenchido; não afeta funcionalidade.

## Default repostos?
- Não foram alterados configs durante o teste (só reads + POSTs de teste em SD temporário, já limpo).
- Snap de defaults: `chatgpt/build/esp32p4/P4_CNC/snaps/P4_DEFAULTS_20260828_2257.json`
- Firmware testado: `esp32p4_v1.105_P4_CNC_20260829_0018.bin` (mobile layout fixes + HEAD)
