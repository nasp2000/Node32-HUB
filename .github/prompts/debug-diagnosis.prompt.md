---
agent: agent
description: Runtime diagnosis guide for heap, SD, email, and network issues. Uses built-in endpoints and Telnet.
---

# Runtime Diagnosis

## Quick triage

| Symptom | First check |
|---|---|
| Device unreachable | `GET /api/status` → `network` |
| Email failing | `GET /api/status` → `email` + `heap_free` |
| SD not logging | `GET /api/status` → `sdcard` |
| Crashes | SD `/log/YYYY-MM-DD.log` |
| Heap OOM | `GET /api/status` → `heap_free`, `heap_min_seen` |

## 1. `GET /api/status` — key fields
- `heap_free < 20KB` → pressure active
- `heap_min_seen < 15KB` → memory leak
- `storage_mode: PSRAM_VFS` → SD failed, data in PSRAM only
- `network.mode: NONE` → WiFi+Ethernet both failed
- `network.mode: AP` → fallback AP, could not connect to saved networks

## 2. Telnet (`telnet <ip> 23`)
Commands: `STATUS`, `FLUSH`, `HEAP`, `RESET-STATS`, `HELP`

## 3. SD crash log — `/api/files?path=/log/`
- `ESP_RST_PANIC` → null pointer, stack overflow
- `ESP_RST_WDT` → task blocked >60s (SD I/O, TLS)
- `ESP_RST_BROWNOUT` → power supply issue

## 4. Memory pressure (`heap_free < 20KB`)
- `allowHtmlClient()` throttles new sessions
- Recover: wait for email to finish, or reduce clients
- `heap_min_seen < 15KB` → memory leak in recent changes

## 5. Email checklist
1. `GET /api/status` → `email.last_result`
2. Verify SMTP: `GET /api/config` → `smtp.*`
3. Heap ≥30KB for TLS handshake
4. `POST /api/email-test` for immediate test

## 6. Network
- `NONE` → both failed, check credentials and signal
- `AP` → fallback active
- `ETH` + `ip=0.0.0.0` → cable/switch issue
