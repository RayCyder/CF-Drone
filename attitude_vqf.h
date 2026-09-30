#pragma once

#include "basicvqf.hpp"
#include "quaternion.h"
#include "vector.h"

// Experimental VQF 6D adapter. The production estimator remains the default.
#ifndef ATTITUDE_ESTIMATOR_VQF
#define ATTITUDE_ESTIMATOR_VQF 0
#endif
#ifndef VQF_TAU_ACC
#define VQF_TAU_ACC 3.0
#endif

class VqfAttitudeEstimator {
public:
	VqfAttitudeEstimator() : filter(makeParams(), NOMINAL_SAMPLE_TIME) {}

	void update(Quaternion &attitude, const Vector &gyro, const Vector &acc,
		float dt, bool allowGravityUpdate) {
		if (!initialized) {
			seedFromAttitude(attitude);
			initialized = true;
		}

		// BasicVQF uses a fixed nominal sample time. Scale the gyro sample so
		// strapdown integration still follows the measured loop interval.
		const float sampleScale = dt > 0.0f && isfinite(dt)
			? dt / NOMINAL_SAMPLE_TIME : 1.0f;
		const vqf_real_t gyroSample[3] = {
			gyro.x * sampleScale, gyro.y * sampleScale, gyro.z * sampleScale
		};
		const vqf_real_t accSample[3] = {acc.x, acc.y, acc.z};
		filter.updateGyr(gyroSample);
		if (allowGravityUpdate) filter.updateAcc(accSample);

		vqf_real_t q[4];
		filter.getQuat6D(q);
		attitude = Quaternion((float)q[0], (float)q[1], (float)q[2], (float)q[3]);
		if (attitude.valid()) attitude.normalize();
	}

private:
	static constexpr vqf_real_t NOMINAL_SAMPLE_TIME = 0.001;
	BasicVQF filter;
	bool initialized = false;
	static BasicVQFParams makeParams() {
		BasicVQFParams params;
		params.tauAcc = VQF_TAU_ACC;
		return params;
	}

	void seedFromAttitude(const Quaternion &attitude) {
		BasicVQFState state = filter.getState();
		state.gyrQuat[0] = attitude.w;
		state.gyrQuat[1] = attitude.x;
		state.gyrQuat[2] = attitude.y;
		state.gyrQuat[3] = attitude.z;
		state.accQuat[0] = 1.0;
		state.accQuat[1] = 0.0;
		state.accQuat[2] = 0.0;
		state.accQuat[3] = 0.0;
		filter.setState(state);
	}
};
