#include "Arduino.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include "../vector.h"
#include "../quaternion.h"

double t = 0.0;
float dt = 0.001f;
float controlRoll = 0.0f, controlPitch = 0.0f;
Vector rates, gyro, acc;
Quaternion attitude;
bool landed = false;
bool armed = false;
static bool motorOutputActive = true;
bool motorsActive() { return motorOutputActive; }
void applyGyro();
void applyAcc();
void applyLevel();

#include "../estimate.ino"

#ifndef TEST_EXPECT_EST_ACCEL_FUSION_FILTER_ALPHA
#define TEST_EXPECT_EST_ACCEL_FUSION_FILTER_ALPHA 0.2f
#endif

static void resetEstimator(Quaternion q) {
	attitude = q;
	levelGyroBias = Vector(0, 0, 0);
	accelerationFusionFilter.reset();
	landed = false;
	controlRoll = controlPitch = 0.0f;
}

static Quaternion runLevelCorrection(float sampleDt, int samples) {
	resetEstimator(Quaternion::fromEuler(Vector(0.1f, 0.08f, 0.0f)));
	levelWeight = 0.001f;
	levelBiasGain = 0.0f;
	for (int i = 0; i < samples; ++i) {
		dt = sampleDt;
		applyLevel();
	}
	return attitude;
}

static Quaternion runAccCorrection(float sampleDt, int samples) {
	attitude = Quaternion();
	levelGyroBias = Vector(0, 0, 0);
	landed = true;
	motorOutputActive = false;
	acc = Vector(ONE_G * sinf(0.1f), 0.0f, ONE_G * cosf(0.1f));
	for (int i = 0; i < samples; ++i) {
		dt = sampleDt;
		applyAcc();
	}
	return attitude;
}

static Quaternion runAirborneAccCorrection(float sampleDt, int samples) {
	resetEstimator(Quaternion::fromEuler(Vector(0.1f, 0.0f, 0.0f)));
	armed = true;
	motorOutputActive = true;
	acc = Vector(0.0f, 0.0f, ONE_G);
	levelWeight = 0.0f;
	levelBiasGain = 0.0f;
	for (int i = 0; i < samples; ++i) {
		dt = sampleDt;
		applyAcc();
	}
	assert(accelCorrectionConfidence == 1.0f);
	return attitude;
}

int main() {
	assert(accWeight == 0.0005f);
	assert(EST_RAW_ACCEL_NORM_TOLERANCE == 0.05f);
	assert(fabsf(EST_ACCEL_FUSION_FILTER_ALPHA - TEST_EXPECT_EST_ACCEL_FUSION_FILTER_ALPHA) < 1e-7f);
	accelerationFusionFilter.reset();
	accelerationFusionFilter.update(Vector(0.0f, 0.0f, ONE_G), 0.001f, 0.001f);
	const Vector alphaStep = accelerationFusionFilter.update(Vector(ONE_G, 0.0f, 0.0f), 0.001f, 0.001f);
	assert(fabsf(alphaStep.x - ONE_G * TEST_EXPECT_EST_ACCEL_FUSION_FILTER_ALPHA) < 1e-5f);
	assert(fabsf(alphaStep.z - ONE_G * (1.0f - TEST_EXPECT_EST_ACCEL_FUSION_FILTER_ALPHA)) < 1e-5f);
	assert(fabsf(adaptiveAccelerationWeight(0.003f, 1.0f) - 0.003f) < 1e-7f);
	assert(fabsf(adaptiveAccelerationWeight(0.003f, cosf(radians(5.0f))) - 0.003f) < 1e-6f);
	const float midInnovationAngle = (5.0f + ESTIMATE_ACCEL_INNOVATION_MAX_DEG) * 0.5f;
	const float midInnovation = adaptiveAccelerationWeight(0.003f, cosf(radians(midInnovationAngle)));
	const float minimumAdaptiveWeight = min(0.003f, ESTIMATE_ACCEL_MIN_ADAPTIVE_WEIGHT);
	assert(fabsf(midInnovation - (0.003f + minimumAdaptiveWeight) * 0.5f) < 1e-6f);
	assert(fabsf(adaptiveAccelerationWeight(0.003f, cosf(radians(ESTIMATE_ACCEL_INNOVATION_MAX_DEG))) - minimumAdaptiveWeight) < 1e-6f);
	assert(fabsf(adaptiveAccelerationWeight(0.003f, cosf(radians(ESTIMATE_ACCEL_INNOVATION_MAX_DEG + 10.0f))) - minimumAdaptiveWeight) < 1e-7f);
	assert(fabsf(adaptiveAccelerationWeight(0.0002f, cosf(radians(ESTIMATE_ACCEL_INNOVATION_MAX_DEG + 10.0f))) - 0.0002f) < 1e-7f);

	const Quaternion tilted = Quaternion::fromEuler(Vector(0.1f, 0.08f, 0.0f));
	resetEstimator(tilted);
	levelWeight = 0.0f;
	levelBiasGain = 0.0f;
	applyLevel();
	assert(attitude.w == tilted.w && attitude.x == tilted.x && attitude.y == tilted.y && attitude.z == tilted.z);

	const Quaternion nominal = runLevelCorrection(0.001f, 10);
	const Quaternion jittered = runLevelCorrection(0.0005f, 20);
	assert(fabsf(nominal.w - jittered.w) < 1e-4f);
	assert(fabsf(nominal.x - jittered.x) < 1e-4f);
	assert(fabsf(nominal.y - jittered.y) < 1e-4f);
	const Quaternion accNominal = runAccCorrection(0.001f, 10);
	const Quaternion accJittered = runAccCorrection(0.0005f, 20);
	assert(fabsf(accNominal.w - accJittered.w) < 1e-5f);
	assert(fabsf(accNominal.x - accJittered.x) < 1e-5f);
	assert(fabsf(accNominal.y - accJittered.y) < 1e-5f);

	const Quaternion airborneInitial = Quaternion::fromEuler(Vector(0.1f, 0.0f, 0.0f));
	const Quaternion airborneCorrected = runAirborneAccCorrection(0.001f, 500);
	assert(fabsf(airborneCorrected.toEuler().x) < 0.09f);
	assert(fabsf(airborneCorrected.toEuler().x) + 0.005f < fabsf(airborneInitial.toEuler().x));
	const Quaternion airborneJittered = runAirborneAccCorrection(0.0005f, 1000);
	assert(fabsf(airborneCorrected.x - airborneJittered.x) < 1e-4f);
	assert(fabsf(airborneCorrected.w - airborneJittered.w) < 1e-4f);

	// Strong non-gravitational acceleration must not pull the estimate toward level.
	resetEstimator(airborneInitial);
	armed = true;
	motorOutputActive = true;
	acc = Vector(0.0f, 0.0f, ONE_G * 1.5f);
	dt = 0.001f;
	applyAcc();
	assert(accelCorrectionConfidence == 0.0f);
	assert(fabsf(attitude.x - airborneInitial.x) < 1e-6f);
	assert(fabsf(attitude.w - airborneInitial.w) < 1e-6f);

	// High-frequency vibration can push raw acceleration outside the 1 g gate.
	// The estimator should limit its correction rather than chase each peak.
	resetEstimator(airborneInitial);
	armed = true;
	motorOutputActive = true;
	levelWeight = 0.0f;
	levelBiasGain = 0.0f;
	for (int i = 0; i < 2000; ++i) {
		dt = 0.001f;
		acc = Vector(12.0f * sinf(2.0f * PI * 200.0f * i * dt), 0.0f, ONE_G);
		applyAcc();
	}
	assert(fabsf(attitude.toEuler().x) < 0.09f);
	assert(fabsf(attitude.toEuler().x) + 0.005f < fabsf(airborneInitial.toEuler().x));
	assert(accelCorrectionConfidence < 0.05f);

	// Stick input fades out accelerometer correction during intentional maneuvers.
	resetEstimator(airborneInitial);
	armed = true;
	motorOutputActive = true;
	controlRoll = levelGateThreshold;
	acc = Vector(0.0f, 0.0f, ONE_G);
	applyAcc();
	assert(fabsf(attitude.x - airborneInitial.x) < 1e-6f);
	controlRoll = 0.0f;

	// Gravity provides no heading reference, so yaw-only error is unchanged.
	const Quaternion yawOnly = Quaternion::fromEuler(Vector(0.0f, 0.0f, 0.3f));
	resetEstimator(yawOnly);
	armed = true;
	motorOutputActive = true;
	acc = Vector(0.0f, 0.0f, ONE_G);
	applyAcc();
	assert(fabsf(attitude.w - yawOnly.w) < 1e-6f);
	assert(fabsf(attitude.z - yawOnly.z) < 1e-6f);

	// Disarmed motor tests must not activate airborne gravity fusion.
	resetEstimator(airborneInitial);
	armed = false;
	motorOutputActive = true;
	acc = Vector(0.0f, 0.0f, ONE_G);
	applyAcc();
	assert(fabsf(attitude.x - airborneInitial.x) < 1e-6f);
	assert(fabsf(attitude.w - airborneInitial.w) < 1e-6f);

	resetEstimator(tilted);
	levelWeight = 0.0f;
	levelBiasGain = 0.001f;
	dt = 0.001f;
	applyLevel();
	assert(levelGyroBias.norm() > 0.0f); // enabled I correction remains active
	landed = true;
	applyLevel();
	assert(levelGyroBias.zero()); // landed reset still runs before the disabled-work shortcut

	// Zero is a valid configured stick-gate threshold. At centered sticks it
	// must retain correction without dividing 0 by 0; deflection disables it.
	resetEstimator(tilted);
	landed = false;
	levelWeight = 0.0f;
	levelBiasGain = 0.001f;
	levelGateThreshold = 0.0f;
	controlRoll = controlPitch = 0.0f;
	applyLevel();
	assert(attitude.finite() && levelGyroBias.finite());
	assert(levelGyroBias.norm() > 0.0f);
	const Vector centeredBias = levelGyroBias;
	controlRoll = 0.1f;
	applyLevel();
	assert(levelGyroBias.x == centeredBias.x && levelGyroBias.y == centeredBias.y);
	levelGateThreshold = 0.2f;

	// After a 50 ms loop stall, rate filtering must use the newest measured rate
	// without bilinear-coefficient overshoot before integrating across that gap.
	resetEstimator(Quaternion());
	ratesFilter.reset();
	accelerationFusionFilter.reset();
	levelWeight = 0.0f;
	levelBiasGain = 0.0f;
	armed = false;
	motorOutputActive = true;
	acc = Vector(0.0f, 0.0f, ONE_G);
	gyro = Vector();
	dt = 0.001f;
	estimate();
	gyro = Vector(0.2f, 0.0f, 0.0f);
	dt = 0.050f;
	estimate();
	assert(fabsf(rates.x - 0.2f) < 1e-6f);
	assert(fabsf(attitude.toEuler().x - 0.01f) < 1e-5f);

	// One gravity sample after a long gap must not receive a full-gap correction
	// gain; otherwise a single 5-degree acceleration disturbance causes a jump.
	resetEstimator(Quaternion());
	ratesFilter.reset();
	accelerationFusionFilter.reset();
	accWeight = 0.003f;
	armed = false;
	motorOutputActive = false;
	gyro = Vector();
	acc = Vector(0.0f, 0.0f, ONE_G);
	dt = 0.001f;
	estimate();
	const float accelerationTilt = radians(5.0f);
	acc = Vector(ONE_G * sinf(accelerationTilt), 0.0f, ONE_G * cosf(accelerationTilt));
	dt = 0.050f;
	estimate();
	assert(fabsf(attitude.toEuler().x) < radians(0.08f));
	puts("six-axis estimator resource regression: PASS");
}
