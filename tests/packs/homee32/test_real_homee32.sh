#!/usr/bin/env bash
# =============================================================================
# tests/packs/homee32/test_real_homee32.sh — Teste REAL de persistência: ENVIRONMENT (.24)
# -----------------------------------------------------------------------------
# Um único ficheiro por pack. Faz em modo real:
#   escreve config -> reboot -> lê -> compara -> repõe (snapshot).
# A HomeE32 (pack ENVIRONMENT) expõe: system, rs232, email, access.
# NÃO expõe (GET 302 -> SKIP): inactivity-alert-config, proximity, eth-config.
# Requer: ../../tests/hw/lib_persist.sh
# Uso: bash test_real_homee32.sh   (IP/AUTH via env TEST_IP/TEST_AUTH ou defaults)
# =============================================================================
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="$(cd "$HERE/../../hw" && pwd)/lib_persist.sh"
[ -f "$LIB" ] || LIB="$(cd "$HERE/../.." && pwd)/tests/hw/lib_persist.sh"
source "$LIB" || { echo "lib_persist.sh não encontrada ($LIB)"; exit 2; }

BASE="http://${TEST_IP:-192.168.1.24}"
AUTHARG=()
if [ -n "${TEST_AUTH:-}" ]; then AUTHARG=(-u "$TEST_AUTH"); else AUTHARG=(-u admin:admin); fi

P_PASS=0; P_FAIL=0; P_SKIP=0
declare -a P_FAILS P_SKIPS

echo "===== TESTE REAL PERSISTÊNCIA — HomeE32 @ $BASE ====="

# ---- Snapshot do estado atual ----
phttp GET "/api/config?section=system"
SNAP_NAME=$(printf '%s' "$HTTP_BODY" | pfield system_name)
SNAP_LANG=$(printf '%s' "$HTTP_BODY" | pfield system_language)
SNAP_MDNS=$(printf '%s' "$HTTP_BODY" | pfield mdns_enabled)
# normaliza snapshot para lowercase boolean
case "$SNAP_MDNS" in True) SNAP_MDNS=true;; False) SNAP_MDNS=false;; esac

# ---- system.system_name ----
prun_section "system.system_name" "/api/config?section=system" \
  "section=system&system_name=PERSIST-H32&system_language=pt&mdns_enabled_present=1&mdns_enabled=${SNAP_MDNS:-true}" \
  "system_name" "PERSIST-H32" \
  "section=system&system_name=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_NAME:-HomeE32}'))")&system_language=${SNAP_LANG:-pt}&mdns_enabled_present=1&mdns_enabled=${SNAP_MDNS:-true}"

# ---- system.system_language ----
phttp GET "/api/config?section=system"
SNAP_LANG_NOW=$(printf '%s' "$HTTP_BODY" | pfield system_language)
prun_section "system.system_language" "/api/config?section=system" \
  "section=system&system_name=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_NAME:-HomeE32}'))")&system_language=en&mdns_enabled_present=1&mdns_enabled=${SNAP_MDNS:-true}" \
  "system_language" "en" \
  "section=system&system_name=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_NAME:-HomeE32}'))")&system_language=${SNAP_LANG_NOW:-pt}&mdns_enabled_present=1&mdns_enabled=${SNAP_MDNS:-true}"

# ---- system.mdns_enabled ----
prun_section "system.mdns_enabled" "/api/config?section=system" \
  "section=system&system_name=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_NAME:-HomeE32}'))")&system_language=${SNAP_LANG:-pt}&mdns_enabled_present=1&mdns_enabled=0" \
  "mdns_enabled" "false" \
  "section=system&system_name=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_NAME:-HomeE32}'))")&system_language=${SNAP_LANG:-pt}&mdns_enabled_present=1&mdns_enabled=${SNAP_MDNS:-true}"

# ---- rs232.enabled ----
phttp GET "/api/config?section=rs232"
if [ "$HTTP_CODE" != "404" ]; then
  R_PRE=$(printf '%s' "$HTTP_BODY" | pfield rs232.enabled)
  case "$R_PRE" in True) R_PRE=true;; False) R_PRE=false;; esac
  R_NEW=$([ "$R_PRE" = "true" ] && echo false || echo true)
  prun_section "rs232.enabled" "/api/config?section=rs232" \
    "section=rs232&rs232_enabled=$R_NEW" \
    "rs232.enabled" "$R_NEW" \
    "section=rs232&rs232_enabled=$R_PRE"
else
  pskip "rs232 (404)"
fi

# ---- email.smtp_server ----
phttp GET "/api/config?section=email"
if [ "$HTTP_CODE" != "404" ]; then
  E_PRE=$(printf '%s' "$HTTP_BODY" | pfield smtp.server)
  E_NEW="smtp.persist.test"
  [ "$E_PRE" = "$E_NEW" ] && E_NEW="smtp.persist.test2"
  prun_section "email.smtp_server" "/api/config?section=email" \
    "section=email&smtp_server=$E_NEW" \
    "smtp.server" "$E_NEW" \
    "section=email&smtp_server=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${E_PRE:-}'))")"
else
  pskip "email (404)"
fi

# ---- access.max_sessions ----
phttp GET "/api/access-config"
if [ "$HTTP_CODE" != "404" ]; then
  A_PRE=$(printf '%s' "$HTTP_BODY" | pfield web.max_sessions)
  A_NEW=$([ "$A_PRE" = "4" ] && echo 5 || echo 4)
  prun_section "access.max_sessions" "/api/access-config" \
    "web_max_sessions=$A_NEW&web_idle_timeout_min=15" \
    "web.max_sessions" "$A_NEW" \
    "web_max_sessions=${A_PRE:-3}&web_idle_timeout_min=10"
else
  pskip "access (404)"
fi

# ---- inactivity / proximity / eth: GET 302 => não expostos neste pack => SKIP ----
for ep in inactivity-alert-config proximity/config eth-config; do
  phttp GET "/api/$ep"
  if [ "$HTTP_CODE" = "302" ] || [ "$HTTP_CODE" = "404" ]; then
    pskip "$ep (não exposto: $HTTP_CODE)"
  fi
done

# ---- Repor sistema para snapshot ----
phttp POST "/api/config?section=system" \
  "section=system&system_name=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_NAME:-HomeE32}'))")&system_language=${SNAP_LANG:-pt}&mdns_enabled_present=1&mdns_enabled=${SNAP_MDNS:-true}" >/dev/null 2>&1

pstatus
exit $?
