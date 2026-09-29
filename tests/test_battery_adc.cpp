#include <cassert>
#include <cmath>
#include <cstdio>
#include "../battery_adc.h"

static void batch_tests(uint8_t sampleCount) {
	BatteryAdcAccumulator samples;
	float voltage = 12.0f;
	for (uint8_t i = 1; i < sampleCount; ++i) {
		assert(!samples.add(1000, sampleCount, 43.0f / 33.0f, voltage));
		assert(voltage == 12.0f); // the last complete battery reading stays live
	}
	assert(samples.add(1000, sampleCount, 43.0f / 33.0f, voltage));
	assert(fabsf(voltage - (43.0f / 33.0f)) < 1e-6f);
	assert(samples.sampleCount == 0 && samples.sumMillivolts == 0);
}

int main() {
	batch_tests(8);
	batch_tests(16);
	BatteryAdcSchedule schedule;
	assert(!schedule.due(499));
	assert(schedule.due(500));
	for (uint32_t i = 0; i < 16; ++i) {
		const uint32_t now = 500 + i * BatteryAdcSchedule::SAMPLE_INTERVAL_MS;
		assert(schedule.due(now));
		schedule.sampled(now, i == 15);
		if (i != 15) assert(!schedule.due(now + BatteryAdcSchedule::SAMPLE_INTERVAL_MS - 1));
	}
	assert(!schedule.due(1149) && schedule.due(1150));
	puts("incremental battery ADC regression: PASS");
}
