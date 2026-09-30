#include "task_switch_trace_runtime.h"
#include <Arduino.h>

#if defined(ESP32) && defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
#include <esp_timer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_private/esp_ipc.h"

static constexpr uint8_t TASK_TRACE_CORE_COUNT = 1;

static TaskSwitchTraceRecorder taskSwitchTrace;
static uint32_t loopTaskHandleValue = 0;
static uint8_t loopTaskCore = 0;
static volatile uint32_t taskTraceEnabled = 0;
static volatile uint32_t taskTraceWriters = 0;
static constexpr uint8_t TASK_IPC_TRACE_CAPACITY = 1;
static TaskIpcTraceEvent taskIpcTrace[TASK_IPC_TRACE_CAPACITY] = {};
static uint32_t taskIpcTraceNext = 0;
static uint32_t taskIpcTraceCount = 0;
static uint32_t taskIpcTraceOverwritten = 0;
static portMUX_TYPE taskIpcTraceMux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t currentTaskHandleValue() {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle()));
}

void initializeTaskSwitchTrace() {
    loopTaskHandleValue = currentTaskHandleValue();
    loopTaskCore = static_cast<uint8_t>(xPortGetCoreID());
    taskSwitchTrace.setFlightTaskHandle(loopTaskHandleValue);
}

void enableTaskSwitchTrace() {
    __atomic_store_n(&taskTraceEnabled, 1, __ATOMIC_RELEASE);
}

void setTaskSwitchTraceLoopSequence(uint32_t loopSequence) {
    const BaseType_t core = xPortGetCoreID();
    if (core >= 0 && core == loopTaskCore)
        taskSwitchTrace.setLoopSequence(loopSequence);
}

bool freezeTaskSwitchTrace() {
    __atomic_store_n(&taskTraceEnabled, 0, __ATOMIC_RELEASE);
    const uint32_t startedMs = millis();
    for (;;) {
        bool active = false;
        active = __atomic_load_n(&taskTraceWriters, __ATOMIC_ACQUIRE) != 0;
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
    const TaskSwitchTraceRecorder &trace = taskSwitchTrace;
    oldest = trace.oldestSequence();
    next = trace.nextSequence();
    overwritten = trace.overwritten();
}

bool copyTaskSwitchTrace(uint8_t coreId, uint32_t sequence,
                         TaskSwitchTraceEvent &destination) {
    return coreId < TASK_TRACE_CORE_COUNT && taskSwitchTrace.copy(sequence, destination);
}

uint8_t copyTaskIpcTrace(TaskIpcTraceEvent *destination, uint8_t capacity,
                         uint32_t &overwritten) {
    if (!destination || !capacity) { overwritten = taskIpcTraceOverwritten; return 0; }
    const uint32_t count = taskIpcTraceCount < capacity ? taskIpcTraceCount : capacity;
    const uint32_t first = taskIpcTraceNext - count;
    for (uint32_t i = 0; i < count; ++i)
        destination[i] = taskIpcTrace[(first + i) % TASK_IPC_TRACE_CAPACITY];
    overwritten = taskIpcTraceOverwritten;
    return static_cast<uint8_t>(count);
}

extern "C" void __real_vTaskSwitchContext(void);
extern "C" esp_err_t __real_esp_ipc_call_nonblocking(uint32_t cpuId,
                                                      esp_ipc_func_t function,
                                                      void *argument);
extern "C" void spi_flash_op_block_func(void *argument) __attribute__((weak));

extern "C" esp_err_t __wrap_esp_ipc_call_nonblocking(uint32_t cpuId,
                                                      esp_ipc_func_t function,
                                                      void *argument) {
    if (!__atomic_load_n(&taskTraceEnabled, __ATOMIC_ACQUIRE) || cpuId != loopTaskCore ||
        !spi_flash_op_block_func || function != spi_flash_op_block_func)
        return __real_esp_ipc_call_nonblocking(cpuId, function, argument);

    __atomic_add_fetch(&taskTraceWriters, 1, __ATOMIC_ACQUIRE);
    if (!__atomic_load_n(&taskTraceEnabled, __ATOMIC_ACQUIRE)) {
        __atomic_sub_fetch(&taskTraceWriters, 1, __ATOMIC_RELEASE);
        return __real_esp_ipc_call_nonblocking(cpuId, function, argument);
    }
    const uint32_t startedUs = static_cast<uint32_t>(esp_timer_get_time());
    const BaseType_t callerCoreValue = xPortGetCoreID();
    const uint8_t callerCore = callerCoreValue < 0 ? UINT8_MAX : static_cast<uint8_t>(callerCoreValue);
    const uint32_t callerTask = currentTaskHandleValue();
    const uint32_t callerPc = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    const uint32_t callbackPc = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(function));
    const esp_err_t result = __real_esp_ipc_call_nonblocking(cpuId, function, argument);
    const uint32_t elapsedUs = static_cast<uint32_t>(esp_timer_get_time()) - startedUs;

    portENTER_CRITICAL(&taskIpcTraceMux);
    TaskIpcTraceEvent &event = taskIpcTrace[0];
    event.sequence = taskIpcTraceNext++;
    event.startedUs = startedUs;
    event.elapsedUs = elapsedUs;
    event.callerTask = callerTask;
    event.callerPc = callerPc;
    event.callbackPc = callbackPc;
    event.callerCore = callerCore;
    event.targetCore = static_cast<uint8_t>(cpuId);
    if (taskIpcTraceCount < TASK_IPC_TRACE_CAPACITY) ++taskIpcTraceCount;
    else if (taskIpcTraceOverwritten < UINT32_MAX) ++taskIpcTraceOverwritten;
    portEXIT_CRITICAL(&taskIpcTraceMux);
    __atomic_sub_fetch(&taskTraceWriters, 1, __ATOMIC_RELEASE);
    return result;
}

extern "C" void __wrap_vTaskSwitchContext(void) {
    if (!__atomic_load_n(&taskTraceEnabled, __ATOMIC_ACQUIRE)) {
        __real_vTaskSwitchContext();
        return;
    }
    const BaseType_t coreValue = xPortGetCoreID();
    if (coreValue < 0 || coreValue != loopTaskCore) {
        __real_vTaskSwitchContext();
        return;
    }
    const uint8_t core = static_cast<uint8_t>(coreValue);
    __atomic_add_fetch(&taskTraceWriters, 1, __ATOMIC_ACQUIRE);
    if (__atomic_load_n(&taskTraceEnabled, __ATOMIC_ACQUIRE)) {
        const uint32_t timestampUs = static_cast<uint32_t>(esp_timer_get_time());
        const uint32_t outgoing = currentTaskHandleValue();
        taskSwitchTrace.switchedOut(timestampUs, outgoing, core);
        __real_vTaskSwitchContext();
        const uint32_t incoming = currentTaskHandleValue();
        taskSwitchTrace.switchedIn(timestampUs, incoming, core);
    } else {
        __real_vTaskSwitchContext();
    }
    __atomic_sub_fetch(&taskTraceWriters, 1, __ATOMIC_RELEASE);
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
uint8_t copyTaskIpcTrace(TaskIpcTraceEvent *, uint8_t, uint32_t &overwritten) {
    overwritten = 0;
    return 0;
}
#endif
