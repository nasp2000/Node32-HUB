#include "ConfigValidator.h"
#include <cstring>

namespace ConfigValidator {

bool isSafeConfigJsonKey(const char* key) {
    if (!key) return false;
    size_t len = strlen(key);
    if (len == 0 || len > 64) return false;
    for (size_t i = 0; i < len; ++i) {
        const char c = key[i];
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            (c == '_') || (c == '-') || (c == '.');
        if (!ok) return false;
    }
    return true;
}

void copyJsonSanitized(JsonVariantConst src, JsonVariant dst, int depth) {
    if (depth > 10) {
        dst.set(nullptr);
        return;
    }

    if (src.is<JsonObjectConst>()) {
        JsonObject outObj = dst.to<JsonObject>();
        for (JsonPairConst kv : src.as<JsonObjectConst>()) {
            const char* key = kv.key().c_str();
            if (!isSafeConfigJsonKey(key)) continue;
            JsonVariant child = outObj[key];
            copyJsonSanitized(kv.value(), child, depth + 1);
        }
        return;
    }

    if (src.is<JsonArrayConst>()) {
        JsonArray outArr = dst.to<JsonArray>();
        for (JsonVariantConst item : src.as<JsonArrayConst>()) {
            if (item.is<JsonObjectConst>()) {
                JsonObject o = outArr.createNestedObject();
                copyJsonSanitized(item, o, depth + 1);
            } else if (item.is<JsonArrayConst>()) {
                JsonArray a = outArr.createNestedArray();
                copyJsonSanitized(item, a, depth + 1);
            } else if (item.is<const char*>()) {
                const char* s = item.as<const char*>();
                if (s && strlen(s) > 4096) continue;
                outArr.add(item);
            } else {
                outArr.add(item);
            }
        }
        return;
    }

    if (src.is<const char*>()) {
        const char* s = src.as<const char*>();
        if (s && strlen(s) > 4096) {
            dst.set("");
            return;
        }
    }

    dst.set(src);
}

} // namespace ConfigValidator
