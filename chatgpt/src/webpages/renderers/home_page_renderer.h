#pragma once

#include <Arduino.h>

#include "../../ConfigManager.h"
#include "../../feature_flags.h"
#include "../../grbl_sender.h"
#include "../assets/fragments_system_asset.h"

extern ConfigManager configManager;
extern const char* FIRMWARE_BUILD;

inline bool shouldShowWifiButtonOnHome() {
#if ENABLE_WIFI==1
    if (configManager.getWifiDisabled()) return false;
    JsonVariant wifiHybrid = configManager.getSection("wifi_hybrid");
    if (!wifiHybrid.isNull() && wifiHybrid.containsKey("enabled")) {
        return wifiHybrid["enabled"] | true;
    }
    return true;
#else
    return false;
#endif
}

inline String buildHomePageEtag() {
    String etag;
    etag.reserve(strlen(FIRMWARE_BUILD) + 11);
    etag = FIRMWARE_BUILD;
    etag += F("|c");
    etag += configManager.getChartsPageEnabled() ? '1' : '0';
    etag += F("|s");
    etag += configManager.getSerialMonitorEnabled() ? '1' : '0';
    etag += F("|w");
    etag += shouldShowWifiButtonOnHome() ? '1' : '0';
    return etag;
}

inline void streamHomeProgmemSpan(Print& out, const char* start, const char* end) {
    size_t len = (size_t)(end - start);
    if (len == 0) return;

    static const size_t CHUNK = 512;
    static char buffer[CHUNK];
    size_t offset = 0;
    while (offset < len) {
        size_t current = min(CHUNK, len - offset);
        memcpy_P(buffer, start + offset, current);
        out.write((const uint8_t*)buffer, current);
        offset += current;
    }
}

inline void streamHomePageToPrint(Print& out,
                                  const char* page,
                                  const char* firmwareVersion,
                                  const char* systemSection,
                                  const char* systemSectionSettingsButton,
                                  const char* systemSectionOtaButton,
                                  const char* systemSectionClose) {
    const size_t versionTokenLen = 11;
    const size_t menuTokenLen = 24;
    const size_t modulesTokenLen = 19; // %%MODULES_SECTION%%
    const size_t recipientsTokenLen = 22; // %%RECIPIENTS_SECTION%%
    const size_t commandsTokenLen = 20; // %%COMMANDS_SECTION%%
    const size_t explorerTokenLen = 24;
    const size_t serialMonTokenLen = 22; // %%SERIAL_MONITOR_BTN%%
    const size_t monitorTokenLen = 18; // %%MONITOR_BUTTON%%
    const size_t wifiTokenLen = 15;
    const size_t emailTokenLen = 16; // %%EMAIL_BUTTON%%
    const size_t ethernetTokenLen = 19;
    const size_t networksButtonTokenLen = 19; // %%NETWORKS_BUTTON%%
    const size_t googleScriptTokenLen = 24; // %%GOOGLE_SCRIPT_BUTTON%%
    const size_t ftpButtonTokenLen = 14; // %%FTP_BUTTON%%
    const size_t systemTokenLen = 18;
    const size_t telnetScriptTokenLen = 17;

    const char* cursor = page;
    const char* segmentStart = page;
    char tokenBuffer[32];
    bool showWifiButton = shouldShowWifiButtonOnHome();

    while (pgm_read_byte(cursor)) {
        if (pgm_read_byte(cursor) == '%' && pgm_read_byte(cursor + 1) == '%') {
            for (int i = 0; i < 31; i++) {
                char c = pgm_read_byte(cursor + i);
                tokenBuffer[i] = c;
                if (c == '\0') {
                    break;
                }
            }
            tokenBuffer[31] = '\0';

            if (strncmp(tokenBuffer, "%%MENU_FEATURE_BUTTONS%%", menuTokenLen) == 0) {
                // Section removed from HTML — token is never emitted, but handler kept as no-op.
                streamHomeProgmemSpan(out, segmentStart, cursor);
                cursor += menuTokenLen;
                segmentStart = cursor;
                continue;
            }

            if (strncmp(tokenBuffer, "%%HERO_SUBTITLE%%", 17) == 0) {
                streamHomeProgmemSpan(out, segmentStart, cursor);
                // Pack-specific subtitle
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RAMBOX)
                out.print(F("Pigeon Race Management System"));
#elif defined(SELECTED_PACK) && (SELECTED_PACK == PACK_CAM)
                out.print(F("IP Camera &mdash; Live Stream & Record"));
#elif defined(SELECTED_PACK) && (SELECTED_PACK == PACK_CAM_P4)
                out.print(F("IP Camera &mdash; Live Stream & Record"));
#elif defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)
                out.print(F("Network Radio & MP3 Player"));
#elif defined(SELECTED_PACK) && (SELECTED_PACK == PACK_ENVIRONMENT)
                out.print(F("Environmental Sensors Dashboard"));
#elif defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
                out.print(F("TouchMeteo-7 &mdash; Meteo + Peers Viewer"));
#elif defined(SELECTED_PACK) && (SELECTED_PACK == PACK_P4_CNC)
                out.print(F("CNC Motion & G-code Control"));
#elif defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
                out.print(F("CNC Motion & G-code Control"));
#elif defined(SELECTED_PACK) && (SELECTED_PACK == PACK_P4_USB_SERVER)
                out.print(F("USB/IP Device Sharing Hub"));
#elif defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RS232)
                out.print(F("Development & Testing Platform"));
#elif defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
                out.print(F("Industrial I/O &mdash; 8DI 8DO Controller"));
#else
                out.print(F("Serial Parallel Automation Node"));
#endif
                cursor += 17;
                segmentStart = cursor;
                continue;
            }

            if (strncmp(tokenBuffer, "%%MODULES_SECTION%%", modulesTokenLen) == 0) {
                streamHomeProgmemSpan(out, segmentStart, cursor);
                // Compile-time check: only emit the section if at least one module button exists.
                constexpr bool kHasAnyModule =
                    (ENABLE_DB25==1) || (ENABLE_MODBUS_RTU==1) || (ENABLE_MODBUS_TCP==1) ||
                    (ENABLE_CAN==1) || (ENABLE_PROXIMITY==1) || (ENABLE_BUZZER==1) ||
                    (ENABLE_UNIKON==1) || (ENABLE_BRICON==1) || (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) ||
                    (ENABLE_MQ135==1) || (ENABLE_HX711==1) || (ENABLE_INA226==1) ||
                    (ENABLE_SOUND_SENSOR==1) || (ENABLE_MAX31865==1) ||
                    (ENABLE_UDA1334A==1) || (ENABLE_CAMERA==1) ||
                    (ENABLE_STRUCTURES_PAGE==1) || (ENABLE_RS232==1) || (ENABLE_SNIFFER==1) ||
                    (ENABLE_USBIP_SERVER==1) || (ENABLE_GRBL==1);
                if (kHasAnyModule) {
                out.print(F("<div class='home-section'><h2 style='margin:0 0 12px 0;font-size:18px;color:#60a5fa' data-i18n='homeModulesTitle'>&#128300; Modules</h2>"));
                }
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
                out.print(F("<a href='/ecra' class='btn' style='background:#0f5298;border-color:#ffffff' data-i18n='homeEcra'>&#128241; Screen mirror</a>"));
#endif
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)
                out.print(F("<a href='/radio-ui' target='_blank' class='btn' style='background:#0f5298;border-color:#ffffff'>&#128251; Player Harmonica Box</a>"));
#endif
#if ENABLE_GRBL==1
                out.print(F("<a href='/grbl-controller' target='_blank' class='btn' data-i18n='homeGrblController' style='background:#0f5298;border-color:#ffffff'>&#128736;&#65039; GcodeSender</a>"));
#endif
#if ENABLE_DB25==1
                out.print(F("<a href='/lpt-db25' class='btn' style='background:#0f5298;border-color:#ffffff'>&#128424; Impressora LPT DB25</a>"));
#endif
#if ENABLE_MODBUS_RTU==1
                out.print(F("<a href='/modbus-rtu' class='btn' style='background:#0f5298;border-color:#ffffff'>&#9881; Modbus RTU</a>"));
#endif
// NOTE: Modbus TCP lives on the /networks page (Outras Redes), not here.
#if ENABLE_USBIP_SERVER==1
                out.print(F("<a href='/usb-server' class='btn' data-i18n='homeUsbServer' style='background:#0f5298;border-color:#ffffff'>&#129521; USB Server (USB/IP)</a>"));
#endif
#if ENABLE_CAN==1
                out.print(F("<a href='/can' class='btn' style='background:#0f5298;border-color:#ffffff'>&#128268; Interface CAN Bus</a>"));
#endif
#if ENABLE_PROXIMITY==1
                out.print(F("<a href='/proximity' class='btn' style='background:#0f5298;border-color:#ffffff' data-i18n='homeProximity'>&#128205; Production Management and OEE</a>"));
                out.print(F("<a href='/proximity-dashboard' target='_blank' class='btn' style='background:#0f5298;border-color:#ffffff' data-i18n='homeProximityDash'>&#128202; Charts and OEE</a>"));
#endif
#if ENABLE_BUZZER==1
                out.print(F("<a href='/buzzer' class='btn' style='background:#0f5298;border-color:#ffffff'>&#128276; Buzzer</a>"));
#endif
#if ENABLE_UNIKON==1 || ENABLE_BRICON==1
                out.print(F("<a href='/rambox' class='btn' style='background:#0f5298;border-color:#ffffff'>&#128193; Rambox (Unikon / Bricon)</a>"));
#endif
#if ENABLE_IPG_LASER==1
                out.print(F("<a href='/ipg-laser' class='btn' style='background:#0f5298;border-color:#ffffff'>&#128293; IPG Laser RS232</a>"));
#endif
#if ENABLE_UDA1334A==1
                out.print(F("<a href='/uda1334a' class='btn' style='background:#0f5298;border-color:#ffffff'>&#128265; Placa de Som UDA1334A</a>"));
#endif
#if ENABLE_CAMERA==1
                out.print(F("<a href='/camera' target='_blank' class='btn' style='background:#0f5298;border-color:#ffffff' data-i18n='homeCamera'>&#128247; Camera</a>"));
#endif
#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
                out.print(F("<a href='/sensor-config' class='btn' data-i18n='navSensorAmbient' style='background:#0f5298;border-color:#ffffff'>🌡️ Sensor do Ambiente</a>"));
#endif
#if ENABLE_MQ135==1
                out.print(F("<a href='/mq135-config' class='btn' data-i18n='navMq135' style='background:#0f5298;border-color:#ffffff'>🌫️ Air Quality Sensor</a>"));
#endif
#if ENABLE_HX711==1
                out.print(F("<a href='/hx711' class='btn' style='background:#0f5298;border-color:#ffffff'>&#9878; Sensor de Peso HX711</a>"));
#endif
#if ENABLE_INA226==1
                out.print(F("<a href='/ina226' class='btn' style='background:#0f5298;border-color:#ffffff' data-i18n='homeIna226'>&#9889; INA226 Current/Power Monitor</a>"));
#endif
#if ENABLE_SOUND_SENSOR==1
                out.print(F("<a href='/sound' class='btn' style='background:#0f5298;border-color:#ffffff' data-i18n='homeSound'>&#128266; Sound Level Sensor</a>"));
#endif
#if ENABLE_MAX31865==1
                out.print(F("<a href='/max31865' class='btn' style='background:#0f5298;border-color:#ffffff'>&#127777; Sensor RTD MAX31865</a>"));
#endif
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
                out.print(F("<a href='/falas' class='btn' style='background:#0f5298;border-color:#ffffff'>&#128266; Falas locais + Peers</a>"));
#endif
#if ENABLE_STRUCTURES_PAGE==1
                // Amada Welding — automatic structure detection
                out.print(F("<a href='/structures' class='btn' style='background:#0f5298;border-color:#ffffff'>&#129691; Amada Welding</a>"));
#endif
#if ENABLE_RS232==1 || ENABLE_SNIFFER==1
                // RS232 port (was last; now charts closes the section)
                out.print(F("<a href='/rs232' class='btn' data-i18n='homeRS232' style='background:#0f5298;border-color:#ffffff'>&#128300; Porta RS232</a>"));
#endif
                // Charts LAST in the modules section (Task 2 2026-10-08)
                constexpr bool kHasChartsRoute =
                    (ENABLE_PAGE_MONITOR==1) &&
                    (
                        (ENABLE_RS232==1 && SELECTED_PACK != PACK_RAMBOX)
                        || (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1)
                        || (ENABLE_RS232_CHARTS==1)
                        || (ENABLE_MQ135==1)
                    );
                if (kHasChartsRoute && configManager.getChartsPageEnabled()) {
                    out.print(F("<a href='/sensor-charts' target='_blank' class='btn' data-i18n='navCharts' style='background:linear-gradient(135deg,#1e40af,#2563eb);'>&#128200; Data Chart Monitoring</a>"));
                }
                if (kHasAnyModule) {
                out.print(F("</div>"));
                }
                cursor += modulesTokenLen;
                segmentStart = cursor;
                continue;
            }

            if (strncmp(tokenBuffer, "%%RECIPIENTS_SECTION%%", recipientsTokenLen) == 0) {
                streamHomeProgmemSpan(out, segmentStart, cursor);
                constexpr bool kShowHomeRecipients = (ENABLE_PAGE_RECIPIENTS == 1);
                constexpr bool kShowHomeAlerts     = (ENABLE_PAGE_ALERTS == 1);
                if (kShowHomeRecipients || kShowHomeAlerts) {
                    if (kShowHomeRecipients && kShowHomeAlerts) {
                        out.print(F("<div class='home-section'><h2 style='margin:0 0 12px 0;font-size:18px;color:#60a5fa' data-i18n='homeRecipientsTitle'>&#128231; Recipients & Alerts</h2>"));
                        out.print(F("<a href='/recipients' class='btn' data-i18n='homeRecipients'>&#128231; Recipients</a>"));
                        out.print(F("<a href='/alerts-config' class='btn' data-i18n='homeAlerts'>&#128680; Configurar Alertas</a>"));
                        out.print(F("</div>"));
                    } else if (kShowHomeRecipients) {
                        out.print(F("<div class='home-section'><h2 style='margin:0 0 12px 0;font-size:18px;color:#60a5fa' data-i18n='homeRecipientsTitle'>&#128231; Recipients</h2>"));
                        out.print(F("<a href='/recipients' class='btn' data-i18n='homeRecipients'>&#128231; Recipients</a>"));
                        out.print(F("</div>"));
                    } else {
                        out.print(F("<div class='home-section'><h2 style='margin:0 0 12px 0;font-size:18px;color:#60a5fa' data-i18n='homeAlertsTitle'>&#128680; Alerts</h2>"));
                        out.print(F("<a href='/alerts-config' class='btn' data-i18n='homeAlerts'>&#128680; Configurar Alertas</a>"));
                        out.print(F("</div>"));
                    }
                }
                cursor += recipientsTokenLen;
                segmentStart = cursor;
                continue;
            }

            if (strncmp(tokenBuffer, "%%COMMANDS_SECTION%%", commandsTokenLen) == 0) {
                streamHomeProgmemSpan(out, segmentStart, cursor);
                constexpr bool kHasCommandsSection = (ENABLE_PAGE_COMMANDS==1);
                if (kHasCommandsSection) {
                    out.print(F("<div class='home-section'><h2 style='margin:0 0 12px 0;font-size:18px;color:#60a5fa' data-i18n='homeCmdsTitle'>&#9200; Automatic Commands</h2>"));
#if ENABLE_PAGE_COMMANDS==1
                    out.print(F("<a href='/commands-by-datetime' class='btn' data-i18n='homeCmdsDatetime'>&#128225; Envio comandos por Hora e Data</a>"));
#if ENABLE_PAGE_COMMANDS_INTERVAL==1
                    out.print(F("<a href='/commands' class='btn' data-i18n='homeCmdsInterval'>&#128225; Envio comandos por Intervalo de Tempo</a>"));
#endif
#endif
                    out.print(F("</div>"));
                }
                cursor += commandsTokenLen;
                segmentStart = cursor;
                continue;
            }

            if (strncmp(tokenBuffer, "%%EXPLORADOR_FICHEIROS%%", explorerTokenLen) == 0) {
                streamHomeProgmemSpan(out, segmentStart, cursor);
#if ENABLE_MODBUS==1
                {
                    JsonVariant _mbSec = configManager.getSection("modbus");
                    bool _mbActive = _mbSec["enabled"].as<bool>() &&
                                     (_mbSec["rtu_enabled"].as<bool>() || _mbSec["tcp_enabled"].as<bool>());
                    if (_mbActive) {
                    out.print(F("<a href='/modbus-live' class='btn'>&#128200; Modbus \u2014 Dados Live</a>"));
                    }
                }
#endif
                // Show file explorer only if ENABLE_PAGE_FILES==1
#if ENABLE_PAGE_FILES==1
                out.print(F("<a href='/files' class='btn' data-i18n='navFiles'>&#128193; File Explorer</a>"));
#endif
#if ENABLE_DUALFILE==1
                out.print(F("<a href='/dualfile' target='_blank' class='btn' data-i18n='homeDualFile'>&#128193; Dual File</a>"));
#endif
                // (charts button moved to end of the modules section — Task 2)
                cursor += explorerTokenLen;
                segmentStart = cursor;
                continue;
            }

            if (strncmp(tokenBuffer, "%%SERIAL_MONITOR_BTN%%", serialMonTokenLen) == 0) {
                streamHomeProgmemSpan(out, segmentStart, cursor);
                if (configManager.getSerialMonitorEnabled()) {
                    out.print(F("<a href='/serial-monitor' target='_blank' class='btn' data-i18n='homeSerialMon'>&#128223; Serial Monitor</a>"));
                }
                cursor += serialMonTokenLen;
                segmentStart = cursor;
                continue;
            }

            if (strncmp(tokenBuffer, "%%MONITOR_BUTTON%%", monitorTokenLen) == 0) {
                streamHomeProgmemSpan(out, segmentStart, cursor);
#if ENABLE_PAGE_MONITOR==1
      // Data Reception is only useful when there is data to receive.
      #if (ENABLE_SNIFFER==1 || ENABLE_RS232==1) && SELECTED_PACK != PACK_RAMBOX
        #if !defined(TARGET_BOARD_8DI8DO_POE) || TARGET_BOARD_8DI8DO_POE!=1
                out.print(F("<a href='/monitor' class='btn' data-i18n='homeDataRecv'>&#128229; Rece&ccedil;&atilde;o de Dados</a>"));
        #endif
      #endif
#endif
                cursor += monitorTokenLen;
                segmentStart = cursor;
                continue;
            }

            if (strncmp(tokenBuffer, "%%WIFI_BUTTON%%", wifiTokenLen) == 0) {
                streamHomeProgmemSpan(out, segmentStart, cursor);
#if ENABLE_WIFI==1
                if (showWifiButton) {
                    out.print(F("<a id='btn-wifi-cfg' href='/wifi' class='btn' data-i18n='homeWifi'>&#128246; Configurar Wi-Fi</a>"));
                }
#endif
                cursor += wifiTokenLen;
                segmentStart = cursor;
                continue;
            }

            if (strncmp(tokenBuffer, "%%EMAIL_BUTTON%%", emailTokenLen) == 0) {
                streamHomeProgmemSpan(out, segmentStart, cursor);
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
                out.print(F("<a href='/email' class='btn' data-i18n='homeEmailCfg'>&#9993;&#65039; Configurar E-mail</a>"));
#endif
                cursor += emailTokenLen;
                segmentStart = cursor;
                continue;
            }

            if (strncmp(tokenBuffer, "%%ETHERNET_BUTTON%%", ethernetTokenLen) == 0) {
                streamHomeProgmemSpan(out, segmentStart, cursor);
#if ENABLE_ETHERNET==1
                out.print(F("<a href='/ethernet' class='btn' data-i18n='navEthernet'>🌐 Configurar Ethernet</a>"));
#endif
                cursor += ethernetTokenLen;
                segmentStart = cursor;
                continue;
            }

            if (strncmp(tokenBuffer, "%%NETWORKS_BUTTON%%", networksButtonTokenLen) == 0) {
                streamHomeProgmemSpan(out, segmentStart, cursor);
#if ENABLE_TELNET==1
                out.print(F("<a href='/networks' class='btn'>&#127760; Outras Redes</a>"));
#endif
                cursor += networksButtonTokenLen;
                segmentStart = cursor;
                continue;
            }

            if (strncmp(tokenBuffer, "%%GOOGLE_SCRIPT_BUTTON%%", googleScriptTokenLen) == 0) {
                streamHomeProgmemSpan(out, segmentStart, cursor);
#if ENABLE_PAGE_GOOGLE_SCRIPT==1
                out.print(F("<a href='/google-script' class='btn' data-i18n='homeGoogleScript'>&#127760; Google App Script</a>"));
#endif
                cursor += googleScriptTokenLen;
                segmentStart = cursor;
                continue;
            }

            if (strncmp(tokenBuffer, "%%FTP_BUTTON%%", ftpButtonTokenLen) == 0) {
                streamHomeProgmemSpan(out, segmentStart, cursor);
#if ENABLE_PAGE_FTP==1
                out.print(F("<a href='/ftp' class='btn' data-i18n='homeFtp'>&#128229; FTP Upload</a>"));
#endif
                // Modbus TCP entry: Networks section, below FTP Upload.
                // The config page itself stays at /modbus-tcp (Modbus pages).
#if ENABLE_MODBUS_TCP==1
                out.print(F("<a href='/modbus-tcp' class='btn' style='background:#0f5298;border-color:#ffffff'>&#127760; Modbus TCP</a>"));
#endif
                cursor += ftpButtonTokenLen;
                segmentStart = cursor;
                continue;
            }

            if (strncmp(tokenBuffer, "%%SYSTEM_SECTION%%", systemTokenLen) == 0) {
                streamHomeProgmemSpan(out, segmentStart, cursor);
                out.print(FPSTR(systemSection));
#if ENABLE_PAGE_SDCARD==1
                // Show SD/PSRAM config only if ENABLE_PAGE_SDCARD==1
                out.print(F("<a href='/sdcard' class='btn' data-i18n='navSdcard'>&#128190; Configurar Armazenamento</a>"));
#endif
                out.print(FPSTR(systemSectionSettingsButton));
#if ENABLE_API_REFERENCE_PAGE==1
                out.print(FPSTR(SYSTEM_SECTION_API_REF_BUTTON));
#endif
                out.print(FPSTR(SYSTEM_SECTION_ACCESS_BUTTON));
#if ENABLE_OTA==1
                out.print(FPSTR(systemSectionOtaButton));
#endif
                out.print(FPSTR(systemSectionClose));
                cursor += systemTokenLen;
                segmentStart = cursor;
                continue;
            }

            if (strncmp(tokenBuffer, "%%TELNET_SCRIPT%%", telnetScriptTokenLen) == 0) {
                streamHomeProgmemSpan(out, segmentStart, cursor);
#if ENABLE_TELNET==1
                out.print(F("<script>/* Telnet script */</script>"));
#endif
                cursor += telnetScriptTokenLen;
                segmentStart = cursor;
                continue;
            }

            if (strncmp(tokenBuffer, "%%VERSION%%", versionTokenLen) == 0) {
                streamHomeProgmemSpan(out, segmentStart, cursor);
                out.print(firmwareVersion);
                cursor += versionTokenLen;
                segmentStart = cursor;
                continue;
            }

            Serial.print("DEBUG: Token %% NAO RECONHECIDO: [");
            Serial.print(tokenBuffer);
            Serial.println("]");
        }
        cursor++;
    }

    streamHomeProgmemSpan(out, segmentStart, cursor);
}