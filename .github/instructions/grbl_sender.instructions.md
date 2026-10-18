---
applyTo: "chatgpt/src/grbl_sender.*,chatgpt/src/grbl_web_api.*,chatgpt/src/usb_host_manager.*"
---

# GRBL Sender Instructions

## Architecture (P4)
- All GRBL tasks run on **Core 0** (same as TinyUSB + AsyncTCP web server).
- psramFeedTask: prio 18, USB daemon: prio 19, USB client: prio 18.
- Planner check **DISABLED** — GRBL manages its own planner. ESP32 focus: keep RX buffer full.

## Constants (`grbl_sender_internal.h`)
- `GRBL_RX_BUFFER_STRICT_LIMIT` = 127 (GRBL RX buffer)
- `GRBL_INFLIGHT_FIFO_SIZE` = 64 (tracked pending lines)
- `GRBL_PSRAM_TARGET_BURST_BYTES` = 120 (~94% of RX buffer)
- `GRBL_PSRAM_BURST_MAX_LINES` = 128 (ceiling for short lines)
- Planner keeps its own queue (no host-side planner pacing); ESP32 focus: keep RX buffer full
- `GRBL_ACK_STALL_TIMEOUT_MS` = 5000 (5s no "ok" → HOLD)
- `GRBL_RX_STALL_TIMEOUT_MS` = 30000 (30s no data → HOLD)
- `GRBL_STATUS_POLL_STREAMING_MS` = 500 (500 ms poll during streaming)

## Flow: SD → PSRAM → USB → GRBL → "ok" → wake feeder
1. File loaded to PSRAM, preprocessed (comments removed, uppercased)
2. psramFeedTask reads lines, sends via USB, tracks inflight bytes
3. Buffer full → `vTaskDelay(1)` yield (web server runs on Core 0)
4. "ok" arrives → `fifoPop()`, inflight freed, feeder wakes immediately
5. `GRBLLOG ON` enables trace log to SD `/GRBL_JOBS/`

## Feeder task pattern (burst + yield)
```
burst 24 lines → vTaskDelay(1) → web server runs → burst again
buffer full → vTaskDelay(1) → wait for "ok" notification
```
Never spin indefinitely — always yield after burst to keep web server responsive.

## Resume after ack stall
`grblResume()` allows resume after `ack_stall_safe_stop` with warning (does not block operator).

## Web API (registered in `web_routes_monitor.cpp` + route registry)
- `GET /api/grbl/status` — session, position, flow state
- `POST /api/grbl/send` — manual g-code
- `POST /api/grbl/stream` — start SD file stream
- `POST /api/grbl/pause|resume|cancel` — stream control
- `GET /api/grbl/events` — SSE console events

## Job trace log (`GRBLLOG ON`)
Fields per line: `T=ms;SEQ=n;EV=event;SENT=n;ACK=n;TOT=n;INFLIGHT=n;FIFO=n;ERR=n;POLL=n;DETAIL=text`
Final VERIFY event confirms `ack == sent`.

## USB host (`usb_host_manager.cpp`)
- `usbHostSend()` via TinyUSB FIFO (512B), single write + single flush
- GRBL baudrate configurable via `SET_LINE_CODING` — relevant for USB-UART adapters, not direct USB-CDC