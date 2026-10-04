#include <assert.h>

#include "../level_calibration_policy.h"

int main() {
	constexpr float oneG = 9.80665f;
	assert(levelCalibrationSampleGate(9.799f, 0.0359f, 0.00199f, 0.050f, oneG) == LEVEL_SAMPLE_OK);
	assert(levelCalibrationSampleGate(9.799f, 0.0359f, 0.00199f, 0.061f, oneG) == LEVEL_SAMPLE_GYRO_RATE);
	assert(levelCalibrationSampleGate(9.799f, 0.101f, 0.00199f, 0.0f, oneG) == LEVEL_SAMPLE_ACCEL_NOISE);
	assert(levelCalibrationSampleGate(9.799f, 0.0359f, 0.011f, 0.0f, oneG) == LEVEL_SAMPLE_GYRO_NOISE);
	assert(levelCalibrationSampleGate(8.0f, 0.0f, 0.0f, 0.0f, oneG) == LEVEL_SAMPLE_GRAVITY_NORM);
	return 0;
}
