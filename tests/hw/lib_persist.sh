#!/usr/bin/env bash
# =============================================================================
# lib_persist.sh — Biblioteca comum de regressão de PERSISTÊNCIA (Node32-HUB)
# -----------------------------------------------------------------------------
# Source-ável: não corre nada sozinho. Usado pelos test_real_<pack>.sh.
#
# Fornece:
#   phttp <method> <path> [data]        -> HTTP_BODY + HTTP_CODE
#   pfield <dot.path>                    -> extrai campo JSON de HTTP_BODY
#   preboot_and_wait                     -> reboot + espera online
#   pcheck <nome> <esperado> <real>      -> log OK/FAIL
#   prun_section <nome> <set_cmd> <get_cmd_before> <get_cmd_after> <esperado>
#                                       -> faz SET, reboot, GET, compara
#   pskip <nome> <razao>                 -> log SKIP
#
# Regras:
#   - SET → reboot → GET. Se o valor não sobreviver ao reboot => FAIL.
#   - Campos de pack que não existem (404) => SKIP (não FAIL).
# =============================================================================

# Cores
P_RED='\033[0;31m'; P_GREEN='\033[0;32m'; P_YEL='\033[1;33m'; P_NC='\033[0m'

# Contadores (definidos pelo caller, mas default aqui)
: "${P_PASS:=0}"; : "${P_FAIL:=0}"; : "${P_SKIP:=0}"
declare -a P_FAILS P_SKIPS

# Base + auth (definidos pelo caller antes de source)
# BASE="http://192.168.1.x"
# AUTHARG=(-u user:pass)   ou   AUTHARG=()

phttp() {
  local m="$1" p="$2" d="${3:-}"
  local out
  if [ -n "$d" ]; then
    out=$(curl -s -m 15 "${AUTHARG[@]}" -X "$m" "$BASE$p" \
      -H "Content-Type: application/x-www-form-urlencoded" --data "$d" -w '\n%{http_code}')
  else
    out=$(curl -s -m 15 "${AUTHARG[@]}" -X "$m" "$BASE$p" -w '\n%{http_code}')
  fi
  HTTP_CODE=$(printf '%s' "$out" | tail -1)
  HTTP_BODY=$(printf '%s' "$out" | sed '$d')
}

pfield() {
  python3 -c "
import sys,json,re
try:
    raw=sys.stdin.buffer.read().decode('utf-8', errors='replace')
    d=json.loads(raw)
    for k in '$1'.split('.'):
        m=re.match(r'^(.*?)\[(\d+)\]$', k)   # suporta nome[indice]
        if m:
            key=m.group(1); idx=int(m.group(2))
            if isinstance(d,dict) and key in d and isinstance(d[key],list) and idx<len(d[key]):
                d=d[key][idx]
            else:
                d=''; break
        elif isinstance(d,list) and k.isdigit():
            d=d[int(k)] if int(k)<len(d) else ''
        elif isinstance(d,dict):
            d=d.get(k,'') if k in d else ''
        else:
            d=''; break
    print(d if d!='' else '')
except Exception:
    print('')
" 2>/dev/null <<<"$HTTP_BODY"
}

preboot_and_wait() {
  echo -e "\n${P_YEL}>> reboot $BASE ...${P_NC}"
  phttp POST "/api/restart" "" >/dev/null 2>&1
  sleep 4
  # Espera que a board volte online. O endpoint raiz (/) pode demorar ou
  # redirectar; aceita também /api/status e /api/config?section=system.
  for i in $(seq 1 20); do
    local c
    c=$(curl -s -m 4 -o /dev/null -w '%{http_code}' "${AUTHARG[@]}" "$BASE/" 2>/dev/null)
    if [ "$c" = "200" ]; then echo "  online após ${i}x~3s"; return 0; fi
    c=$(curl -s -m 4 -o /dev/null -w '%{http_code}' "${AUTHARG[@]}" "$BASE/api/status" 2>/dev/null)
    if [ "$c" = "200" ]; then echo "  online após ${i}x~3s (api/status)"; return 0; fi
    sleep 3
  done
  echo -e "  ${P_RED}board não voltou online!${P_NC}"; return 1
}

pcheck() {
  local nome="$1" exp="$2" real="$3"
  # Normaliza booleanos Python (True/False) para lowercase, para comparar com o esperado.
  case "$exp" in True) exp="true";; False) exp="false";; esac
  case "$real" in True) real="true";; False) real="false";; esac
  if [ "$exp" = "$real" ]; then
    echo -e "  ${P_GREEN}OK${P_NC}   $nome ($exp)"
    P_PASS=$((P_PASS+1))
  else
    echo -e "  ${P_RED}FAIL${P_NC}  $nome (esperado=$exp real=$real)"
    P_FAIL=$((P_FAIL+1)); P_FAILS+=("$nome [exp=$exp real=$real]")
  fi
}

pskip() {
  echo -e "  ${P_YEL}SKIP${P_NC}  $1"
  P_SKIP=$((P_SKIP+1)); P_SKIPS+=("$1")
}

# Roda uma secção de persistência completa:
#   $1 = nome
#   $2 = endpoint GET de leitura (ex: /api/config?section=system)
#   $3 = comando SET (ex: "section=system&mdns_enabled_present=1&mdns_enabled=0")
#   $4 = campo a ler no GET (ex: mdns_enabled)
#   $5 = valor esperado após reboot (ex: false)
#   $6 = valor de reset a repor no fim (ex: "section=system&mdns_enabled_present=1&mdns_enabled=1")
prun_section() {
  local nome="$1" getp="$2" setcmd="$3" field="$4" expect="$5" reset="$6"
  echo -e "\n[persist] $nome"
  phttp GET "$getp"
  if [ "$HTTP_CODE" = "404" ]; then pskip "$nome (endpoint $getp 404)"; return; fi
  local pre; pre=$(printf '%s' "$HTTP_BODY" | pfield "$field")
  echo "  pré-reboot: $field=$pre"
  phttp POST "$getp" "$setcmd"
  echo "  SET -> HTTP $HTTP_CODE"
  if ! preboot_and_wait; then echo -e "${P_RED}ABORT reboot${P_NC}"; return 1; fi
  # Após reboot, o endpoint pode demorar a ficar pronto: esperar dados válidos.
  # Considera válido qualquer HTTP 200 com corpo JSON (mesmo que o campo seja false/0,
  # que são valores legítimos de persistência, não "vazio").
  local pos=""
  for attempt in $(seq 1 8); do
    phttp GET "$getp"
    if [ "$HTTP_CODE" = "200" ] && [ -n "$HTTP_BODY" ]; then
      pos=$(printf '%s' "$HTTP_BODY" | pfield "$field")
      break
    fi
    echo "  (GET pós-reboot sem dados, retry $attempt)"; sleep 5
  done
  echo "  pós-reboot: $field=$pos"
  pcheck "$nome" "$expect" "$pos"
  # repor
  if [ -n "$reset" ]; then phttp POST "$getp" "$reset" >/dev/null 2>&1; fi
}

pstatus() {
  echo -e "\n============================================================"
  echo -e " RESUMO $BASE: ${P_GREEN}$P_PASS OK${P_NC}  ${P_RED}$P_FAIL FAIL${P_NC}  ${P_YEL}$P_SKIP SKIP${P_NC}"
  if [ "$P_FAIL" -gt 0 ]; then
    echo -e "${P_RED}FALHAS:${P_NC}"
    for f in "${P_FAILS[@]}"; do echo "  - $f"; done
    echo -e "${P_RED}>>> PERSISTÊNCIA QUEBRADA — NÃO FAZER OTA <<<${P_NC}"
    return 1
  fi
  echo -e "${P_GREEN}>>> PERSISTÊNCIA OK <<<${P_NC}"
  return 0
}
