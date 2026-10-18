#pragma once

// touch7_log.h — UART-visible breadcrumbs for TouchMeteo-7 bring-up.
//
// Serial (USB CDC) lives on the OTG port and log_i is compiled out at
// CORE_DEBUG_LEVEL=0, so normal logs are invisible on the UART monitor.
// T7LOG uses ets_printf (ROM, always UART0, no init needed) and is therefore
// visible even before USB enumerates and across panics. TOUCH7-only.

#include "feature_flags.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

#include <Arduino.h>

#ifndef T7LOG
#define T7LOG(fmt, ...) ets_printf("[T7] " fmt "\n", ##__VA_ARGS__)
#endif

#else

#ifndef T7LOG
#define T7LOG(fmt, ...) ((void)0)
#endif

#endif // SELECTED_PACK == PACK_S3_TOUCH7
