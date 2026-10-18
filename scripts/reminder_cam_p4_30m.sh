#!/bin/bash
# =============================================================================
# Reminder script for P4 HD camera work
# Runs every 30 minutes via cron (every 30 min)
# Purpose: Nuno, remember to check the OV5647 camera on the P4 HD board
#         and verify that the new firmware (v1.115) is working correctly.
# =============================================================================

LOG_FILE="/home/nasp/Code/Node32-HUB/logs/cam_p4_reminder.log"
DATE=$(date '+%Y-%m-%d %H:%M:%S')

echo "[$DATE] Reminder: Check P4 HD camera (OV5647) status after firmware v1.115" >> "$LOG_FILE"

# Check if the camera is currently detected
CAM_STATUS=$(curl -s "http://192.168.1.18/api/serial-monitor" | python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    for msg in d.get('messages', []):
        if 'CAM-P4' in msg.get('text','') or 'ov5647' in msg.get('text',''):
            print(msg.get('text',''))
except:
    print('NO_DATA')
" 2>/dev/null)

if [ -z "$CAM_STATUS" ] || [ "$CAM_STATUS" = "NO_DATA" ]; then
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] ⚠️  Camera not reachable – check wiring, I2C bus, or firmware v1.115 deployment" >> "$LOG_FILE"
else
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] ✅ Camera detected: $CAM_STATUS" >> "$LOG_FILE"
fi

# Additional diagnostic: check if the firmware is the new version
FIRMWARE_VERSION=$(curl -s "http://192.168.1.18/api/status" | python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    print(d.get('firmware_version', 'unknown'))
except:
    print('unknown')
" 2>/dev/null)

echo "[$(date '+%Y-%m-%d %H:%M:%S')] Firmware version: $FIRMWARE_VERSION" >> "$LOG_FILE"

echo "Reminder logged at $(date)"
