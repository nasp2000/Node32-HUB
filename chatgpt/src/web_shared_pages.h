#pragma once

#include <Arduino.h>
#include "feature_flags.h" // ensure BOARD_HAS_TCA9554_DO, ENABLE_API_REFERENCE_PAGE etc. are defined before the guards below

extern const char* FIRMWARE_VERSION;

size_t streamSharedHomePage(Print& out);
size_t streamSharedWifiPage(Print& out);
size_t streamSharedEthernetPage(Print& out);
size_t streamSharedUsbServerPage(Print& out);
size_t streamSharedSdcardPage(Print& out);
size_t streamSharedStatusPage(Print& out);
size_t streamSharedGrblControllerPage(Print& out);
size_t streamSharedPrinterPage(Print& out);
size_t streamSharedLptDb25Page(Print& out);
size_t streamSharedStructuresPage(Print& out);
size_t streamSharedEmailPage(Print& out);
size_t streamSharedGoogleScriptPage(Print& out);
#if ENABLE_PAGE_FTP==1
size_t streamSharedFtpPage(Print& out);
#else
inline size_t streamSharedFtpPage(Print& out) { (void)out; return 0; }
#endif
size_t streamSharedRecipientsPage(Print& out);
size_t streamSharedRs232Page(Print& out);
size_t streamSharedMq135Page(Print& out);
size_t streamSharedSensorConfigPage(Print& out);
size_t streamSharedSensorChartsPage(Print& out);
size_t streamSharedAccessPage(Print& out);
size_t streamSharedNetworksPage(Print& out);
size_t streamSharedProximityPage(Print& out);
#if ENABLE_BUZZER==1
size_t streamSharedBuzzerPage(Print& out);
#endif
#if ENABLE_UDA1334A==1
size_t streamSharedUda1334aPage(Print& out);
size_t streamSharedRadioUiPage(Print& out);
#endif
size_t streamSharedModbusRtuPage(Print& out);
size_t streamSharedModbusTcpPage(Print& out);
size_t streamSharedModbusLivePage(Print& out);
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO==1
size_t streamSharedDoPage(Print& out);
#endif
#if ENABLE_CAN==1
size_t streamSharedCanPage(Print& out);
#endif
#if ENABLE_HX711==1
size_t streamSharedHx711Page(Print& out);
#endif
#if ENABLE_INA226==1
size_t streamSharedIna226Page(Print& out);
#endif
#if ENABLE_SOUND_SENSOR==1
size_t streamSharedSoundPage(Print& out);
#endif
#if ENABLE_MAX31865==1
size_t streamSharedMax31865Page(Print& out);
#endif
#if ENABLE_UNIKON==1
size_t streamSharedUnikonPage(Print& out);
#endif
#if ENABLE_IPG_LASER==1
size_t streamSharedIpgLaserPage(Print& out);
#endif
#if ENABLE_CAMERA==1
size_t streamSharedCameraPage(Print& out);
#endif
#if ENABLE_API_REFERENCE_PAGE==1
size_t streamSharedApiReferencePage(Print& out);
#endif
size_t streamSharedOtaPage(Print& out, bool ethernetMode);
size_t streamSharedCommandsPage(Print& out);
size_t streamSharedCommandsByDatetimePage(Print& out);