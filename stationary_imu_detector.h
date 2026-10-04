#pragma once

#include "vector.h"

// Low-cost block variance check for online gyro-bias updates. A 128-sample
// window at 1 kHz uses constant memory and avoids retaining raw IMU frames.
class StationaryImuDetector {
public:
	enum Result : uint8_t { WINDOW_COLLECTING, NOT_STATIONARY, STATIONARY };
	static constexpr uint16_t WINDOW_SAMPLES = 128;
	static constexpr float MAX_GYRO_MEAN_RAD_S = 0.01f;
	// Some MPU-6500 units start with zero-rate offsets above 0.1 rad/s. This
	// wider limit is used only before the first bias estimate, while disarmed,
	// landed, and after the variance and gravity checks below pass.
	static constexpr float BOOTSTRAP_GYRO_MEAN_RAD_S = 0.20f;
	static constexpr float MAX_GYRO_VARIANCE = 0.0001f;
	static constexpr float MAX_ACCEL_VARIANCE = 0.01f;
	static constexpr float GRAVITY_M_S2 = 9.80665f;
	static constexpr float GRAVITY_TOLERANCE = 0.1f;

	Result update(const Vector &gyro, const Vector &accel, Vector &windowMeanGyro,
		float maxGyroMeanRadS = MAX_GYRO_MEAN_RAD_S) {
		if (!gyro.valid() || !accel.valid()) {
			reset();
			return NOT_STATIONARY;
		}
		add(gyro, gyroMean, gyroM2);
		add(accel, accelMean, accelM2);
		++count;
		if (count < WINDOW_SAMPLES) return WINDOW_COLLECTING;

		const float invCount = 1.0f / count;
		const Vector gyroVariance = gyroM2 * invCount;
		const Vector accelVariance = accelM2 * invCount;
		const Vector meanAccel = accelMean;
		const bool isStationary = gyroMean.norm() <= maxGyroMeanRadS &&
			gyroVariance.x <= MAX_GYRO_VARIANCE &&
			gyroVariance.y <= MAX_GYRO_VARIANCE &&
			gyroVariance.z <= MAX_GYRO_VARIANCE &&
			accelVariance.x <= MAX_ACCEL_VARIANCE &&
			accelVariance.y <= MAX_ACCEL_VARIANCE &&
			accelVariance.z <= MAX_ACCEL_VARIANCE &&
			fabsf(meanAccel.norm() - GRAVITY_M_S2) <= GRAVITY_M_S2 * GRAVITY_TOLERANCE;
		const Vector windowGyroMean = gyroMean;
		resetWindow();
		if (!isStationary) return NOT_STATIONARY;
		windowMeanGyro = windowGyroMean;
		return STATIONARY;
	}

	void reset() {
		resetWindow();
	}

private:
	void resetWindow() {
		count = 0;
		gyroMean = gyroM2 = accelMean = accelM2 = Vector();
	}

	void add(const Vector &sample, Vector &mean, Vector &m2) const {
		// Welford's online update; the same count is shared by all axes.
		const float nextCount = static_cast<float>(count + 1);
		const Vector delta = sample - mean;
		mean += delta / nextCount;
		const Vector deltaAfter = sample - mean;
		m2 += delta * deltaAfter;
	}

	uint16_t count = 0;
	Vector gyroMean;
	Vector gyroM2;
	Vector accelMean;
	Vector accelM2;
};
