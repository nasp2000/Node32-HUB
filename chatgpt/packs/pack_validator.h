#pragma once

// =============================================================================
// pack_validator.h — Build-time pack integrity checks
//
// Validates conflicts and dependencies after all ENABLE_* flags are resolved.
// Included at the end of user_config.h, after all packs and defaults.
// =============================================================================

// ---------------------------------------------------------------------------
// HARDWARE CONFLICTS — shared pins that cannot be active simultaneously
// ---------------------------------------------------------------------------

// Sniffer and RS232 both use UART1 (GPIO 19/20 on N16R8).
#if defined(ENABLE_SNIFFER) && ENABLE_SNIFFER==1 && defined(ENABLE_RS232) && ENABLE_RS232==1
#error "PACK CONFLICT: Sniffer (ENABLE_SNIFFER=1) conflicts with RS232 (ENABLE_RS232=1) — both need UART1."
#endif

// Sniffer and DB25/LPT both use the same UART1 pins.
#if defined(ENABLE_SNIFFER) && ENABLE_SNIFFER==1 && defined(ENABLE_DB25) && ENABLE_DB25==1
#error "PACK CONFLICT: Sniffer (ENABLE_SNIFFER=1) conflicts with DB25/LPT (ENABLE_DB25=1) — both need UART1."
#endif

// Modbus RTU and DB25/LPT both use UART1.
#if defined(ENABLE_MODBUS_RTU) && ENABLE_MODBUS_RTU==1 && defined(ENABLE_DB25) && ENABLE_DB25==1
#error "PACK CONFLICT: Modbus RTU (ENABLE_MODBUS_RTU=1) conflicts with DB25/LPT (ENABLE_DB25=1) — both need UART1."
#endif

// ---------------------------------------------------------------------------
// DEPENDENCY CHECKS — module requires another to be enabled
// ---------------------------------------------------------------------------

// USB/IP server requires USB Host hardware support.
#if defined(ENABLE_USBIP_SERVER) && ENABLE_USBIP_SERVER==1
  #if !defined(ENABLE_USB_HOST) || ENABLE_USB_HOST==0
  #error "PACK ERROR: ENABLE_USBIP_SERVER=1 requires ENABLE_USB_HOST=1."
  #endif
#endif

// Modbus sub-protocols require the Modbus core.
#if defined(ENABLE_MODBUS_RTU) && ENABLE_MODBUS_RTU==1
  #if !defined(ENABLE_MODBUS) || ENABLE_MODBUS==0
  #error "PACK ERROR: ENABLE_MODBUS_RTU=1 requires ENABLE_MODBUS=1."
  #endif
#endif

#if defined(ENABLE_MODBUS_TCP) && ENABLE_MODBUS_TCP==1
  #if !defined(ENABLE_MODBUS) || ENABLE_MODBUS==0
  #error "PACK ERROR: ENABLE_MODBUS_TCP=1 requires ENABLE_MODBUS=1."
  #endif
#endif

#if defined(ENABLE_MODBUS_SLAVE) && ENABLE_MODBUS_SLAVE==1
  #if !defined(ENABLE_MODBUS) || ENABLE_MODBUS==0
  #error "PACK ERROR: ENABLE_MODBUS_SLAVE=1 requires ENABLE_MODBUS=1."
  #endif
#endif

// RS232 charts depend on RS232 being active.
#if defined(ENABLE_RS232_CHARTS) && ENABLE_RS232_CHARTS==1
  #if !defined(ENABLE_RS232) || ENABLE_RS232==0
  #error "PACK ERROR: ENABLE_RS232_CHARTS=1 requires ENABLE_RS232=1."
  #endif
#endif

// Google Script page depends on Google Email module.
#if defined(ENABLE_PAGE_GOOGLE_SCRIPT) && ENABLE_PAGE_GOOGLE_SCRIPT==1
  #if !defined(ENABLE_GOOGLE_EMAIL) || ENABLE_GOOGLE_EMAIL==0
  #error "PACK ERROR: ENABLE_PAGE_GOOGLE_SCRIPT=1 requires ENABLE_GOOGLE_EMAIL=1."
  #endif
#endif

// FTP page depends on FTP client module.
#if defined(ENABLE_PAGE_FTP) && ENABLE_PAGE_FTP==1
  #if !defined(ENABLE_FTP_CLIENT) || ENABLE_FTP_CLIENT==0
  #error "PACK ERROR: ENABLE_PAGE_FTP=1 requires ENABLE_FTP_CLIENT=1."
  #endif
#endif

// Storage page ("Configurar storage") is present in every pack (T12), including
// boards without an SD slot (LittleFS/PSRAM-only): SD-only actions answer 501
// and config/status keep working, so no ENABLE_SDCARD requirement here.

// Dual File Manager requires at least one storage backend (SD or LittleFS).
#if defined(ENABLE_DUALFILE) && ENABLE_DUALFILE==1
  #if (!defined(ENABLE_SDCARD) || ENABLE_SDCARD==0) && (!defined(ENABLE_FILESYSTEM) || ENABLE_FILESYSTEM==0)
  #error "PACK ERROR: ENABLE_DUALFILE=1 requires ENABLE_SDCARD=1 or ENABLE_FILESYSTEM=1."
  #endif
#endif
