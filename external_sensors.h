#pragma once

#include <stdint.h>
#if defined(ARDUINO)
#include <freertos/semphr.h>
#else
typedef void *SemaphoreHandle_t;
#endif
#include "flight_sensor_interfaces.h"

void setupExternalSensors();
void setupSupplementarySensors(bool i2cReady, SemaphoreHandle_t i2cMutex);
void updateSupplementarySensors();
void pollDownwardRangeSensor();
void markDownwardRangeUnavailable();
bool opticalFlowDetected();
bool downwardRangeDetected();
bool opticalFlowAvailable();
bool downwardRangeAvailable();
bool getOpticalFlowSample(OpticalFlowSample &sample, uint32_t &sampleCount,
	uint32_t &failureCount);
bool getDownwardRangeSample(DownwardRangeSample &sample, uint32_t &sampleCount,
	uint32_t &failureCount);
void updateExternalSensors();
void printExternalSensorReadings(float rollRadians, float pitchRadians);
bool compassAvailable();
bool getMagnetometerEstimate(MagnetometerEstimate &estimate);
bool magHeadingTrusted();
float navigationHeadingRadians(float fallbackYawRadians);
bool barometerAvailable();
bool getBarometerEstimate(BarometerEstimate &estimate);
void printMagCalibrationStatus();
bool startMagCalibration();
void stopMagCalibration();
bool saveMagCalibration();
bool resetMagCalibration();
bool alignMagHeading(float knownHeadingDegrees, float rollRadians, float pitchRadians);
