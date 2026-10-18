#pragma once

// Pack identifiers
#ifndef PACK_NONE
#define PACK_NONE   0
#endif
#ifndef PACK_RAMBOX
#define PACK_RAMBOX 1
#endif
#ifndef PACK_CAM
#define PACK_CAM    2
#endif
#ifndef PACK_RS232
#define PACK_RS232  3
#endif
#ifndef PACK_ENVIRONMENT
#define PACK_ENVIRONMENT 4
#endif
#ifndef PACK_RADIO
#define PACK_RADIO 5
#endif
#ifndef PACK_P4_CNC
#define PACK_P4_CNC 6
#endif
#ifndef PACK_P4_USB_SERVER
#define PACK_P4_USB_SERVER 7
#endif
#ifndef PACK_S3_CNC
#define PACK_S3_CNC 8
#endif
#ifndef PACK_WAVESHARE
#define PACK_WAVESHARE 10
#endif
#ifndef PACK_CAM_P4
#define PACK_CAM_P4 11
#endif
#ifndef PACK_S3_TOUCH7
#define PACK_S3_TOUCH7 12
#endif

// Default pack when the user does not select one in user_config.h.
#ifndef SELECTED_PACK
#define SELECTED_PACK PACK_NONE
#endif

#if SELECTED_PACK == PACK_RAMBOX
#include "pack_rambox.h"
#elif SELECTED_PACK == PACK_CAM
#include "pack_cam.h"
#elif SELECTED_PACK == PACK_RS232
#include "pack_rs232.h"
#elif SELECTED_PACK == PACK_ENVIRONMENT
#include "pack_environment.h"
#elif SELECTED_PACK == PACK_RADIO
#include "pack_radio.h"
#elif SELECTED_PACK == PACK_P4_CNC
#include "pack_p4_cnc.h"
#elif SELECTED_PACK == PACK_P4_USB_SERVER
#include "pack_p4_usb_server.h"
#elif SELECTED_PACK == PACK_S3_CNC
#include "pack_s3_cnc_usb.h"
#elif SELECTED_PACK == PACK_WAVESHARE
#include "pack_waveshare.h"
#elif SELECTED_PACK == PACK_CAM_P4
#include "pack_cam_p4.h"
#elif SELECTED_PACK == PACK_S3_TOUCH7
#include "pack_s3_touch7.h"
#elif SELECTED_PACK != PACK_NONE
#error "Unknown SELECTED_PACK value. Use PACK_NONE, PACK_RAMBOX, PACK_CAM, PACK_RS232, PACK_ENVIRONMENT, PACK_RADIO, PACK_P4_CNC, PACK_P4_USB_SERVER, PACK_S3_CNC, PACK_WAVESHARE, PACK_CAM_P4, or PACK_S3_TOUCH7."
#endif

// ---------------------------------------------------------------------------
// Pack identity defaults (overridden by individual pack_*.h)
// ---------------------------------------------------------------------------
#ifndef PACK_SYSTEM_NAME
#define PACK_SYSTEM_NAME "Node32-HUB"
#endif
#ifndef PACK_EMAIL_SUBJECT
#define PACK_EMAIL_SUBJECT "Node32-Hub"
#endif
#ifndef PACK_EMOJI
#define PACK_EMOJI "🤖"
#endif
#ifndef PACK_BRAND_NAME
#define PACK_BRAND_NAME PACK_SYSTEM_NAME
#endif
