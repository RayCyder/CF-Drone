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
static bool motorOutputActive = true;
bool motorsActive() { return motorOutputActive; }
void applyGyro();
void applyAcc();
void applyLevel();

#include "../estimate.ino"

static void resetEstimator(Quaternion q) {
	attitude = q;
	levelGyroBias = Vector(0, 0, 0);
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

int main() {
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

	resetEstimator(tilted);
	levelWeight = 0.0f;
	levelBiasGain = 0.001f;
	dt = 0.001f;
	applyLevel();
	assert(levelGyroBias.norm() > 0.0f); // enabled I correction remains active
	landed = true;
	applyLevel();
	assert(levelGyroBias.zero()); // landed reset still runs before the disabled-work shortcut
	puts("six-axis estimator resource regression: PASS");
}
