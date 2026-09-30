#include "../task_switch_trace.h"
#include <cassert>
#include <cstdio>

static TaskSwitchTraceEvent eventAt(const TaskSwitchTraceRecorder &trace, uint32_t sequence) {
    TaskSwitchTraceEvent event;
    assert(trace.copy(sequence, event));
    return event;
}

int main() {
    static_assert(sizeof(TaskSwitchTraceEvent) <= 24, "trace event must stay compact");
    static_assert(sizeof(TaskSwitchTraceRecorder) <= 2200, "trace recorder RAM budget exceeded");
    TaskSwitchTraceRecorder trace(1);

    // Normal sub-threshold handoffs are discarded and consume no ring space.
    trace.switchedOut(1000, 1, 1);
    trace.switchedIn(1200, 2, 1);
    trace.switchedOut(1300, 2, 1);
    trace.switchedIn(1400, 1, 1);
    assert(trace.count() == 0 && !trace.captureActive());

    // A long dispatch gap retains the task identity and ordered timestamps.
    trace.setLoopSequence(42);
    trace.switchedOut(2000, 1, 1);
    trace.switchedIn(2100, 2, 1);
    trace.switchedOut(7000, 2, 1);
    trace.switchedIn(7100, 3, 1);
    trace.switchedOut(11000, 3, 1);
    trace.switchedIn(12000, 1, 1);
    assert(trace.count() == 6);
    assert(eventAt(trace, 0).kind == TASK_SWITCH_LOOP_OUT);
    assert(eventAt(trace, 1).kind == TASK_SWITCH_TASK_IN);
    assert(eventAt(trace, 1).taskHandle == 2);
    assert(eventAt(trace, 2).kind == TASK_SWITCH_TASK_OUT);
    assert(eventAt(trace, 3).kind == TASK_SWITCH_TASK_IN);
    assert(eventAt(trace, 3).taskHandle == 3);
    assert(eventAt(trace, 5).kind == TASK_SWITCH_LOOP_IN);
    assert(eventAt(trace, 0).captureId == eventAt(trace, 5).captureId);
    assert(eventAt(trace, 0).loopSequence == 42 && eventAt(trace, 5).loopSequence == 42);

    // The ESP-IDF port can restore a task through paths that bypass the wrapped
    // scheduler symbol. The next actual loop entry must close that pending span.
    TaskSwitchTraceRecorder resumed(7);
    resumed.setLoopSequence(90);
    resumed.switchedOut(1000, 7, 1);
    resumed.switchedIn(1100, 8, 1);
    assert(resumed.captureActive());
    resumed.flightTaskLoopStarted(900, 91, 1);
    assert(resumed.captureActive()); // Ignore a timestamp sampled before switch-out.
    resumed.flightTaskLoopStarted(7000, 92, 1);
    assert(!resumed.captureActive() && resumed.count() == 3);
    assert(eventAt(resumed, 2).kind == TASK_SWITCH_LOOP_IN);
    assert(eventAt(resumed, 0).captureId == eventAt(resumed, 2).captureId);

    // Timestamp subtraction is wrap-safe across micros() rollover.
    trace.switchedOut(UINT32_MAX - 999, 1, 1);
    trace.switchedIn(1000, 1, 1);
    assert(trace.count() == 8);
    assert(eventAt(trace, 6).kind == TASK_SWITCH_LOOP_OUT);
    assert(eventAt(trace, 7).kind == TASK_SWITCH_LOOP_IN);

    // Other-core scheduler traffic is ignored by this per-core recorder.
    trace.switchedOut(20000, 1, 1);
    trace.switchedIn(21000, 4, 0);
    trace.switchedIn(20500, 1, 1);
    assert(trace.count() == 8);

    // Bounded pending storage reports truncation instead of allocating memory.
    trace.switchedOut(30000, 1, 1);
    for (uint8_t i = 0; i < TASK_SWITCH_PENDING_CAPACITY + 3; ++i) {
        trace.switchedIn(31000 + i, 5, 1);
        trace.switchedOut(31000 + i, 5, 1);
    }
    trace.switchedIn(33000, 1, 1);
    assert(trace.count() <= TASK_SWITCH_TRACE_CAPACITY);
    const uint32_t last = trace.nextSequence() - 1;
    assert(eventAt(trace, last).kind == TASK_SWITCH_TRACE_OVERFLOW);
    assert(eventAt(trace, last).droppedEvents == 32);

    // Public history is a fixed ring and sequence checks reject overwritten data.
    for (uint8_t i = 0; i < 20; ++i) {
        trace.switchedOut(40000 + i * 3000, 1, 1);
        trace.switchedIn(42000 + i * 3000, 1, 1);
    }
    assert(trace.count() == TASK_SWITCH_TRACE_CAPACITY);
    assert(trace.overwritten() > 0);
    TaskSwitchTraceEvent stale;
    assert(!trace.copy(trace.oldestSequence() - 1, stale));
    puts("task switch trace ring: PASS");
}
