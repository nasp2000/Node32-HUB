#pragma once

#include <pgmspace.h>
#include "../../feature_flags.h"

extern const char PAGE_ALERTS_HEAD[] PROGMEM;
extern const char PAGE_ALERTS_TABS[] PROGMEM;
#if ENABLE_PROXIMITY==1
extern const char PAGE_ALERTS_TABS_OEE[] PROGMEM;
#endif
extern const char PAGE_ALERTS_INACT_OPEN[] PROGMEM;
extern const char PAGE_ALERTS_AFTER_SENSORS_RESET[] PROGMEM;
extern const char PAGE_ALERTS_INACT_CLOSE[] PROGMEM;
extern const char PAGE_ALERTS_INACT_JS[] PROGMEM;
#if ENABLE_PROXIMITY==1
extern const char PAGE_ALERTS_OEE_TAB[] PROGMEM;
extern const char PAGE_ALERTS_OEE_CLOSE[] PROGMEM;
extern const char PAGE_ALERTS_OEE_JS[] PROGMEM;
#endif
extern const char PAGE_ALERTS_FOOTER[] PROGMEM;
