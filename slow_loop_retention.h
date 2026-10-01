#pragma once

#include <stdint.h>
#include <stddef.h>
#include "loop_metrics.h"
#include "task_switch_trace_runtime.h"

constexpr uint32_t SLOW_LOOP_RETENTION_MAGIC = 0x534C4F57UL;
constexpr uint16_t SLOW_LOOP_RETENTION_VERSION = 1;
constexpr uint8_t SLOW_LOOP_RETENTION_CAPACITY = 4;
constexpr uint8_t SLOW_LOOP_RETENTION_SCHEDULER_CAPACITY = 18;
constexpr uint32_t SLOW_LOOP_RETENTION_NEW_WORST_MARGIN_US = 250;
constexpr uint32_t SLOW_LOOP_RETENTION_SLOT_COMMIT = 0xC04D17EDUL;

struct SlowLoopCapture {
    uint32_t sequence = 0;
    uint32_t capturedAtUs = 0;
    LoopOverrunTrace trace = {};
    uint8_t schedulerCount = 0;
    TaskSwitchTraceEvent scheduler[SLOW_LOOP_RETENTION_SCHEDULER_CAPACITY] = {};
    uint8_t hasIpc = 0;
    TaskIpcTraceEvent ipc = {};
};

constexpr uint32_t SLOW_LOOP_RETENTION_SCHEMA = 0x534C0000UL ^ (uint32_t)sizeof(SlowLoopCapture);

struct SlowLoopRetentionSlot {
    uint32_t commit;
    uint32_t crc;
    uint8_t captureBytes[sizeof(SlowLoopCapture)];
};

struct SlowLoopRetentionStore {
    uint32_t magic;
    uint16_t version;
    uint32_t schema;
    uint8_t count;
    uint8_t nextSlot;
    uint32_t nextSequence;
    uint32_t overwritten;
    uint32_t corruptSlots;
    uint32_t headerCrc;
    SlowLoopRetentionSlot slots[SLOW_LOOP_RETENTION_CAPACITY];
};

uint32_t slowLoopCaptureCrc(const SlowLoopCapture &capture);
bool initializeSlowLoopRetentionStore(SlowLoopRetentionStore &store);
void appendSlowLoopCapture(SlowLoopRetentionStore &store, const SlowLoopCapture &capture);
bool copySlowLoopCapture(const SlowLoopRetentionStore &store, uint8_t index,
                         SlowLoopCapture &destination);
bool replaceLatestSlowLoopCapture(SlowLoopRetentionStore &store,
                                  const SlowLoopCapture &capture);
bool validSlowLoopCapture(const SlowLoopRetentionSlot &slot);
