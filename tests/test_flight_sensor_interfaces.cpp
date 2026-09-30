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
