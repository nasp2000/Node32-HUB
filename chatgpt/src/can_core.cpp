// can_core.cpp
// TWAI driver: install, start, stop, send, receive, status.

#include "can_core.h"

#include "feature_flags.h"
#if ENABLE_CAN==1

#include "commands.h"  // sendVirtualMessage, LogLevel
#include "feature_flags.h"

// Internal: track whether driver is installed
static bool s_installed = false;

// Map bitrate in kbps to an ESP-IDF twai_timing_config_t
static bool getBitrateCfg(uint16_t kbps, twai_timing_config_t& out) {
    switch (kbps) {
        case 25:   { twai_timing_config_t c = TWAI_TIMING_CONFIG_25KBITS();   out = c; return true; }
        case 50:   { twai_timing_config_t c = TWAI_TIMING_CONFIG_50KBITS();   out = c; return true; }
        case 100:  { twai_timing_config_t c = TWAI_TIMING_CONFIG_100KBITS();  out = c; return true; }
        case 125:  { twai_timing_config_t c = TWAI_TIMING_CONFIG_125KBITS();  out = c; return true; }
        case 250:  { twai_timing_config_t c = TWAI_TIMING_CONFIG_250KBITS();  out = c; return true; }
        case 500:  { twai_timing_config_t c = TWAI_TIMING_CONFIG_500KBITS();  out = c; return true; }
        case 800:  { twai_timing_config_t c = TWAI_TIMING_CONFIG_800KBITS();  out = c; return true; }
        case 1000: { twai_timing_config_t c = TWAI_TIMING_CONFIG_1MBITS();    out = c; return true; }
        default:   return false;
    }
}

bool canCoreInit(int tx_pin, int rx_pin, uint16_t bitrate_kbps, CanMode mode,
                 uint32_t filter_id, uint32_t filter_mask, bool filter_ext) {
    if (s_installed) {
        sendVirtualMessage(F("[CAN] Already running, stopping first"), DEBUG);
        canCoreStop();
    }

    twai_timing_config_t t_config;
    if (!getBitrateCfg(bitrate_kbps, t_config)) {
        sendVirtualMessage(F("[CAN] Invalid bitrate"), ERROR);
        return false;
    }

    twai_mode_t drv_mode;
    switch (mode) {
        case CAN_MODE_LISTEN:   drv_mode = TWAI_MODE_LISTEN_ONLY; break;
        case CAN_MODE_SELFTEST: drv_mode = TWAI_MODE_NO_ACK;       break;
        default:                drv_mode = TWAI_MODE_NORMAL;        break;
    }

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)tx_pin, (gpio_num_t)rx_pin, drv_mode);
    g_config.rx_queue_len = 64; // Large RX queue so we don't drop frames under load
    g_config.tx_queue_len = 8;
    g_config.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED |
                               TWAI_ALERT_ERR_PASS | TWAI_ALERT_ARB_LOST |
                               TWAI_ALERT_RX_QUEUE_FULL;

    // Build hardware filter
    twai_filter_config_t f_config;
    if (filter_mask == 0xFFFFFFFFUL) {
        // Accept all frames (most common)
        f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    } else {
        if (filter_ext) {
            // Extended 29-bit filter (single-filter mode)
            f_config.acceptance_code = (filter_id & 0x1FFFFFFFUL) << 3;
            f_config.acceptance_mask = ~((filter_mask & 0x1FFFFFFFUL) << 3);
            f_config.single_filter   = true;
        } else {
            // Standard 11-bit filter (single-filter mode)
            f_config.acceptance_code = (filter_id & 0x7FFUL) << 21;
            f_config.acceptance_mask = ~((filter_mask & 0x7FFUL) << 21);
            f_config.single_filter   = true;
        }
    }

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        sendVirtualMessage(F("[CAN] Driver install failed"), ERROR);
        return false;
    }
    if (twai_start() != ESP_OK) {
        twai_driver_uninstall();
        sendVirtualMessage(F("[CAN] Driver start failed"), ERROR);
        return false;
    }

    s_installed = true;
    char buf[64];
    snprintf(buf, sizeof(buf), "[CAN] Started TX=%d RX=%d %uKbps mode=%u", tx_pin, rx_pin, bitrate_kbps, (unsigned)mode);
    sendVirtualMessage(buf, INFO);
    return true;
}

void canCoreStop() {
    if (!s_installed) return;
    twai_stop();
    twai_driver_uninstall();
    s_installed = false;
    sendVirtualMessage(F("[CAN] Driver stopped"), INFO);
}

bool canCoreSend(const CanFrame& frame, uint32_t tx_timeout_ms) {
    if (!s_installed) return false;
    twai_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.identifier         = frame.id;
    msg.extd               = frame.ext ? 1 : 0;
    msg.rtr                = frame.rtr ? 1 : 0;
    msg.data_length_code   = frame.dlc > 8 ? 8 : frame.dlc;
    memcpy(msg.data, frame.data, msg.data_length_code);
    return twai_transmit(&msg, pdMS_TO_TICKS(tx_timeout_ms)) == ESP_OK;
}

bool canCoreReceive(CanFrame& out) {
    if (!s_installed) return false;
    twai_message_t msg;
    if (twai_receive(&msg, 0) != ESP_OK) return false;
    out.id           = msg.identifier;
    out.ext          = msg.extd != 0;
    out.rtr          = msg.rtr  != 0;
    out.dlc          = msg.data_length_code;
    out.timestamp_ms = (uint32_t)millis();
    memset(out.data, 0, sizeof(out.data));
    memcpy(out.data, msg.data, out.dlc);
    return true;
}

void canCoreGetStats(CanBusStats& stats) {
    twai_status_info_t si;
    if (!s_installed || twai_get_status_info(&si) != ESP_OK) {
        memset(&stats, 0, sizeof(stats));
        stats.state = CAN_BUS_STOPPED;
        return;
    }
    stats.rx_frames    = 0;                  // TWAI has no cumulative RX counter; use CanRuntimeStats.total_rx
    stats.tx_frames    = 0;                  // TWAI has no cumulative TX counter; use CanRuntimeStats.total_tx
    stats.rx_errors    = si.rx_missed_count; // frames dropped due to full RX queue
    stats.tx_errors    = si.tx_failed_count; // frames that failed transmission
    stats.bus_off_count = si.bus_error_count;
    stats.arb_lost_count = si.arb_lost_count;
    stats.msgs_to_rx   = si.msgs_to_rx;
    stats.tec          = (uint8_t)si.tx_error_counter;
    stats.rec          = (uint8_t)si.rx_error_counter;

    switch (si.state) {
        case TWAI_STATE_STOPPED:    stats.state = CAN_BUS_STOPPED;    break;
        case TWAI_STATE_RUNNING:    stats.state = CAN_BUS_RUNNING;    break;
        case TWAI_STATE_BUS_OFF:    stats.state = CAN_BUS_BUS_OFF;    break;
        case TWAI_STATE_RECOVERING: stats.state = CAN_BUS_RECOVERING; break;
        default:                    stats.state = CAN_BUS_ERROR;       break;
    }
}

CanBusState canCoreGetState() {
    if (!s_installed) return CAN_BUS_STOPPED;
    twai_status_info_t si;
    if (twai_get_status_info(&si) != ESP_OK) return CAN_BUS_ERROR;
    switch (si.state) {
        case TWAI_STATE_STOPPED:    return CAN_BUS_STOPPED;
        case TWAI_STATE_RUNNING:    return CAN_BUS_RUNNING;
        case TWAI_STATE_BUS_OFF:    return CAN_BUS_BUS_OFF;
        case TWAI_STATE_RECOVERING: return CAN_BUS_RECOVERING;
        default:                    return CAN_BUS_ERROR;
    }
}

void canCoreInitiateRecovery() {
    if (!s_installed) return;
    twai_initiate_recovery();
    sendVirtualMessage(F("[CAN] Bus-off recovery initiated"), WARN);
}

bool canCoreIsRunning() {
    return s_installed && canCoreGetState() == CAN_BUS_RUNNING;
}

#endif // ENABLE_CAN==1
