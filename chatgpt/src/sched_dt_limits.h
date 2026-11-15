#pragma once

// Date/time scheduled command slots (sched_dt): the number of rows the firmware
// stores, exposes and executes.
//
// Single source of truth for ConfigManager's storage bounds, the bulk and
// per-slot web endpoints (WiFi + Ethernet), the status payload, the scheduler
// engine and the /commands-datetime page row cap. Keep the page's `MAX` in sync
// (asserted by tests/native/test_sched_dt_slots.cpp).
//
// The storage document is sized for this many slots in ConfigManager (PSRAM).
static constexpr int SCHED_DT_SLOT_MAX = 16;
