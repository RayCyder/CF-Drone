#pragma once

#include <stdint.h>

void setupExternalSensors();
void updateExternalSensors();
void printExternalSensorReadings(float rollRadians, float pitchRadians);
void printMagCalibrationStatus();
bool startMagCalibration();
void stopMagCalibration();
bool saveMagCalibration();
bool resetMagCalibration();
bool alignMagHeading(float knownHeadingDegrees, float rollRadians, float pitchRadians);
