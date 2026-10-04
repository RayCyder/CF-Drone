#pragma once

struct CalibrationSensorAvailability {
	bool imuReady;
	bool compassReady;
};

// Roll/pitch calibration uses gravity and gyro data from the IMU. A compass
// is optional and is only required for magnetic-heading calibration.
inline bool accelCalibrationSensorsReady(const CalibrationSensorAvailability &sensors) {
	return sensors.imuReady;
}

inline bool levelCalibrationSensorsReady(const CalibrationSensorAvailability &sensors) {
	return sensors.imuReady;
}

inline bool magneticHeadingCalibrationSensorsReady(const CalibrationSensorAvailability &sensors) {
	return sensors.compassReady;
}
