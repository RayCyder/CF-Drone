#pragma once

#include <stdint.h>

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
