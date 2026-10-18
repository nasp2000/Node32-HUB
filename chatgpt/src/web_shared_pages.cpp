#include "web_shared_pages.h"

#include "webpages/assets/fragments_system_asset.h"
#include "webpages/assets/page_home_asset.h"
#include "webpages/renderers/access_page_renderer.h"
#include "webpages/renderers/networks_page_renderer.h"
#include "webpages/renderers/proximity_renderer.h"
#include "webpages/renderers/modbus_renderer.h"
#include "feature_flags.h"
#if ENABLE_BUZZER==1
#  include "webpages/renderers/buzzer_page_renderer.h"
#endif
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO==1
#  include "webpages/renderers/do_renderer.h"
#endif
#if ENABLE_CAN==1
#  include "webpages/renderers/can_page_renderer.h"
#endif
#if ENABLE_HX711==1
#  include "webpages/renderers/hx711_page_renderer.h"
#endif
#if ENABLE_INA226==1
#  include "webpages/renderers/ina226_page_renderer.h"
#endif
#if ENABLE_SOUND_SENSOR==1
#  include "webpages/renderers/sound_page_renderer.h"
#endif
#if ENABLE_MAX31865==1
#  include "webpages/renderers/max31865_page_renderer.h"
#endif
#if ENABLE_UNIKON==1
#  include "webpages/renderers/unikon_page_renderer.h"
#endif
#if ENABLE_IPG_LASER==1
#  include "webpages/renderers/ipg_laser_page_renderer.h"
#endif
#if ENABLE_API_REFERENCE_PAGE==1
#  include "webpages/renderers/api_reference_renderer.h"
#endif
#if ENABLE_UDA1334A==1
#  include "webpages/renderers/page_audio_dac_uda1334a_renderer.h"
#  include "webpages/renderers/radio_ui_page_renderer.h"
#endif
#include "webpages/renderers/commands_datetime_page_renderer.h"
#include "webpages/renderers/commands_page_renderer.h"
#include "webpages/renderers/grbl_controller_page_renderer.h"
#include "webpages/renderers/sdcard_page_renderer.h"
#include "webpages/renderers/status_page_renderer.h"
#include "webpages/renderers/ethernet_page_renderer.h"
#include "webpages/renderers/email_page_renderer.h"
#include "webpages/renderers/google_script_page_renderer.h"
#if ENABLE_PAGE_FTP==1
#include "webpages/renderers/ftp_page_renderer.h"
#endif
#include "webpages/renderers/home_page_renderer.h"
#include "webpages/renderers/mq135_page_renderer.h"
#include "webpages/renderers/ota_page_renderer.h"
#include "webpages/renderers/printer_page_renderer.h"
#include "webpages/renderers/lpt_db25_page_renderer.h"
#include "webpages/renderers/recipients_page_renderer.h"
#include "webpages/renderers/rs232_page_renderer.h"
#include "webpages/renderers/sensor_charts_page_renderer.h"
#include "webpages/renderers/sensor_config_page_renderer.h"
#include "webpages/renderers/usb_server_page_renderer.h"
#if ENABLE_STRUCTURES_PAGE==1
#include "webpages/renderers/structures_page_renderer.h"
#endif
#include "webpages/renderers/wifi_page_renderer.h"
#if ENABLE_CAMERA==1
#  include "webpages/renderers/camera_renderer.h"
#endif

size_t streamSharedHomePage(Print& out) {
    streamHomePageToPrint(
        out,
        PAGE_HOME,
        FIRMWARE_VERSION,
        SYSTEM_SECTION,
        SYSTEM_SECTION_SETTINGS_BUTTON,
        SYSTEM_SECTION_OTA_BUTTON,
        SYSTEM_SECTION_CLOSE
    );
    return 0;
}

size_t streamSharedWifiPage(Print& out) {
    return streamWifiPageToPrint(out);
}

size_t streamSharedEthernetPage(Print& out) {
    return streamEthernetPageToPrint(out);
}

size_t streamSharedUsbServerPage(Print& out) {
    return streamUsbServerPageToPrint(out);
}

size_t streamSharedSdcardPage(Print& out) {
    return streamSdcardPageToPrint(out);
}

size_t streamSharedStatusPage(Print& out) {
    return streamStatusPageToPrint(out);
}

size_t streamSharedGrblControllerPage(Print& out) {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_P4_CNC)
    return streamGrblControllerPageToPrint(out);
#else
    (void)out;
    return 0;
#endif
}

size_t streamSharedPrinterPage(Print& out) {
    return streamPrinterPageToPrint(out);
}

size_t streamSharedLptDb25Page(Print& out) {
    return streamLptDb25PageToPrint(out);
}

size_t streamSharedStructuresPage(Print& out) {
#if ENABLE_STRUCTURES_PAGE==1
    return streamStructuresPageToPrint(out);
#else
    return 0;
#endif
}

size_t streamSharedEmailPage(Print& out) {
    return streamEmailPageToPrint(out);
}

size_t streamSharedGoogleScriptPage(Print& out) {
    return streamGoogleScriptPageToPrint(out);
}

#if ENABLE_PAGE_FTP==1
size_t streamSharedFtpPage(Print& out) {
    return streamFtpPageToPrint(out);
}
#endif

size_t streamSharedRecipientsPage(Print& out) {
    return streamRecipientsPageToPrint(out);
}

size_t streamSharedRs232Page(Print& out) {
    return streamRs232PageToPrint(out);
}

size_t streamSharedMq135Page(Print& out) {
    return streamMq135PageToPrint(out);
}

size_t streamSharedSensorConfigPage(Print& out) {
    return streamSensorConfigPageToPrint(out);
}

size_t streamSharedSensorChartsPage(Print& out) {
    return streamSensorChartsPageToPrint(out);
}

size_t streamSharedAccessPage(Print& out) {
    return streamAccessPageToPrint(out);
}

size_t streamSharedNetworksPage(Print& out) {
    return streamNetworksPageToPrint(out);
}

#if ENABLE_UDA1334A==1
size_t streamSharedUda1334aPage(Print& out) {
    return streamUDA1334APageToPrint(out);
}

size_t streamSharedRadioUiPage(Print& out) {
    return streamRadioUiPageToPrint(out);
}
#endif

size_t streamSharedProximityPage(Print& out) {
    return streamProximityPageToPrint(out);
}

#if ENABLE_BUZZER==1
size_t streamSharedBuzzerPage(Print& out) {
    return streamBuzzerPageToPrint(out);
}
#endif

size_t streamSharedModbusRtuPage(Print& out) {
    return streamModbusRtuPageToPrint(out);
}

#if ENABLE_CAMERA==1
size_t streamSharedCameraPage(Print& out) {
    return streamCameraPageToPrint(out);
}
#endif

size_t streamSharedModbusTcpPage(Print& out) {
    return streamModbusTcpPageToPrint(out);
}

size_t streamSharedModbusLivePage(Print& out) {
    return streamModbusLivePageToPrint(out);
}

#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO==1
size_t streamSharedDoPage(Print& out) {
    return streamDoPageToPrint(out);
}
#endif

#if ENABLE_CAN==1
size_t streamSharedCanPage(Print& out) {
    return streamCanPageToPrint(out);
}
#endif

#if ENABLE_HX711==1
size_t streamSharedHx711Page(Print& out) {
    return streamHx711PageToPrint(out);
}
#endif

#if ENABLE_INA226==1
size_t streamSharedIna226Page(Print& out) {
    return streamIna226PageToPrint(out);
}
#endif

#if ENABLE_SOUND_SENSOR==1
size_t streamSharedSoundPage(Print& out) {
    return streamSoundPageToPrint(out);
}
#endif

#if ENABLE_MAX31865==1
size_t streamSharedMax31865Page(Print& out) {
    return streamMax31865PageToPrint(out);
}
#endif

#if ENABLE_UNIKON==1
size_t streamSharedUnikonPage(Print& out) {
    return streamUnikonPageToPrint(out);
}
#endif

#if ENABLE_IPG_LASER==1
size_t streamSharedIpgLaserPage(Print& out) {
    return streamIpgLaserPageToPrint(out);
}
#endif

#if ENABLE_API_REFERENCE_PAGE==1
size_t streamSharedApiReferencePage(Print& out) {
    return streamApiReferencePage(out);
}
#endif

size_t streamSharedOtaPage(Print& out, bool ethernetMode) {
    return streamOtaPageToPrint(
        out,
        ethernetMode ? OtaPageTransportMode::EthernetBootstrap : OtaPageTransportMode::WifiAsync
    );
}

size_t streamSharedCommandsPage(Print& out) {
    return streamCommandsPageToPrint(out);
}

size_t streamSharedCommandsByDatetimePage(Print& out) {
    return streamCommandsByDatetimePageToPrint(out);
}
