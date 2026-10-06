#pragma once

#include <math.h>
#include <stdint.h>

// Sensor feedback contracts for later altitude and near-ground control work.
// These interfaces intentionally do not imply that a sensor is installed or
// that its data is currently used by the estimator or landing controller.
struct BarometerSample {
	float pressurePa = 0.0f;
	float temperatureC = 0.0f;
	float altitudeMeters = 0.0f;
	uint32_t timestampUs = 0;
	bool valid = false;
};

struct BarometerEstimate {
	BarometerSample sample;
	float relativeAltitudeMeters = 0.0f;
	float verticalSpeedMps = 0.0f; // positive upward
	uint32_t sampleCount = 0;
	uint32_t failureCount = 0;
	bool valid = false;
};

struct DownwardRangeSample {
	float distanceMeters = 0.0f;
	uint8_t quality = 0;
	uint8_t rangeStatus = 0xFF; // normalized ULD status: 0 means valid
	uint8_t rawRangeStatus = 0xFF;
	uint32_t timestampUs = 0;
	bool valid = false;
};

struct OpticalFlowSample {
	int16_t deltaX = 0;
	int16_t deltaY = 0;
	float deltaXAngularRadians = 0.0f;
	float deltaYAngularRadians = 0.0f;
	uint8_t quality = 0;
	uint32_t timestampUs = 0;
	bool motionDetected = false;
	bool valid = false;
};

inline bool sensorSampleFresh(uint32_t nowUs, uint32_t timestampUs, uint32_t maxAgeUs) {
	return (uint32_t)(nowUs - timestampUs) <= maxAgeUs;
}

inline bool barometerSampleUsable(const BarometerSample &sample, uint32_t nowUs,
	uint32_t maxAgeUs) {
	return sample.valid && isfinite(sample.pressurePa) && sample.pressurePa > 0.0f &&
		isfinite(sample.altitudeMeters) && sensorSampleFresh(nowUs, sample.timestampUs, maxAgeUs);
}

inline bool barometerEstimateUsable(const BarometerEstimate &estimate, uint32_t nowUs,
	uint32_t maxAgeUs) {
	return estimate.valid && barometerSampleUsable(estimate.sample, nowUs, maxAgeUs) &&
		isfinite(estimate.relativeAltitudeMeters) && isfinite(estimate.verticalSpeedMps);
}

inline bool downwardRangeSampleUsable(const DownwardRangeSample &sample, uint32_t nowUs,
	uint32_t maxAgeUs, uint8_t minimumQuality) {
	return sample.valid && isfinite(sample.distanceMeters) && sample.distanceMeters > 0.0f &&
		sample.quality >= minimumQuality && sensorSampleFresh(nowUs, sample.timestampUs, maxAgeUs);
}

inline bool opticalFlowSampleUsable(const OpticalFlowSample &sample, uint32_t nowUs,
	uint32_t maxAgeUs, uint8_t minimumQuality) {
	return sample.valid && isfinite(sample.deltaXAngularRadians) &&
		isfinite(sample.deltaYAngularRadians) && sample.quality >= minimumQuality &&
		sensorSampleFresh(nowUs, sample.timestampUs, maxAgeUs);
}

class BarometerInterface {
public:
	virtual ~BarometerInterface() = default;
	virtual bool begin() = 0;
	virtual bool readSample(BarometerSample &sample) = 0;
};

class DownwardRangeInterface {
public:
	virtual ~DownwardRangeInterface() = default;
	virtual bool begin() = 0;
	virtual bool readSample(DownwardRangeSample &sample) = 0;
};
