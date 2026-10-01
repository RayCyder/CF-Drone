#include <cassert>
#include <cstdio>
#include <cstring>

#include "../slow_loop_retention.cpp"

static SlowLoopCapture makeCapture(uint32_t marker) {
    SlowLoopCapture capture = {};
    capture.capturedAtUs = 100000 + marker;
    capture.trace.uptimeMs = 2000 + marker;
    capture.trace.dtUs = 1500 + marker;
    capture.trace.loopSequence = 3000 + marker;
    capture.trace.stageUs[0] = 10 + marker;
    capture.trace.stageUs[LOOP_TRACE_STAGE_COUNT - 1] = 90 + marker;
    capture.schedulerCount = 2;
    capture.scheduler[0].timestampUs = 4000 + marker;
    capture.scheduler[0].loopSequence = capture.trace.loopSequence;
    capture.scheduler[0].taskHandle = 0x1000 + marker;
    capture.scheduler[0].captureId = 7 + marker;
    capture.scheduler[0].coreId = 1;
    capture.scheduler[0].kind = TASK_SWITCH_LOOP_OUT;
    capture.scheduler[1].timestampUs = 5000 + marker;
    capture.scheduler[1].loopSequence = capture.trace.loopSequence;
    capture.scheduler[1].taskHandle = 0x2000 + marker;
    capture.scheduler[1].captureId = 7 + marker;
    capture.scheduler[1].coreId = 1;
    capture.scheduler[1].kind = TASK_SWITCH_LOOP_IN;
    capture.hasIpc = 1;
    capture.ipc.sequence = marker;
    capture.ipc.requestUs = 6000 + marker;
    capture.ipc.callbackStartedUs = 6100 + marker;
    capture.ipc.callbackUs = 70 + marker;
    capture.ipc.callerTask = 0x3000 + marker;
    capture.ipc.callerPc = 0x4000 + marker;
    capture.ipc.callerCore = 0;
    capture.ipc.targetCore = 1;
    return capture;
}

static SlowLoopCapture copiedAt(const SlowLoopRetentionStore &store, uint8_t index) {
    SlowLoopCapture capture = {};
    assert(copySlowLoopCapture(store, index, capture));
    return capture;
}

static void initializationResetsUnrecognizedMemory() {
    SlowLoopRetentionStore store = {};
    memset(&store, 0xA5, sizeof(store));

    assert(!initializeSlowLoopRetentionStore(store));

    assert(store.magic == SLOW_LOOP_RETENTION_MAGIC);
    assert(store.version == SLOW_LOOP_RETENTION_VERSION);
    assert(store.schema == SLOW_LOOP_RETENTION_SCHEMA);
    assert(store.count == 0);
    assert(store.nextSlot == 0);
    assert(store.nextSequence == 0);
    assert(store.overwritten == 0);
    assert(store.corruptSlots == 0);
}

static void appendCopyReturnsCapturesInChronologicalOrder() {
    SlowLoopRetentionStore store = {};
    initializeSlowLoopRetentionStore(store);

    appendSlowLoopCapture(store, makeCapture(1));
    appendSlowLoopCapture(store, makeCapture(2));
    appendSlowLoopCapture(store, makeCapture(3));

    assert(store.count == 3);
    assert(store.nextSlot == 3);
    assert(store.nextSequence == 3);
    assert(copiedAt(store, 0).sequence == 0);
    assert(copiedAt(store, 0).trace.dtUs == 1501);
    assert(copiedAt(store, 1).sequence == 1);
    assert(copiedAt(store, 1).scheduler[1].taskHandle == 0x2002);
    assert(copiedAt(store, 2).sequence == 2);
    assert(copiedAt(store, 2).ipc.callbackUs == 73);
    SlowLoopCapture unused = {};
    assert(!copySlowLoopCapture(store, 3, unused));
}

static void capacityOverwriteKeepsNewestWindowInOrder() {
    SlowLoopRetentionStore store = {};
    initializeSlowLoopRetentionStore(store);

    for (uint32_t marker = 10; marker < 16; ++marker) {
        appendSlowLoopCapture(store, makeCapture(marker));
    }

    assert(store.count == SLOW_LOOP_RETENTION_CAPACITY);
    assert(store.overwritten == 2);
    assert(store.nextSequence == 6);
    for (uint8_t index = 0; index < SLOW_LOOP_RETENTION_CAPACITY; ++index) {
        const SlowLoopCapture capture = copiedAt(store, index);
        assert(capture.sequence == 2 + index);
        assert(capture.trace.dtUs == 1500 + 12 + index);
    }
}

static void replaceLatestPreservesSequenceAndOlderCaptures() {
    SlowLoopRetentionStore store = {};
    initializeSlowLoopRetentionStore(store);
    appendSlowLoopCapture(store, makeCapture(21));
    appendSlowLoopCapture(store, makeCapture(22));
    const SlowLoopCapture oldestBefore = copiedAt(store, 0);

    SlowLoopCapture replacement = makeCapture(99);
    replacement.sequence = 12345;
    assert(replaceLatestSlowLoopCapture(store, replacement));

    const SlowLoopCapture oldestAfter = copiedAt(store, 0);
    const SlowLoopCapture latest = copiedAt(store, 1);
    assert(oldestAfter.sequence == oldestBefore.sequence);
    assert(oldestAfter.trace.dtUs == oldestBefore.trace.dtUs);
    assert(latest.sequence == 1);
    assert(latest.trace.dtUs == 1599);
    assert(latest.ipc.callbackUs == 169);
}

static void validHeaderRecoveryCopiesNonContiguousValidSlotsInOrder() {
    SlowLoopRetentionStore store = {};
    initializeSlowLoopRetentionStore(store);
    appendSlowLoopCapture(store, makeCapture(31));
    appendSlowLoopCapture(store, makeCapture(32));
    appendSlowLoopCapture(store, makeCapture(33));
    store.slots[1].crc ^= 0x1;

    assert(!initializeSlowLoopRetentionStore(store));

    assert(store.count == 2);
    assert(store.corruptSlots >= 2);
    assert(copiedAt(store, 0).sequence == 0);
    assert(copiedAt(store, 0).trace.dtUs == 1531);
    assert(copiedAt(store, 1).sequence == 2);
    assert(copiedAt(store, 1).trace.dtUs == 1533);
    assert(!initializeSlowLoopRetentionStore(store)); // Historical corruption remains reported.

    appendSlowLoopCapture(store, makeCapture(34));
    assert(store.count == 3);
    assert(copiedAt(store, 0).sequence == 0);
    assert(copiedAt(store, 1).sequence == 2);
    assert(copiedAt(store, 2).sequence == 3);
}

static void corruptedHeaderSalvagesNonContiguousCommittedSlotsInOrder() {
    SlowLoopRetentionStore store = {};
    initializeSlowLoopRetentionStore(store);
    appendSlowLoopCapture(store, makeCapture(41));
    appendSlowLoopCapture(store, makeCapture(42));
    appendSlowLoopCapture(store, makeCapture(43));
    store.slots[1].crc ^= 0x1;
    store.headerCrc ^= 0x80;

    assert(!initializeSlowLoopRetentionStore(store));

    assert(store.count == 2);
    assert(store.nextSlot == 3);
    assert(store.nextSequence == 3);
    assert(copiedAt(store, 0).trace.dtUs == 1541);
    assert(copiedAt(store, 1).trace.dtUs == 1543);
    assert(!initializeSlowLoopRetentionStore(store)); // Historical corruption remains reported.

    appendSlowLoopCapture(store, makeCapture(44));
    assert(store.count == 3);
    assert(copiedAt(store, 0).sequence == 0);
    assert(copiedAt(store, 1).sequence == 2);
    assert(copiedAt(store, 2).sequence == 3);
}

static void interruptedFullRingWriteIsRecoveredAfterReboot() {
    SlowLoopRetentionStore store = {};
    initializeSlowLoopRetentionStore(store);
    for (uint32_t marker = 51; marker < 55; ++marker) {
        appendSlowLoopCapture(store, makeCapture(marker));
    }
    assert(store.count == SLOW_LOOP_RETENTION_CAPACITY);
    assert(store.nextSlot == 0);
    const uint32_t overwrittenBefore = store.overwritten;

    store.slots[store.nextSlot].commit = 0;
    const SlowLoopCapture interrupted = makeCapture(99);
    memcpy(store.slots[store.nextSlot].captureBytes, &interrupted, sizeof(interrupted));

    assert(!initializeSlowLoopRetentionStore(store));

    assert(store.count == SLOW_LOOP_RETENTION_CAPACITY - 1);
    assert(store.overwritten == overwrittenBefore + 1);
    assert(copiedAt(store, 0).sequence == 1);
    assert(copiedAt(store, 2).sequence == 3);
    assert(store.nextSlot == 0);
    SlowLoopCapture unused = {};
    assert(!copySlowLoopCapture(store, 3, unused));
}

int main() {
    initializationResetsUnrecognizedMemory();
    appendCopyReturnsCapturesInChronologicalOrder();
    capacityOverwriteKeepsNewestWindowInOrder();
    replaceLatestPreservesSequenceAndOlderCaptures();
    validHeaderRecoveryCopiesNonContiguousValidSlotsInOrder();
    corruptedHeaderSalvagesNonContiguousCommittedSlotsInOrder();
    interruptedFullRingWriteIsRecoveredAfterReboot();
    puts("slow loop retention regressions passed");
}
