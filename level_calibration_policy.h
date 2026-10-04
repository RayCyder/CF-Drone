#pragma once

#include <math.h>

enum LevelCalibrationSampleGate {
	LEVEL_SAMPLE_OK = 0,
	LEVEL_SAMPLE_GRAVITY_INVALID,
	LEVEL_SAMPLE_GRAVITY_NORM,
	LEVEL_SAMPLE_ACCEL_NOISE,
	LEVEL_SAMPLE_GYRO_NOISE,
	LEVEL_SAMPLE_GYRO_RATE
};

inline LevelCalibrationSampleGate levelCalibrationSampleGate(
	float gravityNorm, float accelSd, float gyroSd, float gyroMeanNorm, float oneG) {
	if (!isfinite(gravityNorm) || !isfinite(accelSd) || !isfinite(gyroSd) ||
		!isfinite(gyroMeanNorm)) return LEVEL_SAMPLE_GRAVITY_INVALID;
	if (fabsf(gravityNorm - oneG) > oneG * 0.05f) return LEVEL_SAMPLE_GRAVITY_NORM;
	if (accelSd > 0.10f) return LEVEL_SAMPLE_ACCEL_NOISE;
	if (gyroSd > 0.01f) return LEVEL_SAMPLE_GYRO_NOISE;
	// MPU-6500 startup bias can approach 0.05 rad/s. Low sample variance and
	// stable gravity still provide a valid roll/pitch mounting reference.
	if (gyroMeanNorm > 0.06f) return LEVEL_SAMPLE_GYRO_RATE;
	return LEVEL_SAMPLE_OK;
}

inline const char *levelCalibrationSampleGateReason(LevelCalibrationSampleGate gate) {
	switch (gate) {
		case LEVEL_SAMPLE_OK: return "ready";
		case LEVEL_SAMPLE_GRAVITY_INVALID: return "gravity_invalid";
		case LEVEL_SAMPLE_GRAVITY_NORM: return "gravity_norm_out_of_range";
		case LEVEL_SAMPLE_ACCEL_NOISE: return "accel_noise_too_high";
		case LEVEL_SAMPLE_GYRO_NOISE: return "gyro_noise_too_high";
		case LEVEL_SAMPLE_GYRO_RATE: return "gyro_rate_too_high";
	}
	return "sample_invalid";
}
