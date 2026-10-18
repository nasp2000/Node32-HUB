#pragma once

#include "feature_flags.h"

#if ENABLE_UDA1334A == 1

#include <Arduino.h>
#include <ArduinoJson.h>
#include "audio_dac_uda1334a.h"

#define UDA1334A_CONFIG_PATH "/config/uda1334a.json"

// ==================================================================================
// UDA1334A Configuration Manager — persistent config + runtime state
// ==================================================================================

struct UDA1334AConfig {
    // Persistence fields
    bool enabled;                  // Enable/disable DAC on boot
    uint32_t sampleRate;           // 44100, 48000, 96000, 192000
    int bitDepth;                  // 16, 24, 32
    bool autoRoutingEnabled;       // Auto-play audio when data received
    
    // Runtime state (not persisted)
    bool initError;                // Initialization failed
    
    UDA1334AConfig()
        : enabled(true),
          sampleRate(44100),
          bitDepth(16),
          autoRoutingEnabled(false),
          initError(false)
    {}
};

class UDA1334AConfigManager {
public:
    UDA1334AConfigManager();

    // Load configuration from LittleFS; create defaults if missing
    bool begin();

    // Save configuration to LittleFS (atomic: .tmp → rename)
    bool save();

    // Load configuration from file
    bool load();

    // Get current configuration
    const UDA1334AConfig& getConfig() const { return _config; }

    // Update configuration fields
    void setEnabled(bool enabled);
    void setSampleRate(uint32_t sampleRate);
    void setBitDepth(int bitDepth);
    void setAutoRoutingEnabled(bool enabled);

    // Convert to JSON for web API
    String toJson() const;

    // Update from JSON (from web form)
    bool updateFromJson(const JsonObject& obj);

private:
    UDA1334AConfig _config;
    bool _dirty;

    // Create default config file
    bool createDefaults();
};

extern UDA1334AConfigManager uda1334aConfig;

#endif // ENABLE_UDA1334A == 1

