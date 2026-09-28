#pragma once
#include <stdint.h>
#include <math.h>

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
