// Object Pools :: Memory Reuse System for Performance and Heap Stability
// Implements ESPHome-style object pooling to eliminate repeated heap allocations

#ifndef OBJECT_POOLS_H
#define OBJECT_POOLS_H

#include <Arduino.h>
#include <string>
#include <vector>
#include <ArduinoJson.h>
#include "psram_helpers.h" // For PSRAM allocation

// ═══════════════════════════════════════════════════════════════════════════
// GENERIC POOL TEMPLATE
// ═══════════════════════════════════════════════════════════════════════════

template<typename T>
class ObjectPool {
private:
    struct PoolEntry {
        T* object;
        bool inUse;
        unsigned long lastUsed; // Timestamp for LRU eviction
    };
    
    std::vector<PoolEntry> pool;
    size_t maxSize;
    size_t hits;
    size_t misses;
    bool usePSRAM;
    
public:
    ObjectPool(size_t max = 8, bool psram = false) 
        : maxSize(max), hits(0), misses(0), usePSRAM(psram) {
        pool.reserve(maxSize);
    }
    
    ~ObjectPool() {
        for (auto& entry : pool) {
            delete entry.object;
        }
        pool.clear();
    }
    
    // Acquire object from pool or create new one
    T* acquire() {
        // Try to find available object
        for (auto& entry : pool) {
            if (!entry.inUse) {
                entry.inUse = true;
                entry.lastUsed = millis();
                hits++;
                return entry.object;
            }
        }
        
        // No available object, create new if under limit
        if (pool.size() < maxSize) {
            T* obj = allocateNew();
            if (obj) {
                pool.push_back({obj, true, millis()});
                misses++;
                return obj;
            }
        }
        
        // Pool exhausted - evict LRU
        auto oldest = pool.begin();
        for (auto it = pool.begin(); it != pool.end(); ++it) {
            if (it->lastUsed < oldest->lastUsed) {
                oldest = it;
            }
        }
        
        if (oldest != pool.end()) {
            oldest->inUse = true;
            oldest->lastUsed = millis();
            resetObject(oldest->object);
            misses++;
            return oldest->object;
        }
        
        // Fallback: create temporary (not pooled)
        misses++;
        return allocateNew();
    }
    
    // Release object back to pool
    void release(T* obj) {
        if (!obj) return;
        
        for (auto& entry : pool) {
            if (entry.object == obj) {
                entry.inUse = false;
                resetObject(obj);
                return;
            }
        }
        
        // Object not from pool - delete it
        delete obj;
    }
    
    // Statistics for monitoring
    void getStats(size_t& poolHits, size_t& poolMisses, size_t& poolSize, size_t& poolCapacity) const {
        poolHits = hits;
        poolMisses = misses;
        poolSize = pool.size();
        poolCapacity = maxSize;
    }
    
    void resetStats() {
        hits = 0;
        misses = 0;
    }
    
protected:
    // Pure virtual - subclasses must implement allocation (types may not have default constructor)
    virtual T* allocateNew() = 0;
    
    virtual void resetObject(T* obj) {
        // Default: no reset needed
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// JSON DOCUMENT POOL (StaticJsonDocument for small, SpiRamJsonDocument reuse)
// ═══════════════════════════════════════════════════════════════════════════

template<size_t Capacity>
class JsonDocumentPool : public ObjectPool<SpiRamJsonDocument> {
public:
    JsonDocumentPool(size_t maxPoolSize = 8, bool psram = false) 
        : ObjectPool<SpiRamJsonDocument>(maxPoolSize, psram) {}
    
protected:
    SpiRamJsonDocument* allocateNew() override {
        return new SpiRamJsonDocument(Capacity);
    }
    
    void resetObject(SpiRamJsonDocument* doc) override {
        if (doc) {
            doc->clear(); // Clear without deallocation
        }
    }
};

// Predefined pool sizes matching common use cases
using JsonPool256  = JsonDocumentPool<256>;
using JsonPool512  = JsonDocumentPool<512>;
using JsonPool1024 = JsonDocumentPool<1024>;
using JsonPool2048 = JsonDocumentPool<2048>;

// ═══════════════════════════════════════════════════════════════════════════
// STRING BUFFER POOL (std::string with pre-allocated capacity)
// ═══════════════════════════════════════════════════════════════════════════

class StringBufferPool : public ObjectPool<std::string> {
private:
    size_t bufferCapacity;
    
public:
    StringBufferPool(size_t capacity, size_t maxPoolSize = 8) 
        : ObjectPool<std::string>(maxPoolSize, false), bufferCapacity(capacity) {}
    
protected:
    std::string* allocateNew() override {
        std::string* str = new std::string();
        str->reserve(bufferCapacity);
        return str;
    }
    
    void resetObject(std::string* str) override {
        if (str) {
            str->clear(); // Clear content but keep capacity
        }
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// RAII GUARD FOR AUTOMATIC RELEASE
// ═══════════════════════════════════════════════════════════════════════════

template<typename T>
class PoolGuard {
private:
    ObjectPool<T>* pool;
    T* object;
    
public:
    PoolGuard(ObjectPool<T>* p) : pool(p), object(nullptr) {
        if (pool) {
            object = pool->acquire();
        }
    }
    
    ~PoolGuard() {
        if (pool && object) {
            pool->release(object);
        }
    }
    
    // Prevent copying
    PoolGuard(const PoolGuard&) = delete;
    PoolGuard& operator=(const PoolGuard&) = delete;
    
    // Allow moving
    PoolGuard(PoolGuard&& other) noexcept 
        : pool(other.pool), object(other.object) {
        other.pool = nullptr;
        other.object = nullptr;
    }
    
    T* get() { return object; }
    T* operator->() { return object; }
    T& operator*() { return *object; }
    operator bool() const { return object != nullptr; }
};

// ═══════════════════════════════════════════════════════════════════════════
// GLOBAL POOL INSTANCES
// ═══════════════════════════════════════════════════════════════════════════

// JSON document pools (initialized in chatgpt.ino)
extern JsonPool256  jsonPool256;
extern JsonPool512  jsonPool512;
extern JsonPool1024 jsonPool1024;
extern JsonPool2048 jsonPool2048;

// String buffer pools
extern StringBufferPool stringPool256;
extern StringBufferPool stringPool1024;
extern StringBufferPool stringPool4096;

// ═══════════════════════════════════════════════════════════════════════════
// HELPER MACROS FOR EASY USAGE
// ═══════════════════════════════════════════════════════════════════════════

// Acquire JSON document with automatic release
#define POOLED_JSON_256(var)  PoolGuard<SpiRamJsonDocument> var##_guard(&jsonPool256);  SpiRamJsonDocument* var = var##_guard.get()
#define POOLED_JSON_512(var)  PoolGuard<SpiRamJsonDocument> var##_guard(&jsonPool512);  SpiRamJsonDocument* var = var##_guard.get()
#define POOLED_JSON_1024(var) PoolGuard<SpiRamJsonDocument> var##_guard(&jsonPool1024); SpiRamJsonDocument* var = var##_guard.get()
#define POOLED_JSON_2048(var) PoolGuard<SpiRamJsonDocument> var##_guard(&jsonPool2048); SpiRamJsonDocument* var = var##_guard.get()

// Acquire string buffer with automatic release
#define POOLED_STRING_256(var)  PoolGuard<std::string> var##_guard(&stringPool256);  std::string* var = var##_guard.get()
#define POOLED_STRING_1024(var) PoolGuard<std::string> var##_guard(&stringPool1024); std::string* var = var##_guard.get()
#define POOLED_STRING_4096(var) PoolGuard<std::string> var##_guard(&stringPool4096); std::string* var = var##_guard.get()

// ═══════════════════════════════════════════════════════════════════════════
// SMART JSON DOCUMENT (auto-selects Static vs Dynamic based on size)
// ═══════════════════════════════════════════════════════════════════════════

template<size_t N>
struct SmartJsonDoc {
    using Type = typename std::conditional<
        (N <= 2048),
        StaticJsonDocument<N>,
        SpiRamJsonDocument
    >::type;
};

// Usage: SmartJsonDoc<512>::Type doc; // Uses StaticJsonDocument<512>
//        SmartJsonDoc<4096>::Type doc; // Uses SpiRamJsonDocument(4096)

#endif // OBJECT_POOLS_H
