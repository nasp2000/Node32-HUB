#include "sensor_mq135.h"

#include "feature_flags.h"
#if ENABLE_MQ135==1

#include <Arduino.h>
#include <math.h>
#include "feature_flags.h"

// Global instance — used by sensor_mq135_config.cpp and referenced via extern in sensor_config.cpp
MQ135Sensor mq135Sensor;

MQ135Sensor::MQ135Sensor() :
    pin(SENSOR_MQ135_PIN),
    r0(MQ135_R0_DEFAULT),
    rl(MQ135_RL_VALUE),
    vcc(MQ135_VCC),
    initialized(false),
    bootTime(0)
{
}

// Initialize pin and ADC
bool MQ135Sensor::begin(uint8_t adcPin, float r0Value) {
    pin = adcPin;
    r0  = r0Value;
    bootTime = millis();

    // Configure ADC: 12-bit, 11dB attenuation for 0-3.3V range
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    analogSetPinAttenuation(pin, ADC_11db);

    // Validate pin is analog-capable (basic range check)
    if (pin > 47) {
        Serial.printf("[MQ135] Invalid ADC pin %u\n", pin);
        return false;
    }

    initialized = true;
    Serial.printf("[MQ135] Initialized on GPIO%u, R0=%.2f, RL=%.0f, warming up %lu s\n",
                  pin, r0, rl, MQ135_WARMUP_MS / 1000UL);
    return true;
}

// True if minimum warm-up time has elapsed
bool MQ135Sensor::isWarmedUp() const {
    return initialized && ((millis() - bootTime) >= MQ135_WARMUP_MS);
}

// Milliseconds remaining until sensor is warmed up (0 = ready)
unsigned long MQ135Sensor::getWarmupRemaining() const {
    if (!initialized) return MQ135_WARMUP_MS;
    unsigned long elapsed = millis() - bootTime;
    return (elapsed >= MQ135_WARMUP_MS) ? 0UL : (MQ135_WARMUP_MS - elapsed);
}

// Average MQ135_SAMPLES ADC reads to reduce noise
int MQ135Sensor::readADCAverage() const {
    long sum = 0;
    for (int i = 0; i < MQ135_SAMPLES; i++) {
        sum += analogRead(pin);
        delayMicroseconds(500);
    }
    return (int)(sum / MQ135_SAMPLES);
}

// Derive sensor resistance Rs from ADC value
// Rs = RL * (Vcc/Vadc - 1)  where Vadc = adc/4095 * Vref
float MQ135Sensor::computeRs(int adcRaw) const {
    if (adcRaw <= 0) adcRaw = 1; // avoid division by zero
    float vadc = (float)adcRaw / (float)MQ135_ADC_RESOLUTION * MQ135_ADC_VREF;
    if (vadc <= 0.0f) return 0.0f;
    return rl * ((vcc / vadc) - 1.0f);
}

// Correction factor for temperature and humidity
// Reference point: 20°C, 65%RH → correction = 1.0
float MQ135Sensor::getCorrectionFactor(float temperature, float humidity) const {
    if (temperature < 0.0f) {
        return MQ135_CORA * temperature * temperature
             - MQ135_CORB * temperature
             + MQ135_CORC
             + (humidity - 33.0f) * MQ135_CORD;
    }
    return MQ135_CORE * temperature
         + MQ135_CORF * humidity
         + MQ135_CORG;
}

// ppm = para * (Rs/R0)^parb
float MQ135Sensor::computePPM(float ratio, float para, float parb) const {
    if (ratio <= 0.0f || r0 <= 0.0f) return 0.0f;
    return para * powf(ratio, parb);
}

float MQ135Sensor::getCurrentRs() const {
    if (!initialized) return 0.0f;
    return computeRs(readADCAverage());
}

// Simple EPA-like AQI from CO2 ppm (0-500 display scale)
//   0-450 ppm  → 0-50   (Good / Fresh air)
//   450-1000   → 50-100 (Moderate)
//   1000-2000  → 100-200 (Unhealthy for sensitive)
//   2000-5000  → 200-300 (Unhealthy)
//   >5000      → 300-500 (Hazardous)
float MQ135Sensor::computeAQI(float co2_ppm) const {
    if (co2_ppm <= 450.0f)   return (co2_ppm / 450.0f) * 50.0f;
    if (co2_ppm <= 1000.0f)  return 50.0f  + ((co2_ppm - 450.0f)  / 550.0f)  * 50.0f;
    if (co2_ppm <= 2000.0f)  return 100.0f + ((co2_ppm - 1000.0f) / 1000.0f) * 100.0f;
    if (co2_ppm <= 5000.0f)  return 200.0f + ((co2_ppm - 2000.0f) / 3000.0f) * 100.0f;
    return 300.0f + fminf((co2_ppm - 5000.0f) / 5000.0f * 200.0f, 200.0f);
}

// Main reading function
bool MQ135Sensor::update(float temperature, float humidity) {
    if (!initialized) return false;

    int raw = readADCAverage();
    float voltage = (float)raw / (float)MQ135_ADC_RESOLUTION * MQ135_ADC_VREF;
    float rs = computeRs(raw);

    lastReading.raw_adc  = raw;
    lastReading.voltage  = voltage;
    lastReading.rs       = rs;
    lastReading.timestamp = millis();

    // Base ratio (uncorrected)
    float ratio = (r0 > 0.0f) ? (rs / r0) : 0.0f;
    lastReading.rs_r0_ratio = ratio;

    // Gas ppm from base (uncorrected) ratio
    lastReading.ppm_co2     = computePPM(ratio, MQ135_PARA_CO2,     MQ135_PARB_CO2);
    lastReading.ppm_co      = computePPM(ratio, MQ135_PARA_CO,      MQ135_PARB_CO);
    lastReading.ppm_nh3     = computePPM(ratio, MQ135_PARA_NH3,     MQ135_PARB_NH3);
    lastReading.ppm_nox     = computePPM(ratio, MQ135_PARA_NOX,     MQ135_PARB_NOX);
    lastReading.ppm_alcohol = computePPM(ratio, MQ135_PARA_ALCOHOL, MQ135_PARB_ALCOHOL);
    lastReading.ppm_benzene = computePPM(ratio, MQ135_PARA_BENZENE, MQ135_PARB_BENZENE);
    lastReading.ppm_smoke   = computePPM(ratio, MQ135_PARA_SMOKE,   MQ135_PARB_SMOKE);

    // Temperature + humidity compensation for CO2 (most useful corrected reading)
    bool hasEnvData = (!isnan(temperature) && !isnan(humidity)
                       && temperature > -40.0f && humidity > 0.0f);
    if (hasEnvData) {
        float corr = getCorrectionFactor(temperature, humidity);
        if (corr > 0.0f) {
            float correctedRs = rs / corr;
            float corrRatio   = (r0 > 0.0f) ? (correctedRs / r0) : 0.0f;
            lastReading.ppm_co2_corrected = computePPM(corrRatio, MQ135_PARA_CO2, MQ135_PARB_CO2);
        } else {
            lastReading.ppm_co2_corrected = lastReading.ppm_co2;
        }
        lastReading.temp_compensated = true;
    } else {
        lastReading.ppm_co2_corrected = lastReading.ppm_co2;
        lastReading.temp_compensated  = false;
    }

    // AQI based on corrected CO2
    lastReading.aqi   = computeAQI(lastReading.ppm_co2_corrected);
    lastReading.valid = true;

    Serial.printf("[MQ135] ADC=%d V=%.3f Rs=%.1f ratio=%.3f CO2=%.1f NH3=%.1f AQI=%.0f warm=%s\n",
                  raw, voltage, rs, ratio,
                  lastReading.ppm_co2_corrected, lastReading.ppm_nh3, lastReading.aqi,
                  isWarmedUp() ? "yes" : "no");
    return true;
}

String MQ135Sensor::getStatusString() const {
    if (!initialized) return "Not initialized";
    unsigned long rem = getWarmupRemaining();
    if (rem > 0) {
        return "Warming up... " + String(rem / 1000) + "s remaining";
    }
    if (!lastReading.valid) return "Ready, no reading yet";
    return "OK — CO2 " + String(lastReading.ppm_co2_corrected, 1) + " ppm, AQI " + String((int)lastReading.aqi);
}

#endif // ENABLE_MQ135==1
