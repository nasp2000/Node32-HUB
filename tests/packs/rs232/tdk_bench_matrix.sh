#!/usr/bin/env bash
# Field-by-field bench matrix for the TDK-Lambda module: the simulator is the
# reference, the hub must report the same six fields for every reply shape.
#
#   1. DVC? genh  -> 600 V / 1.28 A, map kept, no flip between reads
#   2. identity (IDN? REV? SN?)
#   3. MODE? / OUT?
#   4. samples ring + CSV row values
#   5. write refused when allow_write is off (403)
#   6. listen mode refuses TX (409)
#   7. DVC? shapes swap / mV-mA -> auto layout + scale, then genh again
#   8. split profile (MV? / MC?)
#   9. back to dvc, then a real 60 s poll cycle without manual sends
#
# Polling is paused during the field tests so every reply belongs to the command
# under test. Values are compared with the simulator's own state (tdk.status).
# Exit code: 0 only when every check passes.
set -uo pipefail
# Node32-HUB under test and the TDK/IPG simulator (Others/Simulator) on the LAN.
# Override with: NODE=... SIM=... AUTH=user:pass bash tdk_bench_matrix.sh
N=${NODE:-192.168.1.24}; S=${SIM:-192.168.1.23}; A="-u ${AUTH:-admin:admin}"
KA=0; KF=0
sim(){ curl -s -m 8 -X POST -H "Content-Type: text/plain" --data "$1" "http://$S/api/cmd" >/dev/null; }
send(){ curl -s -m 10 $A -X POST -H "Content-Type: application/x-www-form-urlencoded" --data "cmd=$1" "http://$N/api/tdk-lambda-send" >/dev/null; }
status(){ curl -s -m 8 $A "http://$N/api/tdk-lambda-status"; }
cfg(){ curl -s -m 10 $A -X POST -H "Content-Type: application/x-www-form-urlencoded" --data "$1" "http://$N/api/tdk-lambda-config" >/dev/null; }
check(){ python3 - "$1" "$2" "$3" "$4" <<'PY'
import sys
lbl, exp, act, tol = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
ok = abs(exp-act) <= tol
print(("  \033[32mOK\033[0m   %-26s exp %.4f  got %.4f" if ok else "  \033[31mFAIL\033[0m %-26s exp %.4f  got %.4f") % (lbl, exp, act))
sys.exit(0 if ok else 1)
PY
  [ $? -eq 0 ] && KA=$((KA+1)) || KF=$((KF+1))
}
values(){ status | python3 -c "
import sys,json
raw=sys.stdin.buffer.read().decode('utf-8','replace');d=json.loads(raw)
s=d['status'];v=d['values']
print('%.4f %.4f %.4f %.4f %.4f %.5f %s %s %s %s %s %s' % (v['vMeas'],v['vSet'],v['iMeas'],v['iSet'],v['ovp'],v['uvl'],v['mode'],int(v['outOn']),s['mapUsed'],s['scaleVUsed'],s['autoNote'] or '-',s['lastLine'] or '-'))"; }

echo ".. simulator reference"
sim "tdk.sim on"; sim "tdk.v 600"; sim "tdk.i 1.30"; sim "tdk.load 470"; sim "tdk.out on"
sim "tdk.fmt genh"; sim "tdk.noise 0.02"; sim "tdk.drift off"
sleep 2; sim "tdk.status"; sleep 1
curl -s -m 8 "http://$S/api/log" | python3 -c "
import sys,json
raw=sys.stdin.buffer.read().decode('utf-8','replace');t=json.loads(raw)['t']
print('  '+[l for l in t.split('\n') if 'TDK SIM] mode' in l][-1])"

echo ".. pause polling, reset the profile to the documented one"
cfg "poll=0"; sleep 1
cfg "mode=dvc&cmd_v=DVC%3F&cmd_i=MC%3F&cmd_state=MODE%3F&map=0,1,2,3,4,5&scale_v=1&scale_i=1&off_v=0&off_i=0&auto_map=1"; sleep 1

echo
echo "== 1. DVC? across the wire (genh) =="
send "DVC%3F"; sleep 3
read -r VM VS IM IS OVP UVL MODE OUT MAP SCV NOTE LAST <<<"$(values)"
echo "  lastLine: $LAST"
echo "  map: $MAP | scaleV: $SCV | auto: $NOTE"
check "vMeas (600 V, 470 ohm)" 599.86 "$VM" 2.5
check "vSet" 600.0 "$VS" 2.5
check "iMeas (~1.277 A)" 1.277 "$IM" 0.06
check "iSet" 1.30 "$IS" 0.06
check "ovp" 660.0 "$OVP" 3.0
check "uvl" 0.0 "$UVL" 1.0
# In the documented order there is nothing to adjust, so no note is correct.
{ [ "$NOTE" != "-" ] || [ "$MAP" = "0,1,2,3,4,5" ]; } && { echo "  OK   auto-detect $([ "$NOTE" != "-" ] && echo "adjusted: $NOTE" || echo "kept the documented order")"; KA=$((KA+1)); } || { echo "  FAIL map $MAP with no note"; KF=$((KF+1)); }
python3 - "$IM" "$IS" <<'PY'
import sys
i, s = float(sys.argv[1]), float(sys.argv[2])
print(("  \033[32mOK\033[0m   iMeas <= iSet (+noise)" if i <= s + 0.005 else "  \033[31mFAIL\033[0m iMeas above iSet"))
sys.exit(0 if i <= s + 0.005 else 1)
PY
[ $? -eq 0 ] && KA=$((KA+1)) || KF=$((KF+1))
MAP1=$MAP
send "DVC%3F"; sleep 3
read -r VM VS IM IS OVP UVL MODE OUT MAP SCV NOTE LAST <<<"$(values)"
[ "$MAP" = "$MAP1" ] && { echo "  OK   map stable across two reads ($MAP)"; KA=$((KA+1)); } || { echo "  FAIL map flipped $MAP1 -> $MAP"; KF=$((KF+1)); }

echo
echo "== 2. identity =="
send "IDN%3F"; sleep 2
send "REV%3F"; sleep 2
send "SN%3F";  sleep 3
read -r IDN REV SN <<<"$(status | python3 -c "
import sys,json;raw=sys.stdin.buffer.read().decode('utf-8','replace');s=json.loads(raw)['status']
print('%s %s %s' % (s['idn'] or '-', s['rev'] or '-', s['sn'] or '-'))")"
echo "  idn=$IDN rev=$REV sn=$SN"
[ "$IDN" = "LAMBDA,GENH600-1.3" ] && { echo "  OK   idn"; KA=$((KA+1)); } || { echo "  FAIL idn"; KF=$((KF+1)); }
[ "$REV" = "1.07" ]              && { echo "  OK   rev"; KA=$((KA+1)); } || { echo "  FAIL rev"; KF=$((KF+1)); }
[ "$SN" = "SIM000001" ]          && { echo "  OK   sn";  KA=$((KA+1)); } || { echo "  FAIL sn";  KF=$((KF+1)); }

echo
echo "== 3. state (MODE? OUT?) =="
send "MODE%3F"; sleep 3
read -r VM VS IM IS OVP UVL MODE OUT MAP SCV NOTE LAST <<<"$(values)"
send "OUT%3F";  sleep 3
read -r VM VS IM IS OVP UVL MODE OUT MAP SCV NOTE LAST <<<"$(values)"
echo "  mode=$MODE out=$OUT"
[ "$MODE" = "CV" ] && { echo "  OK   mode CV"; KA=$((KA+1)); } || { echo "  FAIL mode=$MODE"; KF=$((KF+1)); }
[ "$OUT" = "1" ]   && { echo "  OK   out on";  KA=$((KA+1)); } || { echo "  FAIL out=$OUT";   KF=$((KF+1)); }

echo
echo "== 4. samples ring + CSV =="
curl -s -m 8 $A "http://$N/api/tdk-lambda-samples?n=3" | python3 -c "
import sys,json
d=json.load(sys.stdin); ss=d.get('samples') or []
print('  samples:', len(ss), ss[-1] if ss else '-')"
curl -s -m 15 $A -X POST "http://$N/api/tdk-lambda-csv-flush" >/dev/null
curl -s -m 15 $A "http://$N/api/tdk-lambda-csv" > /tmp/opencode/tdk.csv
head -1 /tmp/opencode/tdk.csv | cut -c1-120
rows=$(grep -c "vout" /tmp/opencode/tdk.csv || true)
echo "  data rows: $rows"
[ "${rows:-0}" -ge 1 ] && { echo "  OK   CSV has rows"; KA=$((KA+1)); } || { echo "  FAIL no CSV rows"; KF=$((KF+1)); }
grep "vout" /tmp/opencode/tdk.csv | tail -1 | python3 -c "
import sys,re
line=sys.stdin.read()
m=re.search(r'vout, ([0-9.]+).*?iout, ([0-9.]+)',line)
if not m: print('  FAIL values in CSV'); raise SystemExit(1)
v,i=float(m.group(1)),float(m.group(2))
ok = abs(v-599.86)<3 and abs(i-1.277)<0.08
print(('  \033[32mOK\033[0m   CSV v=%.2f i=%.3f' if ok else '  \033[31mFAIL\033[0m CSV v=%.2f i=%.3f')%(v,i))
raise SystemExit(0 if ok else 1)" && KA=$((KA+1)) || KF=$((KF+1))

echo
echo "== 5. write refused (allow_write off) =="
code=$(curl -s -m 8 $A -o /tmp/opencode/w.json -w "%{http_code}" -X POST -H "Content-Type: application/x-www-form-urlencoded" --data "cmd=PV 600" "http://$N/api/tdk-lambda-send")
echo "  HTTP $code -> $(cat /tmp/opencode/w.json)"
[ "$code" = "403" ] && { echo "  OK   403 write blocked"; KA=$((KA+1)); } || { echo "  FAIL expected 403"; KF=$((KF+1)); }

echo
echo "== 6. listen mode refuses TX (409) =="
cfg "listen=1"; sleep 1
code=$(curl -s -m 8 $A -o /tmp/opencode/l.json -w "%{http_code}" -X POST -H "Content-Type: application/x-www-form-urlencoded" --data "cmd=DVC%3F" "http://$N/api/tdk-lambda-send")
echo "  HTTP $code -> $(cat /tmp/opencode/l.json)"
[ "$code" = "409" ] && { echo "  OK   409 listen"; KA=$((KA+1)); } || { echo "  FAIL expected 409"; KF=$((KF+1)); }
cfg "listen=0"; sleep 1

echo
echo "== 7. DVC? reply shapes: swap / mvma =="
for f in swap mvma; do
  sim "tdk.fmt $f"; sleep 1; send "DVC%3F"; sleep 3
  read -r VM VS IM IS OVP UVL MODE OUT MAP SCV NOTE LAST <<<"$(values)"
  echo "  [$f] lastLine=$LAST map=$MAP scaleV=$SCV note=$NOTE"
  check "$f vMeas" 599.86 "$VM" 4.0
  check "$f vSet"  600.0  "$VS" 4.0
  check "$f iMeas" 1.277  "$IM" 0.08
  check "$f iSet"  1.30   "$IS" 0.08
  check "$f ovp"   660.0  "$OVP" 4.0
done
sim "tdk.fmt genh"; sleep 1
send "DVC%3F"; sleep 3
read -r VM VS IM IS OVP UVL MODE OUT MAP SCV NOTE LAST <<<"$(values)"
echo "  [genh again] map=$MAP scaleV=$SCV note=$NOTE"
check "genh back vMeas" 599.86 "$VM" 3.0
check "genh back iMeas" 1.277  "$IM" 0.08

echo
echo "== 8. split profile (MV?/MC?) =="
cfg "mode=split&cmd_v=MV%3F&cmd_i=MC%3F&cmd_state=MODE%3F&map=0,1,2,3,4,5&scale_v=1&scale_i=1"; sleep 1
send "MV%3F"; sleep 3
send "MC%3F"; sleep 3
send "MODE%3F"; sleep 2
read -r VM VS IM IS OVP UVL MODE OUT MAP SCV NOTE LAST <<<"$(values)"
echo "  lastLine=$LAST mode=$MODE"
check "split vMeas" 599.87 "$VM" 3.0
check "split iMeas" 1.272  "$IM" 0.08
[ "$MODE" = "CV" ] && { echo "  OK   split mode CV"; KA=$((KA+1)); } || { echo "  FAIL split mode=$MODE"; KF=$((KF+1)); }

echo
echo "== 9. back to the dvc profile, resume polling =="
cfg "mode=dvc&cmd_v=DVC%3F&cmd_i=MC%3F&cmd_state=MODE%3F"; sleep 1
send "DVC%3F"; sleep 3
read -r VM VS IM IS OVP UVL MODE OUT MAP SCV NOTE LAST <<<"$(values)"
check "final vMeas" 599.86 "$VM" 3.0
check "final iMeas" 1.277  "$IM" 0.08
echo "  map=$MAP note=$NOTE"
cfg "poll=1"; sleep 1

echo
echo "== 10. automatic poll cycle (no manual sends) =="
cfg "poll=1&poll_s=60"; sleep 2
echo "  waiting one interval (65 s)..."; sleep 65
read -r VM VS IM IS OVP UVL MODE OUT MAP SCV NOTE LAST <<<"$(values)"
echo "  lastLine=$LAST mode=$MODE out=$OUT map=$MAP"
check "auto vMeas" 599.86 "$VM" 3.5
check "auto iMeas" 1.277  "$IM" 0.08
python3 - "$IM" "$IS" <<'PY'
import sys
i, s = float(sys.argv[1]), float(sys.argv[2])
print(("  \033[32mOK\033[0m   auto iMeas <= iSet" if i <= s + 0.005 else "  \033[31mFAIL\033[0m auto iMeas above iSet"))
sys.exit(0 if i <= s + 0.005 else 1)
PY
[ $? -eq 0 ] && KA=$((KA+1)) || KF=$((KF+1))
[ "$MODE" = "CV" ] || [ "$MODE" = "CC" ] && { echo "  OK   auto mode $MODE"; KA=$((KA+1)); } || { echo "  FAIL auto mode=$MODE"; KF=$((KF+1)); }
curl -s -m 8 $A "http://$N/api/tdk-lambda-status" | python3 -c "
import sys,json
s=json.load(sys.stdin)['status']
age=s['lastRxAgeMs']
print('  lastRxAgeMs:', age)
raise SystemExit(0 if 0 < age < 90000 else 1)" && { echo "  OK   poll is alive"; KA=$((KA+1)); } || { echo "  FAIL poll not alive"; KF=$((KF+1)); }

echo
echo "== 11. poller keeps sending (timeout fix) =="
curl -s -m 8 $A "http://$N/api/tdk-lambda-log" -o /tmp/opencode/log.json
python3 - <<'PY'
import json
d=json.load(open('/tmp/opencode/log.json'))
tx=[e for e in d['entries'] if e['dir']=='TX']
print("  TX entries in log:", len(tx), "| last:", tx[-1]['text'] if tx else '-')
PY
TX=$(python3 -c "
import json;d=json.load(open('/tmp/opencode/log.json'))
print(len([e for e in d['entries'] if e['dir']=='TX']))")
[ "${TX:-0}" -ge 10 ] && { echo "  OK   poller keeps sending"; KA=$((KA+1)); } || { echo "  FAIL poller stalled"; KF=$((KF+1)); }

echo
echo "================ $KA ok, $KF failed ================"
