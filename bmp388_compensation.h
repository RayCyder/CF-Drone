#pragma once

#include <math.h>
#include <stdint.h>

// BMP388 floating-point compensation follows Bosch BMP3 SensorAPI.
// Keeping it independent of Arduino makes coefficient scaling testable on host.
struct Bmp388Calibration {
	double t1, t2, t3;
	double p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11;
};

inline uint16_t bmp388ReadU16(const uint8_t *p) {
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

inline int16_t bmp388ReadS16(const uint8_t *p) {
	return (int16_t)bmp388ReadU16(p);
}

inline Bmp388Calibration bmp388DecodeCalibration(const uint8_t raw[21]) {
	Bmp388Calibration c;
	// Bosch stores par_t1 in units of 1/256 raw-temperature counts, so the
	// quantized coefficient is raw / 2^-8 (equivalent to raw * 256).
	c.t1 = (double)bmp388ReadU16(raw + 0) / 0.00390625;
	c.t2 = (double)bmp388ReadU16(raw + 2) / 1073741824.0;
	c.t3 = (double)(int8_t)raw[4] / 281474976710656.0;
	c.p1 = ((double)bmp388ReadS16(raw + 5) - 16384.0) / 1048576.0;
	c.p2 = ((double)bmp388ReadS16(raw + 7) - 16384.0) / 536870912.0;
	c.p3 = (double)(int8_t)raw[9] / 4294967296.0;
	c.p4 = (double)(int8_t)raw[10] / 137438953472.0;
	c.p5 = (double)bmp388ReadU16(raw + 11) / 0.125;
	c.p6 = (double)bmp388ReadU16(raw + 13) / 64.0;
	c.p7 = (double)(int8_t)raw[15] / 256.0;
	c.p8 = (double)(int8_t)raw[16] / 32768.0;
	c.p9 = (double)bmp388ReadS16(raw + 17) / 281474976710656.0;
	c.p10 = (double)(int8_t)raw[19] / 281474976710656.0;
	c.p11 = (double)(int8_t)raw[20] / 36893488147419103232.0;
	return c;
}

inline bool bmp388Compensate(uint32_t pressureRaw, uint32_t temperatureRaw,
	const Bmp388Calibration &c, double &pressurePa, double &temperatureC) {
	const double partialTemperature = (double)temperatureRaw - c.t1;
	const double tLin = partialTemperature * c.t2 +
		partialTemperature * partialTemperature * c.t3;
	const double t2 = tLin * tLin;
	const double t3 = t2 * tLin;
	const double p = (double)pressureRaw;
	pressurePa = c.p5 + c.p6 * tLin + c.p7 * t2 + c.p8 * t3 +
		p * (c.p1 + c.p2 * tLin + c.p3 * t2 + c.p4 * t3) +
		p * p * (c.p9 + c.p10 * tLin) + p * p * p * c.p11;
	temperatureC = tLin;
	return isfinite(pressurePa) && pressurePa >= 30000.0 && pressurePa <= 125000.0 &&
		isfinite(temperatureC) && temperatureC >= -40.0 && temperatureC <= 85.0;
}
