#pragma once

#include <stdint.h>
#include "flight_sensor_interfaces.h"

void setupExternalSensors();
void updateExternalSensors();
void printExternalSensorReadings(float rollRadians, float pitchRadians);
bool compassAvailable();
bool getBarometerEstimate(BarometerEstimate &estimate);
void printMagCalibrationStatus();
bool startMagCalibration();
void stopMagCalibration();
bool saveMagCalibration();
bool resetMagCalibration();
bool alignMagHeading(float knownHeadingDegrees, float rollRadians, float pitchRadians);
