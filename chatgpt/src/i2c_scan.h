// i2c_scan.h — I2C bus scanner (diagnostic). Scans Wire (+ Wire1 when the
// AHT20/BMP280 sensor module is compiled) and returns a JSON report.
#pragma once
#include <Arduino.h>

String runI2cScanJson();
