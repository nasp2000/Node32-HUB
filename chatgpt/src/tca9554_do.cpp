// tca9554_do.cpp
// TCA9554 I2C digital output expander — implementation.
// Guarded by BOARD_HAS_TCA9554_DO so it compiles to nothing on all other boards.

#include "tca9554_do.h"

#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1

#include <Wire.h>
#include <LittleFS.h>
#include "arduinojson_fix.h"
#include <ArduinoJson.h>
#include "littlefs_manager.h"

// TCA9554 register addresses.
#define TCA9554_REG_INPUT    0x00  // Read-only: physical input state.
#define TCA9554_REG_OUTPUT   0x01  // Read/write: output latch.
#define TCA9554_REG_POLARITY 0x02  // Polarity inversion mask (0 = no inversion).
#define TCA9554_REG_CONFIG   0x03  // Direction: 0 = output, 1 = input.

// LittleFS path for persisted output state.
#define TCA9554_STATE_PATH  "/config/tca9554_do.json"

// Cached bitmask: bit N reflects the current LOGICAL output state of channel N.
// The physical state written to the chip is _mask ^ _invert.
static uint8_t _mask = 0x00;

// Per-channel inversion mask (bit N set = physical output inverted).
static uint8_t _invert = 0x00;

// Write a byte to a TCA9554 register. Returns true on I2C ACK (endTransmission == 0).
static bool _writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission((uint8_t)TCA9554_I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

void tca9554Begin() {
    Wire.begin(TCA9554_SDA_PIN, TCA9554_SCL_PIN);
    _writeReg(TCA9554_REG_POLARITY, 0x00); // No polarity inversion.
    _writeReg(TCA9554_REG_CONFIG,   0x00); // All 8 pins as outputs.
    tca9554LoadState();                    // Restore saved state.
    Serial.printf("[DO] TCA9554 ready (addr=0x%02X SDA=%d SCL=%d mask=0x%02X)\n",
                  (unsigned)TCA9554_I2C_ADDR,
                  (int)TCA9554_SDA_PIN, (int)TCA9554_SCL_PIN,
                  (unsigned)_mask);
}

bool tca9554SetAll(uint8_t mask) {
    bool ok = _writeReg(TCA9554_REG_OUTPUT, mask ^ _invert);
    if (ok) _mask = mask;
    return ok;
}

bool tca9554SetOutput(uint8_t ch, bool state) {
    if (ch >= TCA9554_DO_CHANNELS) return false;
    uint8_t newMask = state
        ? (_mask | (uint8_t)(1u << ch))
        : (_mask & (uint8_t)~(1u << ch));
    return tca9554SetAll(newMask);
}

uint8_t tca9554GetMask() {
    return _mask;
}

void tca9554SetInvert(uint8_t invertMask) {
    _invert = invertMask;
    _writeReg(TCA9554_REG_OUTPUT, _mask ^ _invert); // re-apply current logical state
}

uint8_t tca9554GetInvert() {
    return _invert;
}

void tca9554SaveState() {
    if (!littlefsBegin()) return;
    File f = LittleFS.open("/config/tca9554_do.json.tmp", "w");
    if (!f) return;
    StaticJsonDocument<32> doc;
    doc["mask"] = (unsigned)_mask;
    size_t bytes = serializeJsonPretty(doc, f);
    f.close();
    storNoteLfsWrite(bytes);
    if (bytes > 0) {
        LittleFS.remove(TCA9554_STATE_PATH);
        LittleFS.rename("/config/tca9554_do.json.tmp", TCA9554_STATE_PATH);
    } else {
        LittleFS.remove("/config/tca9554_do.json.tmp");
    }
}

void tca9554LoadState() {
    if (!littlefsBegin()) {
        tca9554SetAll(0x00); // Safe default: all outputs off.
        return;
    }
    File f = LittleFS.open(TCA9554_STATE_PATH, "r");
    if (!f) {
        tca9554SetAll(0x00);
        return;
    }
    StaticJsonDocument<32> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    uint8_t saved = (err == DeserializationError::Ok)
                    ? (uint8_t)(doc["mask"] | 0)
                    : 0x00;
    tca9554SetAll(saved);
}

#endif // BOARD_HAS_TCA9554_DO
