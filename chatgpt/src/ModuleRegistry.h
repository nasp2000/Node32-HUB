#pragma once
#include "interfaces/IModule.h"
#include <vector>
#include <functional>

class ModuleRegistry {
public:
    static ModuleRegistry& instance();

    bool registerModule(IModule* module);
    bool unregisterModule(const char* name);
    IModule* getModule(const char* name);

    void initAll();
    void loopAll();
    void deinitAll();

    size_t count() const { return modules.size(); }
    bool allReady() const;

    using Visitor = std::function<void(IModule*)>;
    void visit(Visitor v);

private:
    ModuleRegistry() = default;
    ~ModuleRegistry();
    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;

    std::vector<IModule*> modules;
};
