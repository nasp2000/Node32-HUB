#pragma once
#include "feature_flags.h"

#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

#include <Arduino.h>

// ── Public API ────────────────────────────────────────────────────────────────
// Called from radioAudioRuntimeBegin() / radioAudioRuntimeLoop().
// All blocking network I/O happens inside upnpClientLoop() in small slices.

void upnpClientBegin();
void upnpClientLoop();

// Trigger a fresh SSDP discovery (called by POST /api/radio/upnp/discover).
void upnpClientDiscover();

// JSON responses for the HTTP handlers.
// All strings are built in PSRAM (SpiRamJsonDocument) and returned by value.
// metadata=true issues a SOAP BrowseMetadata (single object) instead of
// BrowseDirectChildren — used to re-fetch a track's full DIDL metadata.
String upnpServersJson();
String upnpBrowseJson(const String& serverUuid, const String& objectId,
                      int startIndex, int requestedCount, bool metadata = false);

#endif // PACK_RADIO
#endif // ENABLE_UDA1334A
