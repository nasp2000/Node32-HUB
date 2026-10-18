#include "i2c_scan.h"

#include "feature_flags.h"
#include "i2c_scan_logic.h"

#include <Arduino.h>
#include <Wire.h>

#if ENABLE_AHT20_BMP280==1
#include "sensors/sensor_aht20_bmp280.h"
#endif
#if ENABLE_INA226==1
#include "sensors/sensor_ina226.h"
#endif
#if defined(ENABLE_LCD_SCREEN) && ENABLE_LCD_SCREEN==1
#include "screen.h"
#endif

#ifndef TCA9554_I2C_ADDR
#define TCA9554_I2C_ADDR 0x20
#endif

static uint8_t s_found0[128];
static uint8_t s_found1[128];

// Probes every I2C address (0x03..0x77) on the given bus and collects ACKers.
// Write probe only (beginTransmission/endTransmission) — no register access,
// so active sensors are not disturbed beyond the address pulse itself.
static void i2cProbe(TwoWire& wire, uint8_t* out, uint8_t& count) {
    count = 0;
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        wire.beginTransmission(addr);
        if (wire.endTransmission() == 0) {
            if (count < 128) out[count++] = addr;
        }
    }
}

String runI2cScanJson() {
    I2cBusReport buses[2];
    uint8_t busCount = 0;

#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    // Touch-LCD-7: Arduino Wire (port 0, new driver) must NEVER initialize —
    // the display stack owns hardware I2C port 0 with the legacy driver
    // (touch GT911 + CH422G). Initializing port 0 here aborts the chip
    // ("CONFLICT! driver_ng is not allowed to be used with this old driver").
    // Sensors live on Wire1 (port 1) — scan that bus only.
#else
    // Primary bus (Wire): prefer the sensor bus pins (SENSOR_I2C0 — defined by
    // board_pins.h on Waveshare boards, or sensor_aht20_bmp280.h defaults),
    // falling back to the LCD pins, then N16R8 devkit defaults (21/48).
    uint8_t sda0 = 21, scl0 = 48;
#if defined(SENSOR_I2C0_SDA) && defined(SENSOR_I2C0_SCL)
    sda0 = SENSOR_I2C0_SDA;
    scl0 = SENSOR_I2C0_SCL;
#elif defined(LCD_SDA) && defined(LCD_SCL)
    sda0 = LCD_SDA;
    scl0 = LCD_SCL;
#endif
    Wire.begin(sda0, scl0);
    uint8_t n0 = 0;
    i2cProbe(Wire, s_found0, n0);
    buses[busCount].label = "I2C0 (Wire)";
    buses[busCount].sda = sda0;
    buses[busCount].scl = scl0;
    buses[busCount].found = s_found0;
    buses[busCount].foundCount = n0;
    busCount++;
#endif

    // Secondary bus (Wire1) — dedicated sensor bus pins (defaults 8/9).
#if ENABLE_AHT20_BMP280==1
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    // T7-24c: Touch-LCD-7 shares pins 8/9 with the panel I2C0 (GT911+CH422G
    // polled at ~30 Hz) — probing the live bus can deadlock both I2C
    // controllers to the task watchdog. Scan nothing on this board.
#else
    uint8_t n1 = 0;
    Wire1.begin(SENSOR_I2C1_SDA, SENSOR_I2C1_SCL);
    i2cProbe(Wire1, s_found1, n1);
    buses[busCount].label = "I2C1 (Wire1)";
    buses[busCount].sda = SENSOR_I2C1_SDA;
    buses[busCount].scl = SENSOR_I2C1_SCL;
    buses[busCount].found = s_found1;
    buses[busCount].foundCount = n1;
    busCount++;
#endif
#endif

    // Configured chips (compiled into this build) — used for the
    // configured/extra and missing (configured but not detected) annotations.
    I2cExpectedDevice expected[8];
    uint8_t expCount = 0;
#if ENABLE_AHT20_BMP280==1
    expected[expCount].addr = AHT20_I2C_ADDR;  expected[expCount].name = "AHT20";   expCount++;
    expected[expCount].addr = BMP280_I2C_ADDR; expected[expCount].name = "BMP280";  expCount++;
#endif
#if ENABLE_INA226==1
    expected[expCount].addr = SENSOR_INA226_I2C_ADDR; expected[expCount].name = "INA226"; expCount++;
#endif
#if defined(BOARD_HAS_DS3231) && BOARD_HAS_DS3231==1
    expected[expCount].addr = 0x68; expected[expCount].name = "DS3231 RTC"; expCount++;
#endif
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO==1
    expected[expCount].addr = TCA9554_I2C_ADDR; expected[expCount].name = "TCA9554 DO"; expCount++;
#endif
#if defined(ENABLE_LCD_SCREEN) && ENABLE_LCD_SCREEN==1
    expected[expCount].addr = LCD_ADDRESS; expected[expCount].name = "OLED"; expCount++;
#endif

    String out;
    i2cAppendScanJson(out, expected, expCount, buses, busCount);
    return out;
}
