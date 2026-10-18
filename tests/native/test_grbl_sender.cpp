#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <string.h>
#include <stdlib.h>

// ── Standalone reimplementation of GRBL sender pure logic ──────────────
// Tests state machine transitions, FIFO operations, and flow-control
// arithmetic without any ESP32/FreeRTOS dependency.

// ── Constants ─────────────────────────────────────────────────────────
#define GRBL_INFLIGHT_FIFO_SIZE 64
#define GRBL_MAX_LINE_LEN 96
#define GRBL_RX_BUFFER_SIZE 127
#define GRBL_RX_SAFETY_MARGIN_LOCAL_QUEUE 10
#define GRBL_RX_STARTUP_BURST_LIMIT 200
#define GRBL_RX_STRICT_LIMIT 127
#define GRBL_RX_STREAM_INFLIGHT_LIMIT 200

// ── Workflow state machine ────────────────────────────────────────────
#ifdef ERROR
#undef ERROR
#endif
enum class GrblWorkflowState : uint8_t {
    IDLE, RUNNING, PAUSED, HOLD, CANCELLING, DONE, ERROR
};

static bool isWorkflowTransitionAllowed(GrblWorkflowState from, GrblWorkflowState to) {
    if (from == to) return true;
    switch (from) {
        case GrblWorkflowState::IDLE:
            return to == GrblWorkflowState::RUNNING || to == GrblWorkflowState::ERROR;
        case GrblWorkflowState::RUNNING:
            return to == GrblWorkflowState::HOLD || to == GrblWorkflowState::PAUSED ||
                   to == GrblWorkflowState::CANCELLING || to == GrblWorkflowState::DONE ||
                   to == GrblWorkflowState::ERROR;
        case GrblWorkflowState::PAUSED:
            return to == GrblWorkflowState::RUNNING || to == GrblWorkflowState::CANCELLING ||
                   to == GrblWorkflowState::ERROR || to == GrblWorkflowState::DONE;
        case GrblWorkflowState::HOLD:
            return to == GrblWorkflowState::PAUSED || to == GrblWorkflowState::RUNNING ||
                   to == GrblWorkflowState::CANCELLING || to == GrblWorkflowState::ERROR;
        case GrblWorkflowState::CANCELLING:
            return to == GrblWorkflowState::IDLE || to == GrblWorkflowState::ERROR;
        case GrblWorkflowState::DONE:
            return to == GrblWorkflowState::IDLE || to == GrblWorkflowState::RUNNING;
        case GrblWorkflowState::ERROR:
            return to == GrblWorkflowState::IDLE || to == GrblWorkflowState::RUNNING;
        default:
            return false;
    }
}

static const char* workflowStateName(GrblWorkflowState s) {
    switch (s) {
        case GrblWorkflowState::IDLE:       return "idle";
        case GrblWorkflowState::RUNNING:    return "running";
        case GrblWorkflowState::PAUSED:     return "paused";
        case GrblWorkflowState::HOLD:       return "hold";
        case GrblWorkflowState::CANCELLING: return "cancelling";
        case GrblWorkflowState::DONE:       return "done";
        case GrblWorkflowState::ERROR:      return "error";
        default:                            return "unknown";
    }
}

// ── FIFO (circular buffer for in-flight line tracking) ────────────────
static uint8_t s_fifo[GRBL_INFLIGHT_FIFO_SIZE];
static uint8_t s_fifoHead = 0;
static uint8_t s_fifoTail = 0;
static uint8_t s_fifoCount = 0;
static uint16_t s_inflight = 0;

static void fifoReset() {
    s_fifoHead = 0;
    s_fifoTail = 0;
    s_fifoCount = 0;
    s_inflight = 0;
}

static bool fifoIsFull() { return s_fifoCount >= GRBL_INFLIGHT_FIFO_SIZE; }
static bool fifoIsEmpty() { return s_fifoCount == 0; }

static bool fifoPush(uint8_t lineBytes) {
    if (fifoIsFull()) return false;
    s_fifo[s_fifoHead] = lineBytes;
    s_fifoHead = (s_fifoHead + 1) % GRBL_INFLIGHT_FIFO_SIZE;
    s_fifoCount++;
    s_inflight += lineBytes;
    return true;
}

static uint8_t fifoPop() {
    if (fifoIsEmpty()) return 0;
    uint8_t bytes = s_fifo[s_fifoTail];
    s_fifoTail = (s_fifoTail + 1) % GRBL_INFLIGHT_FIFO_SIZE;
    s_fifoCount--;
    s_inflight -= bytes;
    return bytes;
}

// ── Flow control helpers ──────────────────────────────────────────────
static bool canSendLine(uint16_t lineBytes, uint16_t inflight,
                        uint16_t safetyMargin, bool startupBurst) {
    uint16_t limit = startupBurst ? GRBL_RX_STARTUP_BURST_LIMIT : GRBL_RX_STRICT_LIMIT;
    return (inflight + lineBytes + 1) <= limit - safetyMargin;
}

// Sustained streaming allowance — mirrors grbl_sender_stream.cpp: the PSRAM
// feeder keeps this limit for the whole job so GRBL's planner stays full.
static bool canSendStreamLine(uint16_t lineBytes, uint16_t inflight, uint16_t safetyMargin) {
    return ((uint32_t)inflight + (uint32_t)lineBytes + 1U) <=
           ((uint32_t)GRBL_RX_STREAM_INFLIGHT_LIMIT - (uint32_t)safetyMargin);
}

// ── Test cases ───────────────────────────────────────────────────────────

// ── Workflow transitions (15 "from→to" allowed, 20+ disallowed) ──────

TEST_CASE("Workflow: IDLE can transition to RUNNING and ERROR") {
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::IDLE, GrblWorkflowState::RUNNING));
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::IDLE, GrblWorkflowState::ERROR));
}

TEST_CASE("Workflow: IDLE cannot transition to PAUSED/HOLD/CANCELLING/DONE") {
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::IDLE, GrblWorkflowState::PAUSED));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::IDLE, GrblWorkflowState::HOLD));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::IDLE, GrblWorkflowState::CANCELLING));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::IDLE, GrblWorkflowState::DONE));
}

TEST_CASE("Workflow: RUNNING can transition to HOLD/PAUSED/CANCELLING/DONE/ERROR") {
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::RUNNING, GrblWorkflowState::HOLD));
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::RUNNING, GrblWorkflowState::PAUSED));
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::RUNNING, GrblWorkflowState::CANCELLING));
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::RUNNING, GrblWorkflowState::DONE));
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::RUNNING, GrblWorkflowState::ERROR));
}

TEST_CASE("Workflow: RUNNING cannot go back to IDLE") {
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::RUNNING, GrblWorkflowState::IDLE));
}

TEST_CASE("Workflow: PAUSED can go to RUNNING/CANCELLING/ERROR/DONE") {
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::PAUSED, GrblWorkflowState::RUNNING));
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::PAUSED, GrblWorkflowState::CANCELLING));
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::PAUSED, GrblWorkflowState::ERROR));
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::PAUSED, GrblWorkflowState::DONE));
}

TEST_CASE("Workflow: PAUSED cannot go to IDLE or HOLD") {
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::PAUSED, GrblWorkflowState::IDLE));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::PAUSED, GrblWorkflowState::HOLD));
}

TEST_CASE("Workflow: HOLD can transition to PAUSED/RUNNING/CANCELLING/ERROR") {
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::HOLD, GrblWorkflowState::PAUSED));
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::HOLD, GrblWorkflowState::RUNNING));
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::HOLD, GrblWorkflowState::CANCELLING));
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::HOLD, GrblWorkflowState::ERROR));
}

TEST_CASE("Workflow: HOLD cannot go to IDLE or DONE") {
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::HOLD, GrblWorkflowState::IDLE));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::HOLD, GrblWorkflowState::DONE));
}

TEST_CASE("Workflow: CANCELLING can only go to IDLE or ERROR") {
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::CANCELLING, GrblWorkflowState::IDLE));
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::CANCELLING, GrblWorkflowState::ERROR));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::CANCELLING, GrblWorkflowState::RUNNING));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::CANCELLING, GrblWorkflowState::PAUSED));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::CANCELLING, GrblWorkflowState::HOLD));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::CANCELLING, GrblWorkflowState::DONE));
}

TEST_CASE("Workflow: DONE can go back to IDLE or RUNNING") {
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::DONE, GrblWorkflowState::IDLE));
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::DONE, GrblWorkflowState::RUNNING));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::DONE, GrblWorkflowState::PAUSED));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::DONE, GrblWorkflowState::HOLD));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::DONE, GrblWorkflowState::CANCELLING));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::DONE, GrblWorkflowState::ERROR));
}

TEST_CASE("Workflow: ERROR can go to IDLE or RUNNING") {
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::ERROR, GrblWorkflowState::IDLE));
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::ERROR, GrblWorkflowState::RUNNING));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::ERROR, GrblWorkflowState::PAUSED));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::ERROR, GrblWorkflowState::HOLD));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::ERROR, GrblWorkflowState::CANCELLING));
    CHECK_FALSE(isWorkflowTransitionAllowed(GrblWorkflowState::ERROR, GrblWorkflowState::DONE));
}

TEST_CASE("Workflow: same-state transitions are always allowed") {
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::IDLE, GrblWorkflowState::IDLE));
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::RUNNING, GrblWorkflowState::RUNNING));
    CHECK(isWorkflowTransitionAllowed(GrblWorkflowState::ERROR, GrblWorkflowState::ERROR));
}

TEST_CASE("Workflow: state names match expected strings") {
    CHECK(strcmp(workflowStateName(GrblWorkflowState::IDLE), "idle") == 0);
    CHECK(strcmp(workflowStateName(GrblWorkflowState::RUNNING), "running") == 0);
    CHECK(strcmp(workflowStateName(GrblWorkflowState::DONE), "done") == 0);
    CHECK(strcmp(workflowStateName(GrblWorkflowState::ERROR), "error") == 0);
}

// ── FIFO tests (13 cases) ─────────────────────────────────────────────

TEST_CASE("FIFO: starts empty") {
    fifoReset();
    CHECK(fifoIsEmpty());
    CHECK_FALSE(fifoIsFull());
    CHECK(s_inflight == 0);
}

TEST_CASE("FIFO: push one item") {
    fifoReset();
    CHECK(fifoPush(50));
    CHECK_FALSE(fifoIsEmpty());
    CHECK(s_inflight == 50);
}

TEST_CASE("FIFO: push then pop") {
    fifoReset();
    fifoPush(50);
    uint8_t bytes = fifoPop();
    CHECK(bytes == 50);
    CHECK(fifoIsEmpty());
    CHECK(s_inflight == 0);
}

TEST_CASE("FIFO: push multiple items, inflight sums correctly") {
    fifoReset();
    CHECK(fifoPush(30));
    CHECK(fifoPush(40));
    CHECK(fifoPush(50));
    CHECK(s_inflight == 120);
    CHECK(s_fifoCount == 3);
}

TEST_CASE("FIFO: pop returns correct order (FIFO)") {
    fifoReset();
    fifoPush(10); fifoPush(20); fifoPush(30);
    CHECK(fifoPop() == 10);
    CHECK(fifoPop() == 20);
    CHECK(fifoPop() == 30);
    CHECK(fifoIsEmpty());
}

TEST_CASE("FIFO: pop from empty returns 0") {
    fifoReset();
    CHECK(fifoPop() == 0);
}

TEST_CASE("FIFO: push to full returns false") {
    fifoReset();
    for (int i = 0; i < GRBL_INFLIGHT_FIFO_SIZE; i++) {
        CHECK(fifoPush(10));
    }
    CHECK(fifoIsFull());
    CHECK_FALSE(fifoPush(10));
}

TEST_CASE("FIFO: wrap-around behavior") {
    fifoReset();
    // Fill the FIFO
    for (int i = 0; i < GRBL_INFLIGHT_FIFO_SIZE; i++) fifoPush(10);
    // Pop all
    for (int i = 0; i < GRBL_INFLIGHT_FIFO_SIZE; i++) fifoPop();
    CHECK(fifoIsEmpty());
    // Push again (tests wrap-around)
    CHECK(fifoPush(99));
    CHECK(s_inflight == 99);
    CHECK(fifoPop() == 99);
}

TEST_CASE("FIFO: partial fill and drain") {
    fifoReset();
    fifoPush(5); fifoPush(10); fifoPush(15);
    CHECK(fifoPop() == 5);
    CHECK(fifoPop() == 10);
    fifoPush(20);
    CHECK(fifoPop() == 15);
    CHECK(fifoPop() == 20);
    CHECK(fifoIsEmpty());
}

TEST_CASE("FIFO: max inflight limit") {
    fifoReset();
    uint16_t total = 0;
    for (int i = 0; i < GRBL_INFLIGHT_FIFO_SIZE; i++) {
        fifoPush(127);
        total += 127;
    }
    CHECK(s_inflight == total);
    // All bytes are tracked
    CHECK(s_inflight == (uint16_t)(GRBL_INFLIGHT_FIFO_SIZE * 127));
}

// ── M6 Tool Change detection ─────────────────────────────────────────
static bool isToolChangeLine(const char* line) {
    for (const char* p = line; *p; ++p) {
        if (*p == 'M' || *p == 'm') {
            if (*(p+1) == '6') {
                char a = *(p+2);
                if (a == '\0' || a == ' ' || a == '\t' || a == '(' || a == ';') return true;
            }
            if (*(p+1) == '0' && (*(p+2) == '6' || *(p+2) == '6')) {
                char a = *(p+3);
                if (a == '\0' || a == ' ' || a == '\t' || a == '(' || a == ';') return true;
            }
        }
    }
    return false;
}

TEST_CASE("M6: detects M6 and M06 tool change commands") {
    CHECK(isToolChangeLine("M6"));
    CHECK(isToolChangeLine("M6 T1"));
    CHECK(isToolChangeLine("M06 T1"));
    CHECK(isToolChangeLine("G0 X10 M6"));
    CHECK(isToolChangeLine("M06"));
    CHECK(isToolChangeLine("M6(T1)"));
    CHECK(isToolChangeLine("m6"));
    CHECK_FALSE(isToolChangeLine("M60"));
    CHECK_FALSE(isToolChangeLine("M600"));
    CHECK_FALSE(isToolChangeLine("G0 X10"));
    CHECK_FALSE(isToolChangeLine("G43 H1"));
    CHECK_FALSE(isToolChangeLine("M600 T1"));
}

// ── Flow control tests (5 cases) ──────────────────────────────────────

TEST_CASE("Flow: canSendLine allows within strict limit") {
    CHECK(canSendLine(50, 0, 0, false));     // 50+1 <= 127
    CHECK(canSendLine(126, 0, 0, false));    // 126+1 <= 127
}

TEST_CASE("Flow: canSendLine blocks when over strict limit") {
    CHECK_FALSE(canSendLine(127, 0, 0, false));   // 127+1 > 127
    CHECK_FALSE(canSendLine(50, 80, 0, false));   // 50+80+1 > 127
}

TEST_CASE("Flow: canSendLine respects safety margin") {
    CHECK_FALSE(canSendLine(120, 0, 10, false));  // 120+1 > 127-10
    CHECK(canSendLine(100, 0, 10, false));         // 100+1 <= 127-10=117
}

TEST_CASE("Flow: startup burst allows larger limit") {
    CHECK(canSendLine(199, 0, 0, true));   // 199+1 <= 200
    CHECK_FALSE(canSendLine(200, 0, 0, true)); // 200+1 > 200
}

TEST_CASE("Flow: sustained stream allowance keeps planner fed") {
    // The PSRAM feeder now uses the planner-filling allowance for the WHOLE
    // job, not only the first batch. A line near the 127 strict limit must fit.
    CHECK(canSendStreamLine(199, 0, 0));        // fills planner, 199+1 <= 200
    CHECK_FALSE(canSendStreamLine(200, 0, 0));  // 200+1 > 200
    CHECK(canSendStreamLine(50, 120, 0));       // 120+50+1 <= 200: no deadlock
}

TEST_CASE("Flow: stream allowance does not deadlock on leftover budget") {
    // Regression for the 90s stall: under the old 127-byte cap, inflight=120
    // left a 7-byte budget that the next line could not fit, freezing the
    // feeder while GRBL executed without acking. The 200-byte allowance keeps
    // headroom so a 50-byte line always fits.
    CHECK(canSendStreamLine(50, 120, 0));
    CHECK_FALSE(canSendStreamLine(50, 240, 0));  // beyond allowance, blocks (correct backpressure)
}

TEST_CASE("Flow: inflight tracking prevents overflow on real values") {
    fifoReset();
    // Simulate typical GRBL usage: lines of ~50 bytes
    uint16_t sent = 0;
    uint16_t acked = 0;
    while (sent < 500) {
        if (canSendLine(50, s_inflight, 0, sent < 5)) {
            fifoPush(50);
            sent++;
        } else {
            // "GRBL" acks a line
            if (!fifoIsEmpty()) {
                fifoPop();
                acked++;
            } else {
                break;
            }
        }
    }
    CHECK(sent >= 2);  // At least some lines sent
    CHECK(s_inflight <= GRBL_RX_STRICT_LIMIT);
}

// ── Feeder budget clamp (psramFeedTask remainingBudget) ────────────────
// Mirrors grbl_sender_stream.cpp: activeLimit - inflight must clamp to 0
// instead of underflowing when inflight exceeds the limit (possible right
// after the 200-byte startup burst batch).
static uint16_t computeRemainingBudget(uint16_t inflight, uint16_t activeLimit) {
    return (inflight >= activeLimit) ? 0U : (uint16_t)(activeLimit - inflight);
}

TEST_CASE("Budget: normal case returns the free capacity") {
    CHECK(computeRemainingBudget(0, 200) == 200);
    CHECK(computeRemainingBudget(50, 127) == 77);
}

TEST_CASE("Budget: exactly at limit returns 0") {
    CHECK(computeRemainingBudget(200, 200) == 0);
    CHECK(computeRemainingBudget(127, 127) == 0);
}

TEST_CASE("Budget: exceeding limit clamps to 0, never underflows") {
    // Old arithmetic (activeLimit - inflight as uint16) underflowed to
    // ~65482 after the 200-byte startup batch, which would over-fill the
    // GRBL FIFO/RX buffer.
    CHECK(computeRemainingBudget(200, 127) == 0);
    CHECK(computeRemainingBudget(300, 127) == 0);
}
