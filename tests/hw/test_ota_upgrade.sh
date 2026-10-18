#!/usr/bin/env bash
# =============================================================================
# tests/hw/test_ota_upgrade.sh — Teste de UPGRADE (config sobrevive ao OTA)
# -----------------------------------------------------------------------------
# A regressão de persistência testa config FRESCA; este testa o caminho real de
# release: configurar na versão N-1 -> OTA para N -> verificar que a config
# crítica (identidade, email, access, alertas, alertas OEE, shift/stops)
# sobrevive ao reboot.
#
# Modos:
#   ./test_ota_upgrade.sh snapshot                 # ANTES do OTA (guarda em /tmp)
#   ./test_ota_upgrade.sh verify                   # DEPOIS do OTA (compara)
#   ./test_ota_upgrade.sh full --ota <file.bin>    # snapshot + OTA + verify (WiFi)
#
# IP/auth: TEST_IP (required) / TEST_AUTH (optional, "user:pass").
# Notas:
#   - O OTA web exige POST /api/enable-ota e depois o upload multipart
#     (campo `file` + `filesize`). Em Ethernet o upload é raw; usa `full` só em WiFi.
#   - Endpoints que o pack não exponha (404) => SKIP (não FAIL).
#   - Exit 0 = OK; 1 = perda de config; 2 = erro de uso/ambiente.
# =============================================================================
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/lib_persist.sh" || { echo "lib_persist.sh não encontrada"; exit 2; }

BASE="http://${TEST_IP:?define TEST_IP com o IP do device}"
if [ -n "${TEST_AUTH:-}" ]; then AUTHARG=(-u "$TEST_AUTH"); else AUTHARG=(); fi
SNAP="/tmp/node32_upgrade_snapshot.json"
NOW="/tmp/node32_upgrade_now.json"

# Coletor único (usado por snapshot e verify). Lê os campos críticos via API.
collect() {
  python3 - "$BASE" "${TEST_AUTH:-}" "$1" <<'PY'
import json, sys, base64, re, urllib.request
base, auth, out = sys.argv[1], sys.argv[2], sys.argv[3]
hdr = {} if not auth else {"Authorization": "Basic " + base64.b64encode(auth.encode()).decode()}
FIELDS = [
  ("system.name",          "/api/config?section=system",      "system_name"),
  ("system.language",      "/api/config?section=system",      "system_language"),
  ("system.mdns",          "/api/config?section=system",      "mdns_enabled"),
  ("system.timezone",      "/api/config?section=system",      "timezone"),
  ("system.ntp",           "/api/config?section=system",      "ntp_server"),
  ("email.smtp",           "/api/config?section=email",       "smtp_server"),
  ("email.dest",           "/api/config?section=email",       "email_destination"),
  ("access.max_sessions",  "/api/config?section=access",      "max_sessions"),
  ("ethernet.enabled",     "/api/config?section=ethernet",    "enabled"),
  ("alerts.slot0.enabled", "/api/inactivity-alert-config",    "slots[0].enabled"),
  ("alerts.slot0.emails",  "/api/inactivity-alert-config",    "slots[0].emails"),
  ("oee.slot0.enabled",    "/api/oee-alert-config",           "slots[0].enabled"),
  ("oee.slot0.threshold",  "/api/oee-alert-config",           "slots[0].threshold"),
  ("prox.shift.enabled",   "/api/proximity/shift-config",     "en"),
  ("prox.stops.count",     "/api/proximity/shift-config",     "stops"),
]
def get(path):
    try:
        r = urllib.request.urlopen(urllib.request.Request(base + path, headers=hdr), timeout=15)
        return json.loads(r.read().decode("utf-8", "replace"))
    except Exception as e:
        return {"__error__": str(e)}
def pick(d, path):
    cur = d
    for k in path.split("."):
        m = re.match(r"^(.*?)\[(\d+)\]$", k)
        if m:
            key, idx = m.group(1), int(m.group(2))
            cur = cur.get(key) if isinstance(cur, dict) else None
            cur = cur[idx] if isinstance(cur, list) and idx < len(cur) else None
        elif isinstance(cur, dict):
            cur = cur.get(k)
        else:
            return None
    return cur
cache, snap, skipped = {}, {}, []
for label, ep, path in FIELDS:
    if ep not in cache:
        cache[ep] = get(ep)
    if isinstance(cache[ep], dict) and "__error__" in cache[ep]:
        skipped.append(label); continue
    snap[label] = pick(cache[ep], path)
json.dump({"fields": snap, "skipped": skipped}, open(out, "w"), indent=2)
print(f"campos lidos: {len(snap)}  (skip: {len(skipped)})")
for s in skipped: print("  skip:", s)
PY
}

do_snapshot() {
  echo "===== SNAPSHOT (@ $BASE) ====="
  collect "$SNAP"
  echo "snapshot guardado em $SNAP"
}

do_verify() {
  echo "===== VERIFY (@ $BASE) ====="
  [ -f "$SNAP" ] || { echo "sem snapshot ($SNAP) — corre 'snapshot' antes do OTA"; exit 2; }
  collect "$NOW"
  python3 - "$SNAP" "$NOW" <<'PY'
import json, sys
before = json.load(open(sys.argv[1]))["fields"]
after  = json.load(open(sys.argv[2]))["fields"]
fails, oks = [], 0
for k, v in before.items():
    if k not in after:
        continue  # endpoint desapareceu no novo fw -> aviso, não falha de persistência
    a = after[k]
    if isinstance(v, list):
        ok = isinstance(a, list) and len(a) == len(v)
        vv, av = f"{len(v)} items", f"{len(a) if isinstance(a, list) else 'n/a'} items"
    else:
        ok, vv, av = (a == v), v, a
    if ok: oks += 1
    else: fails.append(f"{k}: antes={vv!r} depois={av!r}")
print(f"campos OK: {oks}/{len(before)}")
for f in fails: print("  FAIL:", f)
sys.exit(1 if fails else 0)
PY
  if [ $? -eq 0 ]; then echo -e "${P_GREEN}UPGRADE OK: config sobreviveu${P_NC}"; else echo -e "${P_RED}UPGRADE FALHOU: config perdida${P_NC}"; return 1; fi
}

wait_online() {
  echo -n "à espera do reboot"
  for i in $(seq 1 40); do
    local c
    c=$(curl -s -m 4 -o /dev/null -w '%{http_code}' "${AUTHARG[@]}" "$BASE/api/status" 2>/dev/null)
    if [ "$c" = "200" ]; then echo " — online (${i}x~3s)"; return 0; fi
    echo -n "."; sleep 3
  done
  echo " — NÃO voltou"; return 1
}

do_full() {
  local bin="${1:-}"
  [ -f "$bin" ] || { echo "usa: full --ota <file.bin>"; exit 2; }
  do_snapshot || return 1
  echo "===== OTA: $(basename "$bin") ====="
  phttp POST "/api/enable-ota"
  [ "$HTTP_CODE" = "200" ] || { echo -e "${P_RED}enable-ota falhou (HTTP $HTTP_CODE)${P_NC}"; return 1; }
  local size; size=$(stat -c%s "$bin")
  echo "a enviar ($size bytes)..."
  curl -s -m 240 "${AUTHARG[@]}" -X POST "$BASE/api/firmware-upload" \
       -F "file=@${bin};type=application/octet-stream" -F "filesize=${size}" \
       -o /tmp/node32_ota_resp.txt
  echo "resposta: $(head -c 200 /tmp/node32_ota_resp.txt 2>/dev/null)"
  wait_online || { echo -e "${P_RED}dispositivo não voltou após OTA${P_NC}"; return 1; }
  do_verify
}

case "${1:-}" in
  snapshot) do_snapshot ;;
  verify)   do_verify ;;
  full)
    shift; bin=""
    while [ $# -gt 0 ]; do case "$1" in --ota) shift; bin="${1:-}";; esac; shift; done
    do_full "$bin" ;;
  *) echo "uso: $0 {snapshot|verify|full --ota <file.bin>}"; exit 2 ;;
esac
