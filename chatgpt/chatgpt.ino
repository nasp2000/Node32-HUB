// Node32-HUB firmware — ESP32-S3/P4 industrial communication hub
// Version: see smtp_core_impl.h (FIRMWARE_VERSION_STR)

size_t getArduinoLoopTaskStackSize() { return 16384; }

#include "src/feature_flags.h"
#include "src/psram_helpers.h"
#include "src/network_access_point.h"
#include "src/network_hybrid_manager.h"
#include "src/structures.h"
#include "src/telnet_server.h"
#include "src/arduino_compat.h"

// SD Card, storage, filesystem
#include "src/sdcard.h"
#include "src/littlefs_manager.h"
#include "src/sd_rotation_manager.h"
#include "src/psram_vfs.h"

// Printer, charset, i18n
#include "src/printer_emulation.h"
#include "src/charset_conversion.h"
#include "src/languages/lang_active.h"
#include "src/priority_manager.h"
#include "src/object_pools.h"
#include "src/observability.h"
#include "src/memory_pressure_guard.h"

// Network, runtime, email
#include "src/network_manual_switch.h"
#include "src/network_saved_profiles.h"
#include "src/runtime_scheduler.h"
#include "src/runtime_email.h"
#include "src/sd_diagnostics.h"
#include "src/email_message_builder.h"
#include "src/file_header_builder.h"
#include "src/monitor_session_state.h"
#include "src/runtime_environment.h"
#include "src/ipg_laser_module.h"
#include "src/runtime_rs232_queue.h"
#include "src/runtime_storage.h"
#include "src/coordinator_hooks.h"
#include "src/async_helpers.h"
#include "src/config_manager_runtime.h"
#include "src/email_runtime_config_state.h"
#include "src/email_transport.h"
#include "src/google_email.h"
#include "src/debug_writer.h"
#include "src/chart_history.h"

// Libraries
#if ENABLE_ETHERNET==1
#include <Ethernet.h>
#endif
#include <CircularBuffer.hpp>
#include <deque>
#include <Preferences.h>
#include "src/arduinojson_fix.h"
#include <ArduinoJson.h>
#include <vector>
#include <map>
#include <cstring>
#include <ctype.h>
#include "src/sniffer.h"
#include "src/string_utils.h"

// Modbus
#include "src/modbus_core.h"
#include "src/modbus_poll.h"
#include "src/modbus_tcp.h"
#include "src/modbus_slave.h"
#include "src/modbus_runtime.h"

#if ENABLE_CAN==1
#include "src/can_core.h"
#include "src/can_runtime.h"
#endif
#if ENABLE_CAMERA==1
#include "src/camera_manager.h"
#endif
#if ENABLE_USB_HOST==1
#include "src/usb_host_manager.h"
#endif
#if ENABLE_USBIP_SERVER==1
#include "src/usbip_server.h"
#endif
#if ENABLE_GRBL==1
#include "src/grbl_sender.h"
#endif
#include "src/psram_queue.h"
#include "src/commands.h"
#include "src/CommandRegistry.h"
#include <utility>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <esp_partition.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <esp_err.h>
#include <esp_task_wdt.h>
#if defined(__has_include)
#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#endif
#if __has_include(<esp_spiram.h>)
#include <esp_spiram.h>
#endif
#endif

#if !defined(TARGET_BOARD_P4) || TARGET_BOARD_P4!=1
#include <esp_bt.h>
#include <esp_coexist.h>
#endif
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Global imports needed before implementation headers
#include "src/direct_log.h"
#include "src/sd_tasks_impl.h"

// Web server
#if ENABLE_WIFI_WEB_SERVER==1
#define USE_ASYNC_WEB_SERVER
#include <ESPAsyncWebServer.h>
AsyncWebServer server(80);
void setupConfigManagerEndpoints();
void setupWebServer(AsyncWebServer &server);
void webclientCleanup();
#endif

extern volatile int activeHtmlSessions;

// Hardware modules
#include "src/arduino_bridge.h"
#include "src/screen.h"
#include "src/sensors/sensor_aht20_bmp280.h"
#include "src/sensors/sensor_config.h"
#if ENABLE_MQ135==1
#include "src/sensors/sensor_mq135_config.h"
#endif
#if ENABLE_HX711==1
#include "src/sensors/sensor_hx711_config.h"
#endif
#if ENABLE_INA226==1
#include "src/sensors/sensor_ina226_config.h"
#endif
#if ENABLE_BH1750==1
#include "src/sensors/sensor_bh1750_config.h"
#endif
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
#include "src/touch7_config.h"
#include "src/touch7_display.h"
#include "src/touch7_peers.h"
#include "src/touch7_weather.h"
#include "src/touch7_audio.h"
#include "src/touch7_pir.h"
#include "src/touch7_alarms.h"
#endif
#if ENABLE_SOUND_SENSOR==1
#include "src/sensors/sensor_sound_config.h"
#endif
#if ENABLE_MAX31865==1
#include "src/sensors/sensor_max31865_config.h"
#endif
#if ENABLE_PROXIMITY==1
#include "src/sensors/proximity_manager.h"
#endif
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
#include "src/tca9554_do.h"
#endif
#if defined(BOARD_HAS_DS3231) && BOARD_HAS_DS3231 == 1
#include "src/rtc_ds3231.h"
#endif
#if ENABLE_BUZZER == 1
#include "src/buzzer.h"
#endif
#if ENABLE_UDA1334A == 1
#include "src/audio_dac_uda1334a.h"
#include "src/audio_dac_uda1334a_config.h"
#include "src/radio_audio_runtime.h"
#endif
#if ENABLE_UNIKON == 1
#include "src/unikon_protocol.h"
#endif
#if ENABLE_BRICON == 1
#include "src/bricon_protocol.h"
#endif
#include "src/ConfigManager.h"
#include "src/structures.h"
#include "src/webpages/help.h"
#include <time.h>
#include <stdarg.h>

// SMTP
#if ENABLE_SMTP==1
#include <ReadyMail.h>
#endif
#include "src/smtp_helpers.h"

extern bool isEmailSendingActive;

#include "src/chatgpt_includes.h"
