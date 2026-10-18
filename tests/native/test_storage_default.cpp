#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <string>

// ====== Standalone reimpl of files_page_handler.cpp resolveDefaultStorageType ======
// Order (all packs): SD card, Flash (LittleFS), PSRAM.
// Mirrors the lambda in registerFilesPageRoute(); keeps the same fallbacks.
static std::string resolveDefaultStorage(bool sdcardEnabled, bool sdMounted,
                                         bool filesystemEnabled, bool flashMounted,
                                         bool psramAvailable) {
    if (sdcardEnabled && sdMounted) return "sd";
    if (filesystemEnabled && flashMounted) return "flash";
    if (psramAvailable) return "psram";
    if (filesystemEnabled) return "flash";
    if (sdcardEnabled) return "sd";
    return "psram";
}

TEST_CASE("storageDefault: SD mounted wins over everything") {
    CHECK(resolveDefaultStorage(true, true, true, true, true) == "sd");
}

TEST_CASE("storageDefault: no SD -> LittleFS before PSRAM") {
    CHECK(resolveDefaultStorage(true, false, true, true, true) == "flash");
    CHECK(resolveDefaultStorage(false, false, true, true, true) == "flash");
}

TEST_CASE("storageDefault: flash not mounted -> PSRAM") {
    CHECK(resolveDefaultStorage(true, false, true, false, true) == "psram");
}

TEST_CASE("storageDefault: fallbacks when backends compiled out") {
    CHECK(resolveDefaultStorage(false, false, true, false, false) == "flash");
    CHECK(resolveDefaultStorage(true, false, false, false, false) == "sd");
    CHECK(resolveDefaultStorage(false, false, false, false, false) == "psram");
}
