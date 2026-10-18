#pragma once

// touch7_audio.h — local WAV phrases/alarms via 1-pin PWM (GPIO6 -> PAM8403).
// WAV 16 kHz mono preferred (any 8/16-bit PCM plays; resampled naively).
// Non-blocking: a dedicated task streams file -> LEDC. PSRAM chunk buffer.

#include "feature_flags.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

#include <Arduino.h>

void touch7AudioBegin();
void touch7AudioTick(); // watchdog: restarts hung playback, applies volume

// Play /falas/<file> (WAV). Returns false if busy or file missing.
bool touch7AudioPlay(const String& file);
bool touch7AudioIsPlaying();
void touch7AudioStop();
void touch7AudioClick(); // 50 ms UI click

#endif // SELECTED_PACK == PACK_S3_TOUCH7
