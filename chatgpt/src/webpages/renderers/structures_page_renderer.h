#pragma once

#include <Arduino.h>
#include "../../feature_flags.h"

#if ENABLE_STRUCTURES_PAGE==1
size_t streamStructuresPageToPrint(Print& out);
#endif // ENABLE_STRUCTURES_PAGE