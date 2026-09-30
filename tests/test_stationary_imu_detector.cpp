#include "Arduino.h"
#include <cassert>
#include <cmath>
#include "../stationary_imu_detector.h"

static bool pushBlock(StationaryImuDetector &detector, const Vector &gyro,
		const Vector &accel) {
	StationaryImuDetector::Result result = StationaryImuDetector::WINDOW_COLLECTING;
	Vector windowMean;
	for (uint16_t i = 0; i < StationaryImuDetector::WINDOW_SAMPLES; ++i)
		result = detector.update(gyro, accel, windowMean);
	return result == StationaryImuDetector::STATIONARY;
}

int main() {
	StationaryImuDetector detector;
	const Vector stationaryGyro(0.02f, -0.01f, 0.005f);
	const Vector gravity(0.0f, 0.0f, StationaryImuDetector::GRAVITY_M_S2);
	Vector windowMean;
	for (uint16_t i = 0; i + 1 < StationaryImuDetector::WINDOW_SAMPLES; ++i)
		assert(detector.update(stationaryGyro, gravity, windowMean) ==
			StationaryImuDetector::WINDOW_COLLECTING);
	assert(detector.update(stationaryGyro, gravity, windowMean) ==
		StationaryImuDetector::STATIONARY);
	assert((windowMean - stationaryGyro).norm() < 1e-6f);

	// A steady rotation is not stationary even when its variance is near zero.
	assert(!pushBlock(detector, Vector(0.1f, 0.0f, 0.0f), gravity));
	assert(pushBlock(detector, stationaryGyro, gravity));

	// Reject angular vibration and translational acceleration by per-axis variance.
	StationaryImuDetector::Result result = StationaryImuDetector::WINDOW_COLLECTING;
	for (uint16_t i = 0; i < StationaryImuDetector::WINDOW_SAMPLES; ++i) {
		const float sign = (i & 1) ? 1.0f : -1.0f;
		result = detector.update(Vector(sign * 0.02f, 0.0f, 0.0f), gravity, windowMean);
	}
	assert(result == StationaryImuDetector::NOT_STATIONARY);
	result = StationaryImuDetector::WINDOW_COLLECTING;
	for (uint16_t i = 0; i < StationaryImuDetector::WINDOW_SAMPLES; ++i) {
		const float sign = (i & 1) ? 1.0f : -1.0f;
		result = detector.update(Vector(), Vector(sign * 0.2f, 0.0f,
			StationaryImuDetector::GRAVITY_M_S2), windowMean);
	}
	assert(result == StationaryImuDetector::NOT_STATIONARY);

	Vector invalidGyro;
	invalidGyro.invalidate();
	assert(detector.update(invalidGyro, gravity, windowMean) ==
		StationaryImuDetector::NOT_STATIONARY);
	assert(!pushBlock(detector, stationaryGyro, Vector(0.0f, 0.0f,
		StationaryImuDetector::GRAVITY_M_S2 * 1.2f)));
	puts("stationary IMU detector regression: PASS");
}
