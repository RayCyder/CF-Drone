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

enum MagnetometerRejectReason : uint16_t {
	MAG_REJECT_NONE = 0,
	MAG_REJECT_NOT_DETECTED = 1u << 0,
	MAG_REJECT_NOT_READY = 1u << 1,
	MAG_REJECT_NOT_CALIBRATED = 1u << 2,
	MAG_REJECT_STALE = 1u << 3,
	MAG_REJECT_BUS = 1u << 4,
	MAG_REJECT_FIELD_NORM = 1u << 5,
	MAG_REJECT_INNOVATION = 1u << 6,
	MAG_REJECT_TILT = 1u << 7,
};

struct MagnetometerEstimate {
	int16_t raw[3] = {};
	float corrected[3] = {};
	float fieldNorm = 0.0f;
	float fieldNormReference = 0.0f;
	float magneticHeadingRadians = 0.0f;
	float navigationHeadingRadians = 0.0f;
	float innovationRadians = 0.0f;
	uint32_t timestampUs = 0;
	uint32_t sequence = 0;
	uint32_t sampleCount = 0;
	uint32_t failureCount = 0;
	uint16_t rejectReasons = MAG_REJECT_NOT_READY;
	bool detected = false;
	bool ready = false;
	bool calibrated = false;
	bool fresh = false;
	bool trusted = false;
};

enum class OpticalFlowSampleState : uint8_t {
	Invalid = 0,
	Stale = 1,
	LowQuality = 2,
	FreshZero = 3,
	FreshMotion = 4,
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

inline OpticalFlowSampleState classifyOpticalFlowSample(const OpticalFlowSample &sample,
	uint32_t nowUs, uint32_t maxAgeUs, uint8_t minimumQuality) {
	if (!sample.valid || !isfinite(sample.deltaXAngularRadians) ||
		!isfinite(sample.deltaYAngularRadians)) return OpticalFlowSampleState::Invalid;
	if (!sensorSampleFresh(nowUs, sample.timestampUs, maxAgeUs))
		return OpticalFlowSampleState::Stale;
	if (sample.quality < minimumQuality) return OpticalFlowSampleState::LowQuality;
	return sample.motionDetected ? OpticalFlowSampleState::FreshMotion :
		OpticalFlowSampleState::FreshZero;
}

inline const char *opticalFlowSampleStateName(OpticalFlowSampleState state) {
	switch (state) {
		case OpticalFlowSampleState::FreshZero: return "fresh_zero";
		case OpticalFlowSampleState::FreshMotion: return "fresh_motion";
		case OpticalFlowSampleState::LowQuality: return "low_quality";
		case OpticalFlowSampleState::Stale: return "stale";
		default: return "invalid";
	}
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
