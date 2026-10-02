#pragma once
#include <stdint.h>
#include <math.h>
#include "imu_wait_trace.h"

constexpr uint8_t LOOP_TRACE_STAGE_COUNT = 16;
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
// Keep the task-trace image within ESP32-D DRAM; the peak and RTC records
// remain separate from this recent-event ring.
constexpr uint8_t LOOP_TRACE_CAPACITY = 16;
#else
// Leave DRAM headroom for the optional per-stage monitor on ESP32-D.
constexpr uint8_t LOOP_TRACE_CAPACITY = 30;
#endif

// Integer Q4 EMA keeps fractional-microsecond precision without floating point in
// the 1 kHz loop. Alpha is 1/16 (lambda = 15/16).
struct LoopStageEma {
    uint32_t averageUsQ4 = 0;
    uint32_t samples = 0;

    void update(uint32_t sampleUs) {
        const uint32_t cappedUs = sampleUs > (uint32_t)(INT32_MAX >> 4) ?
            (uint32_t)(INT32_MAX >> 4) : sampleUs;
        const uint32_t sampleQ4 = cappedUs << 4;
        if (!samples) averageUsQ4 = sampleQ4;
        else {
            const int32_t delta = (int32_t)sampleQ4 - (int32_t)averageUsQ4;
            averageUsQ4 = (uint32_t)((int32_t)averageUsQ4 + delta / 16);
        }
        if (samples < UINT32_MAX) ++samples;
    }

    uint32_t roundedUs() const {
        return (averageUsQ4 + 8) >> 4;
    }
};

struct LoopOverrunTrace {
    uint32_t sequence = 0;
    uint32_t uptimeMs = 0;
    uint32_t dtUs = 0;
    uint32_t loopSequence = 0;
    // Fixed order: IMU wait/process, RC+web, estimate, battery ADC, control,
    // motor output, serial input, MAVLink, flight log, parameter sync, LED,
    // diagnostics, Wi-Fi, loop gap, and unaccounted interval time.
    uint32_t stageUs[LOOP_TRACE_STAGE_COUNT] = {};
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
    ImuWaitTrace imuWait = {};
#endif
};

struct LoopOverrunTraceRing {
    LoopOverrunTrace records[LOOP_TRACE_CAPACITY] = {};
    LoopOverrunTrace worstRecord = {};
    uint32_t nextSequence = 0;
    uint32_t overwritten = 0;
    uint8_t count = 0;
    bool hasWorstRecord = false;
    bool frozen = false;

    void clear() {
        // Old slots are ignored via count/sequence; do not memset the ring in
        // a cross-core critical section.
        nextSequence = 0;
        overwritten = 0;
        count = 0;
        hasWorstRecord = false;
        frozen = false;
    }

    void freeze() { frozen = true; }

    void push(const LoopOverrunTrace &trace) {
        if (frozen) return;
        if (!hasWorstRecord || trace.dtUs > worstRecord.dtUs) {
            worstRecord = trace;
            worstRecord.sequence = nextSequence;
            hasWorstRecord = true;
        }
        LoopOverrunTrace &destination = records[nextSequence % LOOP_TRACE_CAPACITY];
        destination = trace;
        destination.sequence = nextSequence++;
        if (count < LOOP_TRACE_CAPACITY) ++count;
        else if (overwritten < UINT32_MAX) ++overwritten;
    }

    uint32_t oldestSequence() const { return nextSequence - count; }

    bool copy(uint32_t sequence, LoopOverrunTrace &destination) const {
        if (sequence < oldestSequence() || sequence >= nextSequence) return false;
        const LoopOverrunTrace &source = records[sequence % LOOP_TRACE_CAPACITY];
        if (source.sequence != sequence) return false;
        destination = source;
        return true;
    }

    bool copyWorst(LoopOverrunTrace &destination) const {
        if (!hasWorstRecord) return false;
        destination = worstRecord;
        return true;
    }
};

struct LoopTimingMetrics {
    static constexpr uint32_t limits[9] = {500,750,1000,1250,1500,2000,3000,5000,10000};
    uint64_t samples = 0, invalid = 0, over1000 = 0, over1500 = 0, missedSlots = 0;
    uint64_t buckets[10] = {};
    uint32_t maximumUs = 0;
    uint32_t observe(float seconds) {
        if (!isfinite(seconds) || seconds <= 0) { ++invalid; return 0; }
        const float scaled = seconds * 1000000.0f;
        const uint32_t us = scaled >= 4294967040.0f ? UINT32_MAX : (uint32_t)(scaled + 0.5f);
        if (!us) { ++invalid; return 0; }
        ++samples;
        if (us > maximumUs) maximumUs = us;
        if (us > 1000) ++over1000;
        if (us > 1500) ++over1500;
        if (us >= 2000) missedSlots += us / 1000 - 1;
        unsigned bucket = 0;
        while (bucket < 9 && us > limits[bucket]) ++bucket;
        ++buckets[bucket];
        return us;
    }
    // Upper bound of the bucket containing P99; zero denotes >10000 us or no samples.
    uint32_t p99UpperUs() const {
        if (!samples) return 0;
        const uint64_t target = samples - samples / 100;
        uint64_t cumulative = 0;
        for (unsigned i = 0; i < 9; ++i) {
            cumulative += buckets[i];
            if (cumulative >= target) return limits[i];
        }
        return 0;
    }
};
