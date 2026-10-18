// PSRAM-backed queue wrapper (mimics CircularBuffer<T, N> API for drop-in replacement)
// Saves DRAM by using std::deque with PSRAM allocator for queue data storage.
#pragma once
#ifndef PSRAM_QUEUE_H_INCLUDED
#define PSRAM_QUEUE_H_INCLUDED

#include <deque>
#include "psram_helpers.h" // PsramAllocatorStd

template<typename T>
class PsramQueue {
private:
    std::deque<T, PsramAllocatorStd<T>> data;
    size_t maxCapacity;

public:
    explicit PsramQueue(size_t capacity) : maxCapacity(capacity) {}
    
    bool isEmpty() const { return data.empty(); }
    bool isFull() const { return data.size() >= maxCapacity; }
    size_t size() const { return data.size(); }
    
    bool push(const T& item) {
        if (isFull()) return false;
        data.push_back(item);
        return true;
    }
    
    T& first() { return data.front(); }
    const T& first() const { return data.front(); }
    
    bool shift() {
        if (isEmpty()) return false;
        data.pop_front();
        return true;
    }
    
    T& operator[](size_t index) { return data[index]; }
    const T& operator[](size_t index) const { return data[index]; }
};
#endif // PSRAM_QUEUE_H_INCLUDED
