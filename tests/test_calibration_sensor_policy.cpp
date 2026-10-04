#include <assert.h>

#include "../calibration_sensor_policy.h"

int main() {
	const CalibrationSensorAvailability imuOnly = {true, false};
	assert(accelCalibrationSensorsReady(imuOnly));
	assert(levelCalibrationSensorsReady(imuOnly));
	assert(!magneticHeadingCalibrationSensorsReady(imuOnly));

	const CalibrationSensorAvailability compassOnly = {false, true};
	assert(!accelCalibrationSensorsReady(compassOnly));
	assert(!levelCalibrationSensorsReady(compassOnly));
	assert(magneticHeadingCalibrationSensorsReady(compassOnly));

	return 0;
}
