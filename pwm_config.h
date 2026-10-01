#pragma once

#include <limits.h>
#include <math.h>
#include <string.h>

static inline float pwmPeriodMicros(int frequency) {
	if (frequency <= 0) return 0.0f;
	return 1000000.0f / (float)frequency;
}

static inline bool pwmPulseFitsPeriod(float pulseMicros, int frequency) {
	const float period = pwmPeriodMicros(frequency);
	return isfinite(pulseMicros) && period > 0.0f && pulseMicros >= 0.0f && pulseMicros <= period;
}

static inline bool motorPwmConfigurationValid(int frequency, int stop, int minimum, int maximum) {
	if (maximum < 0) return true;
	return maximum > 0 && minimum >= 0 && stop >= 0 && stop <= minimum && minimum < maximum &&
		pwmPulseFitsPeriod((float)maximum, frequency);
}

static inline bool motorPwmCandidateConfigurationValid(const char *name, int candidate,
		int frequency, int stop, int minimum, int maximum) {
	if (!strcmp(name, "MOT_PWM_FREQ")) frequency = candidate;
	else if (!strcmp(name, "MOT_PWM_STOP")) stop = candidate;
	else if (!strcmp(name, "MOT_PWM_MIN")) minimum = candidate;
	else if (!strcmp(name, "MOT_PWM_MAX")) maximum = candidate;
	return motorPwmConfigurationValid(frequency, stop, minimum, maximum);
}

static inline int pwmDutyMaxForResolution(int resolution) {
	if (resolution <= 0) return 0;
	if (resolution >= (int)(sizeof(int) * CHAR_BIT - 1)) return INT_MAX;
	return (1 << resolution) - 1;
}

static inline int pwmDutyFromPulseMicros(float pulseMicros, int frequency, int resolution) {
	const int dutyMax = pwmDutyMaxForResolution(resolution);
	const float period = pwmPeriodMicros(frequency);
	if (!isfinite(pulseMicros) || period <= 0.0f || dutyMax <= 0) return 0;
	if (pulseMicros < 0.0f) pulseMicros = 0.0f;
	if (pulseMicros > period) pulseMicros = period;
	const float duty = roundf((pulseMicros / period) * dutyMax);
	if (duty <= 0.0f) return 0;
	if (duty >= (float)dutyMax) return dutyMax;
	return (int)duty;
}

static inline int motorPwmDutyFromValue(float value, int frequency, int resolution,
		int stop, int minimum, int maximum) {
	if (!isfinite(value) || !motorPwmConfigurationValid(frequency, stop, minimum, maximum)) return 0;
	if (value < 0.0f) value = 0.0f;
	if (value > 1.0f) value = 1.0f;
	if (maximum >= 0) {
		float pulse = minimum + value * (maximum - minimum);
		if (value == 0.0f) pulse = stop;
		return pwmDutyFromPulseMicros(pulse, frequency, resolution);
	}
	return (int)roundf(value * pwmDutyMaxForResolution(resolution));
}
