#include "Arduino.h"
#include <cassert>
#include <cmath>
#include "../attitude_vqf.h"
#undef PI // VQF defines its own PI constant in the vendored implementation.
#include "../basicvqf.cpp"

int main() {
	VqfAttitudeEstimator estimator;
	Quaternion attitude = Quaternion::fromEuler(Vector(radians(5.0f), radians(-3.0f), radians(12.0f)));
	const Quaternion initial = attitude;
	const Vector gravity(0.0f, 0.0f, 9.80665f);

	// The first update seeds VQF from the live attitude; a rejected accel
	// update must not change tilt or heading.
	estimator.update(attitude, Vector(0.0f, 0.0f, 0.0f), gravity, 0.001f, false);
	assert(fabsf(attitude.w - initial.w) < 1e-6f);
	assert(fabsf(attitude.x - initial.x) < 1e-6f);
	assert(fabsf(attitude.y - initial.y) < 1e-6f);
	assert(fabsf(attitude.z - initial.z) < 1e-6f);

	// Gyroscope integration follows dt and keeps the project's quaternion order.
	attitude = Quaternion();
	VqfAttitudeEstimator yawEstimator;
	yawEstimator.update(attitude, Vector(0.0f, 0.0f, 1.0f), gravity, 0.001f, false);
	assert(fabsf(attitude.getYaw() - 0.001f) < 1e-5f);
	assert(attitude.valid() && fabsf(attitude.norm() - 1.0f) < 1e-5f);

	// An allowed gravity update is accepted and remains finite.
	yawEstimator.update(attitude, Vector(0.0f, 0.0f, 0.0f), Vector(4.0f, 0.0f, 9.0f), 0.001f, true);
	assert(attitude.valid() && fabsf(attitude.norm() - 1.0f) < 1e-5f);
}
