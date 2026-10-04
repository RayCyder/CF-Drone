#include "../bmp388_compensation.h"

#include <cassert>
#include <cmath>

int main() {
	const uint8_t rawCalibration[21] = {
		0x70, 0x6b, 0x43, 0x67, 0x03, 0x88, 0x13, 0x98, 0x3a, 0x03, 0xfe,
		0x20, 0x4e, 0xa8, 0x61, 0x0a, 0xfb, 0x18, 0xfc, 0x05, 0xfe,
	};
	const Bmp388Calibration c = bmp388DecodeCalibration(rawCalibration);
	assert(std::fabs(c.t1 - 7041024.0) < 0.001);

	double pressure = 0.0;
	double temperature = 0.0;
	assert(bmp388Compensate(7000000, 8000000, c, pressure, temperature));
	assert(std::fabs(temperature - 23.6193229882) < 1e-7);
	assert(std::fabs(pressure - 92652.7295548) < 1e-5);

	assert(!bmp388Compensate(0, 0, c, pressure, temperature));
}
