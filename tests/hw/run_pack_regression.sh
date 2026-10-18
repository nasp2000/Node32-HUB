#!/usr/bin/env bash
# =============================================================================
# tests/hw/run_pack_regression.sh — PACK de REGRESSÃO de PERSISTÊNCIA (Node32-HUB)
# -----------------------------------------------------------------------------
# Corre o teste REAL de cada pack (escreve config -> reboot -> lê -> compara)
# após cada build. Falha (exit 1) se algum pack tiver perda de persistência.
#
# Um ÚNICO ficheiro de teste por pack, em tests/packs/<pack>/test_real_<pack>.sh:
#   tests/packs/waveshare/test_real_waveshare.sh   (Pack Waveshare 8DI8DO .29)
#   tests/packs/homee32/test_real_homee32.sh       (Pack ENVIRONMENT HomeE32 .24)
#   tests/packs/p4/test_real_p4.sh                 (Pack P4 CNC .7)
#   tests/packs/harmonica/test_real_harmonica.sh   (Pack RADIO Harmonica .30)
# Simuladores (.23/.33) reusam o mesmo teste com outro IP (sem mDNS).
#
# Cada test_real_*.sh é autocontido (usa ../lib_persist.sh) e devolve
# exit 0 (OK) ou 1 (FALHA persistência).
#
# Uso:
#   ./run_pack_regression.sh                 # corre todos os packs reais
#   ./run_pack_regression.sh waveshare       # só um pack (nome da pasta)
#   ./run_pack_regression.sh sim_waveshare   # simulador (usa teste waveshare + IP .33)
#   PACKS_WAVESHARE="192.168.1.100:user:pass" ./run_pack_regression.sh
#
# Regra: se algum teste FAIL, o script devolve 1 => "NÃO FAZER OTA".
# =============================================================================
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
# tests/hw -> repo/tests/packs/<pack>/test_real_<pack>.sh
PACKS_DIR="$(cd "$HERE/../packs" && pwd)"

# Packs reais (pasta do teste -> IP:auth)
declare -A DEF_IP DEF_AUTH
DEF_IP[waveshare]=""; DEF_AUTH[waveshare]=""
DEF_IP[homee32]="";  DEF_AUTH[homee32]=""
DEF_IP[p4]="";        DEF_AUTH[p4]=""
DEF_IP[harmonica]="";DEF_AUTH[harmonica]=""
# Simuladores: reusam o teste do pack real com outro IP
DEF_IP[sim_waveshare]=""; DEF_AUTH[sim_waveshare]=""
DEF_IP[sim_homee32]="";   DEF_AUTH[sim_homee32]=""

RED='\033[0;31m'; GREEN='\033[0;32m'; YEL='\033[1;33m'; NC='\033[0m'
overall=0
WANT="${1:-}"

# devolve o script (test_real_<pack>.sh) para uma key
script_for() {
  local k="$1"
  case "$k" in
    sim_waveshare) echo "$PACKS_DIR/waveshare/test_real_waveshare.sh" ;;
    sim_homee32)   echo "$PACKS_DIR/homee32/test_real_homee32.sh" ;;
    *)             echo "$PACKS_DIR/$k/test_real_$k.sh" ;;
  esac
}

run_one() {
  local key="$1" ip="$2" auth="$3" script="$4"
  if [ ! -f "$script" ]; then
    echo -e "${YEL}[$key] teste nao existe: $script — SKIP${NC}"
    return 0
  fi
  echo -e "\n${YEL}========== PACK: $key ($ip) ==========${NC}"
  export TEST_IP="$ip"
  export TEST_AUTH="$auth"
  bash "$script"
  local rc=$?
  if [ "$rc" -ne 0 ]; then
    echo -e "${RED}[$key] *** FALHA PERSISTÊNCIA (rc=$rc) ***${NC}"
    overall=1
  else
    echo -e "${GREEN}[$key] OK${NC}"
  fi
}

for key in "${!DEF_IP[@]}"; do
  ip="${DEF_IP[$key]}"; auth="${DEF_AUTH[$key]}"
  envkey="PACKS_${key}"
  if [ -n "${!envkey:-}" ]; then ip="${!envkey%%:*}"; auth="${!envkey#*:}"; fi
  # filtro
  if [ -n "$WANT" ] && [ "$WANT" != "$key" ]; then continue; fi
  if [ -z "$ip" ]; then echo "[$key] sem IP (define PACKS_${key}=\"ip:auth\") — SKIP"; continue; fi
  # simuladores: só se pedido explicitamente
  case "$key" in
    sim_*) if [ -z "$WANT" ]; then continue; fi ;;
  esac
  run_one "$key" "$ip" "$auth" "$(script_for "$key")"
done

echo -e "\n============================================================"
if [ "$overall" -eq 0 ]; then
  echo -e "${GREEN}>>> REGRESSÃO DE PERSISTÊNCIA: TODOS OS PACKS OK <<<${NC}"
else
  echo -e "${RED}>>> REGRESSÃO: FALHA EM ALGUM PACK — NÃO FAZER OTA <<<${NC}"
fi
exit $overall
