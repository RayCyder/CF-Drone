// 低通滤波器实现
// Low pass filter implementation

#pragma once

template <typename T> // Using template to make the filter usable for scalar and vector values
class LowPassFilter {
public:
	float alpha; // smoothing constant, 1 means filter disabled
	T output;

	LowPassFilter(float alpha): alpha(alpha) {};

	T update(const T input) {
		if (alpha == 1) { // filter disabled
			return input;
		}

		if (!initialized) {
			output = input;
			initialized = true;
		}

		return output += alpha * (input - output);
	}

	// Keep the configured coefficient calibrated at nominalDt while preserving
	// its time response when the flight-loop interval jitters. The bilinear form
	// is exact at nominalDt and avoids an exponential in the 1 kHz loop.
	T update(const T input, float sampleDt, float nominalDt) {
		if (!isfinite(sampleDt) || !isfinite(nominalDt) || sampleDt <= 0.0f || nominalDt <= 0.0f ||
			!isfinite(alpha) || alpha >= 1.0f || alpha <= 0.0f) {
			return update(input);
		}
		const float x0 = (2.0f * alpha) / (2.0f - alpha);
		// The bilinear coefficient exceeds 1 when a long scheduler/flash stall
		// makes the sample interval much larger than nominal. In that case the
		// filter should consume the newest sample directly, not extrapolate past it.
		const float sampleDtRatio = sampleDt / nominalDt;
		float adjustedAlpha;
		if (sampleDtRatio >= 2.0f / x0) {
			adjustedAlpha = 1.0f;
		} else {
			const float x = x0 * sampleDtRatio;
			adjustedAlpha = x / (1.0f + 0.5f * x);
		}
		if (!initialized) {
			output = input;
			initialized = true;
		}
		return output += adjustedAlpha * (input - output);
	}

	void setCutOffFrequency(float cutOffFreq, float dt) {
		alpha = 1 - exp(-2 * PI * cutOffFreq * dt);
	}

	void reset() {
		initialized = false;
	}

private:
	bool initialized = false;
};
