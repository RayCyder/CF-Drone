#include "../flight_sensor_interfaces.h"
#include <cassert>
#include <cmath>
#include <cstring>

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

	OpticalFlowSample flow;
	flow.valid = true;
	flow.quality = 80;
	flow.timestampUs = 1000;
	flow.motionDetected = false;
	assert(classifyOpticalFlowSample(flow, 1100, 200, 50) == OpticalFlowSampleState::FreshZero);
	assert(!strcmp(opticalFlowSampleStateName(OpticalFlowSampleState::FreshZero), "fresh_zero"));
	assert(opticalFlowSampleUsable(flow, 1100, 200, 50));
	flow.motionDetected = true;
	assert(classifyOpticalFlowSample(flow, 1100, 200, 50) == OpticalFlowSampleState::FreshMotion);
	flow.quality = 20;
	assert(classifyOpticalFlowSample(flow, 1100, 200, 50) == OpticalFlowSampleState::LowQuality);
	flow.quality = 80;
	assert(classifyOpticalFlowSample(flow, 1300, 200, 50) == OpticalFlowSampleState::Stale);
}
