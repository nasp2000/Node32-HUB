#include "ModuleRegistry.h"
#include "direct_log.h"

ModuleRegistry& ModuleRegistry::instance() {
    static ModuleRegistry registry;
    return registry;
}

bool ModuleRegistry::registerModule(IModule* module) {
    if (!module || !module->moduleName()) return false;
    // Check for duplicates
    for (auto* m : modules) {
        if (strcmp(m->moduleName(), module->moduleName()) == 0) {
            return false;
        }
    }
    modules.push_back(module);
    return true;
}

bool ModuleRegistry::unregisterModule(const char* name) {
    for (auto it = modules.begin(); it != modules.end(); ++it) {
        if (strcmp((*it)->moduleName(), name) == 0) {
            (*it)->deinit();
            modules.erase(it);
            return true;
        }
    }
    return false;
}

IModule* ModuleRegistry::getModule(const char* name) {
    for (auto* m : modules) {
        if (strcmp(m->moduleName(), name) == 0) {
            return m;
        }
    }
    return nullptr;
}

void ModuleRegistry::initAll() {
    for (auto* m : modules) {
        if (m->isEnabled() && m->state() != ModuleState::Ready) {
            directLogPrintln(String("  init: ") + m->moduleName());
            m->init();
        }
    }
}

void ModuleRegistry::loopAll() {
    for (auto* m : modules) {
        if (m->isEnabled()) {
            m->loop();
        }
    }
}

void ModuleRegistry::deinitAll() {
    for (auto it = modules.rbegin(); it != modules.rend(); ++it) {
        (*it)->deinit();
    }
}

bool ModuleRegistry::allReady() const {
    for (auto* m : modules) {
        if (m->isEnabled() && m->state() != ModuleState::Ready) {
            return false;
        }
    }
    return true;
}

void ModuleRegistry::visit(Visitor v) {
    for (auto* m : modules) {
        v(m);
    }
}

ModuleRegistry::~ModuleRegistry() {
    deinitAll();
}
