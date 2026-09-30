#include "Arduino.h"
#include <cassert>
#include <cmath>
#include "../vector.h"
#include "../quaternion.h"

double t = 0.0;
float dt = 0.001f;
float controlRoll = 0.0f, controlPitch = 0.0f;
Vector rates, gyro, acc;
Quaternion attitude;
bool landed = false;
bool armed = false;
static bool motorOutputActive = false;
bool motorsActive() { return motorOutputActive; }
void applyGyro();
void applyAcc();
void applyLevel();

#define ATTITUDE_ESTIMATOR_VQF 1
#include "../estimate.ino"
#undef PI
#include "../basicvqf.cpp"

int main() {
	levelWeight = 0.0f;
	levelBiasGain = 0.0f;
	attitude = Quaternion();
	gyro = Vector(0.0f, 0.0f, 0.0f);
	acc = Vector(NAN, 0.0f, 0.0f);
	estimate();
	assert(attitude.valid() && fabsf(attitude.norm() - 1.0f) < 1e-5f);

	// A valid sample after an invalid one must recover rather than inherit a
	// NaN from the gravity low-pass filter.
	acc = Vector(0.0f, 0.0f, ONE_G);
	estimate();
	assert(landed);
	assert(attitude.valid() && fabsf(attitude.norm() - 1.0f) < 1e-5f);

	// Disarmed motor vibration must not update the VQF gravity channel.
	motorOutputActive = true;
	acc = Vector(4.0f, 0.0f, 9.0f);
	const Quaternion beforeMotorSample = attitude;
	estimate();
	assert(fabsf(attitude.w - beforeMotorSample.w) < 1e-6f);
	assert(fabsf(attitude.x - beforeMotorSample.x) < 1e-6f);
	assert(fabsf(attitude.y - beforeMotorSample.y) < 1e-6f);
	assert(fabsf(attitude.z - beforeMotorSample.z) < 1e-6f);
	assert(!landed);

	// Airborne gravity correction must respect pilot stick gating.
	armed = true;
	controlRoll = 0.5f;
	const Quaternion beforeStick = attitude;
	estimate();
	assert(fabsf(attitude.w - beforeStick.w) < 1e-6f);
	assert(fabsf(attitude.x - beforeStick.x) < 1e-6f);
	assert(fabsf(attitude.y - beforeStick.y) < 1e-6f);
	assert(fabsf(attitude.z - beforeStick.z) < 1e-6f);

	controlRoll = 0.0f;
	estimate();
	assert(attitude.valid() && fabsf(attitude.norm() - 1.0f) < 1e-5f);
	assert(fabsf(attitude.x - beforeStick.x) + fabsf(attitude.y - beforeStick.y) > 1e-8f);
}
