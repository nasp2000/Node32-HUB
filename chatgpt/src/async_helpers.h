// Async Helpers :: Non-blocking Operations and Delay Replacement
// Eliminates blocking delay() calls following ESPHome async patterns

#ifndef ASYNC_HELPERS_H
#define ASYNC_HELPERS_H

#include <Arduino.h>
#include <functional>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════════
// ASYNC DELAY (non-blocking alternative to delay())
// ═══════════════════════════════════════════════════════════════════════════

class AsyncDelay {
private:
    unsigned long startTime;
    unsigned long duration;
    bool active;
    
public:
    AsyncDelay() : startTime(0), duration(0), active(false) {}
    
    // Start a non-blocking delay
    void start(unsigned long durationMs) {
        startTime = millis();
        duration = durationMs;
        active = true;
    }
    
    // Check if delay has elapsed
    bool isElapsed() const {
        if (!active) return true;
        return (millis() - startTime) >= duration;
    }
    
    // Get remaining time
    unsigned long remaining() const {
        if (!active) return 0;
        unsigned long elapsed = millis() - startTime;
        return (elapsed >= duration) ? 0 : (duration - elapsed);
    }
    
    // Reset delay
    void reset() {
        active = false;
    }
    
    bool isActive() const { return active; }
};

// ═══════════════════════════════════════════════════════════════════════════
// ASYNC TASK SCHEDULER (simple callback system)
// ═══════════════════════════════════════════════════════════════════════════

class AsyncTaskScheduler {
public:
    using Callback = std::function<void()>;
    
private:
    struct Task {
        Callback callback;
        unsigned long executeAt;
        bool repeat;
        unsigned long interval;
        bool active;
    };
    
    std::vector<Task> tasks;
    size_t nextTaskId;
    
public:
    AsyncTaskScheduler() : nextTaskId(0) {
        tasks.reserve(16); // Pre-allocate for common use cases
    }
    
    // Schedule a one-shot task
    size_t scheduleOnce(Callback cb, unsigned long delayMs) {
        size_t id = nextTaskId++;
        tasks.push_back({
            cb,
            millis() + delayMs,
            false,
            0,
            true
        });
        return id;
    }
    
    // Schedule a repeating task
    size_t scheduleRepeating(Callback cb, unsigned long intervalMs) {
        size_t id = nextTaskId++;
        tasks.push_back({
            cb,
            millis() + intervalMs,
            true,
            intervalMs,
            true
        });
        return id;
    }
    
    // Cancel a scheduled task
    void cancel(size_t id) {
        if (id < tasks.size()) {
            tasks[id].active = false;
        }
    }
    
    // Process pending tasks (call from loop())
    void update() {
        unsigned long now = millis();
        
        for (auto& task : tasks) {
            if (!task.active) continue;
            
            if (now >= task.executeAt) {
                task.callback(); // Execute
                
                if (task.repeat) {
                    task.executeAt = now + task.interval; // Reschedule
                } else {
                    task.active = false; // One-shot complete
                }
            }
        }
        
        // Cleanup inactive tasks periodically (every 100 tasks)
        if (tasks.size() > 100) {
            tasks.erase(
                std::remove_if(tasks.begin(), tasks.end(),
                    [](const Task& t) { return !t.active; }),
                tasks.end()
            );
        }
    }
    
    // Get pending task count
    size_t getPendingCount() const {
        size_t count = 0;
        for (const auto& task : tasks) {
            if (task.active) count++;
        }
        return count;
    }
    
    // Clear all tasks
    void clear() {
        tasks.clear();
        nextTaskId = 0;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// ASYNC STATE MACHINE (for multi-step async operations)
// ═══════════════════════════════════════════════════════════════════════════

template<typename StateEnum>
class AsyncStateMachine {
    using StateHandler = std::function<StateEnum()>; // Returns next state
    
private:
    StateEnum currentState;
    StateEnum initialState;
    std::vector<std::pair<StateEnum, StateHandler>> handlers;
    AsyncDelay stateDelay;
    bool paused;
    
public:
    AsyncStateMachine(StateEnum initial) 
        : currentState(initial), initialState(initial), paused(false) {}
    
    // Register handler for a state
    void onState(StateEnum state, StateHandler handler) {
        handlers.push_back({state, handler});
    }
    
    // Update state machine (call from loop())
    void update() {
        if (paused || !stateDelay.isElapsed()) {
            return;
        }
        
        for (auto& [state, handler] : handlers) {
            if (state == currentState) {
                StateEnum nextState = handler();
                if (nextState != currentState) {
                    currentState = nextState;
                    stateDelay.reset(); // Clear delay on state transition
                }
                break;
            }
        }
    }
    
    // Transition to new state with optional delay
    void transitionTo(StateEnum newState, unsigned long delayMs = 0) {
        currentState = newState;
        if (delayMs > 0) {
            stateDelay.start(delayMs);
        } else {
            stateDelay.reset();
        }
    }
    
    // Reset to initial state
    void reset() {
        currentState = initialState;
        stateDelay.reset();
        paused = false;
    }
    
    StateEnum getState() const { return currentState; }
    void pause() { paused = true; }
    void resume() { paused = false; }
    bool isPaused() const { return paused; }
};

// ═══════════════════════════════════════════════════════════════════════════
// THROTTLE / DEBOUNCE HELPERS
// ═══════════════════════════════════════════════════════════════════════════

class Throttle {
private:
    unsigned long lastExecution;
    unsigned long interval;
    
public:
    Throttle(unsigned long intervalMs) 
        : lastExecution(0), interval(intervalMs) {}
    
    bool canExecute() const {
        return (millis() - lastExecution) >= interval;
    }
    
    void markExecuted() {
        lastExecution = millis();
    }
    
    bool tryExecute() {
        if (canExecute()) {
            markExecuted();
            return true;
        }
        return false;
    }
};

class Debounce {
private:
    unsigned long lastChange;
    unsigned long debounceTime;
    bool lastState;
    bool stableState;
    
public:
    Debounce(unsigned long debounceMs = 50) 
        : lastChange(0), debounceTime(debounceMs), 
          lastState(false), stableState(false) {}
    
    bool update(bool currentState) {
        if (currentState != lastState) {
            lastChange = millis();
            lastState = currentState;
        }
        
        if ((millis() - lastChange) >= debounceTime) {
            if (stableState != lastState) {
                stableState = lastState;
                return true; // State changed
            }
        }
        
        return false; // No change
    }
    
    bool getState() const { return stableState; }
};

// ═══════════════════════════════════════════════════════════════════════════
// GLOBAL INSTANCES
// ═══════════════════════════════════════════════════════════════════════════

extern AsyncTaskScheduler asyncScheduler;

// ═══════════════════════════════════════════════════════════════════════════
// HELPER MACROS
// ═══════════════════════════════════════════════════════════════════════════

// Schedule task with lambda
#define SCHEDULE_ONCE(delayMs, ...) asyncScheduler.scheduleOnce([&]() { __VA_ARGS__; }, delayMs)
#define SCHEDULE_REPEAT(intervalMs, ...) asyncScheduler.scheduleRepeating([&]() { __VA_ARGS__; }, intervalMs)

// Throttled execution
#define THROTTLE(intervalMs, name) static Throttle name(intervalMs); if (name.tryExecute())

// Example usage:
// THROTTLE(1000, myThrottle) {
//     Serial.println("Executed at most once per second");
// }

#endif // ASYNC_HELPERS_H
