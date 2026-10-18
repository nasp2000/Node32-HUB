# Plano de Testes — HomeE32 (<board-ip>, Pack ENVIRONMENT)

> Igual ao esquema dos outros packs (Waveshare .29 / Harmonica .30 / P4 .7).
> Objetivo: validar TODAS as páginas/APIs do pack Environment em hardware real,
> com reboots pelo meio, sem mexer em WiFi/email do board, e repor defaults no fim.

## Board sob teste
- IP: <board-ip> | Pack: ENVIRONMENT (HomeE32) | ESP32-S3 N16R8
- Web: porta 80, pasta /Home32. Auth: HTTP_BASIC_AUTH (user:pass provável p/ OTA).
- NOTA: `/api/status` dá HTTP 000 neste pack — usar `/api/configs` (200) e os
  endpoints específicos abaixo para verificar estado. Mapear o status real no início.

## Princípios (do email "Trata" do Nuno, 28-08)
1. Plano de teste REAL por pack E por página.
2. Testar api/curl + persistência + save + reboots.
3. Testar TODOS os botões e toda a lógica de cada página.
4. Fazer REBOOTS pelo meio (simular perda de energia / restart).
5. NÃO mexer em WiFi nem em email do board.
6. Repor DEFAULTS após os testes.
7. Documentar divergências (o que não bate com o esperado).

## 0. Preparação
- [ ] Confirmar .24 acessível (ping + GET /Home32 302 + /api/configs 200).
- [ ] Identificar endpoint de status real (testar /api/status, /api/system/status,
      /api/info; anotar qual responde e o JSON).
- [ ] Snapshot do estado atual: GET /api/configs, /api/sensor-config, /api/mq135-config,
      /api/rs232-status, /api/sensor-slots, /api/recipient-slot → guardar em ficheiro
      (para repor defaults no fim).
- [ ] Anotar versão firmware (de onde aparecer no status/config).

## 1. Página AHT20/BMP280 (temp/hum/pressão) + charts
Endpoints: /api/sensor-config (GET/POST), /api/sensor-measure (POST),
/api/sensor-reset-stats (POST), /api/sensor-slots (GET), /api/sensor-flush (POST),
/api/sensor-flags (GET), /api/sensor-history (GET), /api/sensor-history-files (GET),
/api/sensor-history-file (GET), /api/sensor-charts-session/heartbeat/session-end (POST).
Testes:
- [ ] Medir (sensor-measure) → valores plausíveis (temp ~20-30°C, hum 30-70%, press ~1000hPa).
- [ ] Configurar sensor-config (intervalo, enable) → persistência (reboot e ver se mantém).
- [ ] Charts session: abrir sessão (sensor-charts-session), heartbeat, session-end.
- [ ] History: medir várias vezes, ler history, ler history-file, confirmar dados gravados.
- [ ] Reset stats (sensor-reset-stats) → zeros após reset.
- [ ] Flush (sensor-flush) → limpa buffer.
- [ ] Reboot pelo meio: após configurar, reboot, confirmar que config e últimos dados
      sobrevivem (LittleFS).

## 2. Página MQ135 (qualidade ar)
Endpoints: /api/mq135-config (GET/POST), /api/mq135-measure (POST),
/api/mq135-calibrate-r0 (POST).
Testes:
- [ ] Measure → ppm plausível (ou erro se sensor desligado — documentar).
- [ ] Config (threshold, enable) → persistência + reboot.
- [ ] Calibrate R0 → valor altera, persiste.
- [ ] Reboot pelo meio.

## 3. Página RS232
Endpoints: /api/rs232-status (GET), /api/rs232/persist-current-template (POST),
+ send/receive (verificar handlers rs232).
Testes:
- [ ] Status reflete estado (baud, connected).
- [ ] Enviar comando por RS232 → eco/resposta (se houver dispositivo ligado; senão documentar).
- [ ] Persistir template atual → sobrevive a reboot.
- [ ] Reboot pelo meio.

## 4. Página Scheduler
Testes:
- [ ] Criar agendamento (comando/envio) → dispara à hora certa.
- [ ] Persistência do schedule (reboot não perde).
- [ ] Editar/eliminar agendamento.
- [ ] Reboot pelo meio.

## 5. Página Commands
Testes:
- [ ] Enviar comando → executa (resposta/log).
- [ ] Histórico de comandos persiste.
- [ ] Reboot pelo meio.

## 6. Página Files
Testes:
- [ ] Listar ficheiros (SDCard + LittleFS).
- [ ] Upload/download (se suportado).
- [ ] Reboot pelo meio (ficheiros mantêm-se).

## 7. Página Recipients
Endpoints: /api/recipient-slot (GET/POST), /api/sensorday-extra-emails (GET/POST).
Testes:
- [ ] Configurar slot de destinatário → persistência + reboot.
- [ ] sensorday-extra-emails → persistência + reboot.
- [ ] NÃO mexer em email real do board (só destinatários/config local).

## 8. Página Monitor (Web Serial Monitor)
Endpoints: /api/serial-monitor (GET, JSON), /api/monitor-open/close (POST).
Testes:
- [ ] Abrir monitor, gerar log (ex.: comando), confirmar que aparece no JSON.
- [ ] Fechar monitor.
- [ ] Reboot pelo meio (monitor volta fechado por def.).

## 9. OTA / Reboot (infra)
- [ ] enable-ota + firmware-upload (via curl, user:pass) → reboot, confirmar uptime baixo.
- [ ] Reboot por API (/api/restart) → confirma reboot.
- [ ] VÁRIOS reboots pelo meio em TODAS as secções acima.

## 10. Repor defaults (fim)
- [ ] Restaurar configs originais do snapshot (secção 0).
- [ ] Confirmar que WiFi e email NÃO foram alterados.
- [ ] Reboot final e confirmar estado limpo.

## 11. Documentar divergências
- [ ] Listar o que não bateu com o esperado (endpoints 404, valores absurdos,
      perda de persistência após reboot, etc.) → ficheiro DIVERGENCIAS.md.

## Entregáveis
- Este plano em SHARE/HomeE32/PLANO_TESTES.md
- Snapshot inicial em SHARE/HomeE32/snapshot_inicial.json (ou .txt)
- DIVERGENCIAS.md com resultados
- (Opcional) script de curl para correr os testes de API
