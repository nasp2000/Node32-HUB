#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <map>
#include <string>
#include <vector>

// Standalone reimplementation of the atomic write pattern used across the
// firmware: ConfigManager::saveToLittleFS(), LittleFsStore::atomicWriteJson(),
// sensor config managers, camera/buzzer/can config, NtpManager, etc.
// Pattern: write .tmp -> validate bytes>0 -> remove original -> rename .tmp.
// Tests verify the invariant "a failed/interrupted write never corrupts the
// existing config file".
//
// NOTE: ConfigManager's writeJsonAtomic() (saveSection/saveToLittleFS) adds a
// step on top of this generic pattern: the .tmp is RE-PARSED as JSON before the
// rename and aborted otherwise. That alerts-focused variant (plus healing of a
// corrupt alerts.json on load) is covered in test_alerts_save_safe.cpp.

namespace {

struct VirtualFs {
    std::map<std::string, std::string> files;

    bool exists(const std::string& p) const { return files.count(p) != 0; }
    std::string read(const std::string& p) const {
        auto it = files.find(p);
        return it == files.end() ? std::string() : it->second;
    }
    bool write(const std::string& p, const std::string& content) {
        files[p] = content;
        return true;
    }
    bool remove(const std::string& p) { return files.erase(p) > 0; }
    bool rename(const std::string& a, const std::string& b) {
        auto it = files.find(a);
        if (it == files.end()) return false;
        files[b] = it->second;
        files.erase(a);
        return true;
    }
};

std::string tmpOf(const std::string& finalPath) { return finalPath + ".tmp"; }

// Mirrors the production pattern exactly (open tmp -> write -> close ->
// bytes==0 check -> remove final -> rename tmp->final). Returns true on success.
// `failOpen` simulates disk full / open failure, `failRename` simulates rename failure.
bool atomicWrite(VirtualFs& fs, const std::string& finalPath,
                 const std::string& content, bool failOpen = false,
                 bool failRename = false) {
    const std::string tmpPath = tmpOf(finalPath);
    if (failOpen) return false;                 // open() failed
    fs.write(tmpPath, content);
    if (content.empty()) {                       // serializeJson wrote 0 bytes
        fs.remove(tmpPath);
        return false;
    }
    if (failRename) {
        fs.remove(tmpPath);
        return false;
    }
    fs.remove(finalPath);
    if (!fs.rename(tmpPath, finalPath)) {
        fs.remove(tmpPath);
        return false;
    }
    return true;
}

// Mirrors ConfigManager::loadFromLittleFS() recoverOrRemoveTmpPath():
// if final is missing and tmp exists -> promote tmp (interrupted save);
// otherwise remove the stale tmp.
void recoverTmp(VirtualFs& fs, const std::string& finalPath) {
    const std::string tmpPath = tmpOf(finalPath);
    if (!fs.exists(tmpPath)) return;
    if (!fs.exists(finalPath)) {
        fs.rename(tmpPath, finalPath);
    } else {
        fs.remove(tmpPath);
    }
}

} // namespace

TEST_CASE("atomic write success keeps final content and no tmp residue") {
    VirtualFs fs;
    fs.write("/config/core.json", "old");
    REQUIRE(atomicWrite(fs, "/config/core.json", "{\"k\":1}"));
    CHECK(fs.read("/config/core.json") == "{\"k\":1}");
    CHECK_FALSE(fs.exists("/config/core.json.tmp"));
}

TEST_CASE("atomic write of 0 bytes aborts and preserves original") {
    VirtualFs fs;
    fs.write("/config/core.json", "old");
    CHECK_FALSE(atomicWrite(fs, "/config/core.json", ""));
    CHECK(fs.read("/config/core.json") == "old");
    CHECK_FALSE(fs.exists("/config/core.json.tmp"));
}

TEST_CASE("atomic write open failure preserves original") {
    VirtualFs fs;
    fs.write("/config/rs232.json", "old");
    CHECK_FALSE(atomicWrite(fs, "/config/rs232.json", "new", /*failOpen*/ true));
    CHECK(fs.read("/config/rs232.json") == "old");
    CHECK_FALSE(fs.exists("/config/rs232.json.tmp"));
}

TEST_CASE("atomic write rename failure preserves original and cleans tmp") {
    VirtualFs fs;
    fs.write("/config/modbus.json", "old");
    CHECK_FALSE(atomicWrite(fs, "/config/modbus.json", "new", /*failOpen*/ false, /*failRename*/ true));
    CHECK(fs.read("/config/modbus.json") == "old");
    CHECK_FALSE(fs.exists("/config/modbus.json.tmp"));
}

TEST_CASE("power-loss between tmp write and final remove leaves original intact") {
    // Simulate: tmp fully written, power cut BEFORE LittleFS.remove(finalPath)
    VirtualFs fs;
    fs.write("/config/access.json", "old");
    fs.write("/config/access.json.tmp", "new");
    // On next boot: recoverTmp sees final exists -> removes stale tmp
    recoverTmp(fs, "/config/access.json");
    CHECK(fs.read("/config/access.json") == "old");
    CHECK_FALSE(fs.exists("/config/access.json.tmp"));
}

TEST_CASE("power-loss after final remove but before rename promotes tmp on boot") {
    // Simulate: final removed, tmp written, power cut BEFORE rename(tmp,final)
    VirtualFs fs;
    fs.remove("/config/access.json");
    fs.write("/config/access.json.tmp", "new");
    recoverTmp(fs, "/config/access.json");
    CHECK(fs.read("/config/access.json") == "new");
    CHECK_FALSE(fs.exists("/config/access.json.tmp"));
}

TEST_CASE("interrupted write (truncated tmp) never replaces valid original") {
    // A half-written tmp file must not be promoted over an existing valid final.
    VirtualFs fs;
    fs.write("/config/system.json", "{\"ntp\":true}");
    fs.write("/config/system.json.tmp", "{\"nt");
    recoverTmp(fs, "/config/system.json");
    CHECK(fs.read("/config/system.json") == "{\"ntp\":true}");
    CHECK_FALSE(fs.exists("/config/system.json.tmp"));
}

TEST_CASE("overwrite of existing file is atomic - old or new, never half") {
    for (int i = 0; i < 5; ++i) {
        VirtualFs fs;
        fs.write("/config/core.json", "old-content-" + std::to_string(i));
        if (atomicWrite(fs, "/config/core.json", "new-content-" + std::to_string(i))) {
            CHECK(fs.read("/config/core.json") == "new-content-" + std::to_string(i));
        } else {
            CHECK(fs.read("/config/core.json") == "old-content-" + std::to_string(i));
        }
        CHECK_FALSE(fs.exists("/config/core.json.tmp"));
    }
}

TEST_CASE("tmp path is derived from final path") {
    VirtualFs fs;
    fs.write("/config/sensor_mq135.json", "a");
    CHECK(tmpOf("/config/sensor_mq135.json") == "/config/sensor_mq135.json.tmp");
    // Recovery on a file that never had a tmp is a no-op
    recoverTmp(fs, "/config/sensor_mq135.json");
    CHECK(fs.read("/config/sensor_mq135.json") == "a");
    CHECK_FALSE(fs.exists("/config/sensor_mq135.json.tmp"));
}

TEST_CASE("multiple stems are independent - one failure does not affect others") {
    VirtualFs fs;
    fs.write("/config/rs232.json", "rs232-old");
    fs.write("/config/core.json", "core-old");
    // rs232 write fails (open failure), core succeeds
    CHECK_FALSE(atomicWrite(fs, "/config/rs232.json", "rs232-new", /*failOpen*/ true));
    CHECK(atomicWrite(fs, "/config/core.json", "core-new"));
    CHECK(fs.read("/config/rs232.json") == "rs232-old");
    CHECK(fs.read("/config/core.json") == "core-new");
    CHECK_FALSE(fs.exists("/config/rs232.json.tmp"));
}
