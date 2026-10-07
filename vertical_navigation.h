#pragma once

#include <math.h>
#include <stdint.h>

#include "flight_sensor_interfaces.h"

constexpr uint32_t VERTICAL_NAV_BARO_MAX_AGE_US = 250000;
constexpr uint32_t VERTICAL_NAV_RANGE_MAX_AGE_US = 120000;
constexpr uint32_t VERTICAL_NAV_FLOW_MAX_AGE_US = 120000;
constexpr uint8_t VERTICAL_NAV_RANGE_MIN_QUALITY = 20;
constexpr uint8_t VERTICAL_NAV_FLOW_MIN_QUALITY = 30;

struct VerticalNavigationInput {
	float worldAccelZMps2 = 0.0f; // positive upward, gravity removed
	float verticalAccelerationMps2 = 0.0f;
	float dtSeconds = 0.0f;
	float rollRadians = 0.0f;
	float pitchRadians = 0.0f;
	BarometerSample barometer;
	DownwardRangeSample range;
	OpticalFlowSample flow;
	uint32_t nowUs = 0;
	uint32_t barometerTimestampUs = 0;
	uint32_t rangeTimestampUs = 0;
	float barometerAltitudeMeters = 0.0f;
	float rangeAltitudeMeters = 0.0f;
	bool accelerationValid = true;
	bool armed = false;
	bool barometerValid = false;
	bool rangeValid = false;
};

struct VerticalNavigationTuning {
	float accelDeadbandMps2 = 0.12f;
	float accelLeakPerSecond = 0.08f;
	float baroPositionAlpha = 0.055f;
	float baroVelocityBeta = 0.18f;
	float rangePositionAlpha = 0.24f;
	float rangeVelocityBeta = 0.48f;
	float baroInnovationGateM = 3.0f;
	float rangeInnovationGateM = 0.7f;
	float rangeMaxTiltRadians = 0.70f;
	float rangeMaxUsableMeters = 3.0f;
	float verticalSpeedLimitMps = 3.0f;
	float dtMinSeconds = 0.001f;
	float dtMaxSeconds = 0.08f;
};

enum class VerticalNavigationHealth : uint8_t {
	NoHeight = 0,
	BarometerOnly = 1,
	RangeOnly = 2,
	Fused = 3,
};

struct VerticalNavigationState {
	float altitudeMeters = 0.0f;
	float verticalSpeedMps = 0.0f;
	float lastBarometerAltitudeMeters = 0.0f;
	float lastRangeAltitudeMeters = 0.0f;
	float lastInnovationMeters = 0.0f;
	uint32_t lastUpdateUs = 0;
	uint32_t acceptedBarometerCount = 0;
	uint32_t acceptedRangeCount = 0;
	uint32_t receivedObservationCount = 0;
	uint32_t acceptedObservationCount = 0;
	uint32_t rejectedObservationCount = 0;
	uint32_t trustedObservationCount = 0;
	uint32_t lastTrustedObservationUs = 0;
	bool initialized = false;
	bool barometerHealthy = false;
	bool rangeHealthy = false;
	bool flowHealthy = false;
	bool healthy = false;
	bool degraded = false;
	uint8_t heightSource = 0;
	VerticalNavigationHealth health = VerticalNavigationHealth::NoHeight;
};

class VerticalNavigationEstimator {
public:
	explicit VerticalNavigationEstimator(const VerticalNavigationTuning &tuning =
		VerticalNavigationTuning()) : tuning_(tuning) {}

	void reset() {
		state_ = VerticalNavigationState();
		lastBarometerTimestampUs_ = 0;
		lastRangeTimestampUs_ = 0;
		lastFlowTimestampUs_ = 0;
		lastBarometerReceivedTimestampUs_ = 0;
		lastRangeReceivedTimestampUs_ = 0;
	}

	const VerticalNavigationState &state() const { return state_; }

	const VerticalNavigationState &update(const VerticalNavigationInput &input) {
		const float worldAccelZ = input.accelerationValid ?
			(input.worldAccelZMps2 != 0.0f ? input.worldAccelZMps2 :
				input.verticalAccelerationMps2) : 0.0f;
		if (!isfinite(worldAccelZ) || input.nowUs == 0) {
			updateHealth(input.nowUs, false, false, false);
			return state_;
		}

		BarometerSample barometer = input.barometer;
		if (input.barometerValid) {
			barometer.valid = true;
			barometer.altitudeMeters = input.barometerAltitudeMeters;
			barometer.pressurePa = barometer.pressurePa > 0.0f ? barometer.pressurePa : 101000.0f;
			barometer.timestampUs = input.barometerTimestampUs ? input.barometerTimestampUs :
				input.nowUs;
		}
		DownwardRangeSample range = input.range;
		if (input.rangeValid) {
			range.valid = true;
			range.distanceMeters = input.rangeAltitudeMeters;
			range.quality = range.quality ? range.quality : 100;
			range.rangeStatus = 0;
			range.timestampUs = input.rangeTimestampUs ? input.rangeTimestampUs : input.nowUs;
		}

		const bool baroFresh = barometerSampleUsable(barometer, input.nowUs,
			VERTICAL_NAV_BARO_MAX_AGE_US);
		const bool rangeFresh = downwardRangeSampleUsable(range, input.nowUs,
			VERTICAL_NAV_RANGE_MAX_AGE_US, VERTICAL_NAV_RANGE_MIN_QUALITY);
		const bool flowFresh = opticalFlowSampleUsable(input.flow, input.nowUs,
			VERTICAL_NAV_FLOW_MAX_AGE_US, VERTICAL_NAV_FLOW_MIN_QUALITY);

		if (!state_.initialized) {
			if (baroFresh) {
				lastBarometerReceivedTimestampUs_ = barometer.timestampUs;
				++state_.receivedObservationCount;
				state_.altitudeMeters = barometer.altitudeMeters;
				state_.lastBarometerAltitudeMeters = barometer.altitudeMeters;
				state_.initialized = true;
				acceptBarometer(barometer);
			} else if (rangeFresh) {
				lastRangeReceivedTimestampUs_ = range.timestampUs;
				++state_.receivedObservationCount;
				const float correctedRange = correctedRangeAltitude(input, range);
				if (isfinite(correctedRange)) {
					state_.altitudeMeters = correctedRange;
					state_.lastRangeAltitudeMeters = correctedRange;
					state_.initialized = true;
					acceptRange(range, correctedRange);
				}
			}
			updateHealth(input.nowUs, baroFresh, rangeFresh, flowFresh);
			state_.lastUpdateUs = input.nowUs;
			return state_;
		}

		const float dt = boundedDt(input.nowUs);
		state_.lastUpdateUs = input.nowUs;
		const float accelZ = applyAccelDeadband(worldAccelZ);
		state_.verticalSpeedMps += accelZ * dt;
		state_.verticalSpeedMps *= fmaxf(0.0f, 1.0f - tuning_.accelLeakPerSecond * dt);
		state_.verticalSpeedMps = clamp(state_.verticalSpeedMps,
			-tuning_.verticalSpeedLimitMps, tuning_.verticalSpeedLimitMps);
		state_.altitudeMeters += state_.verticalSpeedMps * dt;

		if (baroFresh && barometer.timestampUs != lastBarometerReceivedTimestampUs_) {
			lastBarometerReceivedTimestampUs_ = barometer.timestampUs;
			++state_.receivedObservationCount;
			if (fuseObservation(barometer.altitudeMeters, tuning_.baroPositionAlpha,
				tuning_.baroVelocityBeta, tuning_.baroInnovationGateM, dt))
				acceptBarometer(barometer);
		}

		if (rangeFresh && range.timestampUs != lastRangeReceivedTimestampUs_) {
			lastRangeReceivedTimestampUs_ = range.timestampUs;
			++state_.receivedObservationCount;
			const float correctedRange = correctedRangeAltitude(input, range);
			if (isfinite(correctedRange) && correctedRange <= tuning_.rangeMaxUsableMeters) {
				if (fuseObservation(correctedRange, tuning_.rangePositionAlpha,
					tuning_.rangeVelocityBeta, tuning_.rangeInnovationGateM, dt))
					acceptRange(range, correctedRange);
			} else {
				++state_.rejectedObservationCount;
			}
		}

		if (flowFresh) {
			lastFlowTimestampUs_ = input.flow.timestampUs;
		}
		updateHealth(input.nowUs, baroFresh, rangeFresh, flowFresh);
		return state_;
	}

private:
	static float clamp(float value, float minValue, float maxValue) {
		return fminf(maxValue, fmaxf(minValue, value));
	}

	float boundedDt(uint32_t nowUs) const {
		if (state_.lastUpdateUs == 0) return tuning_.dtMinSeconds;
		const float dt = (float)((uint32_t)(nowUs - state_.lastUpdateUs)) * 1.0e-6f;
		if (!isfinite(dt)) return tuning_.dtMinSeconds;
		return clamp(dt, tuning_.dtMinSeconds, tuning_.dtMaxSeconds);
	}

	float applyAccelDeadband(float accelZ) const {
		if (fabsf(accelZ) <= tuning_.accelDeadbandMps2) return 0.0f;
		return accelZ > 0.0f ? accelZ - tuning_.accelDeadbandMps2 :
			accelZ + tuning_.accelDeadbandMps2;
	}

	float correctedRangeAltitude(const VerticalNavigationInput &input,
		const DownwardRangeSample &range) const {
		if (!isfinite(input.rollRadians) || !isfinite(input.pitchRadians)) return NAN;
		if (fabsf(input.rollRadians) > tuning_.rangeMaxTiltRadians ||
			fabsf(input.pitchRadians) > tuning_.rangeMaxTiltRadians) return NAN;
		const float tiltScale = cosf(input.rollRadians) * cosf(input.pitchRadians);
		if (!isfinite(tiltScale) || tiltScale <= 0.4f) return NAN;
		return range.distanceMeters * tiltScale;
	}

	bool fuseObservation(float measuredAltitudeMeters, float positionAlpha, float velocityBeta,
		float innovationGateMeters, float dt) {
		if (!isfinite(measuredAltitudeMeters)) {
			++state_.rejectedObservationCount;
			return false;
		}
		const float innovation = measuredAltitudeMeters - state_.altitudeMeters;
		state_.lastInnovationMeters = innovation;
		if (fabsf(innovation) > innovationGateMeters) {
			++state_.rejectedObservationCount;
			return false;
		}
		state_.altitudeMeters += innovation * positionAlpha;
		state_.verticalSpeedMps += (innovation * velocityBeta) / fmaxf(dt, tuning_.dtMinSeconds);
		state_.verticalSpeedMps = clamp(state_.verticalSpeedMps,
			-tuning_.verticalSpeedLimitMps, tuning_.verticalSpeedLimitMps);
		return true;
	}

	void markTrusted(uint32_t timestampUs) {
		++state_.acceptedObservationCount;
		++state_.trustedObservationCount;
		state_.lastTrustedObservationUs = timestampUs;
	}

	void acceptBarometer(const BarometerSample &barometer) {
		lastBarometerTimestampUs_ = barometer.timestampUs;
		state_.lastBarometerAltitudeMeters = barometer.altitudeMeters;
		++state_.acceptedBarometerCount;
		markTrusted(barometer.timestampUs);
	}

	void acceptRange(const DownwardRangeSample &range, float correctedRangeMeters) {
		lastRangeTimestampUs_ = range.timestampUs;
		state_.lastRangeAltitudeMeters = correctedRangeMeters;
		++state_.acceptedRangeCount;
		markTrusted(range.timestampUs);
	}

	void updateHealth(uint32_t nowUs, bool baroFresh, bool rangeFresh, bool flowFresh) {
		(void)baroFresh;
		(void)rangeFresh;
		state_.barometerHealthy = lastBarometerTimestampUs_ != 0 &&
			sensorSampleFresh(nowUs, lastBarometerTimestampUs_, VERTICAL_NAV_BARO_MAX_AGE_US);
		state_.rangeHealthy = lastRangeTimestampUs_ != 0 &&
			sensorSampleFresh(nowUs, lastRangeTimestampUs_, VERTICAL_NAV_RANGE_MAX_AGE_US);
		state_.flowHealthy = flowFresh && lastFlowTimestampUs_ != 0 &&
			sensorSampleFresh(nowUs, lastFlowTimestampUs_, VERTICAL_NAV_FLOW_MAX_AGE_US);
		if (state_.barometerHealthy && state_.rangeHealthy) {
			state_.health = VerticalNavigationHealth::Fused;
		} else if (state_.barometerHealthy) {
			state_.health = VerticalNavigationHealth::BarometerOnly;
		} else if (state_.rangeHealthy) {
			state_.health = VerticalNavigationHealth::RangeOnly;
		} else {
			state_.health = VerticalNavigationHealth::NoHeight;
		}
		state_.heightSource = static_cast<uint8_t>(state_.health);
		state_.healthy = state_.initialized && state_.health != VerticalNavigationHealth::NoHeight;
		state_.degraded = state_.initialized && state_.health != VerticalNavigationHealth::Fused;
	}

	VerticalNavigationTuning tuning_;
	VerticalNavigationState state_;
	uint32_t lastBarometerTimestampUs_ = 0;
	uint32_t lastRangeTimestampUs_ = 0;
	uint32_t lastFlowTimestampUs_ = 0;
	uint32_t lastBarometerReceivedTimestampUs_ = 0;
	uint32_t lastRangeReceivedTimestampUs_ = 0;
};

struct VerticalHoldTuning {
	float altitudePGain = 0.75f;
	float velocityPGain = 0.22f;
	float velocityIGain = 0.08f;
	float maxClimbRateMps = 0.55f;
	float maxDescendRateMps = 0.32f;
	float maxThrottleCorrection = 0.18f;
	float throttleSlewPerSecond = 0.65f;
	float integralLimit = 0.16f;
	float minThrottle = 0.0f;
	float maxThrottle = 1.0f;
};

struct VerticalHoldOutput {
	float throttle = 0.0f;
	float correction = 0.0f;
	float targetVelocityMps = 0.0f;
	bool active = false;
	bool saturated = false;
};

struct VerticalControlInput {
	float dtSeconds = 0.02f;
	float altitudeMeters = 0.0f;
	float verticalSpeedMps = 0.0f;
	float hoverThrust = 0.48f;
	float throttleInput = 0.5f;
	float hoverThrottleInput = 0.5f;
	float altitudeTargetMeters = 0.0f;
	float outputMinimum = 0.0f;
	float outputMaximum = 1.0f;
	bool directAltitudeTarget = false;
};

class VerticalPositionController {
public:
	explicit VerticalPositionController(const VerticalHoldTuning &tuning = VerticalHoldTuning()) :
		tuning_(tuning) {}

	void reset(float currentThrottle) {
		integral_ = 0.0f;
		lastThrottle_ = clampUnit(currentThrottle);
		lastUpdateUs_ = 0;
		hasOutput_ = false;
	}

	void reset(float altitudeMeters, float verticalSpeedMps, float currentThrust) {
		targetAltitudeMeters_ = altitudeMeters;
		targetVelocityMps_ = verticalSpeedMps;
		reset(currentThrust);
	}

	void disable() {
		integral_ = 0.0f;
		targetVelocityMps_ = 0.0f;
		lastUpdateUs_ = 0;
		hasOutput_ = false;
	}

	float altitudeTargetMeters() const { return targetAltitudeMeters_; }
	float verticalSpeedTargetMps() const { return targetVelocityMps_; }
	float lastThrustCommand() const { return lastThrottle_; }

	VerticalHoldOutput update(float targetAltitudeMeters, float hoverThrottle,
		const VerticalNavigationState &state, uint32_t nowUs, bool enabled) {
		VerticalHoldOutput output;
		const float baseThrottle = clampUnit(hoverThrottle);
		if (!enabled || !state.initialized || state.health == VerticalNavigationHealth::NoHeight ||
			!isfinite(targetAltitudeMeters) || !isfinite(baseThrottle) || nowUs == 0) {
			reset(baseThrottle);
			output.throttle = baseThrottle;
			return output;
		}

		const float dt = boundedDt(nowUs);
		const float altitudeError = targetAltitudeMeters - state.altitudeMeters;
		const float climbLimit = fmaxf(0.0f, tuning_.maxClimbRateMps);
		const float descendLimit = fmaxf(0.0f, tuning_.maxDescendRateMps);
		const float targetVelocity = clamp(altitudeError * tuning_.altitudePGain,
			-descendLimit, climbLimit);
		const float velocityError = targetVelocity - state.verticalSpeedMps;
		const float pTerm = velocityError * tuning_.velocityPGain;
		const float candidateIntegral = clamp(integral_ + velocityError *
			tuning_.velocityIGain * dt, -tuning_.integralLimit, tuning_.integralLimit);
		float correction = clamp(pTerm + candidateIntegral, -tuning_.maxThrottleCorrection,
			tuning_.maxThrottleCorrection);
		float requestedThrottle = clamp(baseThrottle + correction,
			tuning_.minThrottle, tuning_.maxThrottle);
		const bool saturated = fabsf(requestedThrottle - (baseThrottle + correction)) > 1.0e-6f ||
			fabsf(correction) >= tuning_.maxThrottleCorrection - 1.0e-6f;
		if (!saturated || (requestedThrottle <= tuning_.minThrottle && velocityError > 0.0f) ||
			(requestedThrottle >= tuning_.maxThrottle && velocityError < 0.0f)) {
			integral_ = candidateIntegral;
		}
		correction = clamp(pTerm + integral_, -tuning_.maxThrottleCorrection,
			tuning_.maxThrottleCorrection);
		requestedThrottle = clamp(baseThrottle + correction,
			tuning_.minThrottle, tuning_.maxThrottle);
		const float maxStep = tuning_.throttleSlewPerSecond * dt;
		if (!hasOutput_) {
			lastThrottle_ = baseThrottle;
			hasOutput_ = true;
		}
		output.throttle = clamp(requestedThrottle, lastThrottle_ - maxStep,
			lastThrottle_ + maxStep);
		output.correction = output.throttle - baseThrottle;
		output.targetVelocityMps = targetVelocity;
		output.active = true;
		output.saturated = saturated;
		lastThrottle_ = output.throttle;
		lastUpdateUs_ = nowUs;
		targetAltitudeMeters_ = targetAltitudeMeters;
		targetVelocityMps_ = targetVelocity;
		return output;
	}

	float update(const VerticalControlInput &input) {
		if (!input.directAltitudeTarget) {
			const float stickError = input.throttleInput - input.hoverThrottleInput;
			const float deadband = 0.04f;
			if (fabsf(stickError) > deadband) {
				const float normalized = stickError > 0.0f ?
					(stickError - deadband) / fmaxf(0.01f, 1.0f - input.hoverThrottleInput - deadband) :
					(stickError + deadband) / fmaxf(0.01f, input.hoverThrottleInput - deadband);
				targetVelocityMps_ = clamp(normalized,
					-fmaxf(0.0f, tuning_.maxDescendRateMps),
					fmaxf(0.0f, tuning_.maxClimbRateMps));
				targetAltitudeMeters_ = input.altitudeMeters;
			} else {
				targetVelocityMps_ *= fmaxf(0.0f, 1.0f - input.dtSeconds * 2.0f);
				targetAltitudeMeters_ += targetVelocityMps_ * input.dtSeconds;
			}
		} else {
			targetAltitudeMeters_ = input.altitudeTargetMeters;
		}
		VerticalNavigationState state;
		state.altitudeMeters = input.altitudeMeters;
		state.verticalSpeedMps = input.verticalSpeedMps;
		state.initialized = true;
		state.healthy = true;
		state.health = VerticalNavigationHealth::BarometerOnly;
		VerticalHoldTuning savedTuning = tuning_;
		tuning_.minThrottle = input.outputMinimum;
		tuning_.maxThrottle = input.outputMaximum;
		VerticalHoldOutput output = update(targetAltitudeMeters_, input.hoverThrust, state,
			lastUpdateUs_ ? lastUpdateUs_ + (uint32_t)fmaxf(1000.0f, input.dtSeconds * 1000000.0f) :
				(uint32_t)fmaxf(1000.0f, input.dtSeconds * 1000000.0f),
			true);
		tuning_ = savedTuning;
		return output.throttle;
	}

private:
	static float clamp(float value, float minValue, float maxValue) {
		return fminf(maxValue, fmaxf(minValue, value));
	}

	static float clampUnit(float value) {
		if (!isfinite(value)) return 0.0f;
		return clamp(value, 0.0f, 1.0f);
	}

	float boundedDt(uint32_t nowUs) const {
		if (lastUpdateUs_ == 0) return 0.02f;
		const float dt = (float)((uint32_t)(nowUs - lastUpdateUs_)) * 1.0e-6f;
		if (!isfinite(dt)) return 0.02f;
		return clamp(dt, 0.001f, 0.08f);
	}

	VerticalHoldTuning tuning_;
	float integral_ = 0.0f;
	float lastThrottle_ = 0.0f;
	float targetAltitudeMeters_ = 0.0f;
	float targetVelocityMps_ = 0.0f;
	uint32_t lastUpdateUs_ = 0;
	bool hasOutput_ = false;
};

using VerticalHoldController = VerticalPositionController;
