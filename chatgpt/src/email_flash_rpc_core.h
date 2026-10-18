// email_flash_rpc_core.h
// Pure core of the email flash RPC: single-slot request state machine +
// deadline rules. NO ESP/FreeRTOS/Arduino dependencies so the native tests
// (tests/native/test_email_flash_rpc.cpp) drive the exact production
// transitions; the platform wiring (portMUX, lfsMutex, LittleFS ops) lives in
// email_flash_rpc.cpp.
//
// WHY THIS EXISTS (HISTORIC/HomeE32 (8) 2026-10-15):
// ESP-IDF asserts esp_task_stack_is_sane_cache_disabled() on every flash
// operation (cache disable). A task whose stack lives in PSRAM fails that
// assert -> panic_abort. The email tasks (emailTask, emailQueueProc) keep
// PSRAM stacks for the ~170KB heap budget, so their LittleFS requests are
// published here and served by the loop task (internal stack).
//
// Transitions (single requester slot, serialized by the request mutex):
//   Idle  --reqBegin()-->    Req   (requester published, waiting)
//   Req   --servePick()-->   Busy  (loop picked the request)
//   Busy  --serveFinish()--> Resp   (response ready)
//   Resp  --respTake()-->    Idle   (requester consumed the response)
//   Req   --abandonUnserved()--> Idle (requester timeout BEFORE serve)
// A Busy request can never be abandoned (the serve is mid-operation); the
// requester keeps waiting until Resp. A response left behind by a dead
// requester is reclaimed by stale-owner age (deadlineExceeded >= stale limit).

#pragma once

#include <stdint.h>

enum EfRpcState : uint8_t {
    EF_RPC_IDLE = 0,
    EF_RPC_REQ,
    EF_RPC_BUSY,
    EF_RPC_RESP,
};

struct EfRpcMachine {
    EfRpcState st;

    EfRpcMachine() : st(EF_RPC_IDLE) {}

    // Requester: publish a new request (fails while another call is live).
    bool reqBegin() {
        if (st != EF_RPC_IDLE) return false;
        st = EF_RPC_REQ;
        return true;
    }

    // Serve (loop): pick up a published request.
    bool servePick() {
        if (st != EF_RPC_REQ) return false;
        st = EF_RPC_BUSY;
        return true;
    }

    // Serve (loop): publish the response for the waiting requester.
    bool serveFinish() {
        if (st != EF_RPC_BUSY) return false;
        st = EF_RPC_RESP;
        return true;
    }

    // Requester: consume the response and free the slot.
    bool respTake() {
        if (st != EF_RPC_RESP) return false;
        st = EF_RPC_IDLE;
        return true;
    }

    // Requester: timeout abandon — ONLY legal while the request is still
    // unserved (Req). Once the serve picked it up the caller must wait.
    bool abandonUnserved() {
        if (st != EF_RPC_REQ) return false;
        st = EF_RPC_IDLE;
        return true;
    }

    // Last-resort reset (used by stale-owner reclaim and by the requester's
    // bounded busy-wait cap): returns the slot to Idle from ANY state. A serve
    // that is mid-op will simply fail its later serveFinish(); its response is
    // discarded and the requester reports failure. Returns false when already
    // Idle so callers can tell "nothing to do".
    bool forceIdle() {
        if (st == EF_RPC_IDLE) return false;
        st = EF_RPC_IDLE;
        return true;
    }

    // Unsigned millis() deadline (wrap-safe: subtraction is mod 2^32).
    static bool deadlineExceeded(uint32_t nowMs, uint32_t startMs, uint32_t limitMs) {
        return (uint32_t)(nowMs - startMs) >= limitMs;
    }
};
