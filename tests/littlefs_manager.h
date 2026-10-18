#pragma once

// Forwarding header so that the copied sensor_config_base_stub.h can resolve
// `#include "../littlefs_manager.h"` (which from tests/native/ lands here)
// to the stub in tests/native/stubs/littlefs_manager.h.
#include "native/stubs/littlefs_manager.h"
