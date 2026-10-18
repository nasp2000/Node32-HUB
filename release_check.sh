#!/usr/bin/env bash
# =============================================================================
# release_check.sh — Gate único antes de publicar uma release (Node32-HUB)
# -----------------------------------------------------------------------------
# Corre, fail-fast, as verificações que NÃO podem falhar antes de publicar:
#   1. versão consistente (smtp_core_impl.h == modbus_slave.cpp)
#   2. testes nativos (doctest)
#   3. testes web (jsdom)               [precisa de `node`; --no-web salta]
#   4. auditoria i18n das páginas       [--strict torna bloqueante]
#   5. compliance third-party --strict
#   6. build de smoke (opcional --build; compila todos os envs/variantes)
#   7. auditoria ao device (opcional; requer --host)
#   8. regressão de persistência por pack (opcional; requer --host)
#   9. empacotamento da release (opcional --package)
#
# Uso:
#   ./release_check.sh                         # bancada (sem hardware)
#   ./release_check.sh --host 192.168.1.100 --packs waveshare
#   ./release_check.sh --host 192.168.1.100 --auth admin:admin --build --package
#   ./release_check.sh --no-web --no-hw
#
# NÃO sobe a versão — o bump é manual, só quando pedido (AGENTS: bump no commit).
# =============================================================================
set -u
ROOT="$(cd "$(dirname "$0")" && pwd)"
PY="${PY:-python3}"
# node often lives outside PATH on build machines (e.g. ~/.local/bin).
for _nd in "$HOME/.local/bin" "/usr/local/bin" "/opt/node/bin"; do
  case ":$PATH:" in *":$_nd:"*) ;; *) [ -x "$_nd/node" ] && PATH="$_nd:$PATH";; esac
done
export PATH
HOST=""; AUTH="admin:admin"; PACKS=""; ENV="n16r8"
DO_WEB=1; DO_HW=1; DO_BUILD=0; DO_PACKAGE=0; STRICT=0

while [ $# -gt 0 ]; do
  case "$1" in
    --host)    shift; HOST="${1:-}"; DO_HW=1 ;;
    --auth)    shift; AUTH="${1:-}" ;;
    --packs)   shift; PACKS="${1:-}" ;;
    --env)     shift; ENV="${1:-}" ;;
    --build)   DO_BUILD=1 ;;
    --package) DO_PACKAGE=1 ;;
    --no-web)  DO_WEB=0 ;;
    --no-hw)   DO_HW=0; HOST="" ;;
    --strict)  STRICT=1 ;;
    -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "opção desconhecida: $1"; exit 2 ;;
  esac
  shift
done

RED='\033[0;31m'; GRN='\033[0;32m'; YEL='\033[1;33m'; NC='\033[0m'
PASSED=0; FAILED=0; SKIPPED=0
declare -a RESULTS

step() { echo -e "\n${YEL}>>> $*${NC}"; }
ok()   { PASSED=$((PASSED+1)); RESULTS+=("PASS  $1"); echo -e "  ${GRN}PASS${NC} $1"; }
bad()  { FAILED=$((FAILED+1)); RESULTS+=("FAIL  $1"); echo -e "  ${RED}FAIL${NC} $1"; }
skip() { SKIPPED=$((SKIPPED+1)); RESULTS+=("SKIP  $1"); echo -e "  ${YEL}SKIP${NC} $1"; }

cd "$ROOT"

# 0. contexto
echo "==================================================================="
echo "  Node32-HUB release_check   $(date '+%Y-%m-%d %H:%M')"
echo "  git: $(git rev-parse --short HEAD 2>/dev/null)  branch: $(git rev-parse --abbrev-ref HEAD 2>/dev/null)"
echo "==================================================================="
if [ -n "$(git status --porcelain 2>/dev/null)" ]; then
  echo -e "${YEL}AVISO: working tree com alterações não commitadas — a release deve sair de um commit limpo.${NC}"
fi

# 1. versão
step "1/9 Versão consistente"
if "$PY" chatgpt/scripts/validate_version.py; then ok "version"; else bad "version"; echo -e "${RED}ABORTAR: corrige a versão.${NC}"; exit 1; fi

# 2. nativas
step "2/9 Testes nativos"
if "$PY" chatgpt/scripts/run_native_tests.py >/tmp/release_native.log 2>&1; then
  ok "native ($(tail -n1 /tmp/release_native.log | tr -d '\n'))"
else
  bad "native"; tail -n 20 /tmp/release_native.log
fi

# 3. web
step "3/9 Testes web (jsdom)"
if [ "$DO_WEB" = "0" ]; then
  skip "web (--no-web)"
elif command -v node >/dev/null 2>&1; then
  if (cd tests/web && node run_all.js) >/tmp/release_web.log 2>&1; then
    ok "web ($(grep -oE '[0-9]+/[0-9]+ passed' /tmp/release_web.log | tail -1))"
  else
    bad "web"; tail -n 25 /tmp/release_web.log
  fi
else
  skip "web (sem node nesta máquina — correr no PC de release)"
fi

# 4. auditoria i18n
step "4/9 Auditoria i18n das páginas"
"$PY" tests/web/audit_i18n.py >/tmp/release_i18n.log 2>&1 || true
n=$(grep -cE '^  (TEXT|ATTR)' /tmp/release_i18n.log || true)
n=${n:-0}
if [ "$n" = "0" ]; then
  ok "i18n (sem hardcoded)"
elif [ "$STRICT" = "1" ]; then
  bad "i18n ($n achados — ver /tmp/release_i18n.log)"
else
  skip "i18n ($n achados, inclui falsos positivos; usar --strict p/ bloquear — ver /tmp/release_i18n.log)"
fi

# 5. compliance
step "5/9 Compliance third-party"
if "$PY" chatgpt/scripts/validate_third_party.py --strict; then ok "third-party"; else bad "third-party"; fi

# 6. build de smoke
step "6/9 Build de smoke"
if [ "$DO_BUILD" = "0" ]; then
  skip "build (--build para compilar todos os envs/variantes)"
else
  if "$PY" chatgpt/scripts/smoke_build.py >/tmp/release_build.log 2>&1; then ok "build"; else bad "build"; tail -n 25 /tmp/release_build.log; fi
fi

# 7. auditoria ao device
step "7/9 Auditoria ao device"
if [ "$DO_HW" = "0" ] || [ -z "$HOST" ]; then
  skip "device (--host <ip>)"
else
  if "$PY" scripts/audit_device.py --host "$HOST" --auth "$AUTH" >/tmp/release_device.log 2>&1; then
    ok "device @ $HOST"; tail -n 6 /tmp/release_device.log
  else
    bad "device @ $HOST"; tail -n 25 /tmp/release_device.log
  fi
fi

# 8. regressão por pack
step "8/9 Regressão de persistência por pack"
if [ "$DO_HW" = "0" ] || [ -z "$HOST" ]; then
  skip "packs (--host <ip>)"
else
  args=(); [ -n "$PACKS" ] && args=(${PACKS//,/ })
  if [ -n "$PACKS" ]; then
    for p in "${args[@]}"; do
      if PACKS_$(echo "$p" | tr 'a-z' 'A-Z')="$HOST:$AUTH" bash tests/hw/run_pack_regression.sh "$p" >/tmp/release_pack_$p.log 2>&1; then
        ok "pack $p"
      else
        bad "pack $p (NÃO fazer OTA)"; tail -n 15 /tmp/release_pack_$p.log
      fi
    done
  else
    if bash tests/hw/run_pack_regression.sh >/tmp/release_packs.log 2>&1; then ok "packs (todos)"; else bad "packs"; tail -n 20 /tmp/release_packs.log; fi
  fi
fi

# 9. empacotamento
step "9/9 Empacotamento"
if [ "$DO_PACKAGE" = "0" ]; then
  skip "package (--package)"
else
  if "$PY" chatgpt/scripts/package_release.py --env "$ENV" >/tmp/release_pkg.log 2>&1; then
    ok "package ($ENV)"; grep -iE "zip|archive" /tmp/release_pkg.log | tail -3
  else
    bad "package"; tail -n 15 /tmp/release_pkg.log
  fi
fi

# resumo
echo
echo "==================================================================="
echo "  RESUMO: $PASSED pass, $FAILED fail, $SKIPPED skip"
for r in "${RESULTS[@]}"; do echo "    $r"; done
echo "==================================================================="
if [ "$FAILED" -gt 0 ]; then
  echo -e "${RED}NÃO PUBLICAR: há verificações falhadas.${NC}"
  exit 1
fi
echo -e "${GRN}OK para publicar (rever os SKIP e o bump de versão).${NC}"
exit 0
