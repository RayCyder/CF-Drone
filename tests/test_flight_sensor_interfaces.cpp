#include "../flight_sensor_interfaces.h"
#include <cassert>
#include <cmath>

int main() {
	BarometerSample barometer;
	barometer.pressurePa = 100800.0f;
	barometer.altitudeMeters = 12.5f;
	barometer.timestampUs = 0xfffffff0u;
	barometer.valid = true;
	assert(barometerSampleUsable(barometer, 0x10u, 32u)); // micros wrap
	assert(!barometerSampleUsable(barometer, 0x20u, 32u));
	barometer.pressurePa = NAN;
	assert(!barometerSampleUsable(barometer, 0x10u, 32u));

	BarometerEstimate estimate;
	estimate.sample.pressurePa = 100800.0f;
	estimate.sample.altitudeMeters = 12.5f;
	estimate.sample.timestampUs = 1000;
	estimate.sample.valid = true;
	estimate.relativeAltitudeMeters = 1.2f;
	estimate.verticalSpeedMps = -0.5f;
	estimate.valid = true;
	assert(barometerEstimateUsable(estimate, 1100, 200));
	estimate.verticalSpeedMps = NAN;
	assert(!barometerEstimateUsable(estimate, 1100, 200));

	DownwardRangeSample range;
	range.distanceMeters = 0.42f;
	range.quality = 80;
	range.timestampUs = 1000;
	range.valid = true;
	assert(downwardRangeSampleUsable(range, 1100, 200, 50));
	assert(!downwardRangeSampleUsable(range, 1100, 200, 81));
	range.distanceMeters = 0.0f;
	assert(!downwardRangeSampleUsable(range, 1100, 200, 50));
}
