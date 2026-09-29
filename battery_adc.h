#pragma once

#include <stdint.h>

struct BatteryAdcSchedule {
	static constexpr uint32_t SAMPLE_INTERVAL_MS = 10;
	static constexpr uint32_t BATCH_PAUSE_MS = 500;
	uint32_t nextSampleMs = BATCH_PAUSE_MS;

	bool due(uint32_t nowMs) const {
		return (int32_t)(nowMs - nextSampleMs) >= 0;
	}

	void sampled(uint32_t nowMs, bool batchComplete) {
		nextSampleMs = nowMs + (batchComplete ? BATCH_PAUSE_MS : SAMPLE_INTERVAL_MS);
	}
};

struct BatteryAdcAccumulator {
	uint32_t sumMillivolts = 0;
	uint8_t sampleCount = 0;

	bool add(uint32_t millivolts, uint8_t requiredSamples, float divider, float &voltage) {
		if (requiredSamples == 0) return false;
		sumMillivolts += millivolts;
		if (++sampleCount < requiredSamples) return false;
		voltage = (sumMillivolts / (1000.0f * requiredSamples)) * divider;
		sumMillivolts = 0;
		sampleCount = 0;
		return true;
	}
};
