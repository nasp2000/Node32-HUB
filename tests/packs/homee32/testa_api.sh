#!/usr/bin/env bash
# Teste API HomeE32 (TEST_IP, Pack ENVIRONMENT) — corre via curl, regista resultados.
B="${TEST_IP:?define TEST_IP com o IP do device}"
AUTH="${TEST_AUTH:+-u $TEST_AUTH}"
OUT=/home/nasp/Code/SHARE/HomeE32/testes_api_resultado.txt
> "$OUT"
log(){ echo "$*" | tee -a "$OUT"; }

log "===== TESTE API HomeE32 ($(date)) ====="
log "Board: $B  Pack: ENVIRONMENT (HomeE32)  ESP32-S3 N16R8"

# 0. Acessibilidade
log ""
log "--- 0. ACESSIBILIDADE ---"
for p in / /Home32 /api/configs /status; do
  c=$(curl -s -m 6 "http://$B$p" -o /dev/null -w "%{http_code}" 2>/dev/null)
  log "GET $p -> $c"
done

# Mapear status real
log ""
log "--- 0b. MAPEAR STATUS ---"
for p in api/status system/status info health; do
  c=$(curl -s -m 6 "http://$B/$p" -o /tmp/h.txt -w "%{http_code}" 2>/dev/null)
  log "GET /$p -> $c | head: $(head -c 120 /tmp/h.txt | tr '\n' ' ')"
done

# 1. Sensor (AHT20/BMP280)
log ""
log "--- 1. SENSORES (AHT20/BMP280) ---"
for ep in api/sensor-config api/sensor-slots api/sensor-flags api/sensor-measure api/sensor-reset-stats api/sensor-flush api/sensor-history api/sensor-history-files; do
  c=$(curl -s -m 6 "http://$B/$ep" -o /tmp/h.txt -w "%{http_code}" 2>/dev/null)
  log "GET /$ep -> $c | $(head -c 150 /tmp/h.txt | tr '\n' ' ')"
done
# POST sensor-measure
c=$(curl -s -m 8 $AUTH -X POST "http://$B/api/sensor-measure" -o /tmp/h.txt -w "%{http_code}" 2>/dev/null)
log "POST /api/sensor-measure -> $c | $(head -c 150 /tmp/h.txt | tr '\n' ' ')"
# POST sensor-config (enable + intervalo)
c=$(curl -s -m 8 $AUTH -X POST "http://$B/api/sensor-config" -H "Content-Type: application/json" -d '{"enabled":true,"interval_ms":5000}' -o /tmp/h.txt -w "%{http_code}" 2>/dev/null)
log "POST /api/sensor-config (enable,5s) -> $c | $(head -c 150 /tmp/h.txt | tr '\n' ' ')"

# 2. MQ135
log ""
log "--- 2. MQ135 ---"
for ep in api/mq135-config api/mq135-measure api/mq135-calibrate-r0; do
  c=$(curl -s -m 6 "http://$B/$ep" -o /tmp/h.txt -w "%{http_code}" 2>/dev/null)
  log "GET /$ep -> $c | $(head -c 120 /tmp/h.txt | tr '\n' ' ')"
done
c=$(curl -s -m 8 $AUTH -X POST "http://$B/api/mq135-measure" -o /tmp/h.txt -w "%{http_code}" 2>/dev/null)
log "POST /api/mq135-measure -> $c | $(head -c 120 /tmp/h.txt | tr '\n' ' ')"

# 3. RS232
log ""
log "--- 3. RS232 ---"
for ep in api/rs232-status api/rs232/persist-current-template; do
  c=$(curl -s -m 6 "http://$B/$ep" -o /tmp/h.txt -w "%{http_code}" 2>/dev/null)
  log "GET /$ep -> $c | $(head -c 120 /tmp/h.txt | tr '\n' ' ')"
done

# 4-7. Scheduler / Commands / Files / Recipients
log ""
log "--- 4-7. SCHEDULER/COMMANDS/FILES/RECIPIENTS ---"
for ep in api/scheduler api/commands api/files api/recipient-slot api/sensorday-extra-emails; do
  c=$(curl -s -m 6 "http://$B/$ep" -o /tmp/h.txt -w "%{http_code}" 2>/dev/null)
  log "GET /$ep -> $c | $(head -c 120 /tmp/h.txt | tr '\n' ' ')"
done

# 8. Monitor
log ""
log "--- 8. MONITOR ---"
for ep in api/serial-monitor api/monitor-open api/monitor-close; do
  c=$(curl -s -m 6 "http://$B/$ep" -o /tmp/h.txt -w "%{http_code}" 2>/dev/null)
  log "GET /$ep -> $c | $(head -c 120 /tmp/h.txt | tr '\n' ' ')"
done

# 9. Restart (cuidado: reboot)
log ""
log "--- 9. RESTART ---"
c=$(curl -s -m 8 $AUTH -X POST "http://$B/api/restart" -o /tmp/h.txt -w "%{http_code}" 2>/dev/null)
log "POST /api/restart -> $c | $(head -c 80 /tmp/h.txt | tr '\n' ' ')"

log ""
log "===== FIM TESTE API HomeE32 ====="
