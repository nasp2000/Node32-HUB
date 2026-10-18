// can_runtime.cpp
// PSRAM ring buffer, FreeRTOS RX task, config persistence, statistics.

#include "can_runtime.h"

#include "feature_flags.h"
#if ENABLE_CAN==1

#include <LittleFS.h>
#include <ArduinoJson.h>
#include "littlefs_manager.h"
#include "commands.h"       // sendVirtualMessage, LogLevel
#include "psram_helpers.h"     // heap_caps_malloc SPIRAM
#include "feature_flags.h"

#define CAN_CFG_PATH  "/config/can.json"

// --- Static state ---

static CanConfig         s_cfg;
static CanRuntimeStats   s_stats;
static CanBusStats       s_bus_stats;

// PSRAM-backed frame ring buffer
static CanFrame*         s_frame_buf  = nullptr;
static size_t            s_buf_head   = 0;  // index of next write
static size_t            s_buf_count  = 0;  // frames currently in buffer
static SemaphoreHandle_t s_buf_mutex  = nullptr;

// FreeRTOS RX task handle
static TaskHandle_t      s_rx_task    = nullptr;

// --- Config persistence ---

static void loadConfig() {
    // Set safe defaults first
    s_cfg.enabled      = true;
    s_cfg.bitrate_kbps = 500;
    s_cfg.mode         = CAN_MODE_NORMAL;
    s_cfg.tx_pin       = CAN_TX_PIN;
    s_cfg.rx_pin       = CAN_RX_PIN;
    s_cfg.filter_id    = 0;
    s_cfg.filter_mask  = 0xFFFFFFFFUL;
    s_cfg.filter_ext   = false;
    s_cfg.log_to_sd    = false;

    if (!littlefsBegin()) return;
    File f = LittleFS.open(CAN_CFG_PATH, "r");
    if (!f) return;

    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
    f.close();

    if (doc.containsKey("enabled"))      s_cfg.enabled      = doc["enabled"].as<bool>();
    if (doc.containsKey("bitrate_kbps")) s_cfg.bitrate_kbps = doc["bitrate_kbps"].as<uint16_t>();
    if (doc.containsKey("mode"))         s_cfg.mode         = (CanMode)doc["mode"].as<uint8_t>();
    if (doc.containsKey("tx_pin"))       s_cfg.tx_pin       = doc["tx_pin"].as<int>();
    if (doc.containsKey("rx_pin"))       s_cfg.rx_pin       = doc["rx_pin"].as<int>();
    if (doc.containsKey("filter_id"))    s_cfg.filter_id    = doc["filter_id"].as<uint32_t>();
    if (doc.containsKey("filter_mask"))  s_cfg.filter_mask  = doc["filter_mask"].as<uint32_t>();
    if (doc.containsKey("filter_ext"))   s_cfg.filter_ext   = doc["filter_ext"].as<bool>();
    if (doc.containsKey("log_to_sd"))    s_cfg.log_to_sd    = doc["log_to_sd"].as<bool>();
}

static bool saveConfig() {
    if (!littlefsBegin()) return false;
    StaticJsonDocument<256> doc;
    doc["enabled"]      = s_cfg.enabled;
    doc["bitrate_kbps"] = s_cfg.bitrate_kbps;
    doc["mode"]         = (uint8_t)s_cfg.mode;
    doc["tx_pin"]       = s_cfg.tx_pin;
    doc["rx_pin"]       = s_cfg.rx_pin;
    doc["filter_id"]    = s_cfg.filter_id;
    doc["filter_mask"]  = s_cfg.filter_mask;
    doc["filter_ext"]   = s_cfg.filter_ext;
    doc["log_to_sd"]    = s_cfg.log_to_sd;
    File f = LittleFS.open("/config/can.json.tmp", "w");
    if (!f) return false;
    size_t bytes = serializeJsonPretty(doc, f);
    f.close();
    storNoteLfsWrite(bytes);
    if (bytes > 0) {
        LittleFS.remove(CAN_CFG_PATH);
        LittleFS.rename("/config/can.json.tmp", CAN_CFG_PATH);
    } else {
        LittleFS.remove("/config/can.json.tmp");
        return false;
    }
    return true;
}

// --- PSRAM ring buffer helpers (call with mutex held) ---

// Push a frame into the ring buffer; drops oldest if full.
static void bufferPush(const CanFrame& frame) {
    if (s_frame_buf == nullptr) return;
    s_frame_buf[s_buf_head] = frame;
    s_buf_head = (s_buf_head + 1) % CAN_FRAME_BUFFER_SIZE;
    if (s_buf_count < CAN_FRAME_BUFFER_SIZE) {
        s_buf_count++;
    } else {
        s_stats.buffer_drops++;  // oldest was overwritten
    }
}

// Copy up to max_frames frames in chronological order into out[].
static size_t bufferRead(CanFrame* out, size_t max_frames) {
    if (s_frame_buf == nullptr || s_buf_count == 0) return 0;
    size_t n = (max_frames < s_buf_count) ? max_frames : s_buf_count;
    // Oldest frame is at (head - count + BUFSIZE) % BUFSIZE
    size_t start = (s_buf_head + CAN_FRAME_BUFFER_SIZE - s_buf_count) % CAN_FRAME_BUFFER_SIZE;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_frame_buf[(start + i) % CAN_FRAME_BUFFER_SIZE];
    }
    return n;
}

// --- FreeRTOS RX task ---

static void canRxTask(void* /* param */) {
    CanFrame frame;
    for (;;) {
        // Drain all frames currently waiting in the TWAI hardware queue
        while (canCoreReceive(frame)) {
            s_stats.total_rx++;
            if (s_buf_mutex && xSemaphoreTake(s_buf_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                bufferPush(frame);
                xSemaphoreGive(s_buf_mutex);
            }
        }

        // Check bus state for automatic recovery after bus-off
        CanBusState state = canCoreGetState();
        if (state == CAN_BUS_BUS_OFF) {
            s_stats.bus_off_events++;
            canCoreInitiateRecovery();
        }

        // Snapshot live stats for the web API (avoids driver call inside handler)
        canCoreGetStats(s_bus_stats);

        vTaskDelay(pdMS_TO_TICKS(2));  // 2ms poll — ~500 frames/s headroom at 1Mbps
    }
}

// --- Public API ---

void canRuntimeInit() {
    memset(&s_stats,     0, sizeof(s_stats));
    memset(&s_bus_stats, 0, sizeof(s_bus_stats));

    loadConfig();

    // Allocate PSRAM ring buffer
    size_t buf_bytes = CAN_FRAME_BUFFER_SIZE * sizeof(CanFrame);
    if (psramFound()) {
        s_frame_buf = (CanFrame*)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
        if (s_frame_buf) {
            memset(s_frame_buf, 0, buf_bytes);
            sendVirtualMessage(F("[CAN] PSRAM frame buffer allocated"), INFO);
        } else {
            sendVirtualMessage(F("[CAN] PSRAM alloc failed, using DRAM"), WARN);
        }
    }
    if (!s_frame_buf) {
        s_frame_buf = (CanFrame*)malloc(buf_bytes);
        if (s_frame_buf) memset(s_frame_buf, 0, buf_bytes);
    }

    s_buf_mutex = xSemaphoreCreateMutex();

    if (!s_cfg.enabled) {
        sendVirtualMessage(F("[CAN] Disabled in config, skipping driver init"), INFO);
        return;
    }

    // Install and start the TWAI driver
    bool ok = canCoreInit(s_cfg.tx_pin, s_cfg.rx_pin, s_cfg.bitrate_kbps,
                          s_cfg.mode, s_cfg.filter_id, s_cfg.filter_mask, s_cfg.filter_ext);
    if (!ok) return;

    // Start the RX task on Core 0 (same as SD / email tasks to keep loop() free)
    xTaskCreatePinnedToCore(canRxTask, "can_rx", 3072, nullptr, 2, &s_rx_task, 0);
}

void canRuntimeStop() {
    if (s_rx_task) {
        vTaskDelete(s_rx_task);
        s_rx_task = nullptr;
    }
    canCoreStop();
}

bool canRuntimeSend(const CanFrame& frame) {
    bool ok = canCoreSend(frame);
    if (ok) s_stats.total_tx++;
    else     s_stats.total_tx_errors++;
    return ok;
}

size_t canRuntimeGetFrames(CanFrame* out, size_t max_frames) {
    if (!s_buf_mutex) return 0;
    if (xSemaphoreTake(s_buf_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    size_t n = bufferRead(out, max_frames);
    xSemaphoreGive(s_buf_mutex);
    return n;
}

size_t canRuntimeFrameCount() {
    if (!s_buf_mutex) return 0;
    if (xSemaphoreTake(s_buf_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return 0;
    size_t n = s_buf_count;
    xSemaphoreGive(s_buf_mutex);
    return n;
}

void canRuntimeClearFrames() {
    if (!s_buf_mutex) return;
    if (xSemaphoreTake(s_buf_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    s_buf_head  = 0;
    s_buf_count = 0;
    xSemaphoreGive(s_buf_mutex);
}

CanRuntimeStats canRuntimeGetStats() {
    return s_stats;
}

CanBusStats canRuntimeGetBusStats() {
    return s_bus_stats;
}

const CanConfig& canRuntimeGetConfig() {
    return s_cfg;
}

bool canRuntimeApplyConfig(const CanConfig& cfg, bool save_to_fs) {
    // Stop existing driver and task
    if (s_rx_task) {
        vTaskDelete(s_rx_task);
        s_rx_task = nullptr;
    }
    canCoreStop();

    s_cfg = cfg;
    if (save_to_fs) saveConfig();

    if (!s_cfg.enabled) {
        sendVirtualMessage(F("[CAN] Disabled by config update"), INFO);
        return true;
    }

    bool ok = canCoreInit(s_cfg.tx_pin, s_cfg.rx_pin, s_cfg.bitrate_kbps,
                          s_cfg.mode, s_cfg.filter_id, s_cfg.filter_mask, s_cfg.filter_ext);
    if (!ok) return false;

    xTaskCreatePinnedToCore(canRxTask, "can_rx", 3072, nullptr, 2, &s_rx_task, 0);
    return true;
}

bool canRuntimeIsRunning() {
    return canCoreIsRunning();
}

#endif // ENABLE_CAN==1
