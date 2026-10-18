# T7 — Runbook de teste: Storage / LittleFS sem SD / SensorDay (Enviroment .24)

> Executar em sessão interativa (o cron não tem HTTP para a LAN).
> Nunca alterar Wi-Fi nem email da board. No fim, repor todos os valores.

## 0. Registar defaults
- `GET /api/sdcard-config` → guardar JSON (write_pause_ms, active, format,
  email, cleanup_threshold, keep_days).
- `GET /api/sd-status` → guardar JSON (mounted, total/free, flash used/total).

## 1. Página Configurar Armazenamento (fix fcc3de1)
- Abrir `/sdcard`; confirmar que a página existe (antes estava desativada).
- Confirmar que a nota de Auto Cleanup renderiza negrito (sem tags
  `<strong>` literais) em PT e EN.
- Mudar `cleanup_threshold` e `keep_days`, Save, `GET /api/sdcard-config`
  (persistência), reboot, voltar a ler (persiste após reboot).

## 2. LittleFS sem SD
- `POST /api/sdcard-config-save` `{unmount:1}` (ou retirar o cartão — humano).
- Gerar dados (aguardar flush ou forçar via `/api/sd-action`).
- `GET /api/sd-status` → `flash_used_bytes` subiu; ficheiros em
  `/data_fallback` (via página Files); só `.csv/.txt/.log/.chart`, nada em
  `/config` apagado.
- `POST ... {mount:1}`; confirmar retorno a SD.

## 3. SensorDay
- Enviar `sensorday` na página de comandos; confirmar email recebido (humano).
- Repetir com data (`sensorday DD-MM-AAAA`) para dia com fallback Flash.

## 4. Repor e fechar
- Repor `cleanup_threshold`/`keep_days`/restantes aos defaults do passo 0.
- `GET /api/sdcard-config` final = inicial. Registar divergências aqui.
