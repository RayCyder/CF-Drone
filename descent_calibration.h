#pragma once

#include <math.h>
#include <stdint.h>

// Calibration is a bounded annotation recorder. It never commands flight controls.
static const uint32_t DESCENT_CALIBRATION_MAX_MS = 30000UL;
static const uint32_t DESCENT_CALIBRATION_SAMPLE_MS = 50UL;
static const uint16_t DESCENT_CALIBRATION_MIN_SAMPLES = 20;
static const uint16_t DESCENT_CALIBRATION_HISTOGRAM_BINS = 101;

enum DescentCalibrationState : uint8_t {
	DESCENT_CALIBRATION_EMPTY = 0,
	DESCENT_CALIBRATION_RECORDING = 1,
	DESCENT_CALIBRATION_COMPLETE = 2,
	DESCENT_CALIBRATION_ABORTED = 3
};

struct DescentCalibrationSample {
	uint32_t elapsedMs;
	uint16_t thrustCenti;
	uint16_t batteryMv;
	int16_t rollCentiDeg;
	int16_t pitchCentiDeg;
	uint16_t rcThrottleCenti;
	uint16_t faults;
	uint8_t mode;
	uint8_t controlSource;
};

struct DescentCalibrationSummary {
	DescentCalibrationState state;
	uint16_t sampleCount;
	uint32_t durationMs;
	float medianThrust;
	float meanBatteryV;
	float maxTiltDeg;
	float thrustP90MinusP10;
	uint16_t faults;
	uint8_t nonStabSamples;
	const char *reason;
	bool usable;
};

class DescentCalibrationRecorder {
public:
	static const uint16_t CAPACITY = DESCENT_CALIBRATION_MAX_MS / DESCENT_CALIBRATION_SAMPLE_MS;

	void start(uint32_t nowMs) {
		state_ = DESCENT_CALIBRATION_RECORDING;
		startedMs_ = nowMs;
		lastSampleMs_ = nowMs - DESCENT_CALIBRATION_SAMPLE_MS;
		count_ = 0;
		samplesDurationMs_ = 0;
		thrustHistogram_[0] = 0;
		for (uint16_t i = 1; i < DESCENT_CALIBRATION_HISTOGRAM_BINS; ++i) thrustHistogram_[i] = 0;
		batteryMvSum_ = 0;
		maxTiltCentiDeg_ = 0;
		faults_ = 0;
		nonStabSamples_ = 0;
		reason_ = "recording";
	}

	void stop(uint32_t nowMs) {
		if (state_ != DESCENT_CALIBRATION_RECORDING) return;
		samplesDurationMs_ = nowMs - startedMs_;
		state_ = DESCENT_CALIBRATION_COMPLETE;
		reason_ = (uint32_t)(nowMs - startedMs_) < 1000UL || count_ < DESCENT_CALIBRATION_MIN_SAMPLES
			? "too_few_samples" : "complete";
	}

	void abort(uint32_t nowMs, const char *reason) {
		if (state_ != DESCENT_CALIBRATION_RECORDING) return;
		samplesDurationMs_ = nowMs - startedMs_;
		state_ = DESCENT_CALIBRATION_ABORTED;
		reason_ = reason ? reason : "aborted";
	}

	void clear() {
		state_ = DESCENT_CALIBRATION_EMPTY;
		count_ = 0;
		reason_ = "empty";
	}

	void tick(uint32_t nowMs, const DescentCalibrationSample &sample) {
		if (state_ != DESCENT_CALIBRATION_RECORDING) return;
		const uint32_t elapsed = nowMs - startedMs_;
		if (elapsed >= DESCENT_CALIBRATION_MAX_MS || count_ >= CAPACITY) {
			samplesDurationMs_ = elapsed;
			state_ = DESCENT_CALIBRATION_COMPLETE;
			reason_ = "max_duration";
			return;
		}
		if ((uint32_t)(nowMs - lastSampleMs_) < DESCENT_CALIBRATION_SAMPLE_MS) return;
		lastSampleMs_ = nowMs;
		DescentCalibrationSample captured = sample;
		captured.elapsedMs = elapsed;
		samples_[count_] = captured;
		const uint16_t bin = sample.thrustCenti > 100 ? 100 : sample.thrustCenti;
		++thrustHistogram_[bin];
		batteryMvSum_ += sample.batteryMv;
		const int32_t roll = sample.rollCentiDeg < 0 ? -(int32_t)sample.rollCentiDeg : sample.rollCentiDeg;
		const int32_t pitch = sample.pitchCentiDeg < 0 ? -(int32_t)sample.pitchCentiDeg : sample.pitchCentiDeg;
		const int32_t tilt = roll > pitch ? roll : pitch;
		if (tilt > maxTiltCentiDeg_) maxTiltCentiDeg_ = tilt;
		faults_ |= sample.faults;
		if (sample.mode != 2) ++nonStabSamples_;
		++count_;
	}

	DescentCalibrationSummary summary(uint32_t nowMs) const {
		DescentCalibrationSummary out = {};
		out.state = state_;
		out.sampleCount = count_;
		out.durationMs = state_ == DESCENT_CALIBRATION_RECORDING
			? (uint32_t)(nowMs - startedMs_) : (count_ ? samplesDurationMs_ : 0);
		out.medianThrust = histogramPercentile_(50) / 100.0f;
		out.meanBatteryV = count_ ? (float)batteryMvSum_ / count_ / 1000.0f : 0.0f;
		out.maxTiltDeg = (float)maxTiltCentiDeg_ / 100.0f;
		out.thrustP90MinusP10 = (float)(histogramPercentile_(90) - histogramPercentile_(10)) / 100.0f;
		out.faults = faults_;
		out.nonStabSamples = nonStabSamples_;
		out.reason = reason_;
		out.usable = state_ == DESCENT_CALIBRATION_COMPLETE && count_ >= DESCENT_CALIBRATION_MIN_SAMPLES &&
			out.durationMs >= 1000UL && faults_ == 0 && nonStabSamples_ == 0 &&
			out.maxTiltDeg <= 10.0f && out.thrustP90MinusP10 <= 0.05f;
		return out;
	}

	void setCompletedDuration(uint32_t durationMs) { samplesDurationMs_ = durationMs; }
	DescentCalibrationState state() const { return state_; }
	bool sampleDue(uint32_t nowMs) const {
		return state_ == DESCENT_CALIBRATION_RECORDING &&
			(uint32_t)(nowMs - lastSampleMs_) >= DESCENT_CALIBRATION_SAMPLE_MS;
	}
	bool copySample(uint16_t index, DescentCalibrationSample &sample) const {
		if (index >= count_) return false;
		sample = samples_[index];
		return true;
	}

private:
	uint16_t histogramPercentile_(uint8_t percentile) const {
		if (!count_) return 0;
		const uint32_t target = ((uint32_t)(count_ - 1) * percentile) / 100;
		uint32_t cumulative = 0;
		for (uint16_t i = 0; i < DESCENT_CALIBRATION_HISTOGRAM_BINS; ++i) {
			cumulative += thrustHistogram_[i];
			if (cumulative > target) return i;
		}
		return DESCENT_CALIBRATION_HISTOGRAM_BINS - 1;
	}

	DescentCalibrationState state_ = DESCENT_CALIBRATION_EMPTY;
	uint32_t startedMs_ = 0, lastSampleMs_ = 0, samplesDurationMs_ = 0;
	uint16_t count_ = 0;
	DescentCalibrationSample samples_[CAPACITY] = {};
	uint16_t thrustHistogram_[DESCENT_CALIBRATION_HISTOGRAM_BINS] = {};
	uint64_t batteryMvSum_ = 0;
	uint16_t maxTiltCentiDeg_ = 0, faults_ = 0;
	uint8_t nonStabSamples_ = 0;
	const char *reason_ = "empty";
};
