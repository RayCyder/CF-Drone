#pragma once

#include <math.h>
#include <stdint.h>

static const uint32_t DESCENT_CALIBRATION_SAMPLE_MS = 50UL;
static const uint32_t DESCENT_CALIBRATION_MAX_SAMPLE_GAP_MS = 120UL;
static const uint32_t DESCENT_CALIBRATION_TAKEOFF_CONFIRM_MS = 500UL;
static const uint32_t DESCENT_CALIBRATION_POST_TAKEOFF_WAIT_MS = 3000UL;
static const uint32_t DESCENT_CALIBRATION_HOVER_WINDOW_MS = 5000UL;
static const uint32_t DESCENT_CALIBRATION_DESCENT_WINDOW_MS = 3000UL;
static const uint32_t DESCENT_CALIBRATION_LOWER_THROTTLE_CONFIRM_MS = 300UL;
static const uint16_t DESCENT_CALIBRATION_TAKEOFF_THRUST_MARGIN = 300; // 0.03
static const uint16_t DESCENT_CALIBRATION_LOWER_THROTTLE_MARGIN = 200; // 2% stick
static const uint16_t DESCENT_CALIBRATION_MAX_THRUST_SPREAD = 500;    // 0.05 thrust
static const uint16_t DESCENT_CALIBRATION_MAX_STICK_SPREAD = 500;     // 5% stick
static const uint16_t DESCENT_CALIBRATION_MAX_TILT_CENTIDEG = 1000;   // 10 degrees
static const uint16_t DESCENT_CALIBRATION_MAX_ATTITUDE_SPREAD = 300;  // 3 degrees

enum DescentCalibrationState : uint8_t {
	DESCENT_CALIBRATION_EMPTY = 0,
	DESCENT_CALIBRATION_WAITING_TAKEOFF = 1,
	DESCENT_CALIBRATION_TAKEOFF_DELAY = 2,
	DESCENT_CALIBRATION_HOVER_CANDIDATE = 3,
	DESCENT_CALIBRATION_HOVER_READY = 4,
	DESCENT_CALIBRATION_DESCENT_TRACKING = 5,
	DESCENT_CALIBRATION_COMPLETE = 6,
	DESCENT_CALIBRATION_ABORTED = 7
};

struct DescentCalibrationSample {
	uint32_t elapsedMs;
	uint16_t thrustTenThousand;
	uint16_t batteryMv;
	int16_t rollCentiDeg;
	int16_t pitchCentiDeg;
	int16_t rcRollTenThousand;
	int16_t rcPitchTenThousand;
	int16_t rcYawTenThousand;
	uint16_t rcThrottleTenThousand;
	uint16_t faults;
	uint8_t mode;
	uint8_t controlSource;
	uint8_t landingGuardActive;
};

struct DescentCalibrationSummary {
	DescentCalibrationState state;
	uint16_t sampleCount;
	uint32_t takeoffElapsedMs;
	uint32_t stableProgressMs;
	float hoverMeanThrust;
	float candidateThrust;
	float thrustDelta;
	float candidateBatteryV;
	float maxTiltDeg;
	float thrustSpread;
	const char *reason;
	bool candidateReady;
};

class DescentCalibrationRecorder {
public:
	static const uint16_t CAPACITY = DESCENT_CALIBRATION_HOVER_WINDOW_MS /
		DESCENT_CALIBRATION_SAMPLE_MS + 12;

	bool start(uint32_t nowMs, float idleThrust, float maximumLandingThrust) {
		if (active()) return false;
		clear();
		state_ = DESCENT_CALIBRATION_WAITING_TAKEOFF;
		startedMs_ = nowMs;
		idleThrust_ = thrustUnits_(idleThrust);
		maximumLandingThrust_ = thrustUnits_(maximumLandingThrust);
		if (maximumLandingThrust_ <= idleThrust_) {
			state_ = DESCENT_CALIBRATION_ABORTED;
			reason_ = "invalid_execution_range";
			return false;
		}
		reason_ = "waiting_takeoff_proxy";
		return true;
	}

	void clear() {
		state_ = DESCENT_CALIBRATION_EMPTY;
		startedMs_ = takeoffCandidateMs_ = takeoffMs_ = lastSampleMs_ = lowerThrottleMs_ = 0;
		count_ = 0;
		hoverMeanThrust_ = hoverMeanThrottle_ = candidateThrust_ = candidateBatteryV_ = 0.0f;
		candidateMaxTiltDeg_ = candidateThrustSpread_ = 0.0f;
		candidateReady_ = false;
		reason_ = "empty";
	}

	void abort(const char *reason) {
		if (!active()) return;
		state_ = DESCENT_CALIBRATION_ABORTED;
		reason_ = reason ? reason : "aborted";
	}

	void onDisarmed() {
		if (state_ == DESCENT_CALIBRATION_WAITING_TAKEOFF) return;
		if (!active()) return;
		if (state_ == DESCENT_CALIBRATION_DESCENT_TRACKING && candidateReady_) {
			state_ = DESCENT_CALIBRATION_COMPLETE;
			reason_ = "candidate_ready";
		} else {
			state_ = DESCENT_CALIBRATION_ABORTED;
			reason_ = "disarmed_without_candidate";
		}
	}

	void tick(uint32_t nowMs, const DescentCalibrationSample &sample) {
		if (!active() || (uint32_t)(nowMs - lastSampleMs_) < DESCENT_CALIBRATION_SAMPLE_MS) return;
		const uint32_t gapMs = lastSampleMs_ ? nowMs - lastSampleMs_ : DESCENT_CALIBRATION_SAMPLE_MS;
		lastSampleMs_ = nowMs;

		if (state_ == DESCENT_CALIBRATION_WAITING_TAKEOFF) {
			const bool powered = sample.mode == 2 && sample.faults == 0 &&
				sample.thrustTenThousand >= idleThrust_ + DESCENT_CALIBRATION_TAKEOFF_THRUST_MARGIN;
			if (!powered) { takeoffCandidateMs_ = 0; return; }
			if (!takeoffCandidateMs_) takeoffCandidateMs_ = nowMs;
			if ((uint32_t)(nowMs - takeoffCandidateMs_) >= DESCENT_CALIBRATION_TAKEOFF_CONFIRM_MS) {
				// The proxy is established only after sustained above-idle thrust.
				// The three-second delay starts here, never from arming.
				takeoffMs_ = nowMs;
				state_ = DESCENT_CALIBRATION_TAKEOFF_DELAY;
				reason_ = "post_takeoff_wait";
			}
			return;
		}

		if (state_ == DESCENT_CALIBRATION_TAKEOFF_DELAY) {
			if ((uint32_t)(nowMs - takeoffMs_) >= DESCENT_CALIBRATION_POST_TAKEOFF_WAIT_MS) {
				state_ = DESCENT_CALIBRATION_HOVER_CANDIDATE;
				resetWindow_("seeking_stable_mark");
			}
			return;
		}

		if (state_ == DESCENT_CALIBRATION_HOVER_READY) {
			const float throttle = sample.rcThrottleTenThousand / 10000.0f;
			const bool lowered = commonValid_(sample) &&
				throttle + DESCENT_CALIBRATION_LOWER_THROTTLE_MARGIN / 10000.0f < hoverMeanThrottle_;
			if (!lowered) { lowerThrottleMs_ = 0; return; }
			if (!lowerThrottleMs_) lowerThrottleMs_ = nowMs;
			if ((uint32_t)(nowMs - lowerThrottleMs_) >= DESCENT_CALIBRATION_LOWER_THROTTLE_CONFIRM_MS) {
				state_ = DESCENT_CALIBRATION_DESCENT_TRACKING;
				resetWindow_("tracking_descent_candidate");
			}
			return;
		}

		if (gapMs > DESCENT_CALIBRATION_MAX_SAMPLE_GAP_MS || !commonValid_(sample)) {
			resetWindow_(gapMs > DESCENT_CALIBRATION_MAX_SAMPLE_GAP_MS ? "sample_gap_reset" : "quality_reset");
			return;
		}
		append_(nowMs, sample);
		const uint32_t requiredMs = state_ == DESCENT_CALIBRATION_HOVER_CANDIDATE
			? DESCENT_CALIBRATION_HOVER_WINDOW_MS : DESCENT_CALIBRATION_DESCENT_WINDOW_MS;
		WindowStats stats = {};
		if (!windowStats_(requiredMs, stats)) return;
		if (!stats.stable) {
			if (state_ == DESCENT_CALIBRATION_HOVER_CANDIDATE) resetWindow_("stable_mark_reset");
			else reason_ = "descent_window_unstable";
			return;
		}
		if (state_ == DESCENT_CALIBRATION_HOVER_CANDIDATE) {
			hoverMeanThrust_ = stats.meanThrust;
			hoverMeanThrottle_ = stats.meanThrottle;
			state_ = DESCENT_CALIBRATION_HOVER_READY;
			lowerThrottleMs_ = 0;
			reason_ = "stable_mark_ready";
			return;
		}
		if (stats.minimumThrust < idleThrust_ || stats.maximumThrust > maximumLandingThrust_) {
			reason_ = "descent_outside_execution_range";
			return;
		}
		if (!(stats.meanThrust < hoverMeanThrust_)) {
			reason_ = "descent_not_below_stable_mark";
			return;
		}
		candidateThrust_ = stats.meanThrust;
		candidateBatteryV_ = stats.meanBatteryV;
		candidateMaxTiltDeg_ = stats.maxTiltDeg;
		candidateThrustSpread_ = stats.thrustSpread;
		candidateReady_ = true;
		reason_ = "descent_candidate_ready";
	}

	DescentCalibrationSummary summary(uint32_t nowMs) const {
		DescentCalibrationSummary out = {};
		out.state = state_;
		out.sampleCount = count_;
		out.takeoffElapsedMs = takeoffMs_ ? nowMs - takeoffMs_ : 0;
		out.stableProgressMs = count_ ? samples_[count_ - 1].elapsedMs - samples_[0].elapsedMs : 0;
		out.hoverMeanThrust = hoverMeanThrust_;
		out.candidateThrust = candidateThrust_;
		out.thrustDelta = hoverMeanThrust_ - candidateThrust_;
		out.candidateBatteryV = candidateBatteryV_;
		out.maxTiltDeg = candidateMaxTiltDeg_;
		out.thrustSpread = candidateThrustSpread_;
		out.reason = reason_;
		out.candidateReady = candidateReady_;
		return out;
	}

	DescentCalibrationState state() const { return state_; }
	bool active() const {
		return state_ >= DESCENT_CALIBRATION_WAITING_TAKEOFF &&
			state_ <= DESCENT_CALIBRATION_DESCENT_TRACKING;
	}
	bool sampleDue(uint32_t nowMs) const {
		return active() && (uint32_t)(nowMs - lastSampleMs_) >= DESCENT_CALIBRATION_SAMPLE_MS;
	}
	bool copySample(uint16_t index, DescentCalibrationSample &sample) const {
		if (index >= count_) return false;
		sample = samples_[index];
		return true;
	}

private:
	struct WindowStats {
		float meanThrust, meanThrottle, meanBatteryV, maxTiltDeg, thrustSpread;
		uint16_t minimumThrust, maximumThrust;
		bool stable;
	};

	static uint16_t thrustUnits_(float value) {
		if (!isfinite(value) || value <= 0.0f) return 0;
		if (value >= 1.0f) return 10000;
		return (uint16_t)lroundf(value * 10000.0f);
	}
	static uint16_t absolute_(int16_t value) { return value < 0 ? (uint16_t)(-(int32_t)value) : (uint16_t)value; }
	static uint16_t tilt_(const DescentCalibrationSample &sample) {
		const uint16_t roll = absolute_(sample.rollCentiDeg), pitch = absolute_(sample.pitchCentiDeg);
		return roll > pitch ? roll : pitch;
	}
	static bool manualSource_(uint8_t source) { return source == 1 || source == 2 || source == 7; }
	static uint16_t spread_(int16_t minimum, int16_t maximum) { return (uint16_t)((int32_t)maximum - minimum); }
	bool commonValid_(const DescentCalibrationSample &sample) const {
		return sample.faults == 0 && sample.mode == 2 && manualSource_(sample.controlSource) &&
			!sample.landingGuardActive && tilt_(sample) <= DESCENT_CALIBRATION_MAX_TILT_CENTIDEG;
	}
	void resetWindow_(const char *reason) { count_ = 0; reason_ = reason; }
	void append_(uint32_t nowMs, const DescentCalibrationSample &sample) {
		if (count_ == CAPACITY) {
			for (uint16_t i = 1; i < count_; ++i) samples_[i - 1] = samples_[i];
			--count_;
		}
		DescentCalibrationSample captured = sample;
		captured.elapsedMs = nowMs;
		samples_[count_++] = captured;
	}

	bool windowStats_(uint32_t requiredMs, WindowStats &out) const {
		if (count_ < 2 || samples_[count_ - 1].elapsedMs - samples_[0].elapsedMs < requiredMs) return false;
		const uint32_t endMs = samples_[count_ - 1].elapsedMs;
		const uint32_t startMs = endMs - requiredMs;
		uint16_t first = 0;
		while (first + 1 < count_ && samples_[first + 1].elapsedMs <= startMs) ++first;
		uint64_t thrustSum = 0, throttleSum = 0, batterySum = 0;
		uint32_t durationSum = 0;
		uint16_t minThrust = UINT16_MAX, maxThrust = 0, maxTilt = 0;
		int16_t minRoll = INT16_MAX, maxRoll = INT16_MIN, minPitch = INT16_MAX, maxPitch = INT16_MIN;
		int16_t minRcRoll = INT16_MAX, maxRcRoll = INT16_MIN, minRcPitch = INT16_MAX, maxRcPitch = INT16_MIN;
		int16_t minRcYaw = INT16_MAX, maxRcYaw = INT16_MIN;
		uint16_t minThrottle = UINT16_MAX, maxThrottle = 0;
		for (uint16_t i = first; i < count_; ++i) {
			const DescentCalibrationSample &sample = samples_[i];
			const uint32_t nextMs = i + 1 < count_ ? samples_[i + 1].elapsedMs : endMs;
			if (nextMs < sample.elapsedMs || nextMs - sample.elapsedMs > DESCENT_CALIBRATION_MAX_SAMPLE_GAP_MS) return false;
			const uint32_t segmentStart = sample.elapsedMs < startMs ? startMs : sample.elapsedMs;
			if (nextMs <= segmentStart) continue;
			const uint32_t duration = nextMs - segmentStart;
			thrustSum += (uint64_t)sample.thrustTenThousand * duration;
			throttleSum += (uint64_t)sample.rcThrottleTenThousand * duration;
			batterySum += (uint64_t)sample.batteryMv * duration;
			durationSum += duration;
			if (sample.thrustTenThousand < minThrust) minThrust = sample.thrustTenThousand;
			if (sample.thrustTenThousand > maxThrust) maxThrust = sample.thrustTenThousand;
			if (sample.rcThrottleTenThousand < minThrottle) minThrottle = sample.rcThrottleTenThousand;
			if (sample.rcThrottleTenThousand > maxThrottle) maxThrottle = sample.rcThrottleTenThousand;
			if (sample.rollCentiDeg < minRoll) minRoll = sample.rollCentiDeg;
			if (sample.rollCentiDeg > maxRoll) maxRoll = sample.rollCentiDeg;
			if (sample.pitchCentiDeg < minPitch) minPitch = sample.pitchCentiDeg;
			if (sample.pitchCentiDeg > maxPitch) maxPitch = sample.pitchCentiDeg;
			if (sample.rcRollTenThousand < minRcRoll) minRcRoll = sample.rcRollTenThousand;
			if (sample.rcRollTenThousand > maxRcRoll) maxRcRoll = sample.rcRollTenThousand;
			if (sample.rcPitchTenThousand < minRcPitch) minRcPitch = sample.rcPitchTenThousand;
			if (sample.rcPitchTenThousand > maxRcPitch) maxRcPitch = sample.rcPitchTenThousand;
			if (sample.rcYawTenThousand < minRcYaw) minRcYaw = sample.rcYawTenThousand;
			if (sample.rcYawTenThousand > maxRcYaw) maxRcYaw = sample.rcYawTenThousand;
			const uint16_t tilt = tilt_(sample); if (tilt > maxTilt) maxTilt = tilt;
		}
		if (durationSum != requiredMs) return false;
		out.meanThrust = (float)thrustSum / durationSum / 10000.0f;
		out.meanThrottle = (float)throttleSum / durationSum / 10000.0f;
		out.meanBatteryV = (float)batterySum / durationSum / 1000.0f;
		out.maxTiltDeg = maxTilt / 100.0f;
		out.thrustSpread = (maxThrust - minThrust) / 10000.0f;
		out.minimumThrust = minThrust; out.maximumThrust = maxThrust;
		out.stable = maxThrust - minThrust <= DESCENT_CALIBRATION_MAX_THRUST_SPREAD &&
			maxThrottle - minThrottle <= DESCENT_CALIBRATION_MAX_STICK_SPREAD &&
			spread_(minRoll, maxRoll) <= DESCENT_CALIBRATION_MAX_ATTITUDE_SPREAD &&
			spread_(minPitch, maxPitch) <= DESCENT_CALIBRATION_MAX_ATTITUDE_SPREAD &&
			spread_(minRcRoll, maxRcRoll) <= DESCENT_CALIBRATION_MAX_STICK_SPREAD &&
			spread_(minRcPitch, maxRcPitch) <= DESCENT_CALIBRATION_MAX_STICK_SPREAD &&
			spread_(minRcYaw, maxRcYaw) <= DESCENT_CALIBRATION_MAX_STICK_SPREAD;
		return true;
	}

	DescentCalibrationState state_ = DESCENT_CALIBRATION_EMPTY;
	uint32_t startedMs_ = 0, takeoffCandidateMs_ = 0, takeoffMs_ = 0, lastSampleMs_ = 0, lowerThrottleMs_ = 0;
	uint16_t idleThrust_ = 0, maximumLandingThrust_ = 10000, count_ = 0;
	DescentCalibrationSample samples_[CAPACITY] = {};
	float hoverMeanThrust_ = 0.0f, hoverMeanThrottle_ = 0.0f;
	float candidateThrust_ = 0.0f, candidateBatteryV_ = 0.0f;
	float candidateMaxTiltDeg_ = 0.0f, candidateThrustSpread_ = 0.0f;
	bool candidateReady_ = false;
	const char *reason_ = "empty";
};

static_assert(sizeof(DescentCalibrationRecorder) <= 5 * 1024,
	"Descent calibration static RAM budget");
