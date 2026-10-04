#include "../landing_barometer_guard.h"

#include <cassert>
#include <cmath>

int main() {
	BarometerEstimate estimate;
	estimate.sample.pressurePa = 101000.0f;
	estimate.sample.altitudeMeters = 10.0f;
	estimate.sample.timestampUs = 1000000;
	estimate.sample.valid = true;
	estimate.relativeAltitudeMeters = 2.0f;
	estimate.verticalSpeedMps = -1.8f;
	estimate.valid = true;
	assert(std::fabs(landingBarometerThrustCorrection(estimate, 1010000) - 0.025f) < 1e-6f);
	estimate.verticalSpeedMps = -0.5f;
	assert(std::fabs(landingBarometerThrustCorrection(estimate, 1010000) - 0.0025f) < 1e-6f);
	estimate.verticalSpeedMps = -0.3f;
	assert(landingBarometerThrustCorrection(estimate, 1010000) == 0.0f);
	estimate.verticalSpeedMps = -2.0f;
	estimate.relativeAltitudeMeters = 0.5f;
	assert(landingBarometerThrustCorrection(estimate, 1010000) == 0.0f);
	estimate.relativeAltitudeMeters = 2.0f;
	assert(landingBarometerThrustCorrection(estimate, 1300001) == 0.0f);
}
