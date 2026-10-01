#pragma once

#include "FreeRTOS.h"
#include <cstddef>

using QueueHandle_t = void*;

inline QueueHandle_t xQueueCreate(unsigned, size_t) { return nullptr; }
inline int xQueueSend(QueueHandle_t, const void*, uint32_t) { return pdFALSE; }
inline int xQueueReceive(QueueHandle_t, void*, uint32_t) { return pdFALSE; }
inline unsigned uxQueueMessagesWaiting(QueueHandle_t) { return 0; }
inline void vQueueDelete(QueueHandle_t) {}
inline int xTaskCreatePinnedToCore(void (*)(void*), const char*, unsigned, void*, unsigned, void*, int) { return pdFALSE; }

