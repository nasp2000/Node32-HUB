#pragma once

// ============================================================================
// Phase 2 MP3 decoder — minimp3 (CC0-1.0) + native I2S -> UDA1334A.
//
// The pipeline mirrors the target architecture: source (SD File) -> decoder
// (minimp3 frame-by-frame) -> PCM -> native I2S driver -> UDA1334A. It is kept
// isolated from radio_audio_runtime on purpose (plan Phase 2: "Do not migrate
// the entire UI yet").
//
// minimp3.h ships in lib/minimp3/src (CC0-1.0, commit ea99364...). The
// MINIMP3_IMPLEMENTATION definition lives in audio_mp3_decoder.cpp only.
// ============================================================================

// Playback a local MP3 file from the SD card through minimp3 -> native I2S ->
// UDA1334A. Blocking; call from a dedicated task. Reports progress/errors via
// sendVirtualMessage (Serial + Web monitor).
void audioMp3PlaySd(const char* path);
