#pragma once

#include "task_switch_trace.h"
#include <stdint.h>

struct TaskIpcTraceEvent {
    uint32_t sequence;
    uint32_t startedUs;
    uint32_t elapsedUs;
    uint32_t callerTask;
    uint32_t callerPc;
    uint32_t callbackPc;
    uint8_t callerCore;
    uint8_t targetCore;
};

void initializeTaskSwitchTrace();
void enableTaskSwitchTrace();
void setTaskSwitchTraceLoopSequence(uint32_t loopSequence);
bool freezeTaskSwitchTrace();
void unfreezeTaskSwitchTrace();
uint8_t taskSwitchTraceCoreCount();
void taskSwitchTraceRange(uint8_t coreId, uint32_t &oldest, uint32_t &next,
                          uint32_t &overwritten);
bool copyTaskSwitchTrace(uint8_t coreId, uint32_t sequence,
                         TaskSwitchTraceEvent &destination);
uint8_t copyTaskIpcTrace(TaskIpcTraceEvent *destination, uint8_t capacity,
                         uint32_t &overwritten);
