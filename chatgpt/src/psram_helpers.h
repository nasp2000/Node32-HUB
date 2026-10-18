#ifndef PSRAM_HELPERS_H
#define PSRAM_HELPERS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "feature_flags.h"

// Helper: Allocator for ArduinoJson to use PSRAM (SPIRAM)
// Requires PSRAM support in the build and on the target hardware
struct SpiRamAllocator {
    void* allocate(size_t size) {
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
        // Try PSRAM first, fallback to DRAM if unavailable
        // Use heap_caps_malloc for both to ensure deallocate consistency
        if (psramFound()) {
            void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
            if (p) return p;
        }
        // Fallback to DRAM using heap_caps_malloc (not malloc)
        return heap_caps_malloc(size, MALLOC_CAP_8BIT);
#else
        return malloc(size);
#endif
    }

    void deallocate(void* pointer) {
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
        // CRITICAL FIX: heap_caps_free works for ALL heap types (DRAM + PSRAM)
        // Always use heap_caps_free when PSRAM support is compiled in
        heap_caps_free(pointer);
#else
        free(pointer);
#endif
    }

    void* reallocate(void* ptr, size_t new_size) {
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
        // Try PSRAM first, fallback to DRAM if unavailable
        // heap_caps_realloc preserves source memory type when possible
        if (psramFound()) {
            void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
            if (p) return p;
        }
        // Fallback to DRAM realloc
        return heap_caps_realloc(ptr, new_size, MALLOC_CAP_8BIT);
#else
        return realloc(ptr, new_size);
#endif
    }
};

// JSON document type that resides in PSRAM
// Use this instead of DynamicJsonDocument for large objects (>2KB)
// to avoid saturating the internal heap (DRAM).
// Behavior: if PSRAM is available, allocate there; otherwise fall back to normal heap.
// ArduinoJson v7 removed BasicJsonDocument (custom-allocator variant).
// Use DynamicJsonDocument (deprecated compat wrapper in v7) which still accepts a capacity
// argument — ignored at runtime, but allows existing SpiRamJsonDocument doc(N) call sites
// to compile without modification across all source files.
#if ARDUINOJSON_VERSION_MAJOR >= 7
using SpiRamJsonDocument = DynamicJsonDocument;
#else
using SpiRamJsonDocument = BasicJsonDocument<SpiRamAllocator>;
#endif

// C++ standard allocator for use with std::vector and other containers
// Allows vectors to allocate their internal storage in PSRAM
template <class T>
struct PsramAllocatorStd {
    typedef T value_type;
    PsramAllocatorStd() = default;
    template <class U> constexpr PsramAllocatorStd(const PsramAllocatorStd<U>&) noexcept {}
    
    T* allocate(std::size_t n) {
        if(n > std::size_t(-1) / sizeof(T)) return nullptr;
        #if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
        // Try PSRAM first, fallback to DRAM if PSRAM unavailable or full
        // CRITICAL: Always use heap_caps_malloc to match deallocate (heap_caps_free)
        if(psramFound()) {
            T* p = static_cast<T*>(heap_caps_malloc(n*sizeof(T), MALLOC_CAP_SPIRAM));
            if(p) return p;
        }
        // Fallback to DRAM using heap_caps_malloc (not malloc) for consistency
        T* p = static_cast<T*>(heap_caps_malloc(n*sizeof(T), MALLOC_CAP_8BIT));
        return p;
        #else
        // No PSRAM support - use standard malloc
        T* p = static_cast<T*>(malloc(n*sizeof(T)));
        return p;
        #endif
    }
    
    void deallocate(T* p, std::size_t n) noexcept {
        // CRITICAL FIX: heap_caps_free works for ALL heap types (DRAM + PSRAM)
        // Always use heap_caps_free to match allocate() strategy (which may use malloc or heap_caps_malloc)
        // Using free() here would cause heap corruption if pointer came from heap_caps_malloc
        #if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
             heap_caps_free(p); // Works for both DRAM and PSRAM allocations
        #else
             free(p); // Standard C free for builds without PSRAM support
        #endif
    }
};
template <class T, class U>
bool operator==(const PsramAllocatorStd<T>&, const PsramAllocatorStd<U>&) { return true; }
template <class T, class U>
bool operator!=(const PsramAllocatorStd<T>&, const PsramAllocatorStd<U>&) { return false; }


// Dynamic circular buffer allocated in PSRAM
// Compatible with the CircularBuffer API but uses dynamic memory (PSRAM/heap)
// instead of a static array in .bss or stack. Ideal for large buffers (>10KB).
template<typename T>
class PsramCircularBuffer {
public:
    // Constructor: allocate buffer on initialization (or defer if deferred=true)
    PsramCircularBuffer(size_t capacity, bool deferred = false) : _capacity(capacity), _size(0), _head(0), _tail(0) {
        _buffer = nullptr;
        if (!deferred) allocate();
    }
    
    // Explicit allocation (call in setup() if using deferred global instances)
    bool begin() {
        if (!_buffer) allocate();
        return (_buffer != nullptr);
    }

    // Destructor: free memory
    ~PsramCircularBuffer() {
        if (_buffer) {
            #if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
                heap_caps_free(_buffer);
            #else
                free(_buffer);
            #endif
        }
    }

    // Add item. If full, remove the oldest (overwrite / rolling)
    bool push(T item) {
        if (!_buffer) return false;
        if (_capacity == 0) return false;
        
        // If full, advance tail (discard oldest)
        if (isFull()) {
            _tail = (_tail + 1) % _capacity;
            _size--;
        }

        _buffer[_head] = item;
        _head = (_head + 1) % _capacity;
        _size++;
        return true;
    }

    // Remove and return oldest item (FIFO) - compatible with shift()
    T shift() {
        if (_size == 0 || !_buffer) return T();
        T item = _buffer[_tail];
        _tail = (_tail + 1) % _capacity;
        _size--;
        return item;
    }

    // Remove and return most recent item (LIFO) - compatible with pop()
    T pop() {
        if (_size == 0 || !_buffer) return T();
        size_t pos = (_head == 0 ? _capacity - 1 : _head - 1);
        T item = _buffer[pos];
        _head = pos;
        _size--;
        return item;
    }
    
    // Remove most recent item (LIFO) - API compatibility
    T popLast() {
        if (_size == 0 || !_buffer) return T();
        size_t pos = (_head == 0 ? _capacity - 1 : _head - 1);
        T item = _buffer[pos];
        _head = pos;
        _size--;
        return item;
    }
    
    // Clear buffer
    void clear() {
        _head = 0;
        _tail = 0;
        _size = 0;
    }

    // State
    bool isEmpty() const { return _size == 0; }
    bool isFull() const { return _size == _capacity; }
    size_t size() const { return _size; }
    size_t capacity() const { return _capacity; }
    
    // Random access (non-destructive)
    T& operator[](size_t index) {
        if (!_buffer) { static T dummy; return dummy; }
        return _buffer[(_tail + index) % _capacity];
    }
    
    T& first() { return operator[](0); }
    T& last()  { return _buffer[(_head == 0 ? _capacity - 1 : _head - 1)]; }

private:
    void allocate() {
        if (_buffer) return;
        #if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
        if (psramFound()) {
            _buffer = (T*)heap_caps_malloc(_capacity * sizeof(T), MALLOC_CAP_SPIRAM);
        }
        // CRITICAL: Use heap_caps_malloc for fallback (not malloc) so destructor's
        // heap_caps_free() always matches the allocation function used.
        if (!_buffer) {
            _buffer = (T*)heap_caps_malloc(_capacity * sizeof(T), MALLOC_CAP_8BIT);
        }
        #else
        _buffer = (T*)malloc(_capacity * sizeof(T));
        #endif
        if (!_buffer) _capacity = 0;
    }

    T* _buffer;
    size_t _capacity;
    size_t _size;
    size_t _head;
    size_t _tail;
};

#endif // PSRAM_HELPERS_H
