#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// Compiles the REAL production LittleFsStore.cpp (copied to this dir so
// quoted includes resolve to stubs/) and exercises its atomic-write methods
// against an in-memory filesystem stub with failure injection.

#include "LittleFsStore.h"
#include "LittleFS.h"

// Provide definitions for the extern globals declared in the stubs.
SerialStub Serial;
LittleFSClass LittleFS;

namespace {
void resetFs() {
    LittleFS.files.clear();
    LittleFS.failOpen = false;
    LittleFS.failRename = false;
}
} // namespace

TEST_CASE("atomicWriteJson writes tmp then renames to final") {
    resetFs();
    LittleFsStore store;
    JsonVariantConst payload("{\"k\":1}");
    CHECK(store.atomicWriteJson("/config/core.json", payload, 0));
    CHECK(LittleFS.exists("/config/core.json"));
    CHECK_FALSE(LittleFS.exists("/config/core.json.tmp"));
    CHECK(LittleFS.files["/config/core.json"] == "{\"k\":1}");
}

TEST_CASE("atomicWriteJson with 0-byte payload aborts, no final created") {
    resetFs();
    LittleFsStore store;
    JsonVariantConst empty("");
    CHECK_FALSE(store.atomicWriteJson("/config/core.json", empty, 0));
    CHECK_FALSE(LittleFS.exists("/config/core.json"));
    CHECK_FALSE(LittleFS.exists("/config/core.json.tmp"));
}

TEST_CASE("atomicWriteJson preserves existing final on 0-byte payload") {
    resetFs();
    LittleFS.files["/config/core.json"] = "old";
    LittleFsStore store;
    JsonVariantConst empty("");
    CHECK_FALSE(store.atomicWriteJson("/config/core.json", empty, 0));
    CHECK(LittleFS.files["/config/core.json"] == "old");
}

TEST_CASE("atomicWriteJson oversized payload is skipped entirely") {
    resetFs();
    LittleFsStore store;
    JsonVariantConst big(std::string(100, 'x'));
    // maxBytes < payload size -> skipped before any file IO
    CHECK_FALSE(store.atomicWriteJson("/config/core.json", big, 10));
    CHECK_FALSE(LittleFS.exists("/config/core.json"));
    CHECK_FALSE(LittleFS.exists("/config/core.json.tmp"));
}

TEST_CASE("atomicWriteJson open failure returns false and touches nothing") {
    resetFs();
    LittleFS.failOpen = true;
    LittleFsStore store;
    JsonVariantConst payload("data");
    CHECK_FALSE(store.atomicWriteJson("/config/core.json", payload, 0));
    CHECK_FALSE(LittleFS.exists("/config/core.json"));
    CHECK_FALSE(LittleFS.exists("/config/core.json.tmp"));
}

TEST_CASE("atomicWriteJson rename failure cleans tmp and returns false") {
    resetFs();
    LittleFS.files["/config/core.json"] = "old";
    LittleFS.failRename = true;
    LittleFsStore store;
    JsonVariantConst payload("new");
    CHECK_FALSE(store.atomicWriteJson("/config/core.json", payload, 0));
    CHECK_FALSE(LittleFS.exists("/config/core.json.tmp"));
    // Production removes final BEFORE rename; on rename failure final is gone.
    // Caller is expected to fall back (existing behavior of saveToLittleFS).
    CHECK_FALSE(LittleFS.exists("/config/core.json"));
}

TEST_CASE("atomicWriteRaw success writes tmp then renames") {
    resetFs();
    LittleFsStore store;
    CHECK(store.atomicWriteRaw("/config/raw.json", String("hello")));
    CHECK(LittleFS.files["/config/raw.json"] == "hello");
    CHECK_FALSE(LittleFS.exists("/config/raw.json.tmp"));
}

TEST_CASE("atomicWriteRaw 0-byte content aborts") {
    resetFs();
    LittleFsStore store;
    CHECK_FALSE(store.atomicWriteRaw("/config/raw.json", String("")));
    CHECK_FALSE(LittleFS.exists("/config/raw.json"));
    CHECK_FALSE(LittleFS.exists("/config/raw.json.tmp"));
}

TEST_CASE("atomicWriteRaw rename failure cleans tmp") {
    resetFs();
    LittleFS.failRename = true;
    LittleFsStore store;
    CHECK_FALSE(store.atomicWriteRaw("/config/raw.json", String("data")));
    CHECK_FALSE(LittleFS.exists("/config/raw.json.tmp"));
}

TEST_CASE("recoverTmpFiles promotes tmp when final missing (interrupted save)") {
    resetFs();
    LittleFS.files["/config/core.json.tmp"] = "new";
    LittleFsStore store;
    store.recoverTmpFiles("/config/core.json");
    CHECK(LittleFS.files["/config/core.json"] == "new");
    CHECK_FALSE(LittleFS.exists("/config/core.json.tmp"));
}

TEST_CASE("recoverTmpFiles removes stale tmp when final exists") {
    resetFs();
    LittleFS.files["/config/core.json"] = "old";
    LittleFS.files["/config/core.json.tmp"] = "new";
    LittleFsStore store;
    store.recoverTmpFiles("/config/core.json");
    CHECK(LittleFS.files["/config/core.json"] == "old");
    CHECK_FALSE(LittleFS.exists("/config/core.json.tmp"));
}

TEST_CASE("recoverTmpFiles no-op when no tmp present") {
    resetFs();
    LittleFS.files["/config/core.json"] = "old";
    LittleFsStore store;
    store.recoverTmpFiles("/config/core.json");
    CHECK(LittleFS.files["/config/core.json"] == "old");
}

TEST_CASE("readFile round-trip") {
    resetFs();
    LittleFS.files["/config/system.json"] = "{\"x\":1}";
    LittleFsStore store;
    CHECK(store.readFile("/config/system.json") == "{\"x\":1}");
}

TEST_CASE("readFile returns empty for missing file") {
    resetFs();
    LittleFsStore store;
    CHECK(store.readFile("/config/missing.json") == String());
}

TEST_CASE("exists, remove, rename, mkdir wrappers") {
    resetFs();
    LittleFsStore store;
    CHECK_FALSE(store.exists("/config/a.json"));
    LittleFS.files["/config/a.json"] = "x";
    CHECK(store.exists("/config/a.json"));
    CHECK(store.remove("/config/a.json"));
    CHECK_FALSE(store.exists("/config/a.json"));

    LittleFS.files["/config/b.json"] = "y";
    CHECK(store.rename("/config/b.json", "/config/c.json"));
    CHECK_FALSE(store.exists("/config/b.json"));
    CHECK(LittleFS.files["/config/c.json"] == "y");

    CHECK(store.mkdir("/config/sub"));
}

TEST_CASE("ensureDir creates when missing") {
    resetFs();
    LittleFsStore store;
    CHECK(store.ensureDir("/config/newdir"));
}

TEST_CASE("path helpers format stem and tmp paths") {
    CHECK(configStemPath("core") == "/config/core.json");
    CHECK(configStemPath("sensor_mq135") == "/config/sensor_mq135.json");
    CHECK(configTmpPath("/config/core.json") == "/config/core.json.tmp");
}

TEST_CASE("begin and isMounted report true (stub littlefs manager)") {
    resetFs();
    LittleFsStore store;
    CHECK(store.begin());
    CHECK(store.isMounted());
}

TEST_CASE("atomic write over existing file leaves tmp clean") {
    resetFs();
    LittleFS.files["/config/core.json"] = "old";
    LittleFsStore store;
    JsonVariantConst payload("{\"a\":1,\"b\":2}");
    CHECK(store.atomicWriteJson("/config/core.json", payload, 0));
    CHECK(LittleFS.files["/config/core.json"] == "{\"a\":1,\"b\":2}");
    CHECK_FALSE(LittleFS.exists("/config/core.json.tmp"));
}
