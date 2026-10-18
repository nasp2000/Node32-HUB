#!/usr/bin/env bash
# =============================================================================
# tests/hw/packs/waveshare.sh — Teste REAL de persistência: Pack Waveshare (.29)
# -----------------------------------------------------------------------------
# Um único ficheiro por pack (na raiz do repo, em tests/hw/packs/).
# Faz em modo real: escreve config -> reboot -> lê -> compara -> repõe (snapshot).
# Campos cobertos (auditoria 2026-08-30):
#   system: mdns_enabled, system_name, system_language, timezone, ntp_server
#   inatividade: slot0 enabled + emails
#   email: smtp_server, email_destination
#   access: max_sessions, idle_timeout
#   ethernet: enabled
# Endpoint 404 (pack não expõe) => SKIP (não FAIL).
# Requer: ../lib_persist.sh (source).
# Uso: bash tests/hw/packs/waveshare.sh   (IP/AUTH via env TEST_IP/TEST_AUTH ou defaults)
# =============================================================================
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="$(cd "$HERE/../../hw" && pwd)/lib_persist.sh"
[ -f "$LIB" ] || LIB="$(cd "$HERE/../.." && pwd)/tests/hw/lib_persist.sh"
source "$LIB" || { echo "lib_persist.sh não encontrada ($LIB)"; exit 2; }

BASE="http://${TEST_IP:-192.168.1.29}"
AUTHARG=()
if [ -n "${TEST_AUTH:-}" ]; then AUTHARG=(-u "$TEST_AUTH"); else AUTHARG=(-u admin:admin); fi

P_PASS=0; P_FAIL=0; P_SKIP=0
declare -a P_FAILS P_SKIPS

echo "===== TESTE REAL PERSISTÊNCIA — Waveshare @ $BASE ====="

# ---- Snapshot ----
phttp GET "/api/config?section=system"
SNAP_NAME=$(printf '%s' "$HTTP_BODY" | pfield system_name)
SNAP_MDNS=$(printf '%s' "$HTTP_BODY" | pfield mdns_enabled)
SNAP_LANG=$(printf '%s' "$HTTP_BODY" | pfield system_language)
SNAP_TZ=$(printf '%s' "$HTTP_BODY" | pfield ntp.timezone)
SNAP_NTP=$(printf '%s' "$HTTP_BODY" | pfield ntp.server)
phttp GET "/api/config?section=email"
SNAP_SMTP=$(printf '%s' "$HTTP_BODY" | pfield smtp.server)
SNAP_EMAIL_DEST=$(printf '%s' "$HTTP_BODY" | pfield email.destination)
echo "snapshot: name=$SNAP_NAME mdns=$SNAP_MDNS lang=$SNAP_LANG tz=$SNAP_TZ ntp=$SNAP_NTP smtp=$SNAP_SMTP dest=$SNAP_EMAIL_DEST"
# Fail-safe (2026-10-15: empty snapshot + default-restore CLOBBERED ntp_server and
# email_destination on .29). Never mutate when the baseline is unknown.
for _v in "$SNAP_NAME" "$SNAP_MDNS" "$SNAP_LANG" "$SNAP_TZ" "$SNAP_NTP" "$SNAP_SMTP" "$SNAP_EMAIL_DEST"; do
  if [ -z "$_v" ]; then echo "ABORT: snapshot vazio — a recusar correr (podia apagar config)"; exit 2; fi
done
unset _v

prun_section "system.mdns_enabled" "/api/config?section=system" \
  "section=system&system_name=PERSIST-TEST&system_language=pt&mdns_enabled_present=1&mdns_enabled=0&timezone=PERSIST-TZ&ntp_server=time.cloudflare.com" \
  "mdns_enabled" "false" \
  "section=system&system_name=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_NAME:-Equip15}'))")&system_language=${SNAP_LANG:-pt}&mdns_enabled_present=1&mdns_enabled=${SNAP_MDNS:-true}&timezone=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_TZ:-WET0WEST,M3.5.0/1,M10.5.0}'))")&ntp_server=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_NTP:-pool.ntp.org}'))")"

prun_section "system.system_name" "/api/config?section=system" \
  "section=system&system_name=PERSIST-TEST&system_language=pt" \
  "system_name" "PERSIST-TEST" \
  "section=system&system_name=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_NAME:-Equip15}'))")&system_language=${SNAP_LANG:-pt}"

prun_section "system.system_language" "/api/config?section=system" \
  "section=system&system_language=pt" \
  "system_language" "pt" \
  "section=system&system_language=${SNAP_LANG:-pt}"

prun_section "system.timezone" "/api/config?section=system" \
  "section=system&timezone=PERSIST-TZ" \
  "ntp.timezone" "PERSIST-TZ" \
  "section=system&timezone=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_TZ:-WET0WEST,M3.5.0/1,M10.5.0}'))")"

prun_section "system.ntp_server" "/api/config?section=system" \
  "section=system&ntp_server=time.cloudflare.com" \
  "ntp.server" "time.cloudflare.com" \
  "section=system&ntp_server=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_NTP:-pool.ntp.org}'))")"

prun_section "inactivity.slot0" "/api/inactivity-alert-config" \
  "alert_enabled_0=1&alert_timeout_0=7&alert_max_0=3&alert_emails_0=owner@example.com&alert_enabled_1=0&alert_enabled_2=0&alert_enabled_3=0&alert_enabled_4=0&alert_enabled_5=0" \
  "slots[0].enabled" "True" \
  "alert_enabled_0=0&alert_enabled_1=0&alert_enabled_2=0&alert_enabled_3=0&alert_enabled_4=0&alert_enabled_5=0"

prun_section "email.smtp_server" "/api/config?section=email" \
  "section=email&smtp_server=smtp.persist.test&email_method=smtp&email_destination=family@example.com" \
  "smtp.server" "smtp.persist.test" \
  "section=email&smtp_server=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_SMTP}'))")&email_destination=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_EMAIL_DEST}'))")"

phttp GET "/api/access-config"
if [ "$HTTP_CODE" != "404" ]; then
  prun_section "access.max_sessions" "/api/access-config" \
    "web_max_sessions=4&web_idle_timeout_min=15" \
    "web.max_sessions" "4" "web_max_sessions=3&web_idle_timeout_min=10"
else
  pskip "access (endpoint 404)"
fi

phttp GET "/api/eth-config"
if [ "$HTTP_CODE" != "404" ]; then
  prun_section "ethernet.enabled" "/api/eth-config" \
    "enabled=false" "enabled" "false" "enabled=true"
else
  pskip "ethernet (endpoint 404)"
fi

# Repor extra por segurança
phttp POST "/api/config?section=system" \
  "section=system&system_name=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_NAME:-Equip15}'))")&system_language=${SNAP_LANG:-pt}&mdns_enabled_present=1&mdns_enabled=${SNAP_MDNS:-true}&timezone=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_TZ:-WET0WEST,M3.5.0/1,M10.5.0}'))")&ntp_server=$(python3 -c "import urllib.parse;print(urllib.parse.quote('${SNAP_NTP:-pool.ntp.org}'))")" >/dev/null 2>&1
phttp POST "/api/inactivity-alert-config" "slot0_enabled=0&slot1_enabled=0&slot2_enabled=0&slot3_enabled=0&slot4_enabled=0&slot5_enabled=0" >/dev/null 2>&1

pstatus
exit $?
