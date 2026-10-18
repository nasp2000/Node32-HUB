// Native tests for the email flash RPC core (chatgpt/src/email_flash_rpc_core.h).
//
// Background (HISTORIC/HomeE32 (8), 2026-10-15/16): the email tasks
// (emailTask 16KB, emailQueueProc 32KB) keep PSRAM stacks to hold the ~170KB
// internal-heap budget. A PSRAM stack must never perform a flash op (IDF
// asserts esp_task_stack_is_sane_cache_disabled), so their LittleFS requests
// are published to a single request slot and served by the loop task.
//
// These tests drive the EXACT production state machine:
//   * legal transitions Idle->Req->Busy->Resp->Idle,
//   * illegal transitions are rejected and never corrupt the state,
//   * a Busy request cannot be abandoned (the serve owns it),
//   * stale-owner reclaim / last-resort forceIdle behaviour,
//   * wrap-safe millis() deadline math.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "email_flash_rpc_core.h"

// ---------------------------------------------------------------------------
// Full happy path: requester publishes, the serve picks it up, finishes, and
// the requester consumes the response. Each step must be exactly once.
// ---------------------------------------------------------------------------
TEST_CASE("machine: full happy path Idle->Req->Busy->Resp->Idle") {
    EfRpcMachine m;
    CHECK(m.st == EF_RPC_IDLE);

    CHECK(m.reqBegin() == true);
    CHECK(m.st == EF_RPC_REQ);

    CHECK(m.servePick() == true);
    CHECK(m.st == EF_RPC_BUSY);

    CHECK(m.serveFinish() == true);
    CHECK(m.st == EF_RPC_RESP);

    CHECK(m.respTake() == true);
    CHECK(m.st == EF_RPC_IDLE);

    // Slot is reusable immediately after the full cycle.
    CHECK(m.reqBegin() == true);
    CHECK(m.st == EF_RPC_REQ);
}

TEST_CASE("machine: illegal transitions are rejected and leave the state intact") {
    // From Idle, only reqBegin is legal.
    {
        EfRpcMachine m;
        CHECK(m.servePick() == false);
        CHECK(m.serveFinish() == false);
        CHECK(m.respTake() == false);
        CHECK(m.abandonUnserved() == false);
        CHECK(m.forceIdle() == false);   // already idle: reports "nothing to do"
        CHECK(m.st == EF_RPC_IDLE);
    }
    // From Req: servePick and abandonUnserved are legal; nothing else.
    {
        EfRpcMachine m;
        m.reqBegin();
        CHECK(m.reqBegin() == false);     // double publish rejected
        CHECK(m.serveFinish() == false);
        CHECK(m.respTake() == false);
        CHECK(m.st == EF_RPC_REQ);
        CHECK(m.abandonUnserved() == true);
        CHECK(m.st == EF_RPC_IDLE);
    }
    // From Busy: only serveFinish (and the last-resort forceIdle) are legal.
    {
        EfRpcMachine m;
        m.reqBegin();
        m.servePick();
        CHECK(m.reqBegin() == false);
        CHECK(m.servePick() == false);
        CHECK(m.respTake() == false);
        CHECK(m.abandonUnserved() == false);  // serve mid-op: cannot abandon
        CHECK(m.st == EF_RPC_BUSY);
        CHECK(m.serveFinish() == true);
        CHECK(m.st == EF_RPC_RESP);
    }
    // From Resp: only respTake (and forceIdle) are legal.
    {
        EfRpcMachine m;
        m.reqBegin();
        m.servePick();
        m.serveFinish();
        CHECK(m.reqBegin() == false);
        CHECK(m.servePick() == false);
        CHECK(m.serveFinish() == false);
        CHECK(m.abandonUnserved() == false);
        CHECK(m.st == EF_RPC_RESP);
        CHECK(m.respTake() == true);
        CHECK(m.st == EF_RPC_IDLE);
    }
}

// ---------------------------------------------------------------------------
// Abandon rules: a request that was never served can be abandoned (requester
// timeout); one the loop already picked up cannot (the op is in flight).
// ---------------------------------------------------------------------------
TEST_CASE("abandon: unserved request is abandoned cleanly, once") {
    EfRpcMachine m;
    m.reqBegin();
    CHECK(m.abandonUnserved() == true);
    CHECK(m.st == EF_RPC_IDLE);
    // Second abandon is a no-op (state is Idle, not Req).
    CHECK(m.abandonUnserved() == false);
}

TEST_CASE("abandon: Busy request refuses abandon; a late serveFinish then completes") {
    EfRpcMachine m;
    m.reqBegin();
    m.servePick();                          // loop took it: Busy
    CHECK(m.abandonUnserved() == false);    // requester must keep waiting
    CHECK(m.st == EF_RPC_BUSY);
    CHECK(m.serveFinish() == true);         // serve completes normally
    CHECK(m.st == EF_RPC_RESP);
    CHECK(m.respTake() == true);
}

// ---------------------------------------------------------------------------
// Recovery paths used by efCall():
//   * stale-owner reclaim (Req/Resp older than the stale limit) -> forceIdle
//   * last-resort forceIdle while Busy (bounded busy-wait cap)
// forceIdle must work from every non-idle state, and a serve that finishes
// after a forceIdle simply fails (its result is discarded).
// ---------------------------------------------------------------------------
TEST_CASE("recovery: forceIdle resets from Req/Busy/Resp and reports each time") {
    for (int st = 0; st < 3; ++st) {
        EfRpcMachine m;
        m.reqBegin();                       // Req
        if (st >= 1) m.servePick();         // Busy
        if (st >= 2) m.serveFinish();       // Resp

        EfRpcState before = m.st;
        CHECK(before != EF_RPC_IDLE);
        CHECK(m.forceIdle() == true);
        CHECK(m.st == EF_RPC_IDLE);
        CHECK(m.forceIdle() == false);      // idempotent
    }
}

TEST_CASE("recovery: serveFinish after a forced reset is rejected (result dropped)") {
    EfRpcMachine m;
    m.reqBegin();
    m.servePick();                          // Busy
    CHECK(m.forceIdle() == true);           // requester gave up (wedged serve)
    CHECK(m.serveFinish() == false);        // late serve cannot publish
    CHECK(m.st == EF_RPC_IDLE);
    // Machine is healthy again for the next request.
    CHECK(m.reqBegin() == true);
}

// ---------------------------------------------------------------------------
// Deadline math: millis() wraps at 2^32, subtraction must stay correct.
// ---------------------------------------------------------------------------
TEST_CASE("deadline: normal and wrap-around arithmetic") {
    // Not yet expired.
    CHECK(EfRpcMachine::deadlineExceeded(1000, 0, 20000) == false);
    // Exactly at the limit counts as expired (>=).
    CHECK(EfRpcMachine::deadlineExceeded(20000, 0, 20000) == true);
    CHECK(EfRpcMachine::deadlineExceeded(20001, 0, 20000) == true);

    // Wrap: start near the top, now just past the wrap -> 100ms elapsed.
    uint32_t start = 0xFFFFFF00u;           // 256ms before wrap
    uint32_t now   = 0x00000064u;           // 100ms after wrap
    CHECK(EfRpcMachine::deadlineExceeded(now, start, 20000) == false); // 356ms elapsed
    CHECK(EfRpcMachine::deadlineExceeded(now, start, 300) == true);    // > 300ms elapsed

    // Identical timestamps: never expired for a non-zero limit.
    CHECK(EfRpcMachine::deadlineExceeded(12345, 12345, 1) == false);
}
