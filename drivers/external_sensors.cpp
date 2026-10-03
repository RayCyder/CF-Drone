#include "external_sensors.h"

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <math.h>
#include "board_config.h"
#include "flight_sensor_interfaces.h"

extern Preferences storage;
extern bool armed;
extern bool motorTestActive;
bool motorsActive();
bool beginPersistentWriteBatch();
void finishPersistentWriteBatch(bool wroteAny);

namespace {
constexpr uint8_t BMP388_ADDRS[] = {0x76, 0x77};
constexpr uint8_t BMP388_CHIP_ID = 0x50;
constexpr uint8_t QMC5883P_ADDR = 0x2C;
constexpr uint8_t QMC5883P_CHIP_ID = 0x80;
constexpr uint32_t MAG_SAMPLE_INTERVAL_MS = 20;
constexpr uint32_t MAG_CAL_MAGIC = 0x4D414731; // MAG1

struct MagCalibration {
	float offset[3] = {};
	float scale[3] = {1.0f, 1.0f, 1.0f};
	float headingOffset = 0.0f;
	bool valid = false;
};

struct PersistedMagCalibration {
	uint32_t magic;
	float offset[3];
	float scale[3];
	float headingOffset;
};

struct MagRuntimeState {
	MagCalibration calibration;
	int16_t minimum[3] = {INT16_MAX, INT16_MAX, INT16_MAX};
	int16_t maximum[3] = {INT16_MIN, INT16_MIN, INT16_MIN};
	uint32_t samples = 0;
	uint32_t lastSampleMs = 0;
	bool active = false;
	bool compassReady = false;
	uint8_t barometerAddress = 0;
};

static_assert(sizeof(MagRuntimeState) <= 64, "Magnetometer runtime RAM budget");
MagRuntimeState magRuntime;
#define magCalibration (magRuntime.calibration)
#define magMinimum (magRuntime.minimum)
#define magMaximum (magRuntime.maximum)
#define magCalibrationSamples (magRuntime.samples)
#define lastMagSampleMs (magRuntime.lastSampleMs)
#define magCalibrationActive (magRuntime.active)
#define qmcReady (magRuntime.compassReady)
#define bmpAddress (magRuntime.barometerAddress)

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
	if (!readRegisters(QMC5883P_ADDR, 0x09, &status, 1) || !(status & 0x01) || (status & 0x02)) return false;
	uint8_t raw[6];
	if (!readRegisters(QMC5883P_ADDR, 0x01, raw, sizeof(raw))) return false;
	x = readS16(raw);
	y = readS16(raw + 2);
	z = readS16(raw + 4);
	return true;
}

bool calculateMagCalibration(float offset[3], float scale[3]) {
	if (magCalibrationSamples < 300) return false;
	float halfRange[3];
	float meanHalfRange = 0.0f;
	for (int axis = 0; axis < 3; ++axis) {
		const int32_t span = (int32_t)magMaximum[axis] - (int32_t)magMinimum[axis];
		if (span < 500) return false; // Require rotation that excites every sensor axis.
		offset[axis] = ((float)magMaximum[axis] + (float)magMinimum[axis]) * 0.5f;
		halfRange[axis] = (float)span * 0.5f;
		meanHalfRange += halfRange[axis] / 3.0f;
	}
	if (!isfinite(meanHalfRange) || meanHalfRange < 250.0f) return false;
	for (int axis = 0; axis < 3; ++axis) {
		scale[axis] = meanHalfRange / halfRange[axis];
		if (!isfinite(scale[axis]) || scale[axis] < 0.33f || scale[axis] > 3.0f) return false;
	}
	return true;
}

bool calibratedMag(int16_t rawX, int16_t rawY, int16_t rawZ, float corrected[3]) {
	if (!magCalibration.valid) return false;
	const int16_t raw[3] = {rawX, rawY, rawZ};
	for (int axis = 0; axis < 3; ++axis) {
		corrected[axis] = ((float)raw[axis] - magCalibration.offset[axis]) * magCalibration.scale[axis];
	}
	return true;
}

float calculateHeadingDegrees(const float mag[3], float roll, float pitch) {
	// Rotate the calibrated magnetic vector into the level plane before atan2.
	const float horizontalX = mag[0] * cosf(pitch) + mag[2] * sinf(pitch);
	const float horizontalY = mag[0] * sinf(roll) * sinf(pitch) +
		mag[1] * cosf(roll) - mag[2] * sinf(roll) * cosf(pitch);
	float heading = atan2f(-horizontalY, horizontalX) * 57.2957795f + magCalibration.headingOffset;
	while (heading < 0.0f) heading += 360.0f;
	while (heading >= 360.0f) heading -= 360.0f;
	return heading;
}

void loadMagCalibration() {
	PersistedMagCalibration stored;
	if (storage.getBytesLength("MAG_CAL") != sizeof(stored) ||
		storage.getBytes("MAG_CAL", &stored, sizeof(stored)) != sizeof(stored) || stored.magic != MAG_CAL_MAGIC) return;
	for (int axis = 0; axis < 3; ++axis) {
		if (!isfinite(stored.offset[axis]) || !isfinite(stored.scale[axis]) ||
			stored.scale[axis] < 0.33f || stored.scale[axis] > 3.0f) return;
		magCalibration.offset[axis] = stored.offset[axis];
		magCalibration.scale[axis] = stored.scale[axis];
	}
	if (!isfinite(stored.headingOffset)) return;
	magCalibration.headingOffset = stored.headingOffset;
	magCalibration.valid = true;
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
	const bool bmpReady = beginBmp388(address);
	bmpAddress = bmpReady ? address : 0;
	qmcReady = beginQmc5883p();
	loadMagCalibration();
	Serial.printf("EXT_SENSOR bus=ready sda=%d scl=%d bmp388=%s addr=0x%02X qmc5883p=%s addr=0x%02X\n",
		BOARD_I2C_SDA, BOARD_I2C_SCL, bmpReady ? "ready" : "not_found", bmpAddress,
		qmcReady ? "ready" : "not_found", qmcReady ? QMC5883P_ADDR : 0);
}

void updateExternalSensors() {
	if (!magCalibrationActive) return;
	if (armed || motorTestActive || motorsActive()) {
		magCalibrationActive = false;
		Serial.println("MAG_CAL state=aborted reason=outputs_active");
		return;
	}
	const uint32_t now = millis();
	if ((uint32_t)(now - lastMagSampleMs) < MAG_SAMPLE_INTERVAL_MS) return;
	lastMagSampleMs = now;
	if (!qmcReady) return;
	int16_t x, y, z;
	if (!readQmc5883p(x, y, z)) return;
	const int16_t sample[3] = {x, y, z};
	for (int axis = 0; axis < 3; ++axis) {
		if (sample[axis] < magMinimum[axis]) magMinimum[axis] = sample[axis];
		if (sample[axis] > magMaximum[axis]) magMaximum[axis] = sample[axis];
	}
	++magCalibrationSamples;
}

bool startMagCalibration() {
	if (!qmcReady || armed || motorTestActive || motorsActive()) return false;
	for (int axis = 0; axis < 3; ++axis) {
		magMinimum[axis] = INT16_MAX;
		magMaximum[axis] = INT16_MIN;
	}
	magCalibrationSamples = 0;
	lastMagSampleMs = millis();
	magCalibrationActive = true;
	return true;
}

void stopMagCalibration() {
	magCalibrationActive = false;
}

void printMagCalibrationStatus() {
	float offsets[3], scales[3];
	const bool candidateValid = calculateMagCalibration(offsets, scales);
	Serial.printf("MAG_CAL state=%s samples=%lu saved=%u candidate=%s", magCalibrationActive ? "collecting" : "stopped",
		(unsigned long)magCalibrationSamples, magCalibration.valid ? 1 : 0, candidateValid ? "ready" : "insufficient_coverage");
	for (int axis = 0; axis < 3; ++axis) {
		Serial.printf(" min%d=%d max%d=%d", axis, magMinimum[axis], axis, magMaximum[axis]);
		if (candidateValid) Serial.printf(" off%d=%.1f scale%d=%.3f", axis, offsets[axis], axis, scales[axis]);
	}
	Serial.printf(" heading_offset_deg=%.2f\n", magCalibration.headingOffset);
}

bool saveMagCalibration() {
	if (magCalibrationActive || armed || motorTestActive || motorsActive()) return false;
	float offsets[3], scales[3];
	const bool haveNewCalibration = calculateMagCalibration(offsets, scales);
	if (!haveNewCalibration && !magCalibration.valid) return false;
	if (!beginPersistentWriteBatch()) return false;
	PersistedMagCalibration record = {};
	record.magic = MAG_CAL_MAGIC;
	if (haveNewCalibration) {
		for (int axis = 0; axis < 3; ++axis) {
			record.offset[axis] = offsets[axis];
			record.scale[axis] = scales[axis];
		}
	} else {
		memcpy(record.offset, magCalibration.offset, sizeof(record.offset));
		memcpy(record.scale, magCalibration.scale, sizeof(record.scale));
	}
	record.headingOffset = magCalibration.headingOffset;
	PersistedMagCalibration readBack = {};
	const bool wroteAny = storage.putBytes("MAG_CAL", &record, sizeof(record)) == sizeof(record);
	const bool ok = wroteAny && storage.getBytes("MAG_CAL", &readBack, sizeof(readBack)) == sizeof(readBack) &&
		memcmp(&record, &readBack, sizeof(record)) == 0;
	finishPersistentWriteBatch(wroteAny);
	if (!ok) return false;
	if (haveNewCalibration) {
		for (int axis = 0; axis < 3; ++axis) {
			magCalibration.offset[axis] = offsets[axis];
			magCalibration.scale[axis] = scales[axis];
		}
	}
	magCalibration.valid = true;
	return true;
}

bool resetMagCalibration() {
	if (armed || motorTestActive || motorsActive() || magCalibrationActive || !beginPersistentWriteBatch()) return false;
	const bool wasPresent = storage.isKey("MAG_CAL");
	const bool removed = !wasPresent || storage.remove("MAG_CAL");
	const bool wroteAny = wasPresent && removed;
	finishPersistentWriteBatch(wroteAny);
	if (!removed) return false;
	magCalibration = MagCalibration();
	return true;
}

bool alignMagHeading(float knownHeadingDegrees, float rollRadians, float pitchRadians) {
	if (!magCalibration.valid || !isfinite(knownHeadingDegrees) || knownHeadingDegrees < 0.0f || knownHeadingDegrees >= 360.0f ||
		!isfinite(rollRadians) || !isfinite(pitchRadians) || armed || motorTestActive || motorsActive()) return false;
	int16_t x, y, z;
	if (!readQmc5883p(x, y, z)) return false;
	float corrected[3];
	calibratedMag(x, y, z, corrected);
	const float current = calculateHeadingDegrees(corrected, rollRadians, pitchRadians);
	magCalibration.headingOffset += knownHeadingDegrees - current;
	while (magCalibration.headingOffset < -360.0f) magCalibration.headingOffset += 360.0f;
	while (magCalibration.headingOffset > 360.0f) magCalibration.headingOffset -= 360.0f;
	return true;
}

void printExternalSensorReadings(float rollRadians, float pitchRadians) {
	if (!qmcReady && !bmpAddress) { Serial.println("EXT_SENSOR error=unavailable"); return; }
	if (bmpAddress) {
		BarometerSample sample;
		if (readBmp388(bmpAddress, sample)) {
			Serial.printf("BARO bmp388 pressure_pa=%.2f temperature_c=%.2f altitude_m=%.2f\n",
				sample.pressurePa, sample.temperatureC, sample.altitudeMeters);
		} else {
			Serial.println("BARO bmp388 read=not_ready_or_invalid");
		}
	} else {
		Serial.println("BARO bmp388=not_found");
	}
	if (qmcReady) {
		int16_t x, y, z;
		if (readQmc5883p(x, y, z)) {
			if (magCalibration.valid) {
				float corrected[3];
				calibratedMag(x, y, z, corrected);
				Serial.printf("COMPASS qmc5883p raw_x=%d raw_y=%d raw_z=%d calibrated_x=%.1f calibrated_y=%.1f calibrated_z=%.1f magnetic_heading_deg=%.1f source=magnetometer_tilt_compensated\n",
					x, y, z, corrected[0], corrected[1], corrected[2], calculateHeadingDegrees(corrected, rollRadians, pitchRadians));
			} else {
				Serial.printf("COMPASS qmc5883p raw_x=%d raw_y=%d raw_z=%d heading=unavailable reason=magcal_required\n", x, y, z);
			}
		} else {
			Serial.println("COMPASS qmc5883p read=not_ready");
		}
	} else {
		Serial.println("COMPASS qmc5883p=not_found");
	}
}
