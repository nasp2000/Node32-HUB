// =============================================================
// ethernet.cpp — minimal W5500 bring-up for ESP32
// =============================================================
#include <Arduino.h>
#include <esp_task_wdt.h>     // WDT reset inside long synchronous W5500 writes
#include "eth_w5500.h"
#include "ConfigManager.h"
#include "email_transport.h"
#include "network_hybrid_manager.h"
#include "system_status_payload.h"
#include "network_status_payload.h"
#include "network_access_point.h"
#include "network_runtime_state.h"
#include "web_shared_logic.h"
#include "web_shared_pages.h"
#include "web_monitor_pages.h"
#include "psram_helpers.h"
#include "psram_vfs.h"
#include "runtime_environment.h"
#include "littlefs_manager.h"
#include "network_manual_switch.h"
#include "monitor_session_state.h"
#include "network_saved_profiles.h"
#include "sd_diagnostics.h"
#include "sd_security.h"
#include "debug_writer.h"
#include "log.h"
#include "commands.h"   // LogLevel enum (INFO, WARN, etc.) + sendVirtualMessage
#include "direct_log.h" // directLogPrintln
#include "observability.h"
#include "runtime_scheduler.h"
#include "languages/lang_active.h"
#include "webpages/auto_js.h"
#include "webpages/assets/config_manager_js_asset.h"
#include "webpages/assets/google_apps_script_asset.h"
#include "webpages/assets/page_mq135_config_asset.h"
#include "webpages/assets/page_sensor_charts_asset.h"
#include "webpages/assets/page_sensor_config_asset.h"
#include "webpages/core_css.h"
#include "webpages/renderers/home_page_renderer.h"
#include "sensors/sensor_config.h"
#include "sensors/sensor_mq135_config.h"
#include "sensors/sensor_mq135.h"
#include "feature_flags.h"
#if ENABLE_HX711==1
#include "sensors/sensor_hx711_config.h"
#endif
#if ENABLE_INA226==1
#include "sensors/sensor_ina226_config.h"
#endif
#if ENABLE_SOUND_SENSOR==1
#include "sensors/sensor_sound_config.h"
#endif
#if ENABLE_MAX31865==1
#include "sensors/sensor_max31865_config.h"
#endif
#if ENABLE_UDA1334A==1
#include "audio_dac_uda1334a.h"
#include "audio_dac_uda1334a_config.h"
#endif
#include "sensors/proximity_manager.h"
#include "chart_history.h"
#if ENABLE_CAMERA==1
#  include "camera_manager.h"
#  include "camera_config.h"
#  include "camera_motion.h"
#  if ENABLE_SDCARD==1
#    include "camera_record.h"
#    include "sdcard.h"
#  endif
#  include "psram_helpers.h"
#endif
#include "modbus_scan_api.h"
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO==1
#  include "tca9554_do.h"
#endif
#if ENABLE_MODBUS==1
#  include "modbus_runtime.h"
#  include "modbus_poll.h"
#  include "modbus_core.h"
#  include "modbus_tcp.h"
#endif
#if ENABLE_CAN==1
#  include "can_core.h"
#  include "can_runtime.h"
#endif
#include "webpages/handlers/email_handlers.h"
#include <Preferences.h>
#include <SD.h>
#include <Update.h>
#include <base64.h>
#include <ctype.h>

#if ENABLE_ETHERNET==1

#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1
// ============================================================
// RMII Ethernet path — ESP32-P4 + IP101GRI PHY
// ============================================================
// ETH.h (arduino-esp32 >= 3.2.0) drives the internal RMII MAC and
// manages DHCP via lwIP. No W5500-specific SPI or socket handling needed.
// Bootstrap HTTP server uses WiFiServer/WiFiClient (same lwIP stack as WiFi).
//
// Macro aliases: EthernetClient/Server/ChunkedPrint are redefined to their
// WiFi equivalents so ethernet_bootstrap_*.h compiles unchanged.
// Safe because <Ethernet.h> is NOT included in this path.
#include "eth_rmii.h"
#include <ETH.h>
#include <WiFi.h>
#include <esp_wifi.h>

#define EthernetClient      WiFiClient
#define EthernetServer      WiFiServer
// EthernetChunkedPrint (defined in ethernet_bootstrap_response.h) wraps
// EthernetClient& (-> WiFiClient&) for 512-byte buffered writes.
// On P4 native TCP the chunking is unnecessary but harmless.

// TLS SMTP over the RMII Ethernet netif (lwIP is shared with WiFi).
#include <ESP_SSLClient.h>

// TLS SMTP over the RMII Ethernet netif. ESP_SSLClient handles TLS on secure
// ports (465 is in the library's secure-port list). Gmail requires port 465.
static WiFiClient _ethEmailTlsRaw;   // reused; SMTP sends are serialized

Client* emailTransportCreateEthernetClient(const EmailTlsClientConfig& config) {
    _ethEmailTlsRaw.stop();
    ESP_SSLClient* ssl = new ESP_SSLClient();
    if (!ssl) return nullptr;
    ssl->setClient(&_ethEmailTlsRaw, true);
    if (config.insecure) ssl->setInsecure();
    ssl->setBufferSizes(2048, 1024);
    ssl->setTimeout(config.timeoutMs / 1000);           // ESP_SSLClient timeout is in seconds
    ssl->setHandshakeTimeout(config.handshakeTimeoutSeconds);
    return ssl;
}

bool emailTransportConfigureEthernetClient(Client& client, const EmailTlsClientConfig& config) {
    ESP_SSLClient& ssl = static_cast<ESP_SSLClient&>(client);
    if (config.insecure) ssl.setInsecure();
    ssl.setTimeout(config.timeoutMs / 1000);
    ssl.setHandshakeTimeout(config.handshakeTimeoutSeconds);
    return true;
}

static volatile int  _ethStatus            = -1;
static WiFiServer    _ethHttpServer(80);
static volatile bool _ethHttpServerStarted = false;
static unsigned long _ethLastStatusMs      = 0;
static SystemStatusPayloadState _ethStatusPayloadState;
static unsigned long _ethNoLinkLogMs       = 0;
static uint32_t      _ethNoLinkLogSuppressed = 0;
static unsigned long _ethInitLogMs         = 0;
static uint32_t      _ethInitLogSuppressed = 0;
static bool          _ethBootLoggedBegin   = false;
static bool          _ethBootLoggedNoLink  = false;

static const int _ETH_BLOG_MAX_LINES = 20;
static const int _ETH_BLOG_LINE_LEN  = 120;
static char      _ethBootLogBuf[_ETH_BLOG_MAX_LINES][_ETH_BLOG_LINE_LEN];
static int       _ethBootLogCount    = 0;

static void ethBootLogPush(const char* msg) {
    if (_ethBootLogCount >= _ETH_BLOG_MAX_LINES) return;
    strncpy(_ethBootLogBuf[_ethBootLogCount], msg, _ETH_BLOG_LINE_LEN - 1);
    _ethBootLogBuf[_ethBootLogCount][_ETH_BLOG_LINE_LEN - 1] = '\0';
    _ethBootLogCount++;
}

static void ethernetDirectBootLog(const __FlashStringHelper *msg) {
    String line(F("[ETH_BOOT] "));
    line += msg;
    directLogPrintln(line);
    ethBootLogPush(line.c_str());
}
static void ethernetDirectBootLog(const String &msg) {
    String line(F("[ETH_BOOT] "));
    line += msg;
    directLogPrintln(line);
    ethBootLogPush(line.c_str());
}

static void ethernetLogNoLinkBackgroundThrottled() {
    const unsigned long kLogIntervalMs = 300000UL; // 5 min
    unsigned long now = millis();
    _ethNoLinkLogSuppressed++;
    if (_ethNoLinkLogMs == 0 || (now - _ethNoLinkLogMs) >= kLogIntervalMs) {
        _ethNoLinkLogMs = now;
        char buf[96];
        snprintf(buf,
                 sizeof(buf),
                 "ETH (RMII): no link/IP, retrying in background (x%lu)",
                 (unsigned long)_ethNoLinkLogSuppressed);
        sendVirtualMessage(String(buf), INFO);
        _ethNoLinkLogSuppressed = 0;
    }
}

static void ethernetLogInitThrottled() {
    const unsigned long kLogIntervalMs = 300000UL; // 5 min
    const unsigned long now = millis();
    _ethInitLogSuppressed++;
    if (_ethInitLogMs == 0 || (now - _ethInitLogMs) >= kLogIntervalMs) {
        _ethInitLogMs = now;
        if (_ethInitLogSuppressed <= 1) {
            sendVirtualMessage(F("ETH (RMII): initializing IP101GRI..."), INFO);
        } else {
            char buf[96];
            snprintf(buf,
                     sizeof(buf),
                     "ETH (RMII): initializing IP101GRI... (x%lu)",
                     (unsigned long)_ethInitLogSuppressed);
            sendVirtualMessage(String(buf), INFO);
        }
        _ethInitLogSuppressed = 0;
    }
}

// MAC helper: kept for API parity; on P4 the ETH stack manages its own MAC internally.
static void ethernetBuildUniqueLocalMac(byte mac[6]) {
    uint64_t chipMac = ESP.getEfuseMac();
    mac[0] = (byte)((((chipMac >> 40) & 0xFF) | 0x02) & 0xFE);
    mac[1] = (byte)((chipMac >> 32) & 0xFF);
    mac[2] = (byte)((chipMac >> 24) & 0xFF);
    mac[3] = (byte)((chipMac >> 16) & 0xFF);
    mac[4] = (byte)((chipMac >>  8) & 0xFF);
    mac[5] = (byte)( chipMac        & 0xFF);
}
static void ethernetFormatMac(const byte mac[6], char* out, size_t outSize) {
    snprintf(out, outSize, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

IPAddress ethernetLocalIP() { return ETH.localIP(); }

void ethernetFlushBootLogToSD() {
    if (_ethBootLogCount == 0) return;
    const unsigned long kTimeoutMs = 15000UL;
    unsigned long start = millis();
    while (!sdcardIsMounted() && (millis() - start) < kTimeoutMs)
        vTaskDelay(pdMS_TO_TICKS(250));
    String details;
    details.reserve((size_t)_ethBootLogCount * 90);
    for (int i = 0; i < _ethBootLogCount; i++) {
        if (i > 0) details += '\n';
        details += _ethBootLogBuf[i];
    }
    logSystemEvent(String(F("ETH_BOOT")), details);
    _ethBootLogCount = 0;
}

void ethernetBootLogLine(const char* msg) { ethBootLogPush(msg); }

#if ENABLE_RS232==1
extern int rs232_baudrate_index;
extern int rs232_data_bits;
extern String rs232_parity;
extern int rs232_stop_bits;
extern bool rs232_invert_pins;
extern int rs232_read_timeout_ms;
extern bool rs232_template_use_xon_xoff;
extern const int RS232_BAUDRATES[];
extern const int RS232_BAUDRATES_COUNT;
extern PsramCircularBuffer<Rs232LivePoint> rs232LiveBuffer;
extern SemaphoreHandle_t rs232LiveMutex;
#endif

static String ethernetHtmlEscape(const String& input);
static String ethernetFormatBytes(uint64_t value);
static String ethernetUrlDecode(const String& value);
static String ethernetGetFormValue(const String& body, const char* key);
static bool ethernetFormHasTruthyValue(const String& body, const char* key);
static String ethernetGetRequestValue(const String& body, const char* key);
static String ethernetGetQueryParam(const String& query, const char* key);
static String ethernetUrlEncode(const String& value);
static void ethernetBootstrapSendResponse(EthernetClient& client, const char* status, const char* contentType, const String& body);
static void ethernetBootstrapBeginStreamResponse(EthernetClient& client, const char* status, const char* contentType, const char* cacheControl = nullptr, const char* etag = nullptr, const char* contentLanguage = nullptr);
static void ethernetSendSensorFlags(EthernetClient& client);

extern volatile bool isReceivingData;
extern unsigned long lastReceptionTime;

extern SpiRamJsonDocument get_all_configs();
extern void syncGlobalVariables();
extern void syncGlobalVariables(bool includeRS232);
#if ENABLE_RS232==1
extern void applyRS232Config();
#else
static inline void applyRS232Config() {}
#endif
extern unsigned long otaEnabledUntil;
extern void safeReboot(const char* reason);
extern bool flushSdcardNowBlocking(const char *reason, unsigned long waitMs);
extern bool acquireSDAccess(const char* owner, uint32_t timeoutMs);
extern void releaseSDAccess();
extern volatile StorageMode currentStorageMode;
extern bool retrySDStorage();
extern const char* getStorageModeString();
extern const char* FIRMWARE_BUILD_DATETIME;
extern const char* FIRMWARE_BUILD;
extern bool sdcardIsMounted();
extern bool sdMutexLock(TickType_t timeoutTicks);
extern void sdMutexUnlock();

#include "ethernet_bootstrap_response.h"
#include "buzzer.h"
#include "ethernet_bootstrap_handlers.h"
#include "ethernet_bootstrap_routes.h"

static void ethernetStartBootstrapServerIfNeeded() {
    if (_ethStatus == 0 && !_ethHttpServerStarted) {
        // On P4, AsyncWebServer handles all HTTP on all interfaces via the shared lwIP stack.
        // Starting a separate WiFiServer on port 80 would conflict with AsyncWebServer:
        // both would compete for the same lwIP port, and repeated begin() calls in the
        // loop corrupt AsyncTCP socket state, making the web server unreachable over time.
        _ethHttpServerStarted = true;
        sendVirtualMessage(F("ETH (RMII): active — AsyncWebServer handles HTTP on port 80"), INFO);
        ethernetDirectBootLog(F("ETH ready; AsyncWebServer handles HTTP"));
    }
}

void ethernetBegin() {
    ethernetLogInitThrottled();
    if (!_ethBootLoggedBegin) {
        ethernetDirectBootLog(F("ethernetBegin RMII"));
        _ethBootLoggedBegin = true;
    }
    extern ConfigManager configManager;
    String mode   = configManager.getEthernetMode();
    String ipCfg  = configManager.getEthernetIP();
    String gwCfg  = configManager.getEthernetGW();
    String dnsCfg = configManager.getEthernetDNS();

    // Static IP: configure before ETH.begin() so lwIP picks it up at init time.
    if (mode == "static" && ipCfg.length()>0 && gwCfg.length()>0 && dnsCfg.length()>0) {
        auto parseIp=[](const String &s, IPAddress &out) -> bool {
            int parts[4]={0,0,0,0}; int idx=0; int start=0;
            while (start < (int)s.length() && idx < 4) {
                int dot = s.indexOf('.', start);
                String seg = (dot==-1) ? s.substring(start) : s.substring(start, dot);
                seg.trim(); parts[idx++] = seg.toInt();
                if (dot==-1) break; start = dot+1;
            }
            if (idx==4) { out = IPAddress(parts[0],parts[1],parts[2],parts[3]); return true; }
            return false;
        };
        IPAddress ip, gw, dns;
        if (parseIp(ipCfg, ip) && parseIp(gwCfg, gw) && parseIp(dnsCfg, dns)) {
            ETH.config(ip, gw, IPAddress(255,255,255,0), dns);
            sendVirtualMessage(F("ETH (RMII): static IP configured"), INFO);
            ethernetDirectBootLog(String(F("Static IP ")) + ipCfg);
        } else {
            sendVirtualMessage(F("ETH (RMII): invalid static config, using DHCP"), WARN);
        }
    }

    // ETH.begin() starts the RMII MAC + PHY and kicks off DHCP (or uses the static config above).
    // EMAC_CLK_EXT_IN: external RMII ref clock input (IP101GRI generates 50 MHz from its 25 MHz crystal).
    // On P4: eth_clock_mode_t is typedef'd as emac_rmii_clock_mode_t; EMAC_CLK_EXT_IN is the correct constant.
    bool started = ETH.begin(
        ETH_PHY_IP101,           // eth_phy_type_t: IP101GRI PHY type
        ETH_RMII_PHY_ADDR,       // int32_t: PHY address (0x01)
        ETH_RMII_MDC_PIN,        // int: MDC GPIO
        ETH_RMII_MDIO_PIN,       // int: MDIO GPIO
        ETH_RMII_PHY_RESET_PIN,  // int: PHY reset GPIO (-1 if unused)
        EMAC_CLK_EXT_IN          // eth_clock_mode_t: external ref clock
    );
    if (!started) {
        sendVirtualMessage(F("ETH (RMII): ETH.begin() failed! Check RMII pins."), WARN);
        ethernetDirectBootLog(F("ETH.begin() failed"));
        _ethStatus = -1;
        return;
    }

    // Wait up to 15 s for link-up and a valid DHCP/static IP.
    unsigned long deadline = millis() + 15000UL;
    while (millis() < deadline) {
        if (ETH.linkUp() && ethernetHasUsableIP(ETH.localIP())) {
            _ethStatus = 0;
            { char _buf[64]; snprintf(_buf, sizeof(_buf), "ETH (RMII): IP %s", ETH.localIP().toString().c_str()); sendVirtualMessage(String(_buf), INFO); }
            ethernetDirectBootLog(String(F("IP ")) + ETH.localIP().toString());
#if ENABLE_NTP==1
            {
                String _tz  = configManager.getNtpTimezone();
                String _ntp = configManager.getNtpServer();
                configTzTime(_tz.c_str(), _ntp.c_str());
                sendVirtualMessage(F("ETH (RMII): NTP sync started"), INFO);
            }
#endif
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    ethernetLogNoLinkBackgroundThrottled();
    if (!_ethBootLoggedNoLink) {
        ethernetDirectBootLog(F("Timeout waiting for link+IP"));
        _ethBootLoggedNoLink = true;
    }
    _ethStatus = -1;
}

int ethernetStatus() { return _ethStatus; }

volatile bool _ethReinitTaskRunning = false;

static void taskReinitEthernet(void* param) {
    ethernetBegin();
    _ethReinitTaskRunning = false;
    vTaskDelete(nullptr);
}

void ethernetLoop() {
    static unsigned long _lastLinkCheckMs    = 0;
    static unsigned long _lastRetryMs        = 0;
    const unsigned long  LINK_CHECK_INTERVAL_MS    = 5000UL;
    const unsigned long  RETRY_INTERVAL_MS         = 30000UL;
    unsigned long now = millis();

    if (_ethStatus != 0) {
        if (!_ethReinitTaskRunning && (now - _lastRetryMs >= RETRY_INTERVAL_MS)) {
            _lastRetryMs = now;
            _ethReinitTaskRunning = true;
            ethernetLogNoLinkBackgroundThrottled();
            BaseType_t r = xTaskCreatePinnedToCore(
                taskReinitEthernet, "eth_ri", 8192, nullptr, 1, nullptr, 0);
            if (r != pdPASS) {
                _ethReinitTaskRunning = false;
                sendVirtualMessage(F("ETH (RMII): failed to create reinit task (heap?)"), ERROR);
            }
        }
        return;
    }

    ethernetStartBootstrapServerIfNeeded();
    if (!_ethHttpServerStarted) return;

    if (now - _lastLinkCheckMs >= LINK_CHECK_INTERVAL_MS) {
        _lastLinkCheckMs = now;
        if (_ethReinitTaskRunning) return;

        static uint8_t _linkOffCount = 0;
        if (!ETH.linkUp()) {
            _linkOffCount++;
            if (_linkOffCount < 2) return;
            sendVirtualMessage(F("ETH (RMII): cable disconnected, waiting for reconnection..."), WARN);
            _linkOffCount = 0;
            _ethStatus = -1;
            _ethHttpServerStarted = false;
            return;
        }
        _linkOffCount = 0;

        if (!ethernetHasUsableIP(ETH.localIP())) {
            sendVirtualMessage(F("ETH (RMII): no valid IP, forcing reinit..."), WARN);
            _ethStatus = -1;
            _ethHttpServerStarted = false;
            return;
        }
        // No Ethernet.maintain() needed: lwIP handles DHCP renewal internally.
    }

    extern void manageSerialOutput();
    manageSerialOutput();
    // On P4, HTTP is served by AsyncWebServer (all interfaces via shared lwIP).
    // No bootstrap HTTP client handling needed here.
}

#else
// ============================================================
// W5500 SPI path — ESP32-S3 N16R8 devkit / Waveshare 8DI8DO POE
// ============================================================

// Increase W5500 SPI clock from 14 MHz (library default) to 25 MHz.
// W5500 supports up to 80 MHz; 25 MHz is safe for the Waveshare board PCB traces.
#define SPI_ETHERNET_SETTINGS SPISettings(25000000, MSBFIRST, SPI_MODE0)
// Use 8 W5500 sockets (library default). Each gets 2 KB TX + 2 KB RX buffer.
// 4 sockets (previous setting) caused CLOSE_WAIT exhaustion: the browser makes
// 4 concurrent requests (HTML + /core.css + /auto.js + /config-manager.js) and
// after they finish those 4 sockets sit in CLOSE_WAIT until the browser sends FIN.
// During that window all sockets are gone → new requests get TCP-rejected.
// With 8 sockets there is always room for new connections while old ones drain.
// 2 KB TX buffer is enough because our chunk size (512 B) fits comfortably.
#define MAX_SOCK_NUM 8
#include <SPI.h>
#include <Ethernet.h>
#include <utility/w5100.h>    // SnSR::CLOSE_WAIT constant + W5100.readSnSR() — must follow Ethernet.h
#include <WiFi.h>
#include <esp_wifi.h>

// w5100.h unconditionally redefines SPI_ETHERNET_SETTINGS to 14 MHz.
// Reassert our 25 MHz setting after all includes.
#undef SPI_ETHERNET_SETTINGS
#define SPI_ETHERNET_SETTINGS SPISettings(25000000, MSBFIRST, SPI_MODE0)

// TLS SMTP over the W5500 SPI stack (mbedTLS via ESP_SSLClient).
#include <ESP_SSLClient.h>

// ESP_SSLClient handles TLS on secure ports (465 is in the secure-port list).
// Gmail requires port 465 (implicit TLS).
static EthernetClient _ethEmailTlsRaw;   // reused; SMTP sends are serialized

Client* emailTransportCreateEthernetClient(const EmailTlsClientConfig& config) {
    _ethEmailTlsRaw.stop();
    ESP_SSLClient* ssl = new ESP_SSLClient();
    if (!ssl) return nullptr;
    ssl->setClient(&_ethEmailTlsRaw, true);
    if (config.insecure) ssl->setInsecure();
    ssl->setBufferSizes(2048, 1024);
    ssl->setTimeout(config.timeoutMs / 1000);           // ESP_SSLClient timeout is in seconds
    ssl->setHandshakeTimeout(config.handshakeTimeoutSeconds);
    return ssl;
}

bool emailTransportConfigureEthernetClient(Client& client, const EmailTlsClientConfig& config) {
    ESP_SSLClient& ssl = static_cast<ESP_SSLClient&>(client);
    if (config.insecure) ssl.setInsecure();
    ssl.setTimeout(config.timeoutMs / 1000);
    ssl.setHandshakeTimeout(config.handshakeTimeoutSeconds);
    return true;
}

IPAddress ethernetLocalIP(){
    return ::Ethernet.localIP();
}

// Written from Core 0 (taskInitEthernet/taskReinitEthernet), read from Core 1 (loop).
// 'volatile' prevents the compiler from register-caching these across task boundaries.
static volatile int  _ethStatus = -1;
static bool _ethUsedDhcp = false;   // true only when a live DHCP lease is active; controls maintain()
static EthernetServer _ethHttpServer(80);
static volatile bool _ethHttpServerStarted = false;
static unsigned long _ethLastStatusMs = 0;
static SystemStatusPayloadState _ethStatusPayloadState;

static byte _defaultMac[6] = { 0xDE,0xAD,0xBE,0xEF,0xFE,0xED };

// Boot log ring buffer: captures all [ETH_BOOT] messages during init.
// Flushed to the SD daily log file once SD is mounted (ethernetFlushBootLogToSD).
static const int _ETH_BLOG_MAX_LINES = 20;
static const int _ETH_BLOG_LINE_LEN  = 120;
static char      _ethBootLogBuf[_ETH_BLOG_MAX_LINES][_ETH_BLOG_LINE_LEN];
static int       _ethBootLogCount = 0;

static void ethBootLogPush(const char* msg) {
    if (_ethBootLogCount >= _ETH_BLOG_MAX_LINES) return;
    strncpy(_ethBootLogBuf[_ethBootLogCount], msg, _ETH_BLOG_LINE_LEN - 1);
    _ethBootLogBuf[_ethBootLogCount][_ETH_BLOG_LINE_LEN - 1] = '\0';
    _ethBootLogCount++;
}

static void ethernetBuildUniqueLocalMac(byte mac[6]) {
    uint64_t chipMac = ESP.getEfuseMac();
    if (chipMac == 0ULL) {
        memcpy(mac, _defaultMac, sizeof(_defaultMac));
        return;
    }

    mac[0] = (byte)((((chipMac >> 40) & 0xFF) | 0x02) & 0xFE);
    mac[1] = (byte)((chipMac >> 32) & 0xFF);
    mac[2] = (byte)((chipMac >> 24) & 0xFF);
    mac[3] = (byte)((chipMac >> 16) & 0xFF);
    mac[4] = (byte)((chipMac >> 8) & 0xFF);
    mac[5] = (byte)(chipMac & 0xFF);
}

static void ethernetFormatMac(const byte mac[6], char* out, size_t outSize) {
    snprintf(out,
             outSize,
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void ethernetDirectBootLog(const __FlashStringHelper *msg) {
    String line(F("[ETH_BOOT] "));
    line += msg;
    directLogPrintln(line);
    ethBootLogPush(line.c_str());
}

static void ethernetDirectBootLog(const String &msg) {
    String line(F("[ETH_BOOT] "));
    line += msg;
    directLogPrintln(line);
    ethBootLogPush(line.c_str());
}

extern SpiRamJsonDocument get_all_configs();
extern void syncGlobalVariables();
extern void syncGlobalVariables(bool includeRS232);
#if ENABLE_RS232==1
extern void applyRS232Config();
#else
static inline void applyRS232Config() {}
#endif
extern unsigned long otaEnabledUntil;
extern void safeReboot(const char* reason);
extern bool flushSdcardNowBlocking(const char *reason, unsigned long waitMs);
extern bool acquireSDAccess(const char* owner, uint32_t timeoutMs);
extern void releaseSDAccess();
extern volatile StorageMode currentStorageMode;
extern bool retrySDStorage();
extern const char* getStorageModeString();
extern const char* FIRMWARE_BUILD_DATETIME;
extern bool sdcardIsMounted();

// Wait for SD to be mounted (up to kTimeoutMs), then write all buffered [ETH_BOOT]
// messages to the daily SD log file as a single logSystemEvent entry.
// Called at the end of taskInitEthernet after ethernetBegin() completes.
void ethernetFlushBootLogToSD() {
    if (_ethBootLogCount == 0) return;
    const unsigned long kTimeoutMs = 15000UL;
    unsigned long start = millis();
    while (!sdcardIsMounted() && (millis() - start) < kTimeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    String details;
    details.reserve((size_t)_ethBootLogCount * 90);
    for (int i = 0; i < _ethBootLogCount; i++) {
        if (i > 0) details += '\n';
        details += _ethBootLogBuf[i];
    }
    logSystemEvent(String(F("ETH_BOOT")), details);
    _ethBootLogCount = 0;
}

// Push a raw line to the boot log buffer (for callers outside ethernet.cpp, e.g. taskInitEthernet).
void ethernetBootLogLine(const char* msg) {
    ethBootLogPush(msg);
}
#if ENABLE_RS232==1
extern int rs232_baudrate_index;
extern int rs232_data_bits;
extern String rs232_parity;
extern int rs232_stop_bits;
extern bool rs232_invert_pins;
extern int rs232_read_timeout_ms;
extern bool rs232_template_use_xon_xoff;
extern const int RS232_BAUDRATES[];
extern const int RS232_BAUDRATES_COUNT;
extern PsramCircularBuffer<Rs232LivePoint> rs232LiveBuffer;
extern SemaphoreHandle_t rs232LiveMutex;
#endif // ENABLE_RS232

static String ethernetHtmlEscape(const String& input);
static String ethernetFormatBytes(uint64_t value);
static String ethernetUrlDecode(const String& value);
static String ethernetGetFormValue(const String& body, const char* key);
static bool ethernetFormHasTruthyValue(const String& body, const char* key);
static String ethernetGetRequestValue(const String& body, const char* key);
static String ethernetGetQueryParam(const String& query, const char* key);
static String ethernetUrlEncode(const String& value);
static void ethernetBootstrapSendResponse(EthernetClient& client, const char* status, const char* contentType, const String& body);
static void ethernetBootstrapBeginStreamResponse(EthernetClient& client, const char* status, const char* contentType, const char* cacheControl = nullptr, const char* etag = nullptr, const char* contentLanguage = nullptr);
static void ethernetSendSensorFlags(EthernetClient& client);

extern volatile bool isReceivingData;
extern unsigned long lastReceptionTime;


// Utility/response helpers (ethernetHtmlEscape, ethernetUrlDecode, ethernetBootstrapSendResponse, etc.)
#include "ethernet_bootstrap_response.h"

// Buzzer driver — needed by ethernetHandleBuzzer* inside ethernet_bootstrap_handlers.h
#include "buzzer.h"

// HTTP handler implementations (ethernetHandleUnifiedConfigPost, ethernetSendSavedConfigs, etc.)
// NOTE: this translation unit is intentionally large (includes implementation headers).
// Requires -mtext-section-literals (set in boards.local.txt) to avoid l32r out-of-range
// linker errors on Xtensa ESP32-S3.
#include "ethernet_bootstrap_handlers.h"

// Request dispatch (ethernetBootstrapHandleClient)
#include "ethernet_bootstrap_routes.h"


static void ethernetStartBootstrapServerIfNeeded() {
    // Called from ethernetLoop() (main loop) only — not from FreeRTOS tasks.
    // EthernetServer::begin() configures a W5500 socket via SPI; calling it from a
    // secondary task is unreliable and may leave the socket in a non-LISTEN state.
    if (_ethStatus == 0 && !_ethHttpServerStarted) {
        _ethHttpServer.begin();
        _ethHttpServerStarted = true;
        sendVirtualMessage(F("ETH: HTTP server on port 80 started"), INFO);
        ethernetDirectBootLog(F("HTTP server started on port 80"));
    }
}

// W5500: rate-limit the no-link reconnect log burst to once per 10 min.
// The retry itself keeps running every ~30 s; only the logging is throttled.
static bool _ethW5500LogBurstPending = false;
static bool _ethW5500NoLinkLogDue() {
    static unsigned long _last = 0;
    const unsigned long kIntervalMs = 600000UL; // 10 min
    unsigned long now = millis();
    if (_last == 0 || (now - _last) >= kIntervalMs) {
        _last = now;
        return true;
    }
    return false;
}

void ethernetBegin() {
    // CS pin must be OUTPUT before any SPI transaction; the Ethernet library
    // may not call pinMode if W5100.init() fails, causing HAL GPIO errors.
    pinMode(ETH_CS_PIN, OUTPUT);
    digitalWrite(ETH_CS_PIN, HIGH);

    // Only call SPI.begin() ONCE for the lifetime of the firmware.
    // On ESP32-S3, a second call to SPI.begin() while the peripheral is already
    // active (e.g. triggered by taskReinitEthernet after a link-loss) corrupts
    // the SPI peripheral state: the next transaction sets cmd.usr=1 and never
    // clears it, causing an infinite busy-loop in spiTransferBytesNL and a
    // subsequent TWDT crash (rst:0xc, Saved PC in spiTransferBytesNL:1535).
    static bool _ethSpiInitDone = false;
    if (!_ethSpiInitDone) {
        SPI.begin(ETH_SPI_SCLK_PIN, ETH_SPI_MISO_PIN, ETH_SPI_MOSI_PIN);
        _ethSpiInitDone = true;
    }
    ::Ethernet.init(ETH_CS_PIN);

    pinMode(ETH_RST_PIN, OUTPUT);
    digitalWrite(ETH_RST_PIN, HIGH);
    delay(5);
    digitalWrite(ETH_RST_PIN, LOW);
    delay(20);
    digitalWrite(ETH_RST_PIN, HIGH);
    delay(200);

    // Only print the full no-link cycle once per 10 min (the boot always prints).
    bool _ethW5500CycleLog = _ethW5500NoLinkLogDue() || _ethW5500LogBurstPending;
    _ethW5500LogBurstPending = false;

    if (_ethW5500CycleLog) sendVirtualMessage(F("ETH: initializing..."), INFO);
    ethernetDirectBootLog(F("ethernetBegin"));

    extern ConfigManager configManager;
    String mode = "dhcp"; String ipCfg, gwCfg, dnsCfg, macOverride;
    mode = configManager.getEthernetMode();
    ipCfg = configManager.getEthernetIP();
    gwCfg = configManager.getEthernetGW();
    dnsCfg = configManager.getEthernetDNS();
    macOverride = configManager.getEthernetMacOverride();
    byte macToUse[6];
    ethernetBuildUniqueLocalMac(macToUse);
    if (macOverride.length() == 17) {
        int ok = 0;
        for (int i = 0; i < 6; i++) {
            char hexByte[3];
            hexByte[0] = macOverride[i * 3];
            hexByte[1] = macOverride[i * 3 + 1];
            hexByte[2] = '\0';
            if (isxdigit(hexByte[0]) && isxdigit(hexByte[1])) {
                macToUse[i] = (byte)strtol(hexByte, nullptr, 16);
                ok++;
            }
        }
        if (ok == 6) {
            if (_ethW5500CycleLog) sendVirtualMessage(F("ETH: usando MAC override"), INFO);
        } else {
            if (_ethW5500CycleLog) sendVirtualMessage(F("ETH: invalid MAC override; using unique chip MAC"), WARN);
            ethernetBuildUniqueLocalMac(macToUse);
        }
    }
    {
        char macBuf[32];
        ethernetFormatMac(macToUse, macBuf, sizeof(macBuf));
        if (_ethW5500CycleLog) sendVirtualMessage(String(F("ETH: MAC ")) + macBuf, INFO);
        ethernetDirectBootLog(String(F("MAC ")) + macBuf);
    }
    if(mode == "static" && ipCfg.length()>0 && gwCfg.length()>0 && dnsCfg.length()>0){
        IPAddress ip, gw, dns;
        auto parseIp=[&](const String &s, IPAddress &out){ int parts[4]={0,0,0,0}; int idx=0; int start=0; while(start < (int)s.length() && idx<4){ int dot = s.indexOf('.', start); String seg = (dot==-1)? s.substring(start): s.substring(start,dot); seg.trim(); parts[idx++] = seg.toInt(); if(dot==-1) break; start = dot+1; } if(idx==4){ out = IPAddress(parts[0],parts[1],parts[2],parts[3]); return true;} return false; };
        bool okIp=parseIp(ipCfg, ip); bool okGw=parseIp(gwCfg, gw); bool okDns=parseIp(dnsCfg, dns);
        if(okIp && okGw && okDns){
            sendVirtualMessage(F("ETH: static mode (config)"), INFO);
            ::Ethernet.begin(macToUse, ip, dns, gw);
            IPAddress localIp = ::Ethernet.localIP();
            if (ethernetHasUsableIP(localIp)) {
                _ethStatus = 0;
                { char _buf[48]; snprintf(_buf, sizeof(_buf), "ETH: static IP: %s", localIp.toString().c_str()); sendVirtualMessage(String(_buf), INFO); }
                ethernetDirectBootLog(String(F("Static IP ")) + localIp.toString());
                // Do NOT call ethernetStartBootstrapServerIfNeeded() here.
                // This function runs inside taskInitEthernet (FreeRTOS task);
                // EthernetServer::begin() configures a W5500 socket via SPI and
                // must be called from loop() context. ethernetLoop() will start
                // the server on the next tick when it sees _ethStatus == 0.
                return;
            }
            sendVirtualMessage(F("ETH: static IP failed, fallback DHCP"), WARN);
        } else {
            sendVirtualMessage(F("ETH: invalid static config, fallback DHCP"), WARN);
        }
    }

    // Wait for PHY link UP before sending DHCP DISCOVER.
    // After W5500 hardware reset the PHY needs time to auto-negotiate with the switch.
    // Managed switches with STP can take 4-7 s before the port forwards traffic.
    // 3 s was too short in practice (log showed exactly 3 s elapsed before giving up).
    // 6 s gives the PHY sufficient time while keeping the boot sequence reasonably fast.
    {
        unsigned long linkWait = millis();
        while (::Ethernet.linkStatus() == LinkOFF && (millis() - linkWait) < 6000UL) {
            delay(50);
        }
        unsigned long linkElapsed = millis() - linkWait;
        if (::Ethernet.linkStatus() == LinkOFF) {
            if (_ethW5500CycleLog) sendVirtualMessage(F("ETH: link DOWN before DHCP (no cable?)"), WARN);
            { char _lb[64]; snprintf(_lb, sizeof(_lb), "Link still down after %lums", linkElapsed); ethernetDirectBootLog(String(_lb)); }
        } else {
            char _lb[64]; snprintf(_lb, sizeof(_lb), "ETH: link UP after %lums", linkElapsed);
            if (_ethW5500CycleLog) sendVirtualMessage(String(_lb), INFO);
            ethernetDirectBootLog(String(_lb));
        }
    }

    // 15 s timeout: generous enough for slow DHCP servers / managed-switch STP delays,
    // but much shorter than the library default (60 s).
    int dhcpResult = ::Ethernet.begin(macToUse, 15000);
    if (dhcpResult == 0) {
        // DHCP failed: log hardware/link status for diagnostics then schedule a retry.
        // hwStatus: 0=chip not detected, 1=W5100, 2=W5200, 3=W5500
        // linkStatus: 0=no cable, 1=cable OK, 2=unknown
        EthernetHardwareStatus hwSt  = ::Ethernet.hardwareStatus();
        EthernetLinkStatus     lnkSt = ::Ethernet.linkStatus();
        { char _buf[64]; snprintf(_buf, sizeof(_buf), "ETH: DHCP failed | hwStatus=%d linkStatus=%d", (int)hwSt, (int)lnkSt); if (_ethW5500CycleLog) sendVirtualMessage(String(_buf), WARN); }
        ethernetDirectBootLog(String(F("DHCP failed hw=")) + String((int)hwSt) + String(F(" link=")) + String((int)lnkSt));
        if (hwSt == EthernetNoHardware) {
            // W5500 chip not responding on SPI — cannot continue.
            { char _buf[96]; snprintf(_buf, sizeof(_buf), "ETH: W5500 NOT DETECTED on SPI! SCLK=%d MISO=%d MOSI=%d CS=%d RST=%d", ETH_SPI_SCLK_PIN, ETH_SPI_MISO_PIN, ETH_SPI_MOSI_PIN, ETH_CS_PIN, ETH_RST_PIN); if (_ethW5500CycleLog) sendVirtualMessage(String(_buf), WARN); }
            ethernetDirectBootLog(F("W5500 not detected on SPI"));
        } else if (lnkSt == LinkOFF) {
            if (_ethW5500CycleLog) sendVirtualMessage(F("ETH: No RJ45 cable! Check physical connection."), WARN);
            ethernetDirectBootLog(F("DHCP failed: no cable"));
        } else {
            // Link is up but DHCP server did not respond within the timeout.
            // Most common cause: switch STP delay (port not yet in forwarding state
            // after link-up). ethernetLoop() will retry after RETRY_INTERVAL_MS (30 s),
            // by which time STP should have converged.
            if (_ethW5500CycleLog) sendVirtualMessage(F("ETH: DHCP no response; retrying in 30s..."), WARN);
            ethernetDirectBootLog(F("DHCP failed: retrying in 30s"));
        }
        // No static fallback — user must explicitly configure static IP on the settings
        // page if DHCP is not available. Falling back silently to a hardcoded address
        // causes invisible address conflicts on the network.
        _ethUsedDhcp = false;
        _ethStatus = -1;
    } else {
        _ethUsedDhcp = true;  // Live DHCP lease — maintain() must run to renew/rebind
        _ethStatus = 0;
        { char _buf[48]; snprintf(_buf, sizeof(_buf), "ETH: DHCP OK, IP %s", ::Ethernet.localIP().toString().c_str()); sendVirtualMessage(String(_buf), INFO); }
        ethernetDirectBootLog(String(F("DHCP OK ")) + ::Ethernet.localIP().toString());
    }

    if (_ethStatus == 0) {
        sendVirtualMessage(F("ETH: link/IP ready"), INFO);
        ethernetDirectBootLog(F("Link and IP ready"));
        // Re-trigger NTP sync now that Ethernet has an IP.
        // The initial configTzTime() at boot runs before any network is available
        // (Ethernet init takes 30+ s); without this call, time(NULL) never crosses
        // the 1 000 000 000 threshold and serial monitor messages show uptime instead
        // of real wall-clock time.
#if ENABLE_NTP==1
        {
            String _tz  = configManager.getNtpTimezone();
            String _ntp = configManager.getNtpServer();
            configTzTime(_tz.c_str(), _ntp.c_str());
            sendVirtualMessage(F("ETH: NTP sync started"), INFO);
        }
#endif
        // NOTE: _ethHttpServer.begin() is deferred to ethernetLoop() (main loop)
        // to ensure the W5500 listen socket is configured from the correct task context.
    }
}

int ethernetStatus() {
    return _ethStatus;
}

// Flag set to true while a reinit task is running, to prevent multiple concurrent tasks.
volatile bool _ethReinitTaskRunning = false;

// Short-lived FreeRTOS task: runs ethernetBegin() off the main loop to avoid:
//   1. Blocking loop() for up to 15+ s (DHCP timeout + link-up wait)
//   2. SPI bus conflict with SD card (ethernetBegin calls SPI.begin on the shared FSPI bus)
// Same pattern as taskInitEthernet in sd_tasks_impl.h.
static void taskReinitEthernet(void* param) {
    ethernetBegin();
    _ethReinitTaskRunning = false;
    vTaskDelete(nullptr);
}

void ethernetLoop() {
    // Check intervals
    static unsigned long _lastLinkCheckMs    = 0;
    static unsigned long _lastRetryMs        = 0;
    static unsigned long _lastSocketReopenMs = 0;
    static unsigned long _lastCloseWaitScanMs = 0;
    const unsigned long  LINK_CHECK_INTERVAL_MS   = 5000UL;
    const unsigned long  RETRY_INTERVAL_MS        = 30000UL;
    const unsigned long  SOCKET_REOPEN_INTERVAL_MS = 20000UL;
    // Scan for CLOSE_WAIT sockets every 5 s and force-close them.
    // Each browser tab that loads a page leaves sockets in CLOSE_WAIT for up to
    // 120 s (OS TCP timeout) unless the firmware explicitly terminates them.
    // With only 8 slots, a few uncleared CLOSE_WAIT sockets starve new connections.
    const unsigned long  CLOSE_WAIT_SCAN_MS       = 5000UL;

    unsigned long now = millis();

    // If Ethernet never came up (boot failure) or link was lost, retry periodically.
    // Reinit is offloaded to a FreeRTOS task to avoid blocking loop() during DHCP
    // and to prevent SPI bus conflicts with the SD card on the shared FSPI bus.
    if (_ethStatus != 0) {
        if (!_ethReinitTaskRunning && (now - _lastRetryMs >= RETRY_INTERVAL_MS)) {
            _lastRetryMs = now;
            _ethReinitTaskRunning = true;
            if (_ethW5500NoLinkLogDue()) {
                _ethW5500LogBurstPending = true;
                sendVirtualMessage(F("ETH: no link, trying to reconnect..."), WARN);
            }
            // 8 KB stack: ethernetBegin DHCP needs ~5-6 KB (DhcpClass packet buffer + SPI + lwIP).
            // 4 KB was too small and caused stack-overflow panics on some DHCP exchanges.
            // CRITICAL: reset the flag if task creation fails; otherwise _ethReinitTaskRunning
            // stays true forever and the retry loop is permanently blocked.
            BaseType_t _ethRetryTaskResult = xTaskCreatePinnedToCore(
                taskReinitEthernet, "eth_ri", 8192, nullptr, 1, nullptr, 0);
            if (_ethRetryTaskResult != pdPASS) {
                _ethReinitTaskRunning = false; // allow retry on the next interval
                sendVirtualMessage(F("ETH: failed to create reinit task (heap?)"), ERROR);
            }
        }
        return;
    }

    // Start the HTTP server from the main loop (not from the FreeRTOS init task).
    // EthernetServer::begin() must run in the same task context as EthernetServer::available()
    // to avoid W5500 socket state corruption.
    ethernetStartBootstrapServerIfNeeded();

    if (!_ethHttpServerStarted) {
        return;
    }

    // Periodic link check + DHCP maintenance (every 5s).
    // Calling maintain() every loop iteration generates unnecessary SPI traffic.
    if (now - _lastLinkCheckMs >= LINK_CHECK_INTERVAL_MS) {
        _lastLinkCheckMs = now;

        // Skip all SPI reads while a reinit task is running on Core 0.
        // taskReinitEthernet performs a W5500 hardware reset followed by up to 15s of DHCP
        // packet exchanges over SPI. Reading PHYCFGR or SIPR from Core 1 concurrently
        // produces corrupted results (spurious LinkOFF / 0.0.0.0) that immediately set
        // _ethStatus=-1 and schedule yet another reinit, creating infinite oscillation.
        if (_ethReinitTaskRunning) {
            return;
        }

        // Check physical link first (cable presence).
        // LinkOFF = cable unplugged or switch port down.
        // Require 2 consecutive checks to confirm link-down before declaring failure.
        // A single false-positive reading would otherwise trigger a full reinit (and
        // the associated SPI re-initialisation) unnecessarily.
        static uint8_t _linkOffCount = 0;
        EthernetLinkStatus lnk = ::Ethernet.linkStatus();
        if (lnk == LinkOFF) {
            _linkOffCount++;
            if (_linkOffCount < 2) {
                // First reading: wait for the next check before acting.
                return;
            }
            sendVirtualMessage(F("ETH: cable disconnected, waiting for reconnection..."), WARN);
            _linkOffCount = 0;
            _ethStatus = -1;
            _ethHttpServerStarted = false; // force HTTP server restart after reconnect
            return;
        }
        _linkOffCount = 0; // reset counter on successful link-up reading

        // Check for usable IP (0.0.0.0 = no address).
        if (!ethernetHasUsableIP(::Ethernet.localIP())) {
            sendVirtualMessage(F("ETH: no valid IP, forcing reinit..."), WARN);
            _ethStatus = -1;
            _ethHttpServerStarted = false;
            return;
        }

        // DHCP lease maintenance — only when we have a live DHCP lease.
        // With a static IP (DHCP-fallback or explicitly configured), the W5500 library
        // retains internal DHCP state from the failed Ethernet.begin(mac, timeout) call
        // and maintain() keeps returning 3 (rebind_fail), which would trigger a full
        // reinit every LINK_CHECK_INTERVAL_MS. Skip maintain() for static-IP sessions.
        // Return values: 0=ok/nothing, 1=renew_fail, 2=renew_ok, 3=rebind_fail, 4=rebind_ok
        if (_ethUsedDhcp) {
            int m = ::Ethernet.maintain();
            if (m == 1 || m == 3) {
                // Renew or rebind failed — DHCP server unreachable; force full reinit.
                char buf[56];
                snprintf(buf, sizeof(buf), "ETH: DHCP failed (maintain=%d), reinit...", m);
                sendVirtualMessage(String(buf), WARN);
                _ethUsedDhcp = false;
                _ethStatus = -1;
                _ethHttpServerStarted = false;
                return;
            }
        }
    }

    // Active CLOSE_WAIT scanner: iterate over all W5500 sockets and force-close any
    // that are stuck in CLOSE_WAIT (0x1C). CLOSE_WAIT means the browser sent FIN (its
    // half-close) but our firmware hasn't replied with FIN yet. Without this, each page
    // load leaves a socket occupied for minutes, and after ~8 loads all sockets are gone.
    // Use execCmdSn(Sock_DISCON) directly — avoids constructing EthernetClient with a raw
    // socket index, which leaves the internal socket pointer null and crashes on stop().
    if (now - _lastCloseWaitScanMs >= CLOSE_WAIT_SCAN_MS) {
        _lastCloseWaitScanMs = now;
        bool anyFound = false;
        for (uint8_t s = 0; s < MAX_SOCK_NUM; s++) {
            if (W5100.readSnSR(s) == SnSR::CLOSE_WAIT) {
                W5100.execCmdSn(s, Sock_DISCON); // send FIN: CLOSE_WAIT -> LAST_ACK -> CLOSED
                anyFound = true;
            }
        }
        if (anyFound) {
            _ethHttpServer.begin(); // Reopen a LISTEN socket after freeing the stale ones
        }
    }

    // Flush any pending Serial output before entering the synchronous HTTP serve block.
    // ethernetBootstrapHandleClient() can block loop() for 100-500ms per request;
    // without this, heartbeat and other log messages pile up in printerOutputBuffer
    // and never reach the Serial Monitor while Ethernet is active.
    extern void manageSerialOutput();
    manageSerialOutput();

    // Serve up to 6 pending HTTP clients per loop() visit.
    // 6 matches the parallel request burst used by most browsers and reduces
    // queueing on the home page (HTML + CSS + JS + API fetches).
    // The hard cap prevents the WDT from triggering when the browser floods requests
    // (e.g. retries after a timeout): without it the while() never yields and the
    // 60-second task watchdog aborts. Remaining clients are served on the next loop().
    // After each served request, call begin() immediately so the W5500 reopens a
    // LISTEN socket — this minimises the window where all sockets are in CLOSE_WAIT
    // and new browser connections are rejected (progressive degradation symptom).
    for (int _ethN = 0; _ethN < 6; _ethN++) {
        if (!ethernetBootstrapHandleClient()) break;
        _ethHttpServer.begin(); // Reopen a LISTEN socket ASAP after closing this one
    }

    // Periodic proactive socket reopen: even if no requests were served, call begin()
    // every 20 s to rescue any sockets that got stuck in CLOSE_WAIT / FIN_WAIT.
    if (now - _lastSocketReopenMs >= SOCKET_REOPEN_INTERVAL_MS) {
        _lastSocketReopenMs = now;
        _ethHttpServer.begin();
    }
}

#endif // TARGET_BOARD_P4 / W5500 else

#endif // ENABLE_ETHERNET
