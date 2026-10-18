#ifndef WEBPAGE_WEBPAGE_H
#define WEBPAGE_WEBPAGE_H

#include "../feature_flags.h"

// Shared HTML session counter used by both Wi-Fi and fallback web paths.
extern volatile int activeHtmlSessions;

// Keep the include path stable while selecting the active web implementation.
#if ENABLE_WIFI_WEB_SERVER==1
#include "webpage_impl.h"
#else
#include "../web_shared_pages.h"
#endif

#endif // WEBPAGE_WEBPAGE_H
