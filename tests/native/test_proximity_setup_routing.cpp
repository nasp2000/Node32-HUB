#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstdint>

// Standalone reimpl of the setup-routing decision from proximity_manager.cpp:
// when a channel has a gate_ch configured and that gate PRESENCE channel's state
// is OFF, incoming pulses route to setup_count (NOT production count_in) — UNLESS
// the channel is a scrap channel (referenced as scrap_ch by another channel), in
// which case gate-OFF pulses are DISCARDED (neither setup_count nor count_in).
// When the gate is ON (or no gate configured) pulses route to count_in. total_in
// always counts every pulse (it is the non-resettable lifetime total).

namespace routing {

struct Ch {
    uint8_t gate_ch = 255;   // 255 = no gate
    bool    gate_state = false; // PRESENCE state of the gate channel
    bool    is_scrap = false;   // channel is the scrap target of a production channel
    uint32_t count_in = 0;
    uint32_t setup_count = 0;
    uint32_t total_in = 0;
};

// Mirrors firmware: gateOff is only true when (gate_ch valid && != channel index)
// && gate state==false. Also note config clamps gate_ch==i to 255 at load time.
void applyPulse(Ch& ch, uint8_t chIndex, uint32_t dMul) {
    bool gateOff = false;
    if (ch.gate_ch < 8 && ch.gate_ch != chIndex) {
        gateOff = !ch.gate_state;
    }
    if (gateOff) {
        // non-scrap -> setup; scrap -> discard
        if (!ch.is_scrap) {
            ch.setup_count += dMul;
        }
    } else {
        ch.count_in += dMul;
    }
    ch.total_in += dMul;
}

} // namespace routing

using routing::Ch;

TEST_CASE("setup: no gate configured -> pulses go to count_in") {
    Ch c;
    applyPulse(c, 0, 5);
    CHECK(c.count_in == 5);
    CHECK(c.setup_count == 0);
    CHECK(c.total_in == 5);
}

TEST_CASE("setup: gate OFF routes pulses to setup_count") {
    Ch c;
    c.gate_ch = 2;        // DI3 gates DI1
    c.gate_state = false; // gate OFF
    applyPulse(c, 0, 3);
    CHECK(c.setup_count == 3);
    CHECK(c.count_in == 0);
    CHECK(c.total_in == 3); // lifetime total always counts
}

TEST_CASE("setup: gate ON routes pulses back to count_in") {
    Ch c;
    c.gate_ch = 2;
    c.gate_state = false;
    applyPulse(c, 0, 3);   // while gate OFF -> setup
    CHECK(c.setup_count == 3);
    c.gate_state = true; // gate opens -> count returns to DI1
    applyPulse(c, 0, 7);
    CHECK(c.setup_count == 3);
    CHECK(c.count_in == 7);
    CHECK(c.total_in == 10);
}

TEST_CASE("setup: gate_ch == channel index is treated as no gate (self-ref guard)") {
    Ch c;
    c.gate_ch = 0; // self-reference for ch0 -> the gate-ch == i guard disables routing
    c.gate_state = false;
    applyPulse(c, 0, 4);
    CHECK(c.count_in == 4);
    CHECK(c.setup_count == 0);
}

TEST_CASE("setup: multiplier is applied before routing (dMul)") {
    Ch c;
    c.gate_ch = 1;
    c.gate_state = false;
    // 2 pulses x multiplier 4 = 8 -> routed to setup
    applyPulse(c, 0, 8);
    CHECK(c.setup_count == 8);
    CHECK(c.count_in == 0);
}

TEST_CASE("scrap: gate OFF discards pulses (no setup, no count_in)") {
    Ch c;
    c.gate_ch = 4;        // DI5 gates DI2 scrap
    c.gate_state = false; // gate OFF
    c.is_scrap = true;    // referenced as scrap_ch by a production channel
    applyPulse(c, 1, 3);
    CHECK(c.setup_count == 0); // discarded, NOT routed to setup
    CHECK(c.count_in == 0);
    CHECK(c.total_in == 3);    // lifetime total always counts
}

TEST_CASE("scrap: gate ON routes pulses to count_in") {
    Ch c;
    c.gate_ch = 4;
    c.gate_state = false;
    c.is_scrap = true;
    applyPulse(c, 1, 3);   // gate OFF -> discarded
    CHECK(c.setup_count == 0);
    CHECK(c.count_in == 0);
    c.gate_state = true;   // gate opens -> scrap counts
    applyPulse(c, 1, 5);
    CHECK(c.setup_count == 0);
    CHECK(c.count_in == 5);
    CHECK(c.total_in == 8);
}

TEST_CASE("scrap: no gate configured still counts to count_in") {
    Ch c;
    c.gate_ch = 255; // no gate
    c.is_scrap = true;
    applyPulse(c, 1, 4);
    CHECK(c.count_in == 4);
    CHECK(c.setup_count == 0);
}

TEST_CASE("scrap: DI1 non-scrap gate OFF still routes to setup (regression)") {
    Ch c;
    c.gate_ch = 4;
    c.gate_state = false; // gate OFF
    c.is_scrap = false;   // production channel, NOT scrap
    applyPulse(c, 0, 6);
    CHECK(c.setup_count == 6);  // unchanged behaviour preserved
    CHECK(c.count_in == 0);
    CHECK(c.total_in == 6);
}

TEST_CASE("scrap: DI1/DI2 gate ON both route to count_in") {
    Ch c1;
    c1.gate_ch = 4;
    c1.gate_state = true; // gate ON
    c1.is_scrap = false;  // DI1 production
    applyPulse(c1, 0, 7);
    CHECK(c1.count_in == 7);
    CHECK(c1.setup_count == 0);

    Ch c2;
    c2.gate_ch = 4;
    c2.gate_state = true; // gate ON
    c2.is_scrap = true;   // DI2 scrap
    applyPulse(c2, 1, 2);
    CHECK(c2.count_in == 2);
    CHECK(c2.setup_count == 0);
}

TEST_CASE("inverted gate: sensor physically active (state true after optocoupler inversion) -> production") {
    Ch c;
    c.gate_ch = 2;        // DI3 is the PRESENCE gate (optocoupler inverts: GPIO LOW -> state true)
    c.gate_state = true;  // sensor active -> state true after inversion
    applyPulse(c, 0, 5);
    CHECK(c.count_in == 5);
    CHECK(c.setup_count == 0);
    CHECK(c.total_in == 5);
}

TEST_CASE("inverted gate: sensor physically inactive (state false after optocoupler inversion) -> setup") {
    Ch c;
    c.gate_ch = 2;        // DI3 is the PRESENCE gate
    c.gate_state = false; // sensor inactive -> state false after inversion
    applyPulse(c, 0, 4);
    CHECK(c.count_in == 0);
    CHECK(c.setup_count == 4);
    CHECK(c.total_in == 4);
}