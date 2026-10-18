---
applyTo: "chatgpt/src/sensors/**"
---

# Sensor Instructions

## Structure — 4 files per sensor
```
src/sensors/
  sensor_<name>.h          Driver: pin defines, struct, class
  sensor_<name>.cpp        Driver: hardware read
  sensor_<name>_config.h   Config: class + struct
  sensor_<name>_config.cpp Config: persistence, scheduling
```
Never add sensor logic to `chatgpt.ino` or `webpage_impl.h`.

## Feature flag (mandatory in `user_config.h`)
```cpp
#define ENABLE_MY_SENSOR  0  // default
#define ENABLE_MY_SENSOR  1  // user uncomment
```
Guards: `#if ENABLE_MY_SENSOR==1`, never `#ifdef`. First include: `#include "../feature_flags.h"`.

## Driver header pattern
```cpp
#include "../feature_flags.h"
#if ENABLE_MY_SENSOR==1
struct MyReading { float value; unsigned long timestamp; bool valid; };
class MySensor { public: bool begin(); bool update(); MyReading getLastReading(); };
extern MySensor mySensor;
#endif
```

## Config pattern
- `begin()` — load from ConfigManager, register extra slots
- `update()` — read hardware, `stageHistorySlots()`, `injectDataToRXBuffer()` on schedule
- `formatPretty()` / `formatCSV()` — output

## LittleFS atomic write (MANDATORY)
```cpp
String tmp = _path + ".tmp";
File f = LittleFS.open(tmp, "w");
size_t bytes = f.print(toJson());
f.close();
if (bytes == 0) { LittleFS.remove(tmp); return false; }
LittleFS.remove(_path);
return LittleFS.rename(tmp, _path);
```
Use `littlefsBegin()` — NEVER `littlefsMounted()`.

## Data injection
```cpp
extern void injectDataToRXBuffer(const String &data, const String &source);
injectDataToRXBuffer(formattedLine, "MYSENSOR");
```

## Charts integration — extra slots
```cpp
sensorConfig.registerExtraSlots({{0,true,"co2_ppm","CO₂","ppm",1.0f}});
sensorConfig.stageHistorySlots({{0, (float)reading.co2_ppm}});
```
Charts auto-show sensor in dropdowns and record to `/sensor_history/`.

## Web pages — follow 4-layer split in `webpages.instructions.md`. Guard routes with `#if ENABLE_MY_SENSOR==1`.

## SD paths: `/sensor_history/YYYY-MM-DD/YYYY-MM-DD.csv`

