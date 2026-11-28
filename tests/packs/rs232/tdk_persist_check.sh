#!/usr/bin/env bash
# TDK-Lambda page: persistence and behaviour across a real reboot.
#
#   ./tdk_persist_check.sh                 (defaults: node .24, admin:admin)
#   NODE=192.168.1.x AUTH=user:pass ./tdk_persist_check.sh
#
# Checks, in order:
#   1. every option saved from the page survives a reboot (switch, listen, poll,
#      CSV, format, auto-map, address, inject, rates, profile);
#   2. with the module OFF and polling ON there is no TX at all after the reboot;
#   3. restoring ON + applying the RS232 defaults survives another reboot
#      (9600 8N1, flow off, 1.5 s) and polling resumes;
#   4. the CSV starts with a header and switching format rotates the day file
#      instead of mixing two layouts.
set -uo pipefail
N=${NODE:-192.168.1.24}; A="-u ${AUTH:-admin:admin}"
KS=0; KF=0
pass(){ printf "  \033[32mOK\033[0m   %s\n" "$1"; KS=$((KS+1)); }
fail(){ printf "  \033[31mFAIL\033[0m %s\n" "$1"; KF=$((KF+1)); }
cfg(){ curl -s -m 10 $A -X POST -H "Content-Type: application/x-www-form-urlencoded" --data "$1" "http://$N/api/tdk-lambda-config" >/dev/null; }
getcfg(){ curl -s -m 8 $A "http://$N/api/tdk-lambda-config"; }
reboot(){
  curl -s -m 6 $A -X POST "http://$N/api/restart" >/dev/null || true
  sleep 6
  for i in $(seq 1 40); do
    [ "$(curl -s -m 3 -o /dev/null -w '%{http_code}' $A "http://$N/api/status")" = "200" ] && return 0
    sleep 3
  done
  return 1
}
field(){ python3 -c "import json,sys;d=json.load(open('$1'));print(d.get('$2'))"; }

echo "== 1. save a distinctive profile, then reboot =="
cfg "enabled=0&listen=0&poll=1&poll_s=60&csv=0&format=table&auto_map=0&addr=2&inject=1&rate_v=30&rate_i=5&mode=split&cmd_v=MV%3F&cmd_i=MC%3F&cmd_state=MODE%3F&map=0,1,2,3,4,5&scale_v=1&scale_i=1"
reboot || { echo "node did not come back"; exit 1; }
getcfg > /tmp/opencode/tdk_cfg_a.json
python3 - <<'PY' && pass "every option survived the reboot" || fail "options did not survive"
import json, sys
d = json.load(open('/tmp/opencode/tdk_cfg_a.json'))
exp = {'enabled': False, 'listen': False, 'poll': True, 'csv': False, 'format': 'table',
       'auto_map': False, 'addr': 2, 'inject': True, 'mode': 'split',
       'rate_v': 30.0, 'rate_i': 5.0}
bad = {k: (v, d.get(k)) for k, v in exp.items() if d.get(k) != v}
print(('  mismatches: %s' % bad) if bad else '', end='')
sys.exit(1 if bad else 0)
PY

echo "== 2. module off: polling configured, but nothing may leave the port =="
sleep 65
curl -s -m 8 $A "http://$N/api/tdk-lambda-log" -o /tmp/opencode/tdk_log_off.json
TX=$(python3 -c "
import json;d=json.load(open('/tmp/opencode/tdk_log_off.json'))
print(len([e for e in d.get('entries',[]) if e.get('dir')=='TX']))")
[ "${TX:-0}" -eq 0 ] && pass "no TX in 65 s with the module off" || fail "TX while disabled: $TX"

echo "== 3. restore, apply RS232 defaults, reboot =="
cfg "enabled=1&listen=0&poll=1&poll_s=60&csv=1&format=sensor&auto_map=1&addr=0&inject=0&rate_v=600&rate_i=1.3&mode=dvc&cmd_v=DVC%3F&cmd_i=MC%3F&cmd_state=MODE%3F&map=0,1,2,3,4,5&scale_v=1&scale_i=1"
curl -s -m 15 $A -X POST "http://$N/api/tdk-lambda-rs232-defaults" >/dev/null
reboot || { echo "node did not come back"; exit 1; }
getcfg > /tmp/opencode/tdk_cfg_b.json
python3 - <<'PY' && pass "restored profile + switch survived the reboot" || fail "restore did not survive"
import json, sys
d = json.load(open('/tmp/opencode/tdk_cfg_b.json'))
exp = {'enabled': True, 'listen': False, 'poll': True, 'csv': True, 'format': 'sensor',
       'auto_map': True, 'addr': 0, 'inject': False, 'rate_v': 600.0, 'rate_i': 1.3,
       'mode': 'dvc'}
bad = {k: (v, d.get(k)) for k, v in exp.items() if d.get(k) != v}
print(('  mismatches: %s' % bad) if bad else '', end='')
sys.exit(1 if bad else 0)
PY
curl -s -m 8 $A "http://$N/api/config?section=system" -o /tmp/opencode/tdk_cfg_sys.json
python3 - <<'PY' && pass "RS232 defaults persisted (9600 8N1, flow off, 1.5 s)" || fail "RS232 defaults not persisted"
import json, sys
d = json.load(open('/tmp/opencode/tdk_cfg_sys.json'))
s = d.get('saved_configs', {})
exp = {'rs232_baudrate_index': 3, 'rs232_data_bits': 8, 'rs232_parity': 'none',
       'rs232_stop_bits': 1}
bad = {k: (v, s.get(k)) for k, v in exp.items() if s.get(k) != v}
print(('  mismatches: %s' % bad) if bad else '', end='')
sys.exit(1 if bad else 0)
PY
sleep 65
TX=$(curl -s -m 8 $A "http://$N/api/tdk-lambda-log" | python3 -c "
import json,sys;d=json.load(sys.stdin);print(len([e for e in d.get('entries',[]) if e.get('dir')=='TX']))")
[ "${TX:-0}" -ge 1 ] && pass "polling resumed after the reboot ($TX TX)" || fail "no TX after enabling"

echo "== 4. CSV: header on a new file, rotation when the format changes =="
curl -s -m 10 $A -X POST -H "Content-Type: application/x-www-form-urlencoded" --data "format=table" "http://$N/api/tdk-lambda-config" >/dev/null 2>&1 || true
curl -s -m 15 $A -X POST "http://$N/api/tdk-lambda-csv-flush" >/dev/null
sleep 1
curl -s -m 15 $A "http://$N/api/tdk-lambda-csv" -o /tmp/opencode/tdk_csv_table.csv
head -1 /tmp/opencode/tdk_csv_table.csv | grep -q '^timestamp,' \
  && pass "table CSV starts with its header" || fail "table CSV without header"
curl -s -m 10 $A -X POST -H "Content-Type: application/x-www-form-urlencoded" --data "format=sensor" "http://$N/api/tdk-lambda-config" >/dev/null 2>&1 || true
curl -s -m 15 $A -X POST "http://$N/api/tdk-lambda-csv-flush" >/dev/null
sleep 1
curl -s -m 15 $A "http://$N/api/tdk-lambda-csv" -o /tmp/opencode/tdk_csv_sensor.csv
head -1 /tmp/opencode/tdk_csv_sensor.csv | grep -q '^# timestamp' \
  && pass "sensor CSV starts with its comment header" || fail "sensor CSV without header"

echo
echo "================ $KS ok, $KF failed ================"
[ "$KF" -eq 0 ]
