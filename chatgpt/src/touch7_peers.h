#pragma once

// touch7_peers.h — viewer of other Node32-HUB nodes (e.g. pack CAM).
//
// Snapshot-first (never MJPEG live): GET /cam/snapshot + /api/camera/status
// per enabled peer, cadence from touch7PeerPollIntervalSec(). JPEG decoded
// with JPEGDecoder into the display's PSRAM tile buffers. Rule nº1: snapshot
// slot (256 KB) + JSON docs in PSRAM; zero heap per poll.

#include "feature_flags.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

#include <Arduino.h>

void touch7PeersBegin();
void touch7PeersTick();

// Force an immediate refresh of one peer (TESTAR button / PIR snapshot).
void touch7PeerRefreshNow(uint8_t idx);

// Fullscreen fetch: snapshot straight into a caller PSRAM buffer.
bool touch7PeerFetchFull(uint8_t idx, uint16_t* buf, uint16_t w, uint16_t h);

#endif // SELECTED_PACK == PACK_S3_TOUCH7
