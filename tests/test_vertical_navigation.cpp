#include <cassert>
#include <cmath>
#include <cstdio>

#include "../vertical_navigation.h"

static BarometerSample baro(float altitudeMeters, uint32_t timestampUs) {
	BarometerSample sample;
	sample.pressurePa = 101000.0f;
	sample.altitudeMeters = altitudeMeters;
	sample.timestampUs = timestampUs;
	sample.valid = true;
	return sample;
}

static DownwardRangeSample range(float distanceMeters, uint32_t timestampUs,
	uint8_t quality = 80) {
	DownwardRangeSample sample;
	sample.distanceMeters = distanceMeters;
	sample.quality = quality;
	sample.rangeStatus = 0;
	sample.timestampUs = timestampUs;
	sample.valid = true;
	return sample;
}

static OpticalFlowSample flow(uint32_t timestampUs, uint8_t quality = 90) {
	OpticalFlowSample sample;
	sample.deltaXAngularRadians = 0.01f;
	sample.deltaYAngularRadians = -0.01f;
	sample.quality = quality;
	sample.timestampUs = timestampUs;
	sample.motionDetected = true;
	sample.valid = true;
	return sample;
}

static VerticalNavigationInput inputAt(uint32_t nowUs) {
	VerticalNavigationInput input;
	input.nowUs = nowUs;
	input.worldAccelZMps2 = 0.0f;
	return input;
}

int main() {
	// VN-1: first usable height source initializes the estimator and missing optional
	// sensors only degrade health; they do not prevent an altitude estimate.
	VerticalNavigationEstimator estimator;
	auto input = inputAt(1000000);
	input.barometer = baro(1.20f, 1000000);
	const auto &initial = estimator.update(input);
	assert(initial.initialized);
	assert(std::fabs(initial.altitudeMeters - 1.20f) < 1.0e-5f);
	assert(initial.health == VerticalNavigationHealth::BarometerOnly);
	assert(initial.acceptedBarometerCount == 1);
	assert(initial.acceptedRangeCount == 0);

	// VN-2: duplicate timestamps are ignored, and a fresh lower range reading pulls
	// the estimate down faster than barometer-only correction.
	input = inputAt(1020000);
	input.barometer = baro(1.20f, 1000000);
	input.range = range(0.90f, 1020000);
	input.flow = flow(1020000);
	const auto &withRange = estimator.update(input);
	assert(withRange.acceptedBarometerCount == 1);
	assert(withRange.acceptedRangeCount == 1);
	assert(withRange.health == VerticalNavigationHealth::Fused);
	assert(withRange.flowHealthy);
	assert(withRange.altitudeMeters < 1.20f);
	assert(withRange.altitudeMeters > 0.90f);

	input = inputAt(1040000);
	input.range = range(0.40f, 1020000);
	const uint32_t acceptedBeforeDuplicate = estimator.state().acceptedRangeCount;
	const float altitudeBeforeDuplicate = estimator.state().altitudeMeters;
	const auto &afterDuplicate = estimator.update(input);
	assert(afterDuplicate.acceptedRangeCount == acceptedBeforeDuplicate);
	assert(std::fabs(afterDuplicate.altitudeMeters - altitudeBeforeDuplicate) < 0.06f);

	// VN-3: unreasonable innovations and excessive tilt are rejected instead of
	// dragging the aircraft toward bad ToF readings.
	input = inputAt(1060000);
	input.range = range(2.80f, 1060000);
	const uint32_t rejectedBefore = estimator.state().rejectedObservationCount;
	const auto &afterOutlier = estimator.update(input);
	assert(afterOutlier.rejectedObservationCount == rejectedBefore + 1);

	input = inputAt(1080000);
	input.range = range(0.60f, 1080000);
	input.rollRadians = 0.90f;
	const uint32_t rangeBeforeTilt = estimator.state().acceptedRangeCount;
	const auto &afterTilt = estimator.update(input);
	assert(afterTilt.acceptedRangeCount == rangeBeforeTilt);

	// VN-4: range-only devices initialize and stay usable, covering IMU+ToF variants
	// without a barometer.
	VerticalNavigationEstimator rangeOnlyEstimator;
	input = inputAt(2000000);
	input.range = range(0.65f, 2000000);
	const auto &rangeOnly = rangeOnlyEstimator.update(input);
	assert(rangeOnly.initialized);
	assert(rangeOnly.health == VerticalNavigationHealth::RangeOnly);
	assert(std::fabs(rangeOnly.altitudeMeters - 0.65f) < 1.0e-5f);

	// VN-5: vertical acceleration prediction is bounded and corrected by later
	// absolute observations.
	VerticalNavigationEstimator accelEstimator;
	input = inputAt(3000000);
	input.barometer = baro(1.00f, 3000000);
	accelEstimator.update(input);
	for (int i = 1; i <= 20; ++i) {
		input = inputAt(3000000 + (uint32_t)i * 20000);
		input.worldAccelZMps2 = 0.70f;
		accelEstimator.update(input);
	}
	assert(accelEstimator.state().altitudeMeters > 1.0f);
	input = inputAt(3440000);
	input.barometer = baro(1.00f, 3440000);
	const float speedBeforeCorrection = accelEstimator.state().verticalSpeedMps;
	const auto &afterCorrection = accelEstimator.update(input);
	assert(afterCorrection.verticalSpeedMps < speedBeforeCorrection);

	// VN-6: controller returns hover throttle when disabled, then slews toward the
	// correction instead of jumping the motors.
	VerticalPositionController controller;
	controller.reset(0.48f);
	auto output = controller.update(1.0f, 0.48f, afterCorrection, 4000000, false);
	assert(!output.active);
	assert(std::fabs(output.throttle - 0.48f) < 1.0e-6f);
	output = controller.update(1.40f, 0.48f, afterCorrection, 4020000, true);
	assert(output.active);
	assert(output.targetVelocityMps > 0.0f);
	assert(output.throttle > 0.48f);
	assert(output.throttle <= 0.4931f);
	const float firstThrottle = output.throttle;
	output = controller.update(1.40f, 0.48f, afterCorrection, 4040000, true);
	assert(output.throttle > firstThrottle);
	assert(output.throttle <= firstThrottle + 0.0131f);

	puts("vertical navigation estimator and hold controller: PASS");
}
