#pragma once

#include <stdint.h>
#if defined(ARDUINO)
#include <freertos/semphr.h>
#else
typedef void *SemaphoreHandle_t;
#endif
#include "flight_sensor_interfaces.h"

struct MagnetometerCalibrationStatus {
	bool available = false;
	bool collecting = false;
	bool saved = false;
	bool attemptStarted = false;
	bool candidateReady = false;
	uint32_t samples = 0;
	uint32_t elapsedMs = 0;
	uint32_t lastSampleAgeMs = UINT32_MAX;
	int32_t minimum[3] = {};
	int32_t maximum[3] = {};
	uint16_t span[3] = {};
	uint8_t sampleProgressPct = 0;
	uint8_t axisProgressPct[3] = {};
	uint8_t overallProgressPct = 0;
	float fieldNorm = 0.0f;
	float quality = 0.0f;
};

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
bool getMagCalibrationStatus(MagnetometerCalibrationStatus &status);
bool startMagCalibration();
void stopMagCalibration();
bool saveMagCalibration();
bool resetMagCalibration();
bool alignMagHeading(float knownHeadingDegrees, float rollRadians, float pitchRadians);
