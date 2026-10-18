// email_flash_rpc.cpp — see email_flash_rpc.h for the contract.
//
// Two paths share ONE op executor (efPerform):
//   * direct  — internal-stack caller runs efPerform itself (lfsMutex inside).
//   * RPC     — external-stack caller publishes EfReq; emailFlashServe()
//               (loop task) runs efPerform and the requester polls the reply.
// Request/response payloads (String paths, read/write buffers) live in the
// requester's memory and are ONLY touched while the state machine holds the
// paired transition, so no copy ever runs inside a critical section:
//   - requester writes EfReq before reqBegin (state Idle; serve does not
//     read EfReq until Req), requester reads EfResp only after RESP->IDLE
//     (serve never writes it again until the next Req it cannot start).
// All EfRpcState transitions go through the portMUX; payload buffers are
// handed over with the same acquire/release edges (FreeRTOS mux on SMP).

#include "email_flash_rpc.h"
#include "email_flash_rpc_core.h"
#include "feature_flags.h"

#if ENABLE_FILESYSTEM==1

#include "littlefs_manager.h"
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>
#include <esp_memory_utils.h>
#include <esp_cpu.h>

namespace {

constexpr int      kEfMaxFiles       = 8;              // concurrent open handles
constexpr uint32_t kEfOpTimeoutMs    = 20000;          // requester waits for serve
constexpr uint32_t kEfStaleOwnerMs   = 30000;          // dead-owner reclaim age
constexpr uint32_t kEfServeLockMs    = 5000;           // lfsMutex slice in serve/direct
constexpr size_t   kEfMaxRead        = 64 * 1024;      // single read op cap (google: 50KB)
constexpr size_t   kEfMaxWrite       = 128 * 1024;     // single write op cap (archive/journal)

enum EfOp : uint8_t {
    EFOP_OPEN = 1,
    EFOP_READ,
    EFOP_SEEK,
    EFOP_CLOSE,
    EFOP_PROBE,
    EFOP_EXISTS,
    EFOP_WRITE,
    EFOP_REMOVE,
    EFOP_RENAME,
    EFOP_MKDIR,
    EFOP_LIST,
};

struct EfSlot {
    bool used;
    File f;
};

struct EfReq {
    uint8_t op;
    int32_t h;
    uint32_t u1;              // seek pos / write length
    uint32_t u2;              // read length
    const uint8_t* data;      // write payload (requester memory)
    uint8_t* out;             // read destination (requester memory)
    String path;
    String path2;             // rename target
    TaskHandle_t owner;       // publishing task (stale-response reclaim)
    uint32_t ownerTs;
};

struct EfResp {
    bool ok;
    int32_t h;
    size_t n;
    size_t sz;
    std::vector<std::pair<String, size_t>> list;
};

EfSlot s_slots[kEfMaxFiles];
EfReq s_req;
EfResp s_resp;
EfRpcMachine s_mach;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t s_reqMutex = nullptr;   // serializes external requesters

// Single op executor. Runs ONLY on a safe stack: direct = internal caller,
// RPC = loop task (emailFlashServe). Always serializes on lfsMutex.
bool efPerform(const EfReq& r, EfResp& resp) {
    resp.ok = false;
    resp.h = -1;
    resp.n = 0;
    resp.sz = 0;
    resp.list.clear();

    if (!littlefsBegin()) return false;          // flag fast-path once mounted
    if (!lfsMutexLock(pdMS_TO_TICKS(kEfServeLockMs))) return false;

    switch (r.op) {
    case EFOP_OPEN: {
        for (int i = 0; i < kEfMaxFiles; ++i) {
            if (s_slots[i].used) continue;
            File f = LittleFS.open(r.path.c_str(), FILE_READ);
            if (f) {
                s_slots[i].f = f;
                s_slots[i].used = true;
                resp.h = i;
                resp.sz = f.size();
                resp.ok = true;
            }
            break;
        }
        break;
    }
    case EFOP_READ: {
        if (r.h >= 0 && r.h < kEfMaxFiles && s_slots[r.h].used && r.out && r.u2 <= kEfMaxRead) {
            resp.n = s_slots[r.h].f.read(r.out, r.u2);
            resp.ok = true;                      // 0 = EOF, callers bound their loops
        }
        break;
    }
    case EFOP_SEEK: {
        if (r.h >= 0 && r.h < kEfMaxFiles && s_slots[r.h].used) {
            resp.ok = s_slots[r.h].f.seek(r.u1);
        }
        break;
    }
    case EFOP_CLOSE: {
        if (r.h >= 0 && r.h < kEfMaxFiles && s_slots[r.h].used) {
            s_slots[r.h].f.close();
            s_slots[r.h].used = false;
            resp.ok = true;
        }
        break;
    }
    case EFOP_PROBE: {
        File f = LittleFS.open(r.path.c_str(), FILE_READ);
        if (f) {
            resp.sz = f.size();
            resp.ok = true;
            f.close();
        }
        break;
    }
    case EFOP_EXISTS: {
        resp.ok = LittleFS.exists(r.path.c_str());
        break;
    }
    case EFOP_WRITE: {
        if (r.data && r.u1 <= kEfMaxWrite) {
            File f = LittleFS.open(r.path.c_str(), FILE_WRITE);
            if (f) {
                size_t w = f.write(r.data, r.u1);
                f.close();
                resp.ok = (w == r.u1);
            }
        }
        break;
    }
    case EFOP_REMOVE: {
        resp.ok = LittleFS.remove(r.path.c_str()) || !LittleFS.exists(r.path.c_str());
        break;
    }
    case EFOP_RENAME: {
        resp.ok = LittleFS.rename(r.path.c_str(), r.path2.c_str());
        break;
    }
    case EFOP_MKDIR: {
        resp.ok = LittleFS.exists(r.path.c_str()) || LittleFS.mkdir(r.path.c_str());
        break;
    }
    case EFOP_LIST: {
        if (LittleFS.exists(r.path.c_str())) {
            File rr = LittleFS.open(r.path.c_str());
            if (rr && rr.isDirectory()) {
                resp.ok = true;
                File ee = rr.openNextFile();
                unsigned count = 0;
                while (ee) {
                    String name = String(ee.name());
                    size_t sz = ee.size();
                    bool isDir = ee.isDirectory();
                    ee.close();
                    if (!isDir) {
                        int slash = name.lastIndexOf('/');
                        String base = (slash >= 0) ? name.substring(slash + 1) : name;
                        resp.list.emplace_back(base, sz);
                    }
                    if ((++count % 32) == 0) esp_task_wdt_reset();
                    ee = rr.openNextFile();
                    yield();
                }
                rr.close();
            } else if (rr) {
                rr.close();
            }
        }
        break;
    }
    default:
        break;
    }

    lfsMutexUnlock();
    return true;                                 // op ran (resp.ok says outcome)
}

// External-stack caller: publish EfReq, wait for the loop's serve, collect
// EfResp. Serialized by s_reqMutex; state transitions under s_mux.
bool efCall(EfReq& r, EfResp& out) {
    if (!s_reqMutex) return false;
    if (xSemaphoreTake(s_reqMutex, pdMS_TO_TICKS(kEfOpTimeoutMs)) != pdTRUE) return false;

    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    uint32_t start = millis();

    // Phase A: wait out Busy (a prior serve mid-op) and reclaim anything
    // left behind by a dead requester (we hold s_reqMutex, so Req/Resp here
    // is always an orphan older than the previous publisher).
    while (true) {
        bool reclaimed = false;
        EfRpcState st;
        portENTER_CRITICAL(&s_mux);
        st = s_mach.st;
        if ((st == EF_RPC_REQ || st == EF_RPC_RESP) &&
            EfRpcMachine::deadlineExceeded(millis(), s_req.ownerTs, kEfStaleOwnerMs)) {
            s_mach.forceIdle();
            reclaimed = true;
        }
        portEXIT_CRITICAL(&s_mux);

        if (st == EF_RPC_IDLE || reclaimed) break;
        if (EfRpcMachine::deadlineExceeded(millis(), start, kEfOpTimeoutMs)) {
            xSemaphoreGive(s_reqMutex);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // Phase B: publish payload (state Idle; serve never touches EfReq until
    // Req, and no other publisher exists — we hold s_reqMutex).
    r.owner = self;
    r.ownerTs = millis();
    s_req = r;
    portENTER_CRITICAL(&s_mux);
    s_mach.reqBegin();
    portEXIT_CRITICAL(&s_mux);

    // Phase C: poll for the response.
    bool success = false;
    int busyExtensions = 0;                       // bounded: never wait forever
    while (true) {
        bool taken = false;
        portENTER_CRITICAL(&s_mux);
        if (s_mach.st == EF_RPC_RESP) {
            s_mach.st = EF_RPC_IDLE;             // RESP->IDLE under mux, payload after
            taken = true;
        }
        portEXIT_CRITICAL(&s_mux);
        if (taken) {
            out = s_resp;                        // safe: state Idle, s_reqMutex held
            std::vector<std::pair<String, size_t>>().swap(s_resp.list); // drop retained capacity
            success = true;
            break;
        }

        if (EfRpcMachine::deadlineExceeded(millis(), start, kEfOpTimeoutMs)) {
            bool abandoned = false;
            portENTER_CRITICAL(&s_mux);
            abandoned = s_mach.abandonUnserved();
            portEXIT_CRITICAL(&s_mux);
            if (abandoned) break;                // still unserved: give up cleanly
            // Serve mid-op: give it a fresh deadline instead of cutting it —
            // but only a bounded number of times, so a wedged serve can never
            // pin this task forever. Force the slot idle as a last resort (the
            // serve's late serveFinish() then just fails; its result is lost).
            if (++busyExtensions > 4) {
                portENTER_CRITICAL(&s_mux);
                s_mach.forceIdle();
                portEXIT_CRITICAL(&s_mux);
                break;
            }
            start = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    xSemaphoreGive(s_reqMutex);
    return success;
}

// Shared preamble: build a request and run it either directly (internal
// stack) or through efCall (external stack).
bool efRun(EfReq& r, EfResp& resp) {
    if (!emailFlashCallerNeedsRpc()) {
        return efPerform(r, resp);
    }
    return efCall(r, resp);
}

} // namespace

void emailFlashRpcInit() {
    if (!s_reqMutex) s_reqMutex = xSemaphoreCreateMutex();
}

bool emailFlashCallerNeedsRpc() {
    // Same fact the IDF assert checks: where the running task's stack lives.
    // esp_cpu_get_sp() = current stack pointer (esp_cpu.h); esp_ptr_external_ram
    // covers PSRAM and any other external-RAM region.
    return esp_ptr_external_ram(esp_cpu_get_sp());
}

int32_t emailFlashOpenRead(const String& path) {
    EfReq r = {};
    r.op = EFOP_OPEN;
    r.h = -1;
    r.path = path;
    EfResp resp;
    if (!efRun(r, resp) || !resp.ok) return -1;
    return resp.h;
}

size_t emailFlashRead(int32_t h, uint8_t* buf, size_t len) {
    if (h < 0 || !buf || len == 0) return 0;
    EfReq r = {};
    r.op = EFOP_READ;
    r.h = h;
    r.u2 = (uint32_t)len;
    r.out = buf;
    EfResp resp;
    if (!efRun(r, resp) || !resp.ok) return 0;
    return resp.n;
}

bool emailFlashSeek(int32_t h, size_t pos) {
    if (h < 0) return false;
    EfReq r = {};
    r.op = EFOP_SEEK;
    r.h = h;
    r.u1 = (uint32_t)pos;
    EfResp resp;
    return efRun(r, resp) && resp.ok;
}

void emailFlashClose(int32_t h) {
    if (h < 0) return;
    EfReq r = {};
    r.op = EFOP_CLOSE;
    r.h = h;
    EfResp resp;
    efRun(r, resp);
}

bool emailFlashProbeSize(const String& path, size_t& outSize) {
    EfReq r = {};
    r.op = EFOP_PROBE;
    r.h = -1;
    r.path = path;
    EfResp resp;
    if (!efRun(r, resp) || !resp.ok) return false;
    outSize = resp.sz;
    return true;
}

bool emailFlashExists(const String& path) {
    EfReq r = {};
    r.op = EFOP_EXISTS;
    r.h = -1;
    r.path = path;
    EfResp resp;
    return efRun(r, resp) && resp.ok;
}

bool emailFlashWriteFile(const String& path, const uint8_t* data, size_t len) {
    EfReq r = {};
    r.op = EFOP_WRITE;
    r.h = -1;
    r.path = path;
    r.data = data;
    r.u1 = (uint32_t)len;
    EfResp resp;
    return efRun(r, resp) && resp.ok;
}

bool emailFlashRemove(const String& path) {
    EfReq r = {};
    r.op = EFOP_REMOVE;
    r.h = -1;
    r.path = path;
    EfResp resp;
    return efRun(r, resp) && resp.ok;
}

bool emailFlashRename(const String& from, const String& to) {
    EfReq r = {};
    r.op = EFOP_RENAME;
    r.h = -1;
    r.path = from;
    r.path2 = to;
    EfResp resp;
    return efRun(r, resp) && resp.ok;
}

bool emailFlashMkdir(const String& path) {
    EfReq r = {};
    r.op = EFOP_MKDIR;
    r.h = -1;
    r.path = path;
    EfResp resp;
    return efRun(r, resp) && resp.ok;
}

bool emailFlashListDir(const String& dir, std::vector<std::pair<String, size_t>>& out) {
    EfReq r = {};
    r.op = EFOP_LIST;
    r.h = -1;
    r.path = dir;
    EfResp resp;
    bool ran = efRun(r, resp);
    out = resp.list;
    return ran && resp.ok;
}

void emailFlashServe() {
    // Pick-up test is a single mux take — near-zero cost per loop() call.
    portENTER_CRITICAL(&s_mux);
    bool pick = s_mach.servePick();
    portEXIT_CRITICAL(&s_mux);
    if (!pick) return;

    // s_req is stable: state Busy keeps the publisher waiting and pinned to
    // s_reqMutex; efPerform serializes on lfsMutex like every other writer.
    EfResp resp;
    efPerform(s_req, resp);
    s_resp = resp;

    portENTER_CRITICAL(&s_mux);
    s_mach.serveFinish();
    portEXIT_CRITICAL(&s_mux);
}

#else // ENABLE_FILESYSTEM!=1

void emailFlashRpcInit() {}
bool emailFlashCallerNeedsRpc() { return false; }
int32_t emailFlashOpenRead(const String&) { return -1; }
size_t emailFlashRead(int32_t, uint8_t*, size_t) { return 0; }
bool emailFlashSeek(int32_t, size_t) { return false; }
void emailFlashClose(int32_t) {}
bool emailFlashProbeSize(const String&, size_t&) { return false; }
bool emailFlashExists(const String&) { return false; }
bool emailFlashWriteFile(const String&, const uint8_t*, size_t) { return false; }
bool emailFlashRemove(const String&) { return false; }
bool emailFlashRename(const String&, const String&) { return false; }
bool emailFlashMkdir(const String&) { return false; }
bool emailFlashListDir(const String&, std::vector<std::pair<String, size_t>>&) { return false; }
void emailFlashServe() {}

#endif // ENABLE_FILESYSTEM==1
