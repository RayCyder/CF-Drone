#include "../optical_flow_calibration.h"

#include <cassert>
#include <cmath>

int main() {
	OpticalFlowCalibrationAccumulator calibration;
	calibration.start(1000);
	calibration.observe(10000, true, true, 10, -20, 0.01f, -0.02f,
		80, 0.0f, 0.0f, 0.10f, true, true);
	calibration.observe(20000, true, true, 10, -20, 0.01f, -0.02f,
		60, 0.0f, 0.0f, 0.10f, true, true);
	calibration.observe(20000, true, true, 99, 99, 1.0f, 1.0f,
		1, 0.0f, 0.0f, 0.10f, true, true);
	calibration.stop(30000);
	const auto &stats = calibration.stats();
	assert(!stats.active);
	assert(stats.sampleCount == 2);
	assert(stats.validSampleCount == 2);
	assert(stats.usableSampleCount == 2);
	assert(stats.rangeSampleCount == 2);
	assert(stats.pixelX == 20 && stats.pixelY == -40);
	assert(std::fabs(stats.estimatedBodyXMeters - 0.004f) < 1e-6f);
	assert(std::fabs(stats.estimatedBodyYMeters - 0.002f) < 1e-6f);
	assert(stats.qualityMinimum == 60 && stats.qualityMaximum == 80);
}
