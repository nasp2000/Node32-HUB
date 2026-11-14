// sd_status_flags.h — the "storage disabled" flag of /api/sd-status.
//
// The /sdcard page has an "SD disabled" state (auto.js: `if (j.disabled)`) that
// was unreachable because the payload never carried the field (BACKLOG SD-4).
// "Disabled" means the storage subsystem is not usable for this build or the
// user turned data saving off — not "no card inserted" (that stays visible as
// Mounted=No with the sizes line).
#pragma once

// sdSupported: the firmware was built with ENABLE_SDCARD (or an equivalent
//              storage backend the page can report on).
// savingEnabled: the persisted "save received data" switch.
inline bool sdStatusDisabled(bool sdSupported, bool savingEnabled) {
    return !sdSupported || !savingEnabled;
}
