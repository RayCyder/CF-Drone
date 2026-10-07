#pragma once

#include <math.h>
#include <stdint.h>

struct NavigationHeadingState {
	float gyroYawRadians = 0.0f;
	float magneticYawRadians = 0.0f;
	float fusedYawRadians = 0.0f;
	float innovationRadians = 0.0f;
	uint32_t lastAcceptedUs = 0;
	uint32_t acceptedCount = 0;
	uint32_t rejectedCount = 0;
	bool initialized = false;
	bool trusted = false;
};

class NavigationHeadingEstimator {
public:
	static constexpr float INNOVATION_LIMIT_RADIANS = 0.34906585f; // 20 degrees
	static constexpr float CORRECTION_RATE_RADIANS_PER_SECOND = 0.17453293f; // 10 deg/s
	static constexpr uint32_t TRUST_MAX_AGE_US = 100000;

	void reset() {
		state_ = NavigationHeadingState();
		offsetRadians_ = 0.0f;
		lastUpdateUs_ = 0;
		rejectionLatched_ = false;
	}

	void predict(float gyroYawRadians, uint32_t nowUs) {
		if (!isfinite(gyroYawRadians)) {
			state_.trusted = false;
			return;
		}
		state_.gyroYawRadians = wrap(gyroYawRadians);
		state_.fusedYawRadians = wrap(state_.gyroYawRadians + offsetRadians_);
		state_.trusted = !rejectionLatched_ && state_.initialized && state_.lastAcceptedUs != 0 &&
			(uint32_t)(nowUs - state_.lastAcceptedUs) <= TRUST_MAX_AGE_US;
	}

	bool observe(float magneticYawRadians, bool sampleTrusted, uint32_t sampleUs,
		bool allowInitialAlignment) {
		if (!sampleTrusted || !isfinite(magneticYawRadians)) {
			++state_.rejectedCount;
			rejectionLatched_ = true;
			state_.trusted = false;
			return false;
		}
		state_.magneticYawRadians = wrap(magneticYawRadians);
		if (!state_.initialized) {
			if (!allowInitialAlignment) {
				++state_.rejectedCount;
				rejectionLatched_ = true;
				state_.trusted = false;
				return false;
			}
			offsetRadians_ = wrap(state_.magneticYawRadians - state_.gyroYawRadians);
			state_.innovationRadians = 0.0f;
			state_.initialized = true;
		} else {
			state_.innovationRadians = wrap(state_.magneticYawRadians - state_.fusedYawRadians);
			if (fabsf(state_.innovationRadians) > INNOVATION_LIMIT_RADIANS) {
				++state_.rejectedCount;
				rejectionLatched_ = true;
				state_.trusted = false;
				return false;
			}
			float elapsedSeconds = 0.02f;
			if (lastUpdateUs_) {
				elapsedSeconds = (float)((uint32_t)(sampleUs - lastUpdateUs_)) * 1.0e-6f;
				if (!isfinite(elapsedSeconds) || elapsedSeconds < 0.001f || elapsedSeconds > 0.2f)
					elapsedSeconds = 0.02f;
			}
			const float maximumCorrection = CORRECTION_RATE_RADIANS_PER_SECOND * elapsedSeconds;
			const float correction = state_.innovationRadians > maximumCorrection ? maximumCorrection :
				(state_.innovationRadians < -maximumCorrection ? -maximumCorrection : state_.innovationRadians);
			offsetRadians_ = wrap(offsetRadians_ + correction);
		}
		lastUpdateUs_ = sampleUs;
		state_.lastAcceptedUs = sampleUs;
		++state_.acceptedCount;
		rejectionLatched_ = false;
		state_.fusedYawRadians = wrap(state_.gyroYawRadians + offsetRadians_);
		state_.trusted = true;
		return true;
	}

	const NavigationHeadingState &state() const { return state_; }

	static float wrap(float angle) {
		while (angle > 3.14159265358979323846f) angle -= 6.28318530717958647692f;
		while (angle < -3.14159265358979323846f) angle += 6.28318530717958647692f;
		return angle;
	}

private:
	NavigationHeadingState state_;
	float offsetRadians_ = 0.0f;
	uint32_t lastUpdateUs_ = 0;
	bool rejectionLatched_ = false;
};

class MagHeadingLossGuard {
public:
	explicit MagHeadingLossGuard(uint32_t timeoutMs = 500) : timeoutMs_(timeoutMs) {}

	bool update(bool routeActive, bool headingTrusted, uint32_t nowMs) {
		if (!routeActive) {
			reset();
			return false;
		}
		if (headingTrusted) {
			hadTrustedHeading_ = true;
			lossTiming_ = false;
			return false;
		}
		if (!hadTrustedHeading_) return false; // Compatibility route never acquired magnetic heading.
		if (!lossTiming_) {
			lossStartedMs_ = nowMs;
			lossTiming_ = true;
			return false;
		}
		return (uint32_t)(nowMs - lossStartedMs_) >= timeoutMs_;
	}

	void reset() {
		hadTrustedHeading_ = false;
		lossTiming_ = false;
		lossStartedMs_ = 0;
	}

private:
	uint32_t timeoutMs_;
	uint32_t lossStartedMs_ = 0;
	bool hadTrustedHeading_ = false;
	bool lossTiming_ = false;
};
