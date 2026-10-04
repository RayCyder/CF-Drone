#pragma once

#include <math.h>
#include <stdint.h>
#include "flight_sensor_interfaces.h"

constexpr uint32_t LANDING_BARO_MAX_AGE_US = 250000;
constexpr float LANDING_BARO_MIN_RELATIVE_ALTITUDE_M = 1.0f;
constexpr float LANDING_BARO_FAST_DESCENT_MPS = -0.4f;
constexpr float LANDING_BARO_GUARD_GAIN = 0.025f;
constexpr float LANDING_BARO_MAX_THRUST_CORRECTION = 0.025f;

// A one-sided guard only adds a small amount of thrust when a fresh barometer
// estimate shows a fast descent above the near-ground region. It never lowers
// the calibrated fixed landing thrust and cannot decide that touchdown occurred.
inline float landingBarometerThrustCorrection(const BarometerEstimate &estimate,
	uint32_t nowUs) {
	if (!barometerEstimateUsable(estimate, nowUs, LANDING_BARO_MAX_AGE_US) ||
		estimate.relativeAltitudeMeters < LANDING_BARO_MIN_RELATIVE_ALTITUDE_M ||
		estimate.verticalSpeedMps >= LANDING_BARO_FAST_DESCENT_MPS) return 0.0f;
	const float correction = (LANDING_BARO_FAST_DESCENT_MPS - estimate.verticalSpeedMps) *
		LANDING_BARO_GUARD_GAIN;
	return fminf(LANDING_BARO_MAX_THRUST_CORRECTION, fmaxf(0.0f, correction));
}
