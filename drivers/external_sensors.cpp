#include "external_sensors.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "board_config.h"
#include "flight_sensor_interfaces.h"

namespace {
constexpr uint8_t BMP388_ADDRS[] = {0x76, 0x77};
constexpr uint8_t BMP388_CHIP_ID = 0x50;
constexpr uint8_t QMC5883P_ADDR = 0x2C;
constexpr uint8_t QMC5883P_CHIP_ID = 0x80;

struct Bmp388Calibration {
	double t1, t2, t3;
	double p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11;
};

bool readRegisters(uint8_t address, uint8_t reg, uint8_t *data, size_t length) {
	Wire.beginTransmission(address);
	Wire.write(reg);
	if (Wire.endTransmission(false) != 0) return false;
	const size_t received = Wire.requestFrom((int)address, (int)length, (int)true);
	if (received != length) {
		while (Wire.available()) Wire.read();
		return false;
	}
	for (size_t i = 0; i < length; ++i) data[i] = (uint8_t)Wire.read();
	return true;
}

bool writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
	Wire.beginTransmission(address);
	Wire.write(reg);
	Wire.write(value);
	return Wire.endTransmission() == 0;
}

uint16_t readU16(const uint8_t *p) {
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

int16_t readS16(const uint8_t *p) {
	return (int16_t)readU16(p);
}

bool beginBmp388(uint8_t &address) {
	uint8_t id = 0;
	for (size_t i = 0; i < sizeof(BMP388_ADDRS); ++i) {
		if (readRegisters(BMP388_ADDRS[i], 0x00, &id, 1) && id == BMP388_CHIP_ID) {
			address = BMP388_ADDRS[i];
			break;
		}
	}
	if (!address) return false;
	if (!writeRegister(address, 0x7E, 0xB6)) return false; // soft reset
	delay(10);
	// Pressure x4, temperature x2, normal mode, 50 Hz output data rate.
	if (!writeRegister(address, 0x1C, 0x0A) || !writeRegister(address, 0x1D, 0x03) ||
		!writeRegister(address, 0x1B, 0x33)) return false;
	delay(50);
	return true;
}

bool readBmp388(uint8_t address, BarometerSample &sample) {
	uint8_t raw[6];
	if (!readRegisters(address, 0x04, raw, sizeof(raw))) return false;
	uint8_t calib[21];
	if (!readRegisters(address, 0x31, calib, sizeof(calib))) return false;
	Bmp388Calibration c;
	c.t1 = (double)readU16(calib + 0) * 0.00390625; // 2^-8
	c.t2 = (double)readU16(calib + 2) * 0.000000000931322574615478515625; // 2^-30
	c.t3 = (double)(int8_t)calib[4] * 0.000000000000003552713678800500929355621337890625; // 2^-48
	c.p1 = ((double)readS16(calib + 5) - 16384.0) * 0.00000095367431640625; // 2^-20
	c.p2 = ((double)readS16(calib + 7) - 16384.0) * 0.00000000186264514923095703125; // 2^-29
	c.p3 = (double)(int8_t)calib[9] * 0.00000000023283064365386962890625; // 2^-32
	c.p4 = (double)(int8_t)calib[10] * 0.0000000000072759576141834259033203125; // 2^-37
	c.p5 = (double)readU16(calib + 11) * 8.0;
	c.p6 = (double)readU16(calib + 13) * 0.015625; // 2^-6
	c.p7 = (double)(int8_t)calib[15] * 0.00390625; // 2^-8
	c.p8 = (double)(int8_t)calib[16] * 0.000030517578125; // 2^-15
	c.p9 = (double)readS16(calib + 17) * 0.000000000000003552713678800500929355621337890625; // 2^-48
	c.p10 = (double)(int8_t)calib[19] * 0.000000000000003552713678800500929355621337890625; // 2^-48
	c.p11 = (double)(int8_t)calib[20] * 0.00000000000000000002710505431213761091814151071071624755859375; // 2^-65
	const uint32_t pressureRaw = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16);
	const uint32_t temperatureRaw = (uint32_t)raw[3] | ((uint32_t)raw[4] << 8) | ((uint32_t)raw[5] << 16);
	const double t = (double)temperatureRaw;
	const double tLin = c.t1 + t * (c.t2 + t * c.t3);
	const double pressure = c.p5 + c.p6 * tLin +
		c.p7 * tLin * tLin + c.p8 * tLin * tLin * tLin +
		(double)pressureRaw * (c.p1 + c.p2 * tLin +
		c.p3 * tLin * tLin + c.p4 * tLin * tLin * tLin) +
		(double)pressureRaw * (double)pressureRaw * (c.p9 + c.p10 * tLin) +
		(double)pressureRaw * (double)pressureRaw * (double)pressureRaw * c.p11;
	if (!isfinite(pressure) || pressure < 30000.0 || pressure > 120000.0 || !isfinite(tLin)) return false;
	sample.pressurePa = (float)pressure;
	sample.temperatureC = (float)tLin;
	sample.altitudeMeters = 44330.0f * (1.0f - powf(sample.pressurePa / 101325.0f, 0.19029495f));
	sample.timestampUs = micros();
	sample.valid = isfinite(sample.altitudeMeters);
	return sample.valid;
}

bool beginQmc5883p() {
	uint8_t id = 0;
	if (!readRegisters(QMC5883P_ADDR, 0x00, &id, 1) || id != QMC5883P_CHIP_ID) return false;
	if (!writeRegister(QMC5883P_ADDR, 0x0B, 0x80)) return false; // soft reset
	delay(10);
	if (!writeRegister(QMC5883P_ADDR, 0x29, 0x06) ||
		!writeRegister(QMC5883P_ADDR, 0x0B, 0x08) ||
		!writeRegister(QMC5883P_ADDR, 0x0A, 0xCD)) return false; // axis sign, 8 G, 200 Hz normal mode
	delay(20);
	return true;
}

bool readQmc5883p(int16_t &x, int16_t &y, int16_t &z) {
	uint8_t status = 0;
	if (!readRegisters(QMC5883P_ADDR, 0x09, &status, 1) || !(status & 0x01)) return false;
	uint8_t raw[6];
	if (!readRegisters(QMC5883P_ADDR, 0x01, raw, sizeof(raw))) return false;
	x = readS16(raw);
	y = readS16(raw + 2);
	z = readS16(raw + 4);
	return true;
}
} // namespace

void setupExternalSensors() {
#if BOARD_I2C_SDA >= 0 && BOARD_I2C_SCL >= 0
	const bool i2cReady = Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, 100000);
#else
	const bool i2cReady = false;
#endif
	if (!i2cReady) {
		Serial.println("EXT_SENSOR bus=unavailable");
		return;
	}
	uint8_t address = 0;
	const bool barometer = beginBmp388(address);
	const bool compass = beginQmc5883p();
	Serial.printf("EXT_SENSOR bus=ready sda=%d scl=%d bmp388=%s addr=0x%02X qmc5883p=%s addr=0x%02X\n",
		BOARD_I2C_SDA, BOARD_I2C_SCL, barometer ? "ready" : "not_found", address,
		compass ? "ready" : "not_found", compass ? QMC5883P_ADDR : 0);
}

void printExternalSensorReadings() {
	if (!Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, 100000)) {
		Serial.println("EXT_SENSOR error=i2c_unavailable");
		return;
	}
	uint8_t address = 0;
	if (beginBmp388(address)) {
		BarometerSample sample;
		if (readBmp388(address, sample)) {
			Serial.printf("BARO bmp388 pressure_pa=%.2f temperature_c=%.2f altitude_m=%.2f\n",
				sample.pressurePa, sample.temperatureC, sample.altitudeMeters);
		} else {
			Serial.println("BARO bmp388 read=not_ready_or_invalid");
		}
	} else {
		Serial.println("BARO bmp388=not_found");
	}
	if (beginQmc5883p()) {
		int16_t x, y, z;
		if (readQmc5883p(x, y, z)) {
			const float heading = atan2f((float)y, (float)x) * 57.2957795f;
			const float normalizedHeading = heading < 0.0f ? heading + 360.0f : heading;
			Serial.printf("COMPASS qmc5883p raw_x=%d raw_y=%d raw_z=%d heading_raw_deg=%.1f\n",
				x, y, z, normalizedHeading);
		} else {
			Serial.println("COMPASS qmc5883p read=not_ready");
		}
	} else {
		Serial.println("COMPASS qmc5883p=not_found");
	}
}
