#pragma once

#include "task_switch_trace.h"
#include <stdint.h>

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
