#include "slow_loop_retention.h"
#include <string.h>

namespace {
uint32_t crcByte(uint32_t crc, uint8_t byte) {
    crc ^= byte;
    for (uint8_t bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    return crc;
}

template <typename T>
uint32_t crcValue(uint32_t crc, const T &value) {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&value);
    for (size_t i = 0; i < sizeof(T); ++i) crc = crcByte(crc, bytes[i]);
    return crc;
}

uint32_t finishCrc(uint32_t crc) { return crc ^ 0xFFFFFFFFUL; }

uint32_t headerCrc(const SlowLoopRetentionStore &store) {
    uint32_t crc = 0xFFFFFFFFUL;
    crc = crcValue(crc, store.magic);
    crc = crcValue(crc, store.version);
    crc = crcValue(crc, store.schema);
    crc = crcValue(crc, store.count);
    crc = crcValue(crc, store.nextSlot);
    crc = crcValue(crc, store.nextSequence);
    crc = crcValue(crc, store.overwritten);
    crc = crcValue(crc, store.corruptSlots);
    return finishCrc(crc);
}

bool validHeader(const SlowLoopRetentionStore &store) {
    return store.magic == SLOW_LOOP_RETENTION_MAGIC &&
        store.version == SLOW_LOOP_RETENTION_VERSION &&
        store.schema == SLOW_LOOP_RETENTION_SCHEMA &&
        store.count <= SLOW_LOOP_RETENTION_CAPACITY &&
        store.nextSlot < SLOW_LOOP_RETENTION_CAPACITY &&
        store.headerCrc == headerCrc(store);
}

void commitHeader(SlowLoopRetentionStore &store) {
    store.headerCrc = headerCrc(store);
    __sync_synchronize();
}

uint32_t captureCrc(const SlowLoopCapture &capture) {
    uint32_t crc = 0xFFFFFFFFUL;
#define CRC_FIELD(value) crc = crcValue(crc, value)
    CRC_FIELD(capture.sequence);
    CRC_FIELD(capture.capturedAtUs);
    const LoopOverrunTrace &trace = capture.trace;
    CRC_FIELD(trace.sequence);
    CRC_FIELD(trace.uptimeMs);
    CRC_FIELD(trace.dtUs);
    CRC_FIELD(trace.loopSequence);
    for (uint8_t i = 0; i < LOOP_TRACE_STAGE_COUNT; ++i) CRC_FIELD(trace.stageUs[i]);
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
    const ImuWaitTrace &imu = trace.imuWait;
    CRC_FIELD(imu.waitStartedUs);
    CRC_FIELD(imu.waitEndedUs);
    CRC_FIELD(imu.lastInterruptUs);
    CRC_FIELD(imu.interruptCount);
    CRC_FIELD(imu.semaphoreTakes);
    CRC_FIELD(imu.semaphoreTimeouts);
    CRC_FIELD(imu.semaphoreWaitMaxUs);
    CRC_FIELD(imu.readAttempts);
    CRC_FIELD(imu.readyReads);
    CRC_FIELD(imu.readTotalUs);
    CRC_FIELD(imu.readMaxUs);
    CRC_FIELD(imu.interruptSource);
    CRC_FIELD(imu.result);
#endif
    CRC_FIELD(capture.schedulerCount);
    for (uint8_t i = 0; i < capture.schedulerCount; ++i) {
        const TaskSwitchTraceEvent &event = capture.scheduler[i];
        CRC_FIELD(event.sequence);
        CRC_FIELD(event.timestampUs);
        CRC_FIELD(event.loopSequence);
        CRC_FIELD(event.taskHandle);
        CRC_FIELD(event.captureId);
        CRC_FIELD(event.droppedEvents);
        CRC_FIELD(event.coreId);
        CRC_FIELD(event.kind);
    }
    CRC_FIELD(capture.hasIpc);
    if (capture.hasIpc) {
        const TaskIpcTraceEvent &event = capture.ipc;
        CRC_FIELD(event.sequence);
        CRC_FIELD(event.requestUs);
        CRC_FIELD(event.callbackStartedUs);
        CRC_FIELD(event.callbackUs);
        CRC_FIELD(event.callerTask);
        CRC_FIELD(event.callerPc);
        CRC_FIELD(event.callerCore);
        CRC_FIELD(event.targetCore);
    }
#undef CRC_FIELD
    return finishCrc(crc);
}
}

uint32_t slowLoopCaptureCrc(const SlowLoopCapture &capture) {
    return capture.schedulerCount <= SLOW_LOOP_RETENTION_SCHEDULER_CAPACITY &&
        capture.hasIpc <= 1 ? captureCrc(capture) : 0;
}

bool validSlowLoopCapture(const SlowLoopRetentionSlot &slot) {
    SlowLoopCapture capture{};
    memcpy(&capture, slot.captureBytes, sizeof(capture));
    return slot.commit == SLOW_LOOP_RETENTION_SLOT_COMMIT &&
        capture.schedulerCount <= SLOW_LOOP_RETENTION_SCHEDULER_CAPACITY &&
        capture.hasIpc <= 1 && slot.crc == captureCrc(capture);
}

bool initializeSlowLoopRetentionStore(SlowLoopRetentionStore &store) {
    if (validHeader(store)) {
        const uint8_t previousCount = store.count;
        uint8_t validCount = 0;
        uint8_t newestSlot = 0;
        uint32_t newestSequence = 0;
        bool haveSequence = false;
        for (uint8_t i = 0; i < SLOW_LOOP_RETENTION_CAPACITY; ++i) {
            const SlowLoopRetentionSlot &slot = store.slots[i];
            if (!validSlowLoopCapture(slot)) {
                if (slot.commit == SLOW_LOOP_RETENTION_SLOT_COMMIT &&
                    store.corruptSlots < UINT32_MAX) ++store.corruptSlots;
                store.slots[i].commit = 0;
                continue;
            }
            SlowLoopCapture capture{};
            memcpy(&capture, slot.captureBytes, sizeof(capture));
            ++validCount;
            if (!haveSequence || capture.sequence > newestSequence) {
                newestSequence = capture.sequence;
                newestSlot = i;
                haveSequence = true;
            }
        }
        store.count = validCount;
        const bool interruptedFullWrite = previousCount == SLOW_LOOP_RETENTION_CAPACITY &&
            validCount < previousCount;
        if (interruptedFullWrite && store.overwritten < UINT32_MAX) ++store.overwritten;
        if (haveSequence) {
            store.nextSlot = (uint8_t)((newestSlot + 1) % SLOW_LOOP_RETENTION_CAPACITY);
            if (store.nextSequence <= newestSequence) {
                if (previousCount == SLOW_LOOP_RETENTION_CAPACITY && !interruptedFullWrite &&
                    store.overwritten < UINT32_MAX) ++store.overwritten;
                store.nextSequence = newestSequence + 1;
            }
        } else {
            store.nextSlot = 0;
        }
        if (validCount != previousCount && store.corruptSlots < UINT32_MAX)
            ++store.corruptSlots;
        commitHeader(store);
        return store.corruptSlots == 0;
    }

    const bool salvageSlots = store.magic == SLOW_LOOP_RETENTION_MAGIC &&
        store.version == SLOW_LOOP_RETENTION_VERSION &&
        store.schema == SLOW_LOOP_RETENTION_SCHEMA;
    if (!salvageSlots) {
        memset(&store, 0, sizeof(store));
        store.magic = SLOW_LOOP_RETENTION_MAGIC;
        store.version = SLOW_LOOP_RETENTION_VERSION;
        store.schema = SLOW_LOOP_RETENTION_SCHEMA;
        commitHeader(store);
        return false;
    }

    store.count = 0;
    store.schema = SLOW_LOOP_RETENTION_SCHEMA;
    store.nextSlot = 0;
    store.nextSequence = 0;
    store.overwritten = 0;
    store.corruptSlots = 0;
    bool haveSequence = false;
    uint32_t newestSequence = 0;
    uint8_t newestSlot = 0;
    for (uint8_t i = 0; i < SLOW_LOOP_RETENTION_CAPACITY; ++i) {
        const SlowLoopRetentionSlot &slot = store.slots[i];
        if (validSlowLoopCapture(slot)) {
            SlowLoopCapture capture{};
            memcpy(&capture, slot.captureBytes, sizeof(capture));
            ++store.count;
            if (!haveSequence || capture.sequence > newestSequence) {
                newestSequence = capture.sequence;
                newestSlot = i;
                haveSequence = true;
            }
        } else if (slot.commit == SLOW_LOOP_RETENTION_SLOT_COMMIT) {
            ++store.corruptSlots;
            store.slots[i].commit = 0;
        }
    }
    if (haveSequence) {
        store.nextSlot = (uint8_t)((newestSlot + 1) % SLOW_LOOP_RETENTION_CAPACITY);
        store.nextSequence = newestSequence + 1;
    }
    commitHeader(store);
    return false;
}

void appendSlowLoopCapture(SlowLoopRetentionStore &store, const SlowLoopCapture &capture) {
    if (!validHeader(store)) (void)initializeSlowLoopRetentionStore(store);
    SlowLoopRetentionSlot &slot = store.slots[store.nextSlot];
    slot.commit = 0;
    SlowLoopCapture committed = capture;
    committed.sequence = store.nextSequence++;
    memcpy(slot.captureBytes, &committed, sizeof(committed));
    slot.crc = captureCrc(committed);
    __sync_synchronize();
    slot.commit = SLOW_LOOP_RETENTION_SLOT_COMMIT;
    __sync_synchronize();
    if (store.count < SLOW_LOOP_RETENTION_CAPACITY) ++store.count;
    else if (store.overwritten < UINT32_MAX) ++store.overwritten;
    store.nextSlot = (uint8_t)((store.nextSlot + 1) % SLOW_LOOP_RETENTION_CAPACITY);
    commitHeader(store);
}

bool copySlowLoopCapture(const SlowLoopRetentionStore &store, uint8_t index,
                         SlowLoopCapture &destination) {
    if (!validHeader(store) || index >= store.count) return false;
    const uint8_t oldestSlot = (uint8_t)((store.nextSlot + SLOW_LOOP_RETENTION_CAPACITY - store.count) %
        SLOW_LOOP_RETENTION_CAPACITY);
    const SlowLoopRetentionSlot &slot = store.slots[(oldestSlot + index) % SLOW_LOOP_RETENTION_CAPACITY];
    if (!validSlowLoopCapture(slot)) return false;
    memcpy(&destination, slot.captureBytes, sizeof(destination));
    return true;
}

bool replaceLatestSlowLoopCapture(SlowLoopRetentionStore &store,
                                  const SlowLoopCapture &capture) {
    if (!validHeader(store) || !store.count) return false;
    const uint8_t latestSlot = (uint8_t)((store.nextSlot + SLOW_LOOP_RETENTION_CAPACITY - 1) %
        SLOW_LOOP_RETENTION_CAPACITY);
    SlowLoopRetentionSlot &slot = store.slots[latestSlot];
    if (!validSlowLoopCapture(slot)) return false;
    SlowLoopCapture replacement = capture;
    SlowLoopCapture previous{};
    memcpy(&previous, slot.captureBytes, sizeof(previous));
    replacement.sequence = previous.sequence;
    slot.commit = 0;
    memcpy(slot.captureBytes, &replacement, sizeof(replacement));
    slot.crc = captureCrc(replacement);
    __sync_synchronize();
    slot.commit = SLOW_LOOP_RETENTION_SLOT_COMMIT;
    __sync_synchronize();
    return true;
}
