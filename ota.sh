#!/usr/bin/env bash
# ============================================================================
# Node32-HUB — OTA deployer (Linux, curl-based, mirrors build_firmware.sh style)
#
# Localiza o signed .bin mais recente do pack, abre a janela OTA no device,
# faz upload (RAW para Ethernet / multipart para WiFi, deteção automática),
# e confirma o reboot via /api/status.
#
# Dois modos de uso:
#   INTERATIVO  ./ota.sh  → escolhe o pack (menus a partir de chatgpt/build),
#                           pergunta o IP do device (os IPs mudam!) e memoriza
#                           o último usado em ~/.config/node32hub-ota
#   PRESETS     ./ota.sh -t <nome> → targets predefinidos no array TARGETS[].
#                           IPs mudam: edita lá ou usa -i para dizer o IP.
#
# Uso:
#   ./ota.sh                          interactivo: pack + IP + transport
#   ./ota.sh -y                       não-interativo, usa último pack/IP (ou default)
#   ./ota.sh -t home                  usa TARGETS["home"] (HomeE32)
#   ./ota.sh -t p4                    usa TARGETS["p4"]    (P4 CNC)
#   ./ota.sh -p RADIO -i 192.168.1.100 pack + IP diretos, sem perguntar
#   ./ota.sh -p RADIO                 pack escolhido; pergunta o IP
#   ./ota.sh -e n16r8                 força o env PlatformIO
#   ./ota.sh -a root:root             credenciais HTTP Basic (default: root:root;
#                                     se a auth estiver OFF no device não são usadas)
#   ./ota.sh -T raw|multipart|auto    força o formato de upload
#   ./ota.sh --dry-run                só mostra o bin escolhido e target, sem upload
#   ./ota.sh --list                   lista todos os targets predefinidos
#
# Exit codes:
#   0  OTA aplicada e confirmada (versão nova + uptime pequeno)
#   1  bin não encontrado / erro de input
#   2  target não responde
#   3  enable-ota falhou
#   4  upload falhou
#   5  versão não mudou após reboot
# ============================================================================
set -u

# ---------------------------------------------------------------------------
# Targets predefinidos — editar aqui para mudar IP/pack/env/transport
# Adicionar novas entradas à vontade.
#
# transport:
#   raw       → upload RAW (Content-Type octet-stream + X-Filename/X-Filesize)  → boards Ethernet (W5500/RMII)
#   multipart → upload multipart/form-data (field "filesize" + file)            → boards só WiFi
#   auto      → decide em runtime: /api/status ethernet_link==true  ⇒ raw, senão multipart
# ---------------------------------------------------------------------------
declare -A TARGETS
TARGETS["default,ip"]=""
TARGETS["default,pack"]="WAVESHARE"
TARGETS["default,env"]="waveshare_8di8do"
TARGETS["default,auth"]="root:root"
TARGETS["default,transport"]="raw"
TARGETS["default,name"]="Waveshare"

TARGETS["home,ip"]=""
TARGETS["home,pack"]="ENVIRONMENT"
TARGETS["home,env"]="n16r8"
TARGETS["home,auth"]="root:root"
TARGETS["home,transport"]="multipart"
TARGETS["home,name"]="HomeE32"

TARGETS["harmonica,ip"]=""
TARGETS["harmonica,pack"]="RADIO"
TARGETS["harmonica,env"]="n16r8"
TARGETS["harmonica,auth"]="root:root"
TARGETS["harmonica,transport"]="multipart"
TARGETS["harmonica,name"]="Harmonica/RADIO"

TARGETS["p4,ip"]=""
TARGETS["p4,pack"]="P4_CNC"
TARGETS["p4,env"]="esp32p4"
TARGETS["p4,auth"]="root:root"
TARGETS["p4,transport"]="raw"
TARGETS["p4,name"]="P4 CNC"

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
TARGET="default"
T_TGT_SET=0
PACK=""
ENV=""
TRANSPORT="auto"
IP_OVERRIDE=""
AUTH_OPT=""
T_OPT_SET=0
DRY_RUN=0
LIST=0
VERBOSE=0
ASSUME_YES=0

usage() {
    sed -n '3,36p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -t)        TARGET="$2"; T_TGT_SET=1; shift 2 ;;
        -p)        PACK="$2";   shift 2 ;;
        -e)        ENV="$2";    shift 2 ;;
        -i)        IP_OVERRIDE="$2"; shift 2 ;;
        -a)        AUTH_OPT="$2"; shift 2 ;;
        -T)        TRANSPORT="$2"; T_OPT_SET=1; shift 2 ;;
        --dry-run) DRY_RUN=1;   shift ;;
        -y|--yes)  ASSUME_YES=1; shift ;;
        --list)    LIST=1;      shift ;;
        -v|--verbose) VERBOSE=1; shift ;;
        -h|--help) usage ;;
        *)         echo "Opção desconhecida: $1" >&2; exit 1 ;;
    esac
done

case "$TRANSPORT" in
    raw|multipart|auto) ;;
    *) echo "ERRO: transport '$TRANSPORT' inválido (raw|multipart|auto)" >&2; exit 1 ;;
esac

# List mode
if [[ $LIST -eq 1 ]]; then
    echo "Targets predefinidos:"
    for key in $(echo "${!TARGETS[@]}" | tr ' ' '\n' | sort -u | awk -F, '{print $1}' | sort -u); do
        ip="${TARGETS[${key},ip]:-?}"
        pack="${TARGETS[${key},pack]:-?}"
        env="${TARGETS[${key},env]:-?}"
        tr="${TARGETS[${key},transport]:-auto}"
        name="${TARGETS[${key},name]:-$key}"
        printf "  %-12s %-15s pack=%-12s env=%-15s transport=%-9s %s\n" "$key" "$ip" "$pack" "$env" "$tr" "$name"
    done
    exit 0
fi

# ---------------------------------------------------------------------------
# Pré-requisitos
# ---------------------------------------------------------------------------
for cmd in curl python3; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "ERRO: falta '$cmd' no PATH" >&2
        exit 1
    fi
done

# ---------------------------------------------------------------------------
# Estado memorizado (último pack/IP usado) — os IPs mudam, isto ajuda
# ---------------------------------------------------------------------------
STATE_FILE="${XDG_CONFIG_HOME:-$HOME/.config}/node32hub-ota"
LAST_IP=""
LAST_PACK=""
LAST_ENV=""
LAST_TRANSPORT=""
LAST_AUTH=""
if [[ -r "$STATE_FILE" ]]; then
    # shellcheck disable=SC1090
    source "$STATE_FILE" 2>/dev/null || true
fi
save_state() {
    local dir
    dir=$(dirname "$STATE_FILE")
    mkdir -p "$dir" 2>/dev/null || return 0
    printf 'LAST_IP="%s"\nLAST_PACK="%s"\nLAST_ENV="%s"\nLAST_TRANSPORT="%s"\nLAST_AUTH="%s"\n' \
        "$LAST_IP" "$LAST_PACK" "$LAST_ENV" "$LAST_TRANSPORT" "$LAST_AUTH" > "$STATE_FILE" 2>/dev/null || true
}

# ---------------------------------------------------------------------------
# Helpers (pack/chip/env de chatgpt/build, validação de IP, prompts)
# ---------------------------------------------------------------------------
REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"

pack_chip() {
    local p="$1"
    if [[ -d "$REPO_ROOT/chatgpt/build/esp32p4/$p" ]]; then
        echo "esp32p4"
    else
        echo "esp32s3"
    fi
}

pack_stage() {
    case "$1" in
        S3_TOUCH7)    echo "alpha" ;;
        S3_CNC)       echo "OTG only" ;;
        P4_USB_SERVER) echo "alpha" ;;
        CAM_P4)         echo "alpha" ;;
        RADIO)          echo "alpha" ;;

        *)            echo "" ;;
    esac
}

pack_default_env() {
    local p="$1" chip="$2"
    if [[ "$chip" == "esp32p4" ]]; then
        echo "esp32p4"
    elif [[ "$p" == "WAVESHARE" ]]; then
        echo "waveshare_8di8do"
    else
        echo "n16r8"
    fi
}

valid_ip() {
    local ip="$1" o1 o2 o3 o4 IFS=.
    [[ "$ip" =~ ^[0-9]{1,3}(\.[0-9]{1,3}){3}$ ]] || return 1
    IFS=. read -r o1 o2 o3 o4 <<< "$ip"
    [[ "$o1" -le 255 && "$o2" -le 255 && "$o3" -le 255 && "$o4" -le 255 ]]
}

interactive_choose() {
    local pkgs cdflt pref idx pg chip ip t sel dflt_ip dflt_trans root d i mark
    pkgs=()
    local -A seen=()
    for root in "$REPO_ROOT/chatgpt/build/esp32s3" "$REPO_ROOT/chatgpt/build/esp32p4"; do
        for d in "$root"/*/; do
            [[ -d "$d" ]] || continue
            pg=$(basename "$d")
            [[ -n "${seen[$pg]:-}" ]] && continue
            seen[$pg]=1
            pkgs+=("$pg")
        done
    done
    if [[ ${#pkgs[@]} -eq 0 ]]; then
        echo "ERRO: nenhum pack com build em chatgpt/build/ — corre build_firmware.sh primeiro?" >&2
        return 1
    fi

    if [[ -z "$PACK" ]]; then
        cdflt="${LAST_PACK:-WAVESHARE}"
        pref=1; idx=1
        for pg in "${pkgs[@]}"; do
            [[ "$pg" == "$cdflt" ]] && pref=$idx
            idx=$((idx+1))
        done
        echo ""
        echo "Packs com build disponível:"
        for i in $(seq 1 "${#pkgs[@]}"); do
            pg="${pkgs[$((i-1))]}"
            mark=" "
            [[ $i -eq $pref ]] && mark="*"
            printf "  %2d %s %-18s (%s%s)\n" "$i" "$mark" "$pg" "$(pack_chip "$pg")" "$(st=$(pack_stage "$pg"); if [ -n "$st" ]; then echo ", $st"; fi)"
        done
        while :; do
            printf "Escolhe o pack [%d = %s]: " "$pref" "${pkgs[$((pref-1))]}"
            if ! read -r sel; then echo "Cancelado (EOF)." >&2; exit 1; fi
            [[ -z "$sel" ]] && sel="$pref"
            if [[ "$sel" =~ ^[0-9]+$ ]] && (( sel >= 1 && sel <= ${#pkgs[@]} )); then
                TARGET_PACK="${pkgs[$((sel-1))]}"
                break
            fi
            echo "  Inválido — escolhe 1..${#pkgs[@]}" >&2
        done
    else
        TARGET_PACK="$PACK"
    fi

    chip=$(pack_chip "$TARGET_PACK")
    TARGET_ENV="${ENV:-$(pack_default_env "$TARGET_PACK" "$chip")}"

    if [[ -z "$IP_OVERRIDE" ]]; then
        dflt_ip="${LAST_IP:-${TARGETS[default,ip]}}"
        while :; do
            printf "IP do device [%s]: " "$dflt_ip"
            if ! read -r ip; then echo "Cancelado (EOF)." >&2; exit 1; fi
            [[ -z "$ip" ]] && ip="$dflt_ip"
            if valid_ip "$ip"; then
                TARGET_IP="$ip"
                break
            fi
            echo "  IP inválido (ex.: 192.168.1.100)" >&2
        done
    else
        TARGET_IP="$IP_OVERRIDE"
    fi

    if [[ $T_OPT_SET -eq 0 ]]; then
        dflt_trans="${LAST_TRANSPORT:-auto}"
        while :; do
            printf "Transport (auto/raw/multipart) [%s]: " "$dflt_trans"
            if ! read -r t; then echo "Cancelado (EOF)." >&2; exit 1; fi
            [[ -z "$t" ]] && t="$dflt_trans"
            case "$t" in auto|raw|multipart) TARGET_TRANSPORT="$t"; break ;; esac
            echo "  Inválido (auto|raw|multipart)" >&2
        done
    else
        TARGET_TRANSPORT="$TRANSPORT"
    fi

    TARGET_AUTH="${AUTH_OPT:-${LAST_AUTH:-root:root}}"
    TARGET_NAME="$TARGET_PACK (interativo)"
    return 0
}

# ---------------------------------------------------------------------------
# Resolver target (preset / último estado / interactivo)
# ---------------------------------------------------------------------------
if [[ $T_TGT_SET -eq 1 ]]; then
    if [[ -z "${TARGETS[${TARGET},pack]:-}" ]]; then
        echo "ERRO: target '$TARGET' nao existe. Use --list para ver os disponiveis." >&2
        exit 1
    fi
    TARGET_IP="${IP_OVERRIDE:-${TARGETS[${TARGET},ip]:-}}"
    if [[ -z "$TARGET_IP" ]]; then
        if [[ -t 0 && $ASSUME_YES -eq 0 ]]; then
            while :; do
                printf "IP do device: "
                if ! read -r ip; then echo "Cancelado (EOF)." >&2; exit 1; fi
                if valid_ip "$ip"; then TARGET_IP="$ip"; break; fi
                echo "  IP invalido (ex.: 192.168.1.100)" >&2
            done
        else
            echo "ERRO: target '$TARGET' sem IP. Usa -i <ip> ou corre interativo." >&2
            exit 1
        fi
    fi
    if [[ -n "$IP_OVERRIDE" ]] && ! valid_ip "$IP_OVERRIDE"; then
        echo "ERRO: IP '$IP_OVERRIDE' inválido" >&2
        exit 1
    fi
    TARGET_PACK="${PACK:-${TARGETS[${TARGET},pack]}}"
    TARGET_ENV="${ENV:-${TARGETS[${TARGET},env]}}"
    TARGET_TRANSPORT="${TARGETS[${TARGET},transport]:-auto}"
    if [[ $T_OPT_SET -eq 1 ]]; then
        TARGET_TRANSPORT="$TRANSPORT"
    fi
    TARGET_AUTH="${AUTH_OPT:-${TARGETS[${TARGET},auth]:-root:root}}"
    TARGET_NAME="${TARGETS[${TARGET},name]}"
else
    # Sem -t: TTY + sem -y ⇒ pergunta pack/IP; caso contrário usa -p/-e/-i ou o último estado.
    if [[ -t 0 && $ASSUME_YES -eq 0 ]]; then
        interactive_choose || exit 1
    else
        TARGET_PACK="${PACK:-${LAST_PACK:-${TARGETS[default,pack]}}}"
        chip_resolved=$(pack_chip "$TARGET_PACK")
        TARGET_ENV="${ENV:-${LAST_ENV:-$(pack_default_env "$TARGET_PACK" "$chip_resolved")}}"
        TARGET_IP="${IP_OVERRIDE:-${LAST_IP:-${TARGETS[default,ip]}}}"
        TARGET_TRANSPORT="${LAST_TRANSPORT:-auto}"
        if [[ $T_OPT_SET -eq 1 ]]; then
            TARGET_TRANSPORT="$TRANSPORT"
        fi
        TARGET_AUTH="${AUTH_OPT:-${LAST_AUTH:-${TARGETS[default,auth]:-root:root}}}"
        TARGET_NAME="$TARGET_PACK (default/estado)"
    fi
fi

# Memorizar o último alvo (mesmo em modo preset — os IPs mudam, reutiliza-se no next run)
LAST_IP="$TARGET_IP"
LAST_PACK="$TARGET_PACK"
LAST_ENV="$TARGET_ENV"
LAST_TRANSPORT="$TARGET_TRANSPORT"
LAST_AUTH="$TARGET_AUTH"
save_state

# ---------------------------------------------------------------------------
# Localizar o signed .bin mais recente do pack
# ---------------------------------------------------------------------------
# Encontrar a versão mais recente dentro do pack, e dentro dela o timestamp mais recente
# Estrutura: <repo>/chatgpt/build/esp32s3|esp32p4/<PACK>/<ver>/<ts>/<env>_<ver>_<PACK>_<ts>[_signed].bin
PACK_DIR=""
for candidate in \
    "$REPO_ROOT/chatgpt/build/esp32s3/$TARGET_PACK" \
    "$REPO_ROOT/chatgpt/build/$TARGET_ENV/$TARGET_PACK" \
    "$REPO_ROOT/chatgpt/build/esp32p4/$TARGET_PACK" ; do
    if [[ -d "$candidate" ]]; then
        PACK_DIR="$candidate"
        break
    fi
done
if [[ -z "$PACK_DIR" ]]; then
    echo "ERRO: nenhuma pasta de build encontrada para pack=$TARGET_PACK env=$TARGET_ENV" >&2
    echo "      Procurado em:" >&2
    echo "        $REPO_ROOT/chatgpt/build/esp32s3/$TARGET_PACK" >&2
    echo "        $REPO_ROOT/chatgpt/build/$TARGET_ENV/$TARGET_PACK" >&2
    echo "        $REPO_ROOT/chatgpt/build/esp32p4/$TARGET_PACK" >&2
    exit 1
fi

# Apanhar a versão mais recente (lexicograficamente a mais alta, e.g. v1.107 > v1.105)
LATEST_VER=$(ls -1 "$PACK_DIR" 2>/dev/null | sort -V | tail -1)
if [[ -z "$LATEST_VER" ]]; then
    echo "ERRO: $PACK_DIR está vazia (sem versões)" >&2
    exit 1
fi
# Dentro da versão, o timestamp mais recente (lexicograficamente — YYYYMMDD_HHMM ordena certo)
LATEST_TS=$(ls -1d "$PACK_DIR/$LATEST_VER"/*/ 2>/dev/null | sort -V | tail -1)
if [[ -z "$LATEST_TS" ]]; then
    echo "ERRO: $PACK_DIR/$LATEST_VER não tem builds" >&2
    exit 1
fi
if [[ -n "$LATEST_TS" && "$LATEST_TS" == */ ]]; then
    LATEST_TS="${LATEST_TS%/}"
fi
# Preferir o _signed.bin (Waveshare pack); fallback ao .bin plain (outros packs)
SIGNED_BIN=$(ls -t "$LATEST_TS"/*_signed.bin 2>/dev/null | head -1)
BIN_SIGNED=0
if [[ -z "$SIGNED_BIN" ]]; then
    # Fallback: o .bin plain (sem signing). Só pode ser OTA'd via WiFi (multipart):
    # o handler WiFi suporta o caminho legacy (sem "filesize"); o handler Ethernet
    # (ethernet_bootstrap_handlers.h) exige SEMPRE assinatura (totalSize>0 → otaVerifyEnd).
    PLAIN_BIN=$(ls -t "$LATEST_TS"/*.bin 2>/dev/null | grep -v "_signed" | head -1)
    if [[ -n "$PLAIN_BIN" ]]; then
        echo "AVISO: não há *_signed.bin em $LATEST_TS — a usar .bin plain NÃO ASSINADO" >&2
        echo "      Só dá para OTA via WiFi (multipart). Para Ethernet é preciso" >&2
        echo "      rebuilde com build_firmware.sh (gera *_signed.bin)." >&2
        SIGNED_BIN="$PLAIN_BIN"
    else
        echo "ERRO: nenhum .bin em $LATEST_TS" >&2
        exit 1
    fi
else
    BIN_SIGNED=1
fi

BIN_SIZE=$(stat -c%s "$SIGNED_BIN")
BIN_MTIME=$(stat -c%y "$SIGNED_BIN" | cut -d. -f1)
BIN_NAME=$(basename "$SIGNED_BIN")

# Procura o *_signed.bin mais recente em QUALQUER versão/timestamp do pack
# (usado quando a build mais recente é plain mas o target é Ethernet/raw).
find_latest_signed() {
    local v t f
    for v in $(ls -1 "$PACK_DIR" 2>/dev/null | sort -V -r); do
        t=$(ls -1d "$PACK_DIR/$v"/*/ 2>/dev/null | sort -V | tail -1)
        [[ -z "$t" ]] && continue
        t="${t%/}"
        f=$(ls -t "$t"/*_signed.bin 2>/dev/null | head -1)
        if [[ -n "$f" ]]; then
            echo "$f"
            return 0
        fi
    done
    return 1
}

# Se o transport for explicitamente raw (Ethernet) e o bin for NÃO assinado,
# tentar trocar para o signed mais recente do pack; senão abortar.
# (No modo auto, esta validação é feita em runtime, após ler ethernet_link.)
if [[ $BIN_SIGNED -eq 0 && "$TARGET_TRANSPORT" == "raw" ]]; then
    ALT_SIGNED="$(find_latest_signed)"
    if [[ -n "$ALT_SIGNED" ]]; then
        echo "AVISO: transport raw exige signed; a usar em alternativa:" >&2
        echo "       $ALT_SIGNED" >&2
        SIGNED_BIN="$ALT_SIGNED"
        BIN_SIGNED=1
        BIN_SIZE=$(stat -c%s "$SIGNED_BIN")
        BIN_MTIME=$(stat -c%y "$SIGNED_BIN" | cut -d. -f1)
        BIN_NAME=$(basename "$SIGNED_BIN")
    else
        echo "ERRO: bin não assinado + transport raw (Ethernet) — o handler Ethernet" >&2
        echo "      exige firmware assinado (260 bytes + RSA). Rebundle com" >&2
        echo "      build_firmware.sh para gerar *_signed.bin, ou use -T multipart" >&2
        echo "      num target WiFi." >&2
        exit 1
    fi
fi

# ---------------------------------------------------------------------------
# Resumo
# ---------------------------------------------------------------------------
echo "============================================================"
echo "  Node32-HUB OTA Deployer"
echo "============================================================"
echo "  Target:    $TARGET_NAME ($TARGET_IP)"
echo "  Pack:      $TARGET_PACK"
echo "  Env:       $TARGET_ENV"
echo "  Upload:    $TARGET_TRANSPORT"
echo "  Bin:       $SIGNED_BIN"
echo "  Size:      $BIN_SIZE bytes"
echo "  Build:     $BIN_MTIME"
echo "============================================================"

if [[ $DRY_RUN -eq 1 ]]; then
    echo "[dry-run] Saindo sem fazer upload."
    exit 0
fi

TMPDIR_TMP="${TMPDIR:-/tmp}"
STATUS_JSON="$TMPDIR_TMP/ota_status_$$.json"
UPLOAD_CODE="$TMPDIR_TMP/ota_code_$$"
UPLOAD_BODY="$TMPDIR_TMP/ota_resp_$$.txt"
trap 'rm -f "$STATUS_JSON" "$UPLOAD_CODE" "$UPLOAD_BODY"' EXIT

# ---------------------------------------------------------------------------
# 1. Confirmar target reachable (+ descobrir auth)
# ---------------------------------------------------------------------------

# Auth: o device tem um master switch persistido (config web.auth_enabled,
# default OFF → aceita qualquer pedido). Quando está ON, exige a user/pass
# configurada na página /access (não é fixa no firmware).
# Estratégia: tenta SEM credenciais; se 401 → tenta com as credenciais atuais
# (default root:root). AUTHARGS fica vazio ou com -u user:pass conforme o caso.
probe_root() {
    # $@ = args curl extra (ex.: -u user:pass). Devolve o http_code (000 = sem conexão).
    local code
    code=$(curl -s -m 5 "${@}" -o /dev/null -w "%{http_code}" "http://$TARGET_IP/" 2>/dev/null)
    [[ -z "$code" ]] && code="000"
    echo "$code"
}

echo ""
echo "[1/4] A verificar se $TARGET_IP responde..."
AUTHARGS=()
HTTP_HEALTH=$(probe_root)
if [[ "$HTTP_HEALTH" == "401" ]]; then
    AUTHARGS=(-u "$TARGET_AUTH")
    HTTP_HEALTH=$(probe_root "${AUTHARGS[@]}")
fi
if [[ "$HTTP_HEALTH" != "200" ]]; then
    echo "ERRO: $TARGET_IP não responde (HTTP $HTTP_HEALTH)" >&2
    if [[ "$HTTP_HEALTH" == "401" ]]; then
        echo "      Auth activa e credenciais '$TARGET_AUTH' rejeitadas." >&2
        echo "      Confere user/pass na página /access do device, ou passa -a user:pass." >&2
    else
        echo "      O device está powered/ligado à rede?" >&2
    fi
    exit 2
fi
if [[ ${#AUTHARGS[@]} -eq 0 ]]; then
    echo "      (auth desactivada no device — a pedir sem credenciais)"
else
    echo "      (auth activa — a usar '$TARGET_AUTH')"
fi

# Lê /api/status (com auth + retry) e guarda o JSON em $STATUS_JSON.
# O /api/status é throttle 1 req/500ms (core_admin_handler.cpp) e EXIGE auth
# (web_routes_core.cpp:32 no WiFi; auth global no bootstrap Ethernet).
# Por isso fazemos uma única chamada com retry e parseamos todos os campos dela.
fetch_status_json() {
    for attempt in 1 2 3 4 5; do
        # Até o /api/status responder com o payload real (429 gerado pelo
        # rate-limiter também é JSON válido — validamos o campo firmware_version).
        if curl -s -m 8 "${AUTHARGS[@]}" "http://$TARGET_IP/api/status" -o "$STATUS_JSON" 2>/dev/null \
            && python3 -c "import json;d=json.load(open('$STATUS_JSON'));assert 'firmware_version' in d" 2>/dev/null; then
            return 0
        fi
        sleep 2
    done
    return 1
}

# Extrai um campo do JSON guardado (empty string se não existir).
fetch_field() {
    local field="$1"
    python3 -c "
import sys,json
try:
    d=json.load(open('$STATUS_JSON'))
    v=d.get('$field','')
    if isinstance(v,bool):
        v='true' if v else 'false'
    print(v)
except Exception:
    sys.exit(1)" 2>/dev/null
}

if ! fetch_status_json; then
    echo "ERRO: não consegui ler /api/status do device (ainda em init?)" >&2
    echo "      Tenta de novo em 30s, ou verifica se o device está realmente up." >&2
    exit 2
fi
CURRENT_VER=$(fetch_field firmware_version)
CURRENT_BUILD=$(fetch_field firmware_build)
echo "      device responde: firmware=$CURRENT_VER, build='$CURRENT_BUILD'"

# Se não conseguimos ler o build string do device, abortar (em vez de comparar com '?')
if [[ -z "$CURRENT_BUILD" ]]; then
    echo "ERRO: não consegui ler firmware_build do /api/status (device ainda em init?)" >&2
    echo "      Tenta de novo em 30s, ou verifica se o device está realmente up." >&2
    exit 2
fi

# Deteção automática do transport: Ethernet (W5500/RMII) usa RAW; WiFi usa multipart.
# O handler Ethernet (ethernet_bootstrap_handlers.h:1056) lê o BODY CRU + headers
# X-Filename/X-Filesize — multipart corromperia o binário. O handler WiFi
# (ota_admin_handler.cpp) só aceita multipart. Logo o formato TEM de corresponder.
if [[ "$TARGET_TRANSPORT" == "auto" ]]; then
    ETH_LINK=$(fetch_field ethernet_link)
    if [[ "$ETH_LINK" == "true" ]]; then
        TARGET_TRANSPORT="raw"
    else
        TARGET_TRANSPORT="multipart"
    fi
    echo "      transport auto → $TARGET_TRANSPORT (ethernet_link=$ETH_LINK)"
fi

# Guarda de assinatura: bin não assinado NÃO pode ir para Ethernet (handler exige
# signed). Em WiFi, o bin não assinado é enviado SEM o campo filesize (caminho legacy).
# Se o auto-detecto escolheu raw mas só há plain, tenta trocar para um signed.
if [[ $BIN_SIGNED -eq 0 && "$TARGET_TRANSPORT" == "raw" ]]; then
    ALT_SIGNED="$(find_latest_signed)"
    if [[ -n "$ALT_SIGNED" ]]; then
        echo "      AVISO: transport raw exige signed; a usar em alternativa:" >&2
        echo "       $ALT_SIGNED" >&2
        SIGNED_BIN="$ALT_SIGNED"
        BIN_SIGNED=1
        BIN_SIZE=$(stat -c%s "$SIGNED_BIN")
        BIN_MTIME=$(stat -c%y "$SIGNED_BIN" | cut -d. -f1)
        BIN_NAME=$(basename "$SIGNED_BIN")
    else
        echo "ERRO: bin não assinado + transport raw (Ethernet) — o handler Ethernet" >&2
        echo "      exige firmware assinado (ata 260 bytes + RSA). Rebundle com" >&2
        echo "      build_firmware.sh para gerar *_signed.bin, ou use -T multipart" >&2
        echo "      num target WiFi." >&2
        exit 1
    fi
fi
UPLOAD_HAS_FILESIZE=0
if [[ $BIN_SIGNED -eq 1 ]]; then
    UPLOAD_HAS_FILESIZE=1
fi

# Versão esperada = nome do bin (extrai v1.107 do nome)
EXPECTED_VER=$(basename "$SIGNED_BIN" | grep -oE 'v[0-9]+\.[0-9]+' | head -1)
# A comparação AUTENTICA sempre pelo firmware_build (inclui a data/hora de compilação).
# A fwv (v1.107) pode ser igual entre builds diferentes — o build string é a fonte de verdade.
if [[ -n "$EXPECTED_VER" && "$CURRENT_VER" == "$EXPECTED_VER" ]]; then
    echo ""
    echo "  ⚠  AVISO: o device corre firmware_version=$CURRENT_VER (mesma fwv do bin)"
    echo "     Build do device:  '$CURRENT_BUILD'"
    echo "     Build do bin:     '$BIN_MTIME'"
    if [[ $ASSUME_YES -eq 1 ]]; then
        echo "     (-y: a prosseguir sem perguntar)"
    else
        echo "     O device pode já ter ESTA build, ou uma build diferente da mesma fwv."
        echo "     Proceder mesmo assim? [y/N]"
        read -r ans
        [[ "$ans" =~ ^[Yy]$ ]] || { echo "Cancelado."; exit 0; }
    fi
fi

# ---------------------------------------------------------------------------
# 2. Abrir janela OTA
# ---------------------------------------------------------------------------
echo ""
echo "[2/4] A abrir janela OTA (enable-ota)..."
ENABLE_RESP=$(curl -s -m 15 "${AUTHARGS[@]}" -X POST "http://$TARGET_IP/api/enable-ota" 2>&1)
echo "      $ENABLE_RESP"
    if ! echo "$ENABLE_RESP" | grep -qE '"status" ?: ?"ok"'; then
    echo "ERRO: enable-ota falhou" >&2
    exit 3
fi

# ---------------------------------------------------------------------------
# 3. Upload
# ---------------------------------------------------------------------------
echo ""
UPLOAD_START=$(date +%s)
# CURL_BASE: args comuns; a resposta (body) vai para $UPLOAD_BODY e o http_code
# (apenas, sem misturar stderr) para $UPLOAD_CODE via -w.
CURL_BASE=(-s --http1.1 -H "Expect:" \
    --connect-timeout 30 \
    --max-time 600 \
    "${AUTHARGS[@]}" \
    -X POST \
    -o "$UPLOAD_BODY" \
    -w "%{http_code}")
if [[ "$TARGET_TRANSPORT" == "raw" ]]; then
    echo "[3/4] A enviar $BIN_SIZE bytes (RAW) para $TARGET_IP/api/firmware-upload..."
    echo "      (pode demorar 1-3 min; o device pode fazer reboot antes da resposta HTTP)"
    if [[ $UPLOAD_HAS_FILESIZE -eq 1 ]]; then
        curl "${CURL_BASE[@]}" \
            -H "Content-Type: application/octet-stream" \
            -H "X-Filename: $BIN_NAME" \
            -H "X-Filesize: $BIN_SIZE" \
            --data-binary @"$SIGNED_BIN" \
            "http://$TARGET_IP/api/firmware-upload" > "$UPLOAD_CODE" 2>/dev/null
    else
        curl "${CURL_BASE[@]}" \
            -H "Content-Type: application/octet-stream" \
            -H "X-Filename: $BIN_NAME" \
            --data-binary @"$SIGNED_BIN" \
            "http://$TARGET_IP/api/firmware-upload" > "$UPLOAD_CODE" 2>/dev/null
    fi
else
    echo "[3/4] A enviar $BIN_SIZE bytes (multipart) para $TARGET_IP/api/firmware-upload..."
    echo "      (pode demorar 1-3 min; o device pode fazer reboot antes da resposta HTTP)"
    if [[ $UPLOAD_HAS_FILESIZE -eq 1 ]]; then
        curl "${CURL_BASE[@]}" \
            -F "file=@$SIGNED_BIN;type=application/octet-stream" \
            -F "filesize=$BIN_SIZE" \
            "http://$TARGET_IP/api/firmware-upload" > "$UPLOAD_CODE" 2>/dev/null
    else
        curl "${CURL_BASE[@]}" \
            -F "file=@$SIGNED_BIN;type=application/octet-stream" \
            "http://$TARGET_IP/api/firmware-upload" > "$UPLOAD_CODE" 2>/dev/null
    fi
fi
UPLOAD_END=$(date +%s)
echo "      Duração: $((UPLOAD_END - UPLOAD_START))s"

UPLOAD_HTTP=$(tr -d '[:space:]' < "$UPLOAD_CODE" 2>/dev/null || echo "000")
echo "      HTTP: $UPLOAD_HTTP"
if [[ -s "$UPLOAD_BODY" ]]; then
    echo "      Response: $(head -c 400 "$UPLOAD_BODY")"
fi

# Interpretar o resultado do upload:
#   - resposta com "success":true  → OK (reboot iminente)
#   - resposta com "success":false → falha explícita → abortar com detalhe
#   - HTTP 4xx/5xx (auth, 404, 500) → falha → abortar
#   - sem resposta/000 (connection reset durante reboot) → deixar o passo 4 confirmar
if grep -qE '"success" ?: ?false' "$UPLOAD_BODY" 2>/dev/null; then
    echo "ERRO: upload rejeitado pelo device" >&2
    head -c 400 "$UPLOAD_BODY" >&2; echo >&2
    exit 4
fi
case "$UPLOAD_HTTP" in
    200) : ;;
    000|"") echo "      (sem resposta HTTP — pode ser reboot no fim do upload; vou confirmar no passo 4)" ;;
    *)   echo "ERRO: upload devolveu HTTP $UPLOAD_HTTP (esperava 200)" >&2
         [[ -s "$UPLOAD_BODY" ]] && { head -c 400 "$UPLOAD_BODY" >&2; echo >&2; }
         exit 4 ;;
esac

# ---------------------------------------------------------------------------
# 4. Confirmar reboot / versão nova
# ---------------------------------------------------------------------------
echo ""
echo "[4/4] A confirmar reboot + nova versão..."

# Esperar até 60s pelo reboot (mais folga que os 30s anteriores: o P4 arranca lento)
DEVICE_BACK=0
for i in 6 12 18 24 30 36 42 48 54 60; do
    sleep 6
    HTTP_HEALTH=$(probe_root "${AUTHARGS[@]}")
    if [[ "$HTTP_HEALTH" == "200" ]]; then
        echo "      device voltou (após ~${i}s)"
        DEVICE_BACK=1
        break
    fi
    echo "      ainda em reboot... (${i}s)"
done

if [[ $DEVICE_BACK -eq 1 ]]; then
    sleep 2
fi
if ! fetch_status_json; then
    echo "      device não respondeu ao /api/status após reboot"
    NEW_VER="?"
    NEW_BUILD="?"
    NEW_UPTIME="?"
else
    NEW_VER=$(fetch_field firmware_version)
    NEW_BUILD=$(fetch_field firmware_build)
    NEW_UPTIME=$(fetch_field uptime)
fi

echo ""
echo "============================================================"
echo "  Resultado"
echo "============================================================"
echo "  Antes:  $CURRENT_VER  ($CURRENT_BUILD)"
echo "  Agora:  $NEW_VER  ($NEW_BUILD)"
echo "  Uptime: $NEW_UPTIME"

# CONFIRMAÇÃO DEFINITIVA: comparar pelo firmware_build (string completa com data/hora).
# A fwv (v1.107) pode ser a mesma entre builds diferentes; o build string é único.
# Se for idêntico → o OTA não aplicou (ou era exatamente a mesma build — no-op).
if [[ -n "$CURRENT_BUILD" && "$NEW_BUILD" == "$CURRENT_BUILD" ]]; then
    echo "  ✗ AVISO: build string idêntica antes/depois — OTA não aplicou"
    echo "    device build: '$CURRENT_BUILD'"
    echo "    (handler OTA do pack provavelmente resetou antes de gravar)"
    echo "    (ver skill node32-hub-firmware, secção 'OTA HANDLER CLOSES CONNECTION')"
    exit 5
fi
if [[ -n "$EXPECTED_VER" && "$NEW_VER" == "$EXPECTED_VER" ]]; then
    echo "  ✓ OTA aplicada com sucesso"
    echo "    Antes:  $CURRENT_BUILD (uptime antes do reboot)"
    echo "    Agora:  $NEW_BUILD ($NEW_UPTIME)"
    exit 0
else
    echo "  ✗ AVISO: fwv não mudou para $EXPECTED_VER (build: $NEW_BUILD)"
    echo "    (handler OTA avariado — ver skill node32-hub-firmware)"
    exit 5
fi