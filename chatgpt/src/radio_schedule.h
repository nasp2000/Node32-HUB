#pragma once

#include "feature_flags.h"

#if ENABLE_UDA1334A == 1
#include <Arduino.h>

// Weekly radio schedule (alarm / sleep-style windows). Evaluated inside the
// radio runtime loop; only the audio this schedule started is owned by it, so
// a manual play/stop never gets killed or hijacked by the schedule.
//
// Storage: /config/radio_schedule.json  (atomic .tmp write, like the runtime).
// Slot:  days = bitmask (bit0=Sunday..bit6=Saturday), [start,end) local time,
//        type = "url"|"folder"|"playlist", src, name, vol, enabled.
//        end <= start  => overnight slot ending the following day.

void radioScheduleBegin();                 // load + restore ownership after reboot
void radioScheduleLoop();                  // evaluate windows, fire start/stop
void radioScheduleClearOwner();            // user took manual control -> release
String radioScheduleJson();                // slots + current owner + local time
bool radioScheduleSet(const String& slotsJson, String& messageOut); // validate+save

#endif // ENABLE_UDA1334A==1
