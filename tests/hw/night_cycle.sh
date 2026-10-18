#!/bin/bash
# Overnight P4 CAM + OEE cycle: build, OTA, camera tests. Appends to RESULTS.
cd /home/nasp/Code/Node32-HUB || exit 1
R=/home/nasp/Code/Node32-HUB/chatgpt/build/overnight_20260927.log
say() { echo "[$(date '+%H:%M:%S')] $*" | tee -a "$R"; }

say "=== NIGHT CYCLE START ==="

# 1. wait for the in-flight CAM_P4 build (cancel-fix)
for i in $(seq 1 40); do
  if ! pgrep -f "bg-worker" >/dev/null; then break; fi
  sleep 30
done
if tail -2 chatgpt/build/logs/esp32p4cam_CAM_P4.log | grep -q SUCCESS; then
  CAMBIN=$(ls -t chatgpt/build/esp32p4/CAM_P4/v1.115/*/esp32p4cam_v1.115_CAM_P4_*.bin 2>/dev/null | head -1 | sed 's|^chatgpt/||')
  say "P4CAM build OK: $CAMBIN"
else
  say "P4CAM build FAILED"; CAMBIN=""
fi

# 2. OTA .18
if [ -n "$CAMBIN" ]; then
  say "OTA .18 start"
  if TEST_IP=192.168.1.18 ./tests/hw/test_ota_upgrade.sh full --ota "chatgpt/$CAMBIN" >>"$R" 2>&1; then
    say "OTA .18 OK"
  else
    say "OTA .18 DONE-WITH-ISSUES (see above)"
  fi
fi

# 3. camera tests on .18 (MIPI photo + USB attempt bounded 6 min)
say "CAM test MIPI start"
curl -s -m15 -X POST http://192.168.1.18/api/camera/config -d "source=0" >/dev/null
curl -s -m25 -X POST http://192.168.1.18/api/camera/stop >/dev/null; sleep 3
curl -s -m30 -X POST http://192.168.1.18/api/camera/start >/dev/null; sleep 15
ST=$(curl -s -m10 http://192.168.1.18/api/camera/status)
say "MIPI status: $(echo "$ST" | python3 -c "import json,sys; d=json.load(sys.stdin); print(d['ready'],d['model'],d.get('width'),'x',d.get('height'))" 2>/dev/null)"
PH=$(curl -s -m30 -X POST http://192.168.1.18/api/camera/photo)
say "MIPI photo: $PH"
curl -s -m30 "http://192.168.1.18/cam/snapshot" -o /tmp/night_snap.jpg && say "snapshot bytes: $(stat -c %s /tmp/night_snap.jpg)"

say "CAM test USB start (bounded)"
curl -s -m15 -X POST http://192.168.1.18/api/camera/config -d "source=1" >/dev/null
curl -s -m25 -X POST http://192.168.1.18/api/camera/stop >/dev/null; sleep 3
curl -s -m25 -X POST http://192.168.1.18/api/camera/start >/dev/null
sleep 300
UST=$(curl -s -m10 http://192.168.1.18/api/camera/status)
say "USB status: $(echo "$UST" | python3 -c "import json,sys; d=json.load(sys.stdin); print(d['ready'],d['model'])" 2>/dev/null)"
say "USB FLOW lines: $(curl -s -m15 'http://192.168.1.18/api/serial-monitor' | grep -c 'FLOW')"
say "USB revert to MIPI"
curl -s -m15 -X POST http://192.168.1.18/api/camera/config -d "source=0" >/dev/null
curl -s -m25 -X POST http://192.168.1.18/api/camera/stop >/dev/null; sleep 3
curl -s -m30 -X POST http://192.168.1.18/api/camera/start >/dev/null; sleep 12
say "MIPI restored: $(curl -s -m10 http://192.168.1.18/api/camera/status | python3 -c "import json,sys; d=json.load(sys.stdin); print(d['ready'],d['model'])" 2>/dev/null)"

# 4. WAVESHARE build + OTA .29
say "WAVESHARE build start"
bash build_firmware.sh -e waveshare_8di8do -p WAVESHARE --bg >>"$R" 2>&1
for i in $(seq 1 60); do
  if ! pgrep -f "bg-worker" >/dev/null; then break; fi
  sleep 30
done
if tail -2 chatgpt/build/logs/waveshare_8di8do_WAVESHARE.log | grep -q SUCCESS; then
  say "WAVESHARE build OK"
  WBIN=$(ls -t chatgpt/build/esp32s3/WAVESHARE/v1.115/*/waveshare_8di8do_v1.115_WAVESHARE_*.bin 2>/dev/null | head -1)
  say "OTA .29: $WBIN"
  ./tests/hw/test_ota_upgrade.sh full --ota "$WBIN" >>"$R" 2>&1 && say "OTA .29 OK" || say "OTA .29 DONE-WITH-ISSUES"
else
  say "WAVESHARE build FAILED"
fi

# 5. final board versions
say ".18 fw: $(curl -s -m10 http://192.168.1.18/api/status | python3 -c "import json,sys; print(json.load(sys.stdin).get('firmware_version'))" 2>/dev/null)"
say ".29 fw: $(curl -s -m10 http://192.168.1.29/api/status | python3 -c "import json,sys; print(json.load(sys.stdin).get('firmware_version'))" 2>/dev/null)"
say "=== NIGHT CYCLE DONE ==="
