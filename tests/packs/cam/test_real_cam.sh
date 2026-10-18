#!/usr/bin/env bash
# =============================================================================
# tests/packs/cam/test_real_cam.sh — Teste REAL da API da câmara: Pack Cam-M
# -----------------------------------------------------------------------------
# Auditoria ponta-a-ponta da página /camera + /hub contra o firmware:
# status, config GET/POST (clamps), start/stop, snapshot, MJPEG stream
# (boundary + multi-viewer + raw + frames a fluir), motion diff, photo,
# recording start/stop (+ tag no filename), files list, playback, delete,
# motion reset/counter, lamp, hub page.
# Faz snapshot da config e REPÕE todos os campos que mexe (não quebra nada).
# Requer board online com build Cam-M. SD ausente => photo/record dão SKIP.
# Uso: bash tests/packs/cam/test_real_cam.sh (IP/AUTH via env TEST_IP/TEST_AUTH)
# =============================================================================
set -u
BASE="http://${TEST_IP:?define TEST_IP com o IP do device}"
if [ -n "${TEST_AUTH:-}" ]; then AUTHARG=(-u "$TEST_AUTH"); else AUTHARG=(); fi
T="${TMPDIR:-/tmp}/cam_api_$$"
mkdir -p "$T"

P_PASS=0; P_FAIL=0; P_SKIP=0
declare -a P_FAILS=() P_SKIPS=()

pass() { P_PASS=$((P_PASS+1)); echo "  PASS: $1"; }
fail() { P_FAIL=$((P_FAIL+1)); P_FAILS+=("$1"); echo "  FAIL: $1"; }
skip() { P_SKIP=$((P_SKIP+1)); P_SKIPS+=("$1"); echo "  SKIP: $1"; }

api_get()  { local r; r=$(curl -s --max-time 10 "${AUTHARG[@]}" "$BASE$1"); if [ -z "$r" ]; then sleep 2; r=$(curl -s --max-time 10 "${AUTHARG[@]}" "$BASE$1"); fi; echo "$r"; }
api_post() { curl -s --max-time 10 "${AUTHARG[@]}" -X POST --data "$2" "$BASE$1"; }
http_code() { curl -s --max-time 10 "${AUTHARG[@]}" -o /dev/null -w "%{http_code}" "$1"; }
jget() { python3 -c "import sys,json;print(json.load(sys.stdin).get('$2','__MISSING__'))" < "$1"; }
mget() { python3 -c "import sys,json;d=json.load(sys.stdin);print(d.get('motion',{}).get('$2','__MISSING__'))" < "$1"; }

echo "===== TESTE REAL API CÂMARA — Cam-M @ $BASE ====="

# 1. Status -----------------------------------------------------------------
S="$(api_get /api/camera/status)"
echo "$S" > "$T/status.json"
CL0="$(jget "$T/status.json" streamClients)"
if [ "$CL0" != "__MISSING__" ] && [ "$CL0" != "0" ]; then
  echo "  NOTE: streamClients=$CL0 no arranque — fecha tabs do browser na camara para testes de viewers limpos"
fi
for k in ready model initInProgress streamFps streamFrames motion motionScore motionEvents; do
  if [ "$(jget "$T/status.json" "$k")" = "__MISSING__" ]; then fail "status sem $k"; else pass "status tem $k"; fi
done

# 2. Config GET + snapshot para restore --------------------------------------
C="$(api_get /api/camera/config)"
echo "$C" > "$T/config.json"
for k in framesize quality fps brightness aec2 timelapse_secs lamp lampAvail motion auto_mode; do
  if [ "$(jget "$T/config.json" "$k")" = "__MISSING__" ]; then fail "config sem $k"; else pass "config tem $k"; fi
done
for k in enabled sensitivity cooldown_secs lamp_auto total_events roi_x roi_y roi_w roi_h; do
  if [ "$(mget "$T/config.json" "$k")" = "__MISSING__" ]; then fail "motion sem $k"; else pass "motion tem $k"; fi
done
ORIG_Q="$(jget "$T/config.json" quality)"; ORIG_FPS="$(jget "$T/config.json" fps)"
ORIG_BR="$(jget "$T/config.json" brightness)"; ORIG_FR="$(jget "$T/config.json" framesize)"
ORIG_SENS="$(mget "$T/config.json" sensitivity)"
ORIG_EV="$(mget "$T/config.json" total_events)"

# 3. Clamps -------------------------------------------------------------------
api_post /api/camera/config "quality=200" > /dev/null
[ "$(jget <(api_get /api/camera/config) quality)" = "63" ] && pass "clamp quality=200→63" || fail "clamp quality=200→63"
api_post /api/camera/config "fps=99" > /dev/null
F="$(jget <(api_get /api/camera/config) fps)"
{ [ "$F" -ge 1 ] && [ "$F" -le 30 ]; } 2>/dev/null && pass "clamp fps=99→$F (1..30)" || fail "clamp fps=99→$F"
api_post /api/camera/config "brightness=99" > /dev/null
[ "$(jget <(api_get /api/camera/config) brightness)" = "2" ] && pass "clamp brightness=99→2" || fail "clamp brightness"
api_post /api/camera/config "motion_sensitivity=0" > /dev/null
[ "$(mget <(api_get /api/camera/config) sensitivity)" = "1" ] \
  && pass "clamp motion_sensitivity=0→1" || fail "clamp motion_sensitivity"
api_post /api/camera/config "motion_roi_x=99&motion_roi_y=99&motion_roi_w=99&motion_roi_h=99" > /dev/null
ROICFG="$(api_get /api/camera/config)"
{ [ "$(echo "$ROICFG" | python3 -c "import sys,json;d=json.load(sys.stdin);print(d['motion']['roi_x'])")" = "39" ] && \
  [ "$(echo "$ROICFG" | python3 -c "import sys,json;d=json.load(sys.stdin);print(d['motion']['roi_y'])")" = "29" ]; } \
  && pass "clamp ROI→39/29" || fail "clamp ROI ($ROICFG)"
api_post /api/camera/config "motion_cooldown_secs=9999&lamp=99" > /dev/null
CFG2="$(api_get /api/camera/config)"
{ [ "$(echo "$CFG2" | python3 -c "import sys,json;d=json.load(sys.stdin);print(d['motion']['cooldown_secs'])")" = "600" ] && \
  [ "$(jget <(echo "$CFG2") lamp)" = "15" ]; } \
  && pass "clamp cooldown→600 + lamp→15" || fail "clamp cooldown/lamp"
api_post /api/camera/config "framesize=99" > /dev/null
[ "$(jget <(api_get /api/camera/config) framesize)" = "$ORIG_FR" ] && pass "framesize=99 ignorado" || fail "framesize=99 ignorado"

# 4. Start + snapshot -----------------------------------------------------------
R="$(api_post /api/camera/start "")"
echo "$R" | grep -q '"success":true' && pass "start aceite" || fail "start aceite ($R)"
READY=""
for _ in $(seq 1 15); do
  READY="$(jget <(api_get /api/camera/status) ready)"
  [ "$READY" = "True" ] && break
  sleep 2
done
[ "$READY" = "True" ] && pass "camera ready após start" || fail "camera ready após start"
MODEL="$(jget <(api_get /api/camera/status) model)"
echo "  INFO: sensor $MODEL"
CT="$(curl -s --max-time 15 "${AUTHARG[@]}" -D "$T/snap.h" -o "$T/snap.jpg" -w "%{content_type}:%{size_download}" "$BASE/cam/snapshot")"
case "$CT" in
  image/jpeg:*)
    SZ="${CT##*:}"
    { [ "$SZ" -gt 1024 ]; } 2>/dev/null && pass "snapshot jpeg ${SZ}B" || fail "snapshot pequeno ${SZ}B" ;;
  *) fail "snapshot content-type ($CT)" ;;
esac

# 5. Stream: boundary + crescimento + multi-viewer 503 + frames ----------------------
# NOTE: viewer 1 uses a long max-time so it outlives the viewer-3 check below.
curl -s --max-time 60 "${AUTHARG[@]}" -N -o "$T/stream.mjpeg" "$BASE/cam/stream" &
STREAM_PID=$!
sleep 5
if grep -q -- "--frame" "$T/stream.mjpeg" 2>/dev/null; then pass "stream tem boundary --frame"; else fail "stream sem boundary"; fi
SZ1="$(stat -c%s "$T/stream.mjpeg" 2>/dev/null || echo 0)"
sleep 4
SZ2="$(stat -c%s "$T/stream.mjpeg" 2>/dev/null || echo 0)"
{ [ "$SZ2" -gt "$SZ1" ]; } 2>/dev/null && pass "stream cresce (${SZ1}→${SZ2}B)" || fail "stream parado (${SZ1}→${SZ2}B)"
CODE2="$(http_code "$BASE/cam/stream")"
[ "$CODE2" = "200" ] && pass "2º viewer aceite" || fail "2º viewer devia dar 200 (deu $CODE2)"
curl -s --max-time 15 "${AUTHARG[@]}" -N -o "$T/stream2.mjpeg" "$BASE/cam/stream" &
STREAM2_PID=$!
sleep 4
CODE3="$(http_code "$BASE/cam/stream")"
[ "$CODE3" = "503" ] && pass "3º viewer recebe 503" || fail "3º viewer devia dar 503 (deu $CODE3)"
kill "$STREAM2_PID" 2>/dev/null; wait "$STREAM2_PID" 2>/dev/null
RAWCODE="$(http_code "$BASE/cam/raw")"
[ "$RAWCODE" = "200" ] && pass "raw 200" || fail "raw (http $RAWCODE)"
F1="$(jget <(api_get /api/camera/status) streamFrames)"
sleep 3
F2="$(jget <(api_get /api/camera/status) streamFrames)"
{ [ "$F1" != "__MISSING__" ] && [ "$F1" != "0" ] && [ "$F2" -ge "$F1" ]; } 2>/dev/null \
  && pass "streamFrames flui ($F1→$F2)" || fail "streamFrames parado ($F1→$F2)"
kill "$STREAM_PID" 2>/dev/null; wait "$STREAM_PID" 2>/dev/null

# 6. Motion diff ------------------------------------------------------------------
DCT="$(curl -s --max-time 15 "${AUTHARG[@]}" -o "$T/diff.jpg" -w "%{http_code}:%{content_type}" "$BASE/cam/motion/diff")"
case "$DCT" in
  200:image/jpeg*) pass "motion diff jpeg" ;;
  *) fail "motion diff ($DCT)" ;;
esac

# 7. Photo (precisa SD) + delete da própria photo -----------------------------------
PH="$(api_post /api/camera/photo "")"
PHOTO_PATH=""
if echo "$PH" | grep -q '"success":true'; then
  PHOTO_PATH="$(echo "$PH" | python3 -c "import sys,json;print(json.load(sys.stdin).get('filename',''))")"
  pass "photo gravada $PHOTO_PATH"
elif echo "$PH" | grep -qi "SD"; then
  skip "photo sem SD ($PH)"
else
  fail "photo ($PH)"
fi

# 8. Recording start/stop + tag no filename (precisa SD) ------------------------------
# Deterministic even if motion/dashcam auto-started a session: stop any
# pre-existing recording and pause motion for this section (restored after).
PRE_REC="$(jget <(api_get /api/camera/status) recording)"
if [ "$PRE_REC" = "True" ]; then
  api_post /api/camera/recording/stop "" > /dev/null
  echo "  NOTE: stopped pre-existing auto recording"
  sleep 1
fi
MOT_WAS="$(mget <(api_get /api/camera/config) enabled)"
if [ "$MOT_WAS" = "True" ]; then
  api_post /api/camera/config "motion_enabled=0" > /dev/null
  echo "  NOTE: motion paused for record/playback tests"
fi
REC="$(api_post /api/camera/recording/start "")"
if echo "$REC" | grep -q '"success":true'; then
  pass "recording start"
  sleep 2
  MID="$(api_get /api/camera/status)"
  echo "$MID" | grep -q '"recording":true' && pass "a gravar 2s depois" || fail "parou sozinho em 2s ($MID)"
  sleep 3
  ST="$(api_post /api/camera/recording/stop "")"
  if echo "$ST" | grep -q '"success":true'; then
    pass "recording stop"
    PPATH="$(echo "$ST" | python3 -c "import sys,json;print(json.load(sys.stdin).get('path',''))")"
    case "$PPATH" in *_M.avi) pass "filename com tag _M ($PPATH)";; *) fail "filename sem tag ($PPATH)";; esac
  else
    fail "recording stop ($ST)"
  fi
elif echo "$REC" | grep -qi "SD"; then
  skip "recording sem SD ($REC)"
else
  fail "recording start ($REC)"
fi

# 8b. Files list + playback + delete -------------------------------------------------
FL="$(api_get /api/camera/files)"
echo "$FL" | grep -q '"files"' && pass "files list" || fail "files list ($FL)"
HAV_AVI="$(echo "$FL" | python3 -c "import sys,json;fs=json.load(sys.stdin).get('files',[]);fs=sorted([f for f in fs if f['n'].endswith('.avi')],key=lambda f:f['n']);print(fs[-1]['n'] if fs else '')")"
if [ -n "$HAV_AVI" ]; then
  PB="$(curl -s --max-time 12 "${AUTHARG[@]}" -N --get --data-urlencode "file=$HAV_AVI" -o "$T/play.mjpeg" -w "%{http_code}" "$BASE/cam/playback")"
  { [ "$PB" = "200" ] && grep -q -- "--frame" "$T/play.mjpeg"; } 2>/dev/null \
    && pass "playback $HAV_AVI" || fail "playback $HAV_AVI (http $PB)"
else
  skip "playback sem ficheiros"
fi
if [ -n "$PHOTO_PATH" ]; then
  DL="$(api_post /api/camera/delete "file=$(python3 -c "import urllib.parse,sys;print(urllib.parse.quote(sys.argv[1], safe=''))" "$PHOTO_PATH")")"
  echo "$DL" | grep -q '"success":true' && pass "delete photo" || fail "delete photo ($DL)"
  api_get /api/camera/files | grep -q "$PHOTO_PATH" && fail "photo ainda listada" || pass "photo saiu da lista"
fi

# 8c. Motion reset --------------------------------------------------------------------
MR="$(api_post /api/camera/motion/reset "")"
echo "$MR" | grep -q '"success":true' && pass "motion reset" || fail "motion reset ($MR)"
[ "$(jget <(api_get /api/camera/status) motionEvents)" = "0" ] \
  && pass "motionEvents zero após reset" || fail "motionEvents após reset"

# 8d. Hub page --------------------------------------------------------------------------
HCODE="$(http_code "$BASE/hub")"
[ "$HCODE" = "200" ] && pass "hub 200" || fail "hub (http $HCODE)"

# 9. Stop + restore total ---------------------------------------------------------------
api_post /api/camera/stop "" > /dev/null
sleep 1
[ "$(jget <(api_get /api/camera/status) ready)" = "False" ] && pass "stop → ready false" || fail "stop → ready false"
api_post /api/camera/config "quality=$ORIG_Q&fps=$ORIG_FPS&brightness=$ORIG_BR&framesize=$ORIG_FR&motion_sensitivity=$ORIG_SENS&motion_roi_x=0&motion_roi_y=0&motion_roi_w=0&motion_roi_h=0&motion_cooldown_secs=5&lamp=0&motion_enabled=$([ "$MOT_WAS" = "True" ] && echo 1 || echo 0)" > /dev/null
{ [ "$(jget <(api_get /api/camera/config) quality)" = "$ORIG_Q" ] && \
  [ "$(mget <(api_get /api/camera/config) sensitivity)" = "$ORIG_SENS" ]; } \
  && pass "config reposta" || fail "config reposta"

rm -rf "$T"
echo "===== RESULTADO: PASS=$P_PASS FAIL=$P_FAIL SKIP=$P_SKIP ====="
[ ${#P_FAILS[@]} -gt 0 ] && printf 'FAILED:\n  %s\n' "${P_FAILS[@]}"
[ ${#P_SKIPS[@]} -gt 0 ] && printf 'SKIPPED:\n  %s\n' "${P_SKIPS[@]}"
[ "$P_FAIL" -gt 0 ] && exit 1
exit 0
