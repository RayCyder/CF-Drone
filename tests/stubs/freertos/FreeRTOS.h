#pragma once

#include <cstdint>

using portMUX_TYPE = int;

#define portMUX_INITIALIZER_UNLOCKED 0
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)

extern int testCriticalDepth;
extern int testCriticalEnterCount;
extern int testCriticalExitCount;

inline void portENTER_CRITICAL(portMUX_TYPE*) {
    ++testCriticalDepth;
    ++testCriticalEnterCount;
}

inline void portEXIT_CRITICAL(portMUX_TYPE*) {
    --testCriticalDepth;
    ++testCriticalExitCount;
}

inline void vTaskDelay(uint32_t) {}

