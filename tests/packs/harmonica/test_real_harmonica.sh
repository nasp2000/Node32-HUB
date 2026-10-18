#!/usr/bin/env bash
# =============================================================================
# tests/packs/harmonica/test_real_harmonica.sh — Teste REAL de persistência: RADIO (.30)
# -----------------------------------------------------------------------------
# Um único ficheiro por pack. Faz em modo real:
#   escreve config -> reboot -> lê -> compara -> repõe (snapshot).
# Campos de persistência críticos (LittleFS):
#   radio: favorites add, schedule (slots), volume/resume state, queue
#   system: mdns_enabled, system_name, system_language, timezone
# Endpoint 404 (pack não expõe) => SKIP (não FAIL).
# Requer: ../lib_persist.sh (em tests/hw/).
# Uso: bash test_real_harmonica.sh   (IP/AUTH via env TEST_IP/TEST_AUTH ou defaults)
# =============================================================================
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="$(cd "$HERE/../../hw" && pwd)/lib_persist.sh"
[ -f "$LIB" ] || LIB="$(cd "$HERE/../.." && pwd)/tests/hw/lib_persist.sh"
source "$LIB" || { echo "lib_persist.sh não encontrada ($LIB)"; exit 2; }

BASE="http://${TEST_IP:?define TEST_IP com o IP do device}"
AUTHARG=()
if [ -n "${TEST_AUTH:-}" ]; then AUTHARG=(-u "$TEST_AUTH"); else AUTHARG=(); fi

P_PASS=0; P_FAIL=0; P_SKIP=0
declare -a P_FAILS P_SKIPS

echo "===== TESTE REAL PERSISTÊNCIA — Harmonica/RADIO @ $BASE ====="

# ---- Radio favorites ----
phttp GET "/api/radio/favorites"
if [ "$HTTP_CODE" != "404" ]; then
  # add a favorite, reboot, confirm still there
  phttp POST "/api/radio/favorites/add" "name=PERSTEST&url=http://example.com/test.m3u"
  echo "  favorites/add -> HTTP $HTTP_CODE"
  preboot_and_wait || { echo "ABORT reboot"; exit 2; }
  phttp GET "/api/radio/favorites"
  favs=$(printf '%s' "$HTTP_BODY" | python3 -c "import sys,json
try:
    d=json.load(sys.stdin); items=d.get('items',d.get('favorites',[]))
    print('PERSTEST' if any('PERSTEST' in str(x) for x in items) else 'MISSING')
except: print('PARSE_ERR')" 2>/dev/null)
  pcheck "radio.favorites" "PERSTEST" "$favs"
  # cleanup
  phttp POST "/api/radio/favorites/remove" "name=PERSTEST" >/dev/null 2>&1
else
  pskip "radio/favorites (404)"
fi

# ---- Radio schedule ----
phttp GET "/api/radio/schedule"
if [ "$HTTP_CODE" != "404" ]; then
  SCHED='{"slots":[{"day":0,"hour":7,"min":0,"url":"http://example.com/a.m3u"},{"day":6,"hour":20,"min":0,"url":"http://example.com/b.m3u"}]}'
  phttp POST "/api/radio/schedule" "$SCHED"
  echo "  schedule -> HTTP $HTTP_CODE"
  preboot_and_wait || { echo "ABORT reboot"; exit 2; }
  phttp GET "/api/radio/schedule"
  sch=$(printf '%s' "$HTTP_BODY" | python3 -c "import sys,json
try:
    d=json.load(sys.stdin); s=d.get('slots',[])
    print('OK' if len(s)>=2 else 'SHORT')
except: print('PARSE_ERR')" 2>/dev/null)
  pcheck "radio.schedule" "OK" "$sch"
else
  pskip "radio/schedule (404)"
fi

# ---- Radio resume/state (volume) ----
phttp GET "/api/radio/status"
if [ "$HTTP_CODE" != "404" ]; then
  phttp POST "/api/radio/volume" "volume=25"
  echo "  volume 25 -> HTTP $HTTP_CODE"
  preboot_and_wait || { echo "ABORT reboot"; exit 2; }
  phttp GET "/api/radio/status"
  vol=$(printf '%s' "$HTTP_BODY" | pfield volume)
  pcheck "radio.volume" "25" "$vol"
else
  pskip "radio/status (404)"
fi

# ---- System (mdns/name/lang/tz) ----
phttp GET "/api/config?section=system"
if [ "$HTTP_CODE" != "404" ]; then
  SNAP_NAME=$(printf '%s' "$HTTP_BODY" | pfield system_name)
  SNAP_MDNS=$(printf '%s' "$HTTP_BODY" | pfield mdns_enabled)
  SNAP_LANG=$(printf '%s' "$HTTP_BODY" | pfield system_language)
  SNAP_TZ=$(printf '%s' "$HTTP_BODY" | pfield ntp.timezone)
  prun_section "system.mdns_enabled" "/api/config?section=system" \
    "section=system&system_name=PERSIST-TEST&system_language=pt&mdns_enabled_present=1&mdns_enabled=0&timezone=PERSIST-TZ" \
    "mdns_enabled" "false" \
    "section=system&system_name=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_NAME:-Harmonica}'))")&system_language=${SNAP_LANG:-pt}&mdns_enabled_present=1&mdns_enabled=${SNAP_MDNS:-true}&timezone=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_TZ:-WET0WEST,M3.5.0/1,M10.5.0}'))")"
else
  pskip "system (404)"
fi

pstatus
exit $?
