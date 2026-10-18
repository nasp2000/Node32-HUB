#ifndef SENSOR_MQ135_H
#define SENSOR_MQ135_H

#include "../feature_flags.h"

#if ENABLE_MQ135==1

#include <Arduino.h>

// ====================================================================================
// MQ-135 Air Quality Sensor Driver
// ====================================================================================
// The MQ-135 is an analog gas sensor capable of detecting multiple gases.
// It uses a heater element and a sensing resistor whose resistance changes
// in the presence of gases. Readings are derived from the Rs/R0 ratio
// using published datasheet curve constants.
//
// Detectable gases (estimated ppm via curve fitting):
//   CO2, CO, NH3, NOx, Alcohol/Ethanol, Benzene, Smoke
//
// Hardware connection:
//   AOUT -> ESP32 analog GPIO (default SENSOR_MQ135_PIN)
//   DOUT -> optional digital threshold pin (not used in software mode)
//   VCC  -> 5V (module has onboard regulator; 3.3V reduces accuracy)
//   GND  -> GND
//
// Important:
//   - Sensor needs 24-48h burn-in for stable readings (or at minimum ~3 min warm-up)
//   - R0 must be calibrated in CLEAN air before use (default is a typical value)
//   - Temperature and humidity correction improves accuracy significantly

// ADC pin for MQ-135 analog output
// GPIO5 is the safe default — avoids conflict with W5500 ETH_RST_PIN (GPIO4)
#ifndef SENSOR_MQ135_PIN
  #define SENSOR_MQ135_PIN  5   // GPIO5 — safe default (GPIO4 conflicts with W5500 RST)
#endif

// Load resistance on the module (Ohms). Most MQ-135 modules use 10 kOhm.
#ifndef MQ135_RL_VALUE
  #define MQ135_RL_VALUE    10000.0f  // 10 kOhm
#endif

// R0 in clean air (Ohms). Factory default / typical value.
// User must calibrate this via the web page for accurate ppm readings.
#ifndef MQ135_R0_DEFAULT
  #define MQ135_R0_DEFAULT  76.63f
#endif

// Supply voltage (Vcc) fed to the sensor voltage divider
#ifndef MQ135_VCC
  #define MQ135_VCC         5.0f    // 5V
#endif

// ADC resolution and reference voltage for ESP32
#define MQ135_ADC_RESOLUTION  4095  // 12-bit ADC
#define MQ135_ADC_VREF        3.3f  // ESP32 ADC reference voltage

// Minimum warm-up time before readings are valid
#define MQ135_WARMUP_MS  180000UL  // 3 minutes

// Number of ADC samples per reading (averaged to reduce noise)
#define MQ135_SAMPLES     10

// ====================================================================================
// Gas curve constants — from MQ-135 datasheet (a * ratio^b = ppm)
// Each gas has a sensitivity curve: ppm = PARA * (Rs/R0)^PARB
// ====================================================================================
#define MQ135_PARA_CO2      116.6020682f
#define MQ135_PARB_CO2      (-2.769034857f)

#define MQ135_PARA_CO       605.18f
#define MQ135_PARB_CO       (-3.937f)

#define MQ135_PARA_NH3      102.2f
#define MQ135_PARB_NH3      (-2.473f)

#define MQ135_PARA_NOX      44.947f
#define MQ135_PARB_NOX      (-3.445f)

#define MQ135_PARA_ALCOHOL  77.255f
#define MQ135_PARB_ALCOHOL  (-3.18f)

#define MQ135_PARA_BENZENE  34.668f
#define MQ135_PARB_BENZENE  (-3.369f)

#define MQ135_PARA_SMOKE    40000.0f
#define MQ135_PARB_SMOKE    (-3.0f)

// ====================================================================================
// Temperature/humidity correction coefficients
// Corrects sensor resistance for ambient conditions.
// Formula: correction = CORA*t^2 - CORB*t + CORC - (h - CORD_HREF) * CORD
// Reference: t=20°C, h=65%RH
// ====================================================================================
#define MQ135_CORA   0.00035f
#define MQ135_CORB   0.02718f
#define MQ135_CORC   1.39538f
#define MQ135_CORD   0.0018f
#define MQ135_CORE   (-0.003333333f)
#define MQ135_CORF   (-0.001923077f)
#define MQ135_CORG   1.130128205f

// ====================================================================================
// Data Structures
// ====================================================================================

struct MQ135Reading {
    int     raw_adc;        // Raw ADC value (0-4095)
    float   voltage;        // Voltage at AOUT (V)
    float   rs;             // Sensor resistance Rs (Ohm)
    float   rs_r0_ratio;    // Rs/R0 ratio (dimensionless)
    float   ppm_co2;        // CO2 estimate (ppm)
    float   ppm_co;         // CO estimate (ppm)
    float   ppm_nh3;        // Ammonia estimate (ppm)
    float   ppm_nox;        // NOx estimate (ppm)
    float   ppm_alcohol;    // Alcohol/Ethanol estimate (ppm)
    float   ppm_benzene;    // Benzene estimate (ppm)
    float   ppm_smoke;      // Smoke estimate (ppm)
    float   ppm_co2_corrected;   // CO2 corrected for temp+humidity (ppm)
    float   aqi;            // Derived Air Quality Index (0-500 scale)
    bool    temp_compensated;    // True if temp/humidity correction was applied
    unsigned long timestamp;     // millis() when reading was taken
    bool    valid;

    MQ135Reading() :
        raw_adc(0), voltage(0.0f), rs(0.0f), rs_r0_ratio(0.0f),
        ppm_co2(0.0f), ppm_co(0.0f), ppm_nh3(0.0f), ppm_nox(0.0f),
        ppm_alcohol(0.0f), ppm_benzene(0.0f), ppm_smoke(0.0f),
        ppm_co2_corrected(0.0f), aqi(0.0f),
        temp_compensated(false), timestamp(0), valid(false) {}
};

// ====================================================================================
// MQ135Sensor Class
// ====================================================================================

class MQ135Sensor {
private:
    uint8_t pin;
    float   r0;             // R0 calibration value (Ohm)
    float   rl;             // Load resistance (Ohm)
    float   vcc;            // Supply voltage (V)
    bool    initialized;
    unsigned long bootTime; // millis() at begin() — used for warm-up tracking
    MQ135Reading lastReading;

    // Returns Rs derived from raw ADC value
    float computeRs(int adcRaw) const;

    // Returns correction factor for temperature and humidity
    // Set temp=20.0f and humidity=65.0f to get correction=1.0 (no correction)
    float getCorrectionFactor(float temperature, float humidity) const;

    // Compute ppm for a gas using its curve constants (Rs/R0 ratio already computed)
    float computePPM(float ratio, float para, float parb) const;

    // Derive AQI from CO2 ppm (simplified EPA-like scale)
    float computeAQI(float co2_ppm) const;

    // Read ADC multiple times and return average
    int readADCAverage() const;

public:
    MQ135Sensor();

    // Initialize pin and ADC
    bool begin(uint8_t adcPin = SENSOR_MQ135_PIN, float r0Value = MQ135_R0_DEFAULT);

    bool isInitialized() const { return initialized; }

    // True if sensor has been powered long enough for stable readings
    bool isWarmedUp() const;

    unsigned long getWarmupRemaining() const;

    // Perform a full reading.
    // If temperature and humidity are provided (from AHT20), applies correction.
    // Pass NaN (default) to skip temperature/humidity compensation.
    bool update(float temperature = NAN, float humidity = NAN);

    MQ135Reading getLastReading() const { return lastReading; }

    // Get/set R0 (used to save calibrated value from config)
    float getR0() const { return r0; }
    void  setR0(float r0Value) { r0 = r0Value; }

    // Get current Rs (for live calibration display)
    float getCurrentRs() const;

    // Status string for Serial/web
    String getStatusString() const;
};

// Global instance
extern MQ135Sensor mq135Sensor;

#endif // ENABLE_MQ135==1
#endif // SENSOR_MQ135_H
