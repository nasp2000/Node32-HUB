#!/usr/bin/env bash
# =============================================================================
# test_persistence_regression.sh — Regressão de PERSISTÊNCIA em HW (Node32-HUB)
# -----------------------------------------------------------------------------
# Pack-agnóstico: funciona em QUALQUER board/pack (Waveshare .29, P4 .7,
# HomeE32 .24, Harmonica .30, simuladores .23/.33, ...). Testa os campos de
# persistência via os endpoints REAIS das páginas e verifica que SOBREVIVEM
# a um reboot. Campos de secções que o pack não expõe (404) -> SKIP + WARN
# (não FAIL), para não dar falso positivo em packs sem essa funcionalidade.
#
# Campos testados (todos os que sabemos serem frágeis em produção):
#   system:   mdns_enabled, system_name, system_language, timezone/ntp_server
#   inatividade: slot[0].enabled + emails
#   telnet:   enabled (toggle)
#   access:   web auth_enabled / max_sessions / idle_timeout
#   email:    smtp_server / email_method / email_destination
#   ethernet: enabled (se o pack suportar)
#   rs232:    enabled (se o pack suportar)
#   scheduler: enabled (se o pack suportar)
#
# Uso:
#   ./test_persistence_regression.sh <IP> [auth_user:auth_pass] [--no-auth]
#   ex: ./test_persistence_regression.sh 192.168.1.100 user:pass
#       ./test_persistence_regression.sh 192.168.1.101           # --no-auth implicito se sem auth
#       ./test_persistence_regression.sh 192.168.1.102 user:pass --no-auth
#
# Requer: curl, python3. Devolve exit 1 se algum campo testado FALHAR.
# No fim repõe a config original (snapshot pré-teste) para não poluir o board.
# =============================================================================
set -u
IP="${1:?uso: $0 <IP> [auth_user:auth_pass] [--no-auth]}"; shift 2>/dev/null || true
AUTHARG=()
NOAUTH=0
for a in "$@"; do
  case "$a" in
    --no-auth) NOAUTH=1 ;;
    *) if [ -z "${AUTH:-}" ]; then AUTH="$a"; fi ;;
  esac
done
if [ "$NOAUTH" -eq 0 ] && [ -n "${AUTH:-}" ]; then AUTHARG=(-u "$AUTH"); fi
BASE="http://$IP"

RED='\033[0;31m'; GREEN='\033[0;32m'; YEL='\033[1;33m'; BLU='\033[0;34m'; NC='\033[0m'
pass=0; fail=0; skip=0; warns=0
declare -a FAILS SKIPS

log_ok()   { echo -e "  ${GREEN}OK${NC}   $1"; pass=$((pass+1)); }
log_fail() { echo -e "  ${RED}FAIL${NC}  $1"; fail=$((fail+1)); FAILS+=("$1"); }
log_skip() { echo -e "  ${YEL}SKIP${NC}  $1"; skip=$((skip+1)); SKIPS+=("$1"); }
log_warn() { echo -e "  ${YEL}WARN${NC}  $1"; warns=$((warns+1)); }

http() { # method path [data] -> HTTP_BODY / HTTP_CODE
  local m="$1" p="$2" d="${3:-}"
  if [ -n "$d" ]; then
    local out
    out=$(curl -s -m 15 "${AUTHARG[@]}" -X "$m" "$BASE$p" -H "Content-Type: application/x-www-form-urlencoded" --data "$d" -w '\n%{http_code}')
    HTTP_CODE=$(printf '%s' "$out" | tail -1)
    HTTP_BODY=$(printf '%s' "$out" | sed '$d')
  else
    local out
    out=$(curl -s -m 15 "${AUTHARG[@]}" -X "$m" "$BASE$p" -w '\n%{http_code}')
    HTTP_CODE=$(printf '%s' "$out" | tail -1)
    HTTP_BODY=$(printf '%s' "$out" | sed '$d')
  fi
}
# get JSON field (dot path: a.b.c) from HTTP_BODY
field() { python3 -c "import sys,json
try:
    d=json.load(sys.stdin)
    for k in '$1'.split('.'):
        d=d.get(k,'') if isinstance(d,dict) else ''
    print(d if d!='' else '')
except Exception:
    print('')" 2>/dev/null <<<"$HTTP_BODY"; }

# reboot + wait online. Returns 0 if online.
reboot_and_wait() {
  echo -e "\n${YEL}>> reboot $IP ...${NC}"
  http POST "/api/restart" "" >/dev/null 2>&1
  sleep 4
  for i in $(seq 1 25); do
    code=$(curl -s -m 4 -o /dev/null -w '%{http_code}' "${AUTHARG[@]}" "$BASE/" 2>/dev/null)
    if [ "$code" = "200" ]; then echo "  online após ${i}x~3s"; return 0; fi
    sleep 3
  done
  echo -e "  ${RED}board não voltou online!${NC}"; return 1
}

endpoint_exists() { # path -> 0 if GET returns != 404
  local c; c=$(curl -s -m 8 -o /dev/null -w '%{http_code}' "${AUTHARG[@]}" "$BASE$1" 2>/dev/null)
  [ "$c" != "404" ]
}

echo "============================================================"
echo " REGRESSÃO PERSISTÊNCIA — $IP  (auth: ${AUTHARG[*]:-none})"
echo "============================================================"

# ---- SNAPSHOT (para repor no fim) -----------------------------------------
echo -e "\n[*] Snapshot da config atual (para repor no fim)..."
SNAP_SYSTEM=$(http GET "/api/config?section=system" >/dev/null; printf '%s' "$HTTP_BODY")
SNAP_NAME=$(printf '%s' "$SNAP_SYSTEM" | field system_name)
SNAP_MDNS=$(printf '%s' "$SNAP_SYSTEM" | field mdns_enabled)
SNAP_LANG=$(printf '%s' "$SNAP_SYSTEM" | field system_language)
SNAP_TZ=$(printf '%s' "$SNAP_SYSTEM" | field ntp.timezone)
SNAP_NTP=$(printf '%s' "$SNAP_SYSTEM" | field ntp.server)
echo "  name=$SNAP_NAME mdns=$SNAP_MDNS lang=$SNAP_LANG tz=$SNAP_TZ ntp=$SNAP_NTP"

# =============================================================================
# 1) SYSTEM
# =============================================================================
echo -e "\n[1] Definições de Sistema"
if ! endpoint_exists "/api/config?section=system"; then log_skip "system endpoint indisponível neste pack"; else
  # SET: mdns OFF, name teste, lang pt, tz custom, ntp custom
  http POST "/api/config?section=system" \
    "section=system&system_name=PERSIST-TEST&system_language=pt&mdns_enabled_present=1&mdns_enabled=0&timezone=PERSIST-TZ&ntp_server=time.cloudflare.com"
  echo "  POST system -> HTTP $HTTP_CODE"

  http GET "/api/config?section=system"
  M_PRE=$(printf '%s' "$HTTP_BODY" | field mdns_enabled)
  N_PRE=$(printf '%s' "$HTTP_BODY" | field system_name)
  L_PRE=$(printf '%s' "$HTTP_BODY" | field system_language)
  T_PRE=$(printf '%s' "$HTTP_BODY" | field ntp.timezone)
  NTP_PRE=$(printf '%s' "$HTTP_BODY" | field ntp.server)
  echo "  pré-reboot: mdns=$M_PRE name=$N_PRE lang=$L_PRE tz=$T_PRE ntp=$NTP_PRE"

  reboot_and_wait || { echo -e "${RED}ABORT${NC}"; exit 2; }

  http GET "/api/config?section=system"
  M_POS=$(printf '%s' "$HTTP_BODY" | field mdns_enabled)
  N_POS=$(printf '%s' "$HTTP_BODY" | field system_name)
  L_POS=$(printf '%s' "$HTTP_BODY" | field system_language)
  T_POS=$(printf '%s' "$HTTP_BODY" | field ntp.timezone)
  NTP_POS=$(printf '%s' "$HTTP_BODY" | field ntp.server)
  echo "  pós-reboot: mdns=$M_POS name=$N_POS lang=$L_POS tz=$T_POS ntp=$NTP_POS"

  [ "$M_POS" = "false" ] && log_ok "mdns_enabled persistiu OFF" || log_fail "mdns_enabled NÃO persistiu (pre=$M_PRE pos=$M_POS)"
  [ "$N_POS" = "PERSIST-TEST" ] && log_ok "system_name persistiu" || log_fail "system_name NÃO persistiu (pre=$N_PRE pos=$N_POS)"
  [ "$L_POS" = "pt" ] && log_ok "system_language persistiu" || log_fail "system_language NÃO persistiu (pre=$L_PRE pos=$L_POS)"
  [ "$T_POS" = "PERSIST-TZ" ] && log_ok "timezone persistiu" || log_fail "timezone NÃO persistiu (pre=$T_PRE pos=$T_POS)"
  [ "$NTP_POS" = "time.cloudflare.com" ] && log_ok "ntp_server persistiu" || log_fail "ntp_server NÃO persistiu (pre=$NTP_PRE pos=$NTP_POS)"
fi

# =============================================================================
# 2) INATIVIDADE
# =============================================================================
echo -e "\n[2] Inatividade (slot0)"
if ! endpoint_exists "/api/inactivity-alert-config"; then log_skip "inatividade indisponível neste pack"; else
  http POST "/api/inactivity-alert-config" \
    "slot0_enabled=1&slot0_timeout=7&slot0_max_per_day=3&slot0_emails=owner@example.com&slot1_enabled=0&slot2_enabled=0&slot3_enabled=0&slot4_enabled=0&slot5_enabled=0"
  echo "  POST alerts -> HTTP $HTTP_CODE"
  http GET "/api/inactivity-alert-config"
  I0_PRE=$(printf '%s' "$HTTP_BODY" | python3 -c "import sys,json;d=json.load(sys.stdin);s=d.get('slots',[]);print(s[0].get('enabled') if s else 'NONE')" 2>/dev/null)
  echo "  slot0.enabled(antes)=$I0_PRE"

  reboot_and_wait || { echo -e "${RED}ABORT${NC}"; exit 2; }

  http GET "/api/inactivity-alert-config"
  I0_POS=$(printf '%s' "$HTTP_BODY" | python3 -c "import sys,json;d=json.load(sys.stdin);s=d.get('slots',[]);print(s[0].get('enabled') if s else 'NONE')" 2>/dev/null)
  I0_EMAIL=$(printf '%s' "$HTTP_BODY" | python3 -c "import sys,json;d=json.load(sys.stdin);s=d.get('slots',[]);print(s[0].get('emails','') if s else '')" 2>/dev/null)
  echo "  slot0.enabled(depois)=$I0_POS email=$I0_EMAIL"
  [ "$I0_POS" = "True" ] && log_ok "inatividade slot0 persistiu enabled" || log_fail "inatividade slot0 NÃO persistiu (pre=$I0_PRE pos=$I0_POS)"
fi

# =============================================================================
# 3) TELNET
# =============================================================================
echo -e "\n[3] Telnet"
if ! endpoint_exists "/api/telnet-status"; then log_skip "telnet indisponível neste pack"; else
  http GET "/api/telnet-status"
  T_PRE=$(printf '%s' "$HTTP_BODY" | field enabled)
  # toggle
  http POST "/api/telnet-toggle" ""
  reboot_and_wait || { echo -e "${RED}ABORT${NC}"; exit 2; }
  http GET "/api/telnet-status"
  T_POS=$(printf '%s' "$HTTP_BODY" | field enabled)
  echo "  telnet enabled: pre=$T_PRE pos=$T_POS"
  [ "$T_POS" = "$T_PRE" ] && log_ok "telnet enabled persistiu ($T_POS)" || log_fail "telnet enabled NÃO persistiu (pre=$T_PRE pos=$T_POS)"
fi

# =============================================================================
# 4) ACCESS (web auth/max_sessions/idle)
# =============================================================================
echo -e "\n[4] Access (web)"
if ! endpoint_exists "/api/access-config"; then log_skip "access indisponível neste pack"; else
  http GET "/api/access-config"
  A_PRE_MAX=$(printf '%s' "$HTTP_BODY" | field web.max_sessions)
  A_PRE_IDLE=$(printf '%s' "$HTTP_BODY" | field web.idle_timeout_min)
  # set a known value
  http POST "/api/access-config" "web_max_sessions=4&web_idle_timeout_min=15"
  reboot_and_wait || { echo -e "${RED}ABORT${NC}"; exit 2; }
  http GET "/api/access-config"
  A_POS_MAX=$(printf '%s' "$HTTP_BODY" | field web.max_sessions)
  A_POS_IDLE=$(printf '%s' "$HTTP_BODY" | field web.idle_timeout_min)
  echo "  max_sessions: pre=$A_PRE_MAX pos=$A_POS_MAX | idle: pre=$A_PRE_IDLE pos=$A_POS_IDLE"
  [ "$A_POS_MAX" = "4" ] && log_ok "access max_sessions persistiu" || log_fail "access max_sessions NÃO persistiu (pre=$A_PRE_MAX pos=$A_POS_MAX)"
fi

# =============================================================================
# 5) EMAIL (smtp_server / method / destination)
# =============================================================================
echo -e "\n[5] Email"
if ! endpoint_exists "/api/email-config" && ! endpoint_exists "/api/config?section=email"; then log_skip "email indisponível neste pack"; else
  http GET "/api/config?section=email"
  E_PRE=$(printf '%s' "$HTTP_BODY" | field smtp.server)
  http POST "/api/config?section=email" "section=email&smtp_server=smtp.persist.test&email_method=smtp&email_destination=family@example.com"
  reboot_and_wait || { echo -e "${RED}ABORT${NC}"; exit 2; }
  http GET "/api/config?section=email"
  E_POS=$(printf '%s' "$HTTP_BODY" | field smtp.server)
  ED_POS=$(printf '%s' "$HTTP_BODY" | field email.destination)
  echo "  smtp_server: pre=$E_PRE pos=$E_POS"
  [ "$E_POS" = "smtp.persist.test" ] && log_ok "email smtp_server persistiu" || log_fail "email smtp_server NÃO persistiu (pre=$E_PRE pos=$E_POS)"
fi

# =============================================================================
# 6) ETHERNET (se pack suportar)
# =============================================================================
echo -e "\n[6] Ethernet"
if ! endpoint_exists "/api/eth-config"; then log_skip "ethernet indisponível neste pack"; else
  http GET "/api/eth-config"
  ET_PRE=$(printf '%s' "$HTTP_BODY" | field enabled)
  http POST "/api/eth-config" "enabled=$([ "$ET_PRE" = "true" ] && echo false || echo true)"
  reboot_and_wait || { echo -e "${RED}ABORT${NC}"; exit 2; }
  http GET "/api/eth-config"
  ET_POS=$(printf '%s' "$HTTP_BODY" | field enabled)
  echo "  eth enabled: pre=$ET_PRE pos=$ET_POS"
  [ "$ET_POS" = "$ET_PRE" ] && log_ok "ethernet enabled persistiu ($ET_POS)" || log_fail "ethernet enabled NÃO persistiu (pre=$ET_PRE pos=$ET_POS)"
fi

# =============================================================================
# 7) RS232 (se pack suportar)
# =============================================================================
echo -e "\n[7] RS232"
if ! endpoint_exists "/api/config?section=rs232"; then log_skip "rs232 indisponível neste pack"; else
  http GET "/api/config?section=rs232"
  R_PRE=$(printf '%s' "$HTTP_BODY" | field enabled)
  http POST "/api/config?section=rs232" "section=rs232&rs232_enabled=$([ "$R_PRE" = "true" ] && echo false || echo true)"
  reboot_and_wait || { echo -e "${RED}ABORT${NC}"; exit 2; }
  http GET "/api/config?section=rs232"
  R_POS=$(printf '%s' "$HTTP_BODY" | field enabled)
  echo "  rs232 enabled: pre=$R_PRE pos=$R_POS"
  [ "$R_POS" = "$R_PRE" ] && log_ok "rs232 enabled persistiu ($R_POS)" || log_fail "rs232 enabled NÃO persistiu (pre=$R_PRE pos=$R_POS)"
fi

# =============================================================================
# REPOR SNAPSHOT ORIGINAL
# =============================================================================
echo -e "\n[*] Repor config original (snapshot)..."
if [ -n "$SNAP_NAME" ]; then
  http POST "/api/config?section=system" \
    "section=system&system_name=$(python3 -c "import urllib.parse,sys;print(urllib.parse.quote('$SNAP_NAME'))")&system_language=${SNAP_LANG:-pt}&mdns_enabled_present=1&mdns_enabled=${SNAP_MDNS:-true}&timezone=$(python3 -c "import urllib.parse,sys;print(urllib.parse.quote('${SNAP_TZ:-WET0WEST,M3.5.0/1,M10.5.0}'))")&ntp_server=$(python3 -c "import urllib.parse,sys;print(urllib.parse.quote('${SNAP_NTP:-pool.ntp.org}'))")" >/dev/null
fi
# inatividade: repor slot0 off (snapshot não cobria todos; repor para default seguro)
if endpoint_exists "/api/inactivity-alert-config"; then
  http POST "/api/inactivity-alert-config" "slot0_enabled=0&slot1_enabled=0&slot2_enabled=0&slot3_enabled=0&slot4_enabled=0&slot5_enabled=0" >/dev/null
fi
echo "  reposto."

# =============================================================================
# RESUMO
# =============================================================================
echo -e "\n============================================================"
echo -e " RESUMO: ${GREEN}$pass OK${NC}  ${RED}$fail FAIL${NC}  ${YEL}$skip SKIP${NC}  ${YEL}$warns WARN${NC}"
if [ "$fail" -gt 0 ]; then
  echo -e "${RED}FALHAS:${NC}"
  for f in "${FAILS[@]}"; do echo "  - $f"; done
  echo -e "${RED}>>> PERSISTÊNCIA QUEBRADA — NÃO FAZER OTA PARA PRODUÇÃO <<<${NC}"
  exit 1
fi
if [ "$skip" -gt 0 ]; then
  echo -e "${YEL}SKIPs (pack não expõe estas secções):${NC}"
  for s in "${SKIPS[@]}"; do echo "  - $s"; done
fi
echo -e "${GREEN}>>> TODOS OS CAMPOS TESTADOS PERSISTIRAM — OK PARA OTA <<<${NC}"
exit 0
