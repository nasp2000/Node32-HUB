#pragma once
#include <Arduino.h>

enum class ModuleState : uint8_t {
    Uninitialized = 0,
    Initializing = 1,
    Ready = 2,
    Error = 3,
    Disabled = 4
};

struct ModuleInfo {
    const char* name;
    ModuleState state;
    uint32_t lastActivityMs;
    uint32_t errorCount;
    uint32_t cycleCount;
};

class IModule {
public:
    virtual ~IModule() = default;

    virtual const char* moduleName() const = 0;
    virtual ModuleState state() const { return ModuleState::Ready; }

    virtual bool init() { return true; }
    virtual void loop() {}
    virtual void deinit() {}

    virtual bool isEnabled() const { return true; }
    virtual void onConfigChange() {}
};
