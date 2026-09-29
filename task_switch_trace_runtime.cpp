#include "task_switch_trace_runtime.h"
#include <Arduino.h>

#if defined(ESP32) && defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
#include <esp_timer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
static constexpr uint8_t TASK_TRACE_CORE_COUNT = 2;
#else
static constexpr uint8_t TASK_TRACE_CORE_COUNT = 1;
#endif

static TaskSwitchTraceRecorder taskSwitchTrace[TASK_TRACE_CORE_COUNT];
static uint32_t loopTaskHandleValue = 0;
static volatile uint32_t taskTraceEnabled = 0;
static volatile uint32_t taskTraceWriters[TASK_TRACE_CORE_COUNT] = {};

static uint32_t currentTaskHandleValue() {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle()));
}

void initializeTaskSwitchTrace() {
    loopTaskHandleValue = currentTaskHandleValue();
    for (uint8_t core = 0; core < TASK_TRACE_CORE_COUNT; ++core)
        taskSwitchTrace[core].setFlightTaskHandle(loopTaskHandleValue);
}

void enableTaskSwitchTrace() {
    __atomic_store_n(&taskTraceEnabled, 1, __ATOMIC_RELEASE);
}

void setTaskSwitchTraceLoopSequence(uint32_t loopSequence) {
    const BaseType_t core = xPortGetCoreID();
    if (core >= 0 && core < TASK_TRACE_CORE_COUNT)
        taskSwitchTrace[core].setLoopSequence(loopSequence);
}

bool freezeTaskSwitchTrace() {
    __atomic_store_n(&taskTraceEnabled, 0, __ATOMIC_RELEASE);
    const uint32_t startedMs = millis();
    for (;;) {
        bool active = false;
        for (uint8_t core = 0; core < TASK_TRACE_CORE_COUNT; ++core)
            active = active || (__atomic_load_n(&taskTraceWriters[core], __ATOMIC_ACQUIRE) != 0);
        if (!active) return true;
        if ((uint32_t)(millis() - startedMs) >= 100) {
            __atomic_store_n(&taskTraceEnabled, 1, __ATOMIC_RELEASE);
            return false;
        }
        vTaskDelay(1);
    }
}

void unfreezeTaskSwitchTrace() {
    __atomic_store_n(&taskTraceEnabled, 1, __ATOMIC_RELEASE);
}

uint8_t taskSwitchTraceCoreCount() { return TASK_TRACE_CORE_COUNT; }

void taskSwitchTraceRange(uint8_t coreId, uint32_t &oldest, uint32_t &next,
                          uint32_t &overwritten) {
    if (coreId >= TASK_TRACE_CORE_COUNT) { oldest = next = overwritten = 0; return; }
    const TaskSwitchTraceRecorder &trace = taskSwitchTrace[coreId];
    oldest = trace.oldestSequence();
    next = trace.nextSequence();
    overwritten = trace.overwritten();
}

bool copyTaskSwitchTrace(uint8_t coreId, uint32_t sequence,
                         TaskSwitchTraceEvent &destination) {
    return coreId < TASK_TRACE_CORE_COUNT && taskSwitchTrace[coreId].copy(sequence, destination);
}

extern "C" void __real_vTaskSwitchContext(void);

extern "C" void __wrap_vTaskSwitchContext(void) {
    if (!__atomic_load_n(&taskTraceEnabled, __ATOMIC_ACQUIRE)) {
        __real_vTaskSwitchContext();
        return;
    }
    const BaseType_t coreValue = xPortGetCoreID();
    if (coreValue < 0 || coreValue >= TASK_TRACE_CORE_COUNT) {
        __real_vTaskSwitchContext();
        return;
    }
    const uint8_t core = static_cast<uint8_t>(coreValue);
    __atomic_add_fetch(&taskTraceWriters[core], 1, __ATOMIC_ACQUIRE);
    if (__atomic_load_n(&taskTraceEnabled, __ATOMIC_ACQUIRE)) {
        const uint32_t timestampUs = static_cast<uint32_t>(esp_timer_get_time());
        const uint32_t outgoing = currentTaskHandleValue();
        taskSwitchTrace[core].switchedOut(timestampUs, outgoing, core);
        __real_vTaskSwitchContext();
        const uint32_t incoming = currentTaskHandleValue();
        taskSwitchTrace[core].switchedIn(timestampUs, incoming, core);
    } else {
        __real_vTaskSwitchContext();
    }
    __atomic_sub_fetch(&taskTraceWriters[core], 1, __ATOMIC_RELEASE);
}

#else
// Standard production builds do not wrap the FreeRTOS scheduler. The recorder
// API remains available to host tests and the dedicated diagnostic build.
void initializeTaskSwitchTrace() {}
void enableTaskSwitchTrace() {}
void setTaskSwitchTraceLoopSequence(uint32_t) {}
bool freezeTaskSwitchTrace() { return false; }
void unfreezeTaskSwitchTrace() {}
uint8_t taskSwitchTraceCoreCount() { return 0; }
void taskSwitchTraceRange(uint8_t, uint32_t &oldest, uint32_t &next, uint32_t &overwritten) {
    oldest = next = overwritten = 0;
}
bool copyTaskSwitchTrace(uint8_t, uint32_t, TaskSwitchTraceEvent &) { return false; }
#endif
