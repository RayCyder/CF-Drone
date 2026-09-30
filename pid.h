// PID控制器实现
// PID controller implementation

#pragma once

#include "lpf.h"

class PID {
public:
	float p, i, d;
	float windup;
	float dtMax;

	float derivative = 0;
	float integral = 0;

	LowPassFilter<float> lpf; // low pass filter for derivative term

	PID(float p, float i, float d, float windup = 0, float dAlpha = 1, float dtMax = 0.1) :
		p(p), i(i), d(d), windup(windup), dtMax(dtMax), lpf(dAlpha) {}

	// dt is the integer-clock frame interval, not a float absolute-time subtraction.
	float update(float error, float sampleDt = dt, bool integrate = true) {
		if (!isfinite(error) || !isfinite(sampleDt) || sampleDt <= 0 || sampleDt >= dtMax) {
			reset();
			return isfinite(error) ? p * error : 0.0f;
		}
		if (i > 0 && windup > 0) {
			const float limit = windup / i;
			if (integrate) integral += error * sampleDt;
			integral = constrain(integral, -limit, limit);
		} else {
			integral = 0;
		}
		if (isfinite(prevError)) {
			derivative = lpf.update((error - prevError) / sampleDt);
		} else {
			// Prime a filtered derivative at zero so the first finite difference
			// after reset does not bypass the low-pass filter as a full-rate spike.
			lpf.update(0.0f);
			derivative = 0.0f;
		}
		prevError = error;
		return p * error + i * integral + d * derivative;
	}

	void reset() {
		prevError = NAN;
		integral = 0;
		derivative = 0;
		lpf.reset();
	}

private:
	float prevError = NAN;
};
