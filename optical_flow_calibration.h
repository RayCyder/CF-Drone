#pragma once

#include <math.h>
#include <stdint.h>

struct OpticalFlowCalibrationStats {
	bool active = false;
	uint32_t startedUs = 0;
	uint32_t endedUs = 0;
	uint32_t sampleCount = 0;
	uint32_t validSampleCount = 0;
	uint32_t usableSampleCount = 0;
	uint32_t rangeSampleCount = 0;
	uint32_t motionSampleCount = 0;
	int64_t pixelX = 0;
	int64_t pixelY = 0;
	float imageAngleXRad = 0.0f;
	float imageAngleYRad = 0.0f;
	float gyroAngleXRad = 0.0f;
	float gyroAngleYRad = 0.0f;
	float correctedAngleXRad = 0.0f;
	float correctedAngleYRad = 0.0f;
	float estimatedBodyXMeters = 0.0f;
	float estimatedBodyYMeters = 0.0f;
	float qualitySum = 0.0f;
	uint8_t qualityMinimum = 255;
	uint8_t qualityMaximum = 0;
	float rangeSumMeters = 0.0f;
	float rangeMinimumMeters = INFINITY;
	float rangeMaximumMeters = 0.0f;
};

class OpticalFlowCalibrationAccumulator {
public:
	void start(uint32_t nowUs) {
		stats_ = OpticalFlowCalibrationStats();
		stats_.active = true;
		stats_.startedUs = nowUs;
		lastTimestampUs_ = 0;
	}

	void stop(uint32_t nowUs) {
		stats_.active = false;
		stats_.endedUs = nowUs;
	}

	void reset() {
		stats_ = OpticalFlowCalibrationStats();
		lastTimestampUs_ = 0;
	}

	void observe(uint32_t timestampUs, bool transferValid, bool motionDetected,
		int16_t deltaX, int16_t deltaY, float imageAngleXRad, float imageAngleYRad,
		uint8_t quality, float gyroXRadPerSecond, float gyroYRadPerSecond,
		float rangeMeters, bool scaleUsable, bool sampleUsable) {
		if (!stats_.active || !timestampUs || timestampUs == lastTimestampUs_) return;
		float sampleDt = lastTimestampUs_ ?
			(float)(uint32_t)(timestampUs - lastTimestampUs_) * 1e-6f : 0.01f;
		lastTimestampUs_ = timestampUs;
		++stats_.sampleCount;
		if (!transferValid || sampleDt < 0.004f || sampleDt > 0.05f) return;
		++stats_.validSampleCount;
		if (motionDetected) ++stats_.motionSampleCount;
		stats_.pixelX += deltaX;
		stats_.pixelY += deltaY;
		stats_.imageAngleXRad += imageAngleXRad;
		stats_.imageAngleYRad += imageAngleYRad;
		stats_.gyroAngleXRad += gyroXRadPerSecond * sampleDt;
		stats_.gyroAngleYRad += gyroYRadPerSecond * sampleDt;
		stats_.qualitySum += quality;
		if (quality < stats_.qualityMinimum) stats_.qualityMinimum = quality;
		if (quality > stats_.qualityMaximum) stats_.qualityMaximum = quality;
		if (isfinite(rangeMeters) && rangeMeters > 0.0f) {
			++stats_.rangeSampleCount;
			stats_.rangeSumMeters += rangeMeters;
			if (rangeMeters < stats_.rangeMinimumMeters) stats_.rangeMinimumMeters = rangeMeters;
			if (rangeMeters > stats_.rangeMaximumMeters) stats_.rangeMaximumMeters = rangeMeters;
		}
		if (!scaleUsable || !isfinite(rangeMeters)) return;
		if (!sampleUsable) return;
		++stats_.usableSampleCount;
		const float correctedX = imageAngleXRad - gyroXRadPerSecond * sampleDt;
		const float correctedY = imageAngleYRad - gyroYRadPerSecond * sampleDt;
		stats_.correctedAngleXRad += correctedX;
		stats_.correctedAngleYRad += correctedY;
		// Candidate board transform. The calibration report exposes both raw and
		// transformed values so physical tests can confirm or replace it.
		stats_.estimatedBodyXMeters += -correctedY * rangeMeters;
		stats_.estimatedBodyYMeters += correctedX * rangeMeters;
	}

	const OpticalFlowCalibrationStats &stats() const { return stats_; }

private:
	OpticalFlowCalibrationStats stats_;
	uint32_t lastTimestampUs_ = 0;
};
