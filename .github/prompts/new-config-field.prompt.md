---
agent: agent
description: Checklist for adding a new field to ConfigManager. Covers all 6 required touch-points to avoid missing a step.
---

# New Config Field Checklist

Adding a config field touches **6 files**. Missing any step causes missing fields on first boot or broken web saves.

## 6 touch-points

| # | File | Action |
|---|------|--------|
| 1 | `src/json_keys.h` | Add PROGMEM key constant |
| 2 | `src/ConfigManager.cpp` → `initDefaults()` | Set default value |
| 3 | `src/ConfigManager.cpp` → `ensureAllFieldsExist()` | Migration guard (same default) |
| 4 | `src/ConfigManager.cpp` → `saveToLittleFS()` + `loadFromLittleFS()` | One file per page |
| 5 | Web handler / form | Read/write the field |
| 6 | `chatgpt.ino` — `LAST GLOBAL CHANGE` | Update timestamp |

## Step 1 — PROGMEM key (optional, for multi-file/hot-path keys)
```cpp
static const char K_MY_FIELD[] PROGMEM = "my_field";  // in json_keys.h
```

## Step 2 — Default in `initDefaults()` (first boot / factory reset)
```cpp
config["my_section"]["my_field"] = false;  // or 0, ""
```

## Step 3 — Migration in `ensureAllFieldsExist()` (every boot)
```cpp
if (!config["my_section"].containsKey("my_field"))
    config["my_section"]["my_field"] = false;  // MUST match step 2
```

## Step 4 — Persistence one-file-per-page
Update `saveToLittleFS()` and `loadFromLittleFS()`. Never place page-owned fields in `main.gX.json`.

## Step 5 — Web handler
- **GET**: already in `/api/config` if field exists in JSON doc
- **POST**: `configManager.config["section"]["field"] = value; configManager.isDirty = true; configManager.save();`

## Step 6 — Update `LAST GLOBAL CHANGE` in `chatgpt.ino`

## Rules
- **Never** write to NVS (`Preferences`) for config fields — only LittleFS
- NVS reserved for: WiFi, SMTP, saved networks, passwords (boot-critical only)
- Use `littlefsBegin()` — NEVER `littlefsMounted()`
- Prefer `markDirtyAndScheduleSave()` over direct `save()`
- If field drives runtime behavior, update cache variable in setter or after `load()`
