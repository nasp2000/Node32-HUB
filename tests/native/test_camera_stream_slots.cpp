#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// Compiles the PRODUCTION header directly (strategy 3: compile prod source).
// camera_stream_slots.h is the MJPEG frame ring: the writer publishes into a
// slot no viewer is sending from, and each viewer pins the newest slot it has
// not sent yet. v1.120 had one slot and stopped publishing for ANY pin, which
// is what made a second viewer fatal (CAM-75 -> CAM-76).
#include "../../chatgpt/src/camera_stream_slots.h"

TEST_CASE("write slot: round-robin skips pinned slots and reports a full ring") {
    int refs[CAM_STREAM_SLOTS] = {0};
    // From cursor -1 (fresh session) the writer starts at slot 0.
    CHECK(camStreamPickWriteSlot(refs, CAM_STREAM_SLOTS, -1) == 0);
    // Then it walks forward: 1, 2, then wraps to 0.
    CHECK(camStreamPickWriteSlot(refs, CAM_STREAM_SLOTS, 0) == 1);
    CHECK(camStreamPickWriteSlot(refs, CAM_STREAM_SLOTS, 1) == 2);
    CHECK(camStreamPickWriteSlot(refs, CAM_STREAM_SLOTS, 2) == 0);

    // A viewer sending from slot 1: the writer goes around it.
    refs[1] = 1;
    CHECK(camStreamPickWriteSlot(refs, CAM_STREAM_SLOTS, 0) == 2);
    CHECK(camStreamPickWriteSlot(refs, CAM_STREAM_SLOTS, 2) == 0);
    CHECK(camStreamPickWriteSlot(refs, CAM_STREAM_SLOTS, 1) == 2);

    // Both remaining slots pinned as well: nothing to write into (drop frame).
    refs[0] = 1;
    refs[2] = 1;
    CHECK(camStreamPickWriteSlot(refs, CAM_STREAM_SLOTS, 0) == -1);

    // Two viewers sharing one slot still leave a free one (the ring's point).
    refs[0] = 2;
    refs[1] = 0;
    refs[2] = 0;
    CHECK(camStreamPickWriteSlot(refs, CAM_STREAM_SLOTS, 1) == 2);
}

TEST_CASE("reader slot: newest strictly after the viewer's generation") {
    uint32_t gens[CAM_STREAM_SLOTS] = {0};
    CHECK(camStreamPickNewest(gens, CAM_STREAM_SLOTS, 0) == -1);  // nothing published yet

    gens[0] = 10; gens[1] = 12; gens[2] = 11;
    CHECK(camStreamPickNewest(gens, CAM_STREAM_SLOTS, 0) == 1);    // 12 is newest
    CHECK(camStreamPickNewest(gens, CAM_STREAM_SLOTS, 10) == 1);
    CHECK(camStreamPickNewest(gens, CAM_STREAM_SLOTS, 11) == 1);
    CHECK(camStreamPickNewest(gens, CAM_STREAM_SLOTS, 12) == -1);  // already sent: resend held frame
    CHECK(camStreamPickNewest(gens, CAM_STREAM_SLOTS, 13) == -1);

    // Wrap-around in the generation counter must not go backwards.
    gens[0] = 0xFFFFFFFFu; gens[1] = 0; gens[2] = 0;
    CHECK(camStreamPickNewest(gens, CAM_STREAM_SLOTS, 5) == 0);
    // Slot 0 with gen 0 means "never written" and is never picked.
    CHECK(camStreamPickNewest(gens, CAM_STREAM_SLOTS, 0xFFFFFFFFu) == -1);
}

TEST_CASE("ring shape: 3 slots is what the 2-viewer policy needs") {
    // 1 writer + 2 viewers + 1 slack, so a viewer can always keep the frame it
    // is sending while new ones land elsewhere.
    CHECK(CAM_STREAM_SLOTS == 3);
    int refs[CAM_STREAM_SLOTS] = {1, 1, 0};
    CHECK(camStreamPickWriteSlot(refs, CAM_STREAM_SLOTS, 1) == 2);
}
