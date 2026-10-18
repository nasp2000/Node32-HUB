#pragma once
#include <Arduino.h>
#include "../../feature_flags.h"

#if ENABLE_PROXIMITY==1
size_t streamProximityDashboardPageToPrint(Print& out);
#else
inline size_t streamProximityDashboardPageToPrint(Print&) { return 0; }
#endif
