#pragma once

// touch7_alarms.h — T/H threshold alarms with local voice phrases.
// Rising-edge per alarm bit + 15 min cooldown each. Silent when the WAV is
// missing (upload via /files). Poll-based, zero RAM.

#include "feature_flags.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

#include <Arduino.h>

void touch7AlarmsBegin();
void touch7AlarmsTick();

#endif // SELECTED_PACK == PACK_S3_TOUCH7
