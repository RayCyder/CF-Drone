#pragma once

#include <stdint.h>

#if defined(ESP32) && defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
#include <esp_attr.h>
#define TASK_SWITCH_TRACE_IRAM IRAM_ATTR __attribute__((always_inline))
#else
#define TASK_SWITCH_TRACE_IRAM
#endif

// Compact, host-testable recorder for FreeRTOS task-switch hooks.
// Hooks pass raw 32-bit ESP32 task handles; this class calls no FreeRTOS APIs.
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
// The flight-loop diagnostic build records only its execution core and uses a
// smaller ring so the instrumentation still fits the ESP32 internal DRAM.
constexpr uint8_t TASK_SWITCH_TRACE_CAPACITY = 18;
constexpr uint8_t TASK_SWITCH_PENDING_CAPACITY = 16;
#else
constexpr uint8_t TASK_SWITCH_TRACE_CAPACITY = 64;
constexpr uint8_t TASK_SWITCH_PENDING_CAPACITY = 24;
#endif
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
// Keep sub-millisecond handoffs in the diagnostic build so 1.5 ms loop
// warnings can be separated from work done by the flight task itself.
constexpr uint32_t TASK_SWITCH_TRACE_MIN_US = 600;
#else
constexpr uint32_t TASK_SWITCH_TRACE_MIN_US = 1500;
#endif

enum TaskSwitchTraceKind : uint8_t {
    TASK_SWITCH_LOOP_OUT = 0,
    TASK_SWITCH_TASK_IN = 1,
    TASK_SWITCH_TASK_OUT = 2,
    TASK_SWITCH_LOOP_IN = 3,
    TASK_SWITCH_TRACE_OVERFLOW = 4
};

struct TaskSwitchTraceEvent {
    uint32_t sequence = 0;
    uint32_t timestampUs = 0;
    uint32_t loopSequence = 0;
    uint32_t taskHandle = 0;
    uint16_t captureId = 0;
    uint16_t droppedEvents = 0;
    uint8_t coreId = 0;
    uint8_t kind = 0;
};

// A switch-away interval is staged in a small local array. It is committed to
// the public ring only if the flight-loop task was away for >=1.5 ms, avoiding
// trace writes on ordinary short scheduler handoffs.
class TaskSwitchTraceRecorder {
public:
    explicit TaskSwitchTraceRecorder(uint32_t flightTaskHandle = 0)
        : flightTaskHandle_(flightTaskHandle) {}

    void setFlightTaskHandle(uint32_t taskHandle) { flightTaskHandle_ = taskHandle; }
    void setLoopSequence(uint32_t loopSequence) { loopSequence_ = loopSequence; }

    // The loop itself confirms that the flight task resumed. Close pending
    // captures here if an ESP-IDF context-restore path bypassed our wrapper.
    void flightTaskLoopStarted(uint32_t timestampUs, uint32_t loopSequence,
                               uint8_t coreId) {
        const uint32_t elapsedUs = timestampUs - startUs_;
        if (active_ && coreId == coreId_ && elapsedUs < 0x80000000UL)
            switchedIn(timestampUs, flightTaskHandle_, coreId);
        loopSequence_ = loopSequence;
    }

    void TASK_SWITCH_TRACE_IRAM switchedOut(uint32_t timestampUs, uint32_t taskHandle, uint8_t coreId) {
        if (taskHandle == flightTaskHandle_) {
            if (active_) finish(timestampUs);
            active_ = true;
            startUs_ = timestampUs;
            coreId_ = coreId;
            pendingCount_ = 0;
            pendingOverflow_ = 0;
            append(timestampUs, taskHandle, coreId, TASK_SWITCH_LOOP_OUT);
        } else if (active_ && coreId == coreId_) {
            append(timestampUs, taskHandle, coreId, TASK_SWITCH_TASK_OUT);
        }
    }

    void TASK_SWITCH_TRACE_IRAM switchedIn(uint32_t timestampUs, uint32_t taskHandle, uint8_t coreId) {
        if (!active_ || coreId != coreId_) return;
        if (taskHandle == flightTaskHandle_) {
            append(timestampUs, taskHandle, coreId, TASK_SWITCH_LOOP_IN);
            finish(timestampUs);
        } else {
            append(timestampUs, taskHandle, coreId, TASK_SWITCH_TASK_IN);
        }
    }

    bool copy(uint32_t sequence, TaskSwitchTraceEvent &destination) const {
        if (sequence < oldestSequence() || sequence >= nextSequence_) return false;
        const TaskSwitchTraceEvent &source = events_[sequence % TASK_SWITCH_TRACE_CAPACITY];
        if (source.sequence != sequence) return false;
        destination = source;
        return true;
    }

    uint32_t oldestSequence() const { return nextSequence_ - count_; }
    uint32_t nextSequence() const { return nextSequence_; }
    uint32_t overwritten() const { return overwritten_; }
    uint8_t count() const { return count_; }
    bool captureActive() const { return active_; }

private:
    void TASK_SWITCH_TRACE_IRAM append(uint32_t timestampUs, uint32_t taskHandle, uint8_t coreId,
                TaskSwitchTraceKind kind) {
        if (pendingCount_ == TASK_SWITCH_PENDING_CAPACITY) {
            if (pendingOverflow_ < UINT16_MAX) ++pendingOverflow_;
            return;
        }
        TaskSwitchTraceEvent &event = pending_[pendingCount_++];
        event.timestampUs = timestampUs;
        event.loopSequence = loopSequence_;
        event.captureId = nextCaptureId_;
        event.taskHandle = taskHandle;
        event.coreId = coreId;
        event.kind = static_cast<uint8_t>(kind);
    }

    void TASK_SWITCH_TRACE_IRAM publish(const TaskSwitchTraceEvent &event) {
        TaskSwitchTraceEvent &destination = events_[nextSequence_ % TASK_SWITCH_TRACE_CAPACITY];
        destination = event;
        destination.sequence = nextSequence_++;
        if (count_ < TASK_SWITCH_TRACE_CAPACITY) ++count_;
        else if (overwritten_ < UINT32_MAX) ++overwritten_;
    }

    void TASK_SWITCH_TRACE_IRAM finish(uint32_t timestampUs) {
        if (!active_) return;
        const uint32_t durationUs = timestampUs - startUs_; // wrap-safe for intervals < 2^32 us
        if (durationUs >= TASK_SWITCH_TRACE_MIN_US) {
            for (uint8_t i = 0; i < pendingCount_; ++i) publish(pending_[i]);
            if (pendingOverflow_) {
                TaskSwitchTraceEvent overflow{};
                overflow.timestampUs = timestampUs;
                overflow.loopSequence = loopSequence_;
                overflow.captureId = nextCaptureId_;
                overflow.droppedEvents = pendingOverflow_;
                overflow.coreId = coreId_;
                overflow.kind = TASK_SWITCH_TRACE_OVERFLOW;
                publish(overflow);
            }
            ++nextCaptureId_; // Wrap is acceptable; event sequence remains 32-bit unique.
        }
        active_ = false;
        pendingCount_ = 0;
        pendingOverflow_ = 0;
    }

    uint32_t flightTaskHandle_ = 0;
    uint16_t nextCaptureId_ = 0;
    uint8_t coreId_ = 0;
    uint8_t pendingCount_ = 0;
    uint16_t pendingOverflow_ = 0;
    uint32_t startUs_ = 0;
    uint32_t loopSequence_ = 0;
    uint32_t nextSequence_ = 0;
    uint32_t overwritten_ = 0;
    uint8_t count_ = 0;
    bool active_ = false;
    TaskSwitchTraceEvent pending_[TASK_SWITCH_PENDING_CAPACITY] = {};
    TaskSwitchTraceEvent events_[TASK_SWITCH_TRACE_CAPACITY] = {};
};

#undef TASK_SWITCH_TRACE_IRAM
