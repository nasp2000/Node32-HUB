#pragma once

// nvs_write_guard.h -- Pure decision helper for NVS write coalescing.
//
// ConfigManager::save() rewrites several NVS keys (wifi JSON, saved-networks
// JSON, passwords) on every full save. Flash/NVS has wear levelling, but a
// redundant putString still costs heap churn (serialize + verify round-trip)
// and flash wear for zero benefit. The firmware therefore reads the stored
// value first and only writes when it differs; this header holds that pure
// comparison so it stays natively unit-testable (no Arduino dependency).

#include <cstring>

inline bool nvsStringNeedsWrite(const char* stored, const char* fresh) {
    if (!stored) stored = "";
    if (!fresh) fresh = "";
    return std::strcmp(stored, fresh) != 0;
}
