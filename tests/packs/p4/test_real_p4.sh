#!/usr/bin/env bash
# =============================================================================
# tests/packs/p4/test_real_p4.sh — Teste REAL de persistência: P4 CNC (.7)
# -----------------------------------------------------------------------------
# Um único ficheiro por pack. Faz em modo real:
#   escreve config -> reboot -> lê -> compara -> repõe (snapshot).
# Campos de persistência (LittleFS) relevantes no P4:
#   usb_server: enabled / port (se pack expõe USB_HOST server)
#   sdcard: enabled / flush_interval
#   system: mdns_enabled, system_name, system_language, timezone
# NOTA: GRBL runtime (fila, posição, PSRAM) é VOLÁTIL — não testar persistência.
# Endpoint 404 => SKIP (não FAIL).
# Requer: ../../tests/hw/lib_persist.sh
# Uso: bash test_real_p4.sh   (IP/AUTH via env TEST_IP/TEST_AUTH ou defaults)
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

echo "===== TESTE REAL PERSISTÊNCIA — P4 CNC @ $BASE ====="

# System (mdns/name/lang/tz) — se expõe
phttp GET "/api/config?section=system"
if [ "$HTTP_CODE" != "404" ]; then
  SNAP_NAME=$(printf '%s' "$HTTP_BODY" | pfield system_name)
  SNAP_MDNS=$(printf '%s' "$HTTP_BODY" | pfield mdns_enabled)
  SNAP_LANG=$(printf '%s' "$HTTP_BODY" | pfield system_language)
  SNAP_TZ=$(printf '%s' "$HTTP_BODY" | pfield ntp.timezone)
  prun_section "system.mdns_enabled" "/api/config?section=system" \
    "section=system&system_name=PERSIST-TEST&system_language=pt&mdns_enabled_present=1&mdns_enabled=0&timezone=PERSIST-TZ" \
    "mdns_enabled" "false" \
    "section=system&system_name=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_NAME:-P4CNC}'))")&system_language=${SNAP_LANG:-pt}&mdns_enabled_present=1&mdns_enabled=${SNAP_MDNS:-true}&timezone=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_TZ:-WET0WEST,M3.5.0/1,M10.5.0}'))")"
else
  pskip "system (404)"
fi

# SD card config
phttp GET "/api/sdcard-config"
if [ "$HTTP_CODE" != "404" ]; then
  SD_PRE=$(printf '%s' "$HTTP_BODY" | pfield enabled)
  prun_section "sdcard.enabled" "/api/sdcard-config" \
    "enabled=$([ "$SD_PRE" = "true" ] && echo false || echo true)" \
    "enabled" "$([ "$SD_PRE" = "true" ] && echo false || echo true)" \
    "enabled=$SD_PRE"
else
  pskip "sdcard-config (404)"
fi

# USB server config (P4 tem USB_HOST para GRBL; usb_server só se ENABLE_USBIP_SERVER)
phttp GET "/api/usb-server-config"
if [ "$HTTP_CODE" != "404" ]; then
  US_PRE=$(printf '%s' "$HTTP_BODY" | pfield enabled)
  prun_section "usb_server.enabled" "/api/usb-server-config" \
    "enabled=$([ "$US_PRE" = "true" ] && echo false || echo true)" \
    "enabled" "$([ "$US_PRE" = "true" ] && echo false || echo true)" \
    "enabled=$US_PRE"
else
  pskip "usb-server-config (404 — esperado se pack não tem USBIP_SERVER)"
fi

# Repor
phttp POST "/api/config?section=system" \
  "section=system&system_name=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_NAME:-P4CNC}'))")&system_language=${SNAP_LANG:-pt}&mdns_enabled_present=1&mdns_enabled=${SNAP_MDNS:-true}&timezone=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_TZ:-WET0WEST,M3.5.0/1,M10.5.0}'))")" >/dev/null 2>&1

pstatus
exit $?
