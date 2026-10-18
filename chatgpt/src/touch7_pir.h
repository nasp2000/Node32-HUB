#pragma once

// touch7_pir.h — presence sensor: wake/dim the LCD, screensaver policy,
// F3b PIR snapshot trigger. 1 GPIO (TOUCH7_PIR_PIN, -1 = unassigned until
// the schematic review in F0-HW). Poll-based, debounced, zero RAM.

#include "feature_flags.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

#include <Arduino.h>

void touch7PirBegin();
void touch7PirTick();

// Called by the LVGL touch read callback: any touch counts as presence.
void touch7PirTouchActivity();

// Seconds since last presence (for the ⚙ menu / tests).
unsigned long touch7PirIdleSec();

#endif // SELECTED_PACK == PACK_S3_TOUCH7
