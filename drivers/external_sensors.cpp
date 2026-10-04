#include "external_sensors.h"

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <math.h>
#include "board_config.h"
#include "bmp388_compensation.h"
#include "calibration_sensor_policy.h"
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
constexpr uint32_t BARO_SAMPLE_INTERVAL_MS = 50;
constexpr uint32_t BARO_MAX_SAMPLE_AGE_US = 250000;
constexpr uint32_t BARO_BASELINE_SAMPLES = 25;
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

struct Bmp388RuntimeState {
	Bmp388Calibration calibration = {};
	BarometerEstimate estimate;
	bool calibrationReady = false;
};

static_assert(sizeof(MagRuntimeState) <= 64, "Magnetometer runtime RAM budget");
MagRuntimeState magRuntime;
Bmp388RuntimeState bmpRuntime;
portMUX_TYPE bmpSampleMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t externalSensorI2cMutex = nullptr;
#define magCalibration (magRuntime.calibration)
#define magMinimum (magRuntime.minimum)
#define magMaximum (magRuntime.maximum)
#define magCalibrationSamples (magRuntime.samples)
#define lastMagSampleMs (magRuntime.lastSampleMs)
#define magCalibrationActive (magRuntime.active)
#define qmcReady (magRuntime.compassReady)
#define bmpAddress (magRuntime.barometerAddress)

bool readRegisters(uint8_t address, uint8_t reg, uint8_t *data, size_t length) {
	if (externalSensorI2cMutex && xSemaphoreTake(externalSensorI2cMutex, pdMS_TO_TICKS(20)) != pdTRUE) return false;
	Wire.beginTransmission(address);
	Wire.write(reg);
	if (Wire.endTransmission(false) != 0) {
		if (externalSensorI2cMutex) xSemaphoreGive(externalSensorI2cMutex);
		return false;
	}
	const size_t received = Wire.requestFrom((int)address, (int)length, (int)true);
	if (received != length) {
		while (Wire.available()) Wire.read();
		if (externalSensorI2cMutex) xSemaphoreGive(externalSensorI2cMutex);
		return false;
	}
	for (size_t i = 0; i < length; ++i) data[i] = (uint8_t)Wire.read();
	if (externalSensorI2cMutex) xSemaphoreGive(externalSensorI2cMutex);
	return true;
}

bool writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
	if (externalSensorI2cMutex && xSemaphoreTake(externalSensorI2cMutex, pdMS_TO_TICKS(20)) != pdTRUE) return false;
	Wire.beginTransmission(address);
	Wire.write(reg);
	Wire.write(value);
	const bool ok = Wire.endTransmission() == 0;
	if (externalSensorI2cMutex) xSemaphoreGive(externalSensorI2cMutex);
	return ok;
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
	uint8_t calibration[21];
	if (!readRegisters(address, 0x31, calibration, sizeof(calibration))) return false;
	bmpRuntime.calibration = bmp388DecodeCalibration(calibration);
	bmpRuntime.calibrationReady = true;
	// Pressure x4, temperature x2, normal mode, 25 Hz output data rate.
	if (!writeRegister(address, 0x1C, 0x0A) || !writeRegister(address, 0x1D, 0x03) ||
		!writeRegister(address, 0x1F, 0x06) || // IIR coefficient 7
		!writeRegister(address, 0x1B, 0x33)) return false;
	delay(50);
	return true;
}

bool readBmp388(uint8_t address, BarometerSample &sample) {
	if (!bmpRuntime.calibrationReady) return false;
	uint8_t status = 0;
	if (!readRegisters(address, 0x03, &status, 1) || (status & 0x60) != 0x60) return false;
	uint8_t raw[6];
	if (!readRegisters(address, 0x04, raw, sizeof(raw))) return false;
	const uint32_t pressureRaw = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16);
	const uint32_t temperatureRaw = (uint32_t)raw[3] | ((uint32_t)raw[4] << 8) | ((uint32_t)raw[5] << 16);
	double pressure = 0.0;
	double temperature = 0.0;
	if (!bmp388Compensate(pressureRaw, temperatureRaw, bmpRuntime.calibration,
		pressure, temperature)) return false;
	sample.pressurePa = (float)pressure;
	sample.temperatureC = (float)temperature;
	sample.altitudeMeters = 44330.0f * (1.0f - powf(sample.pressurePa / 101325.0f, 0.19029495f));
	sample.timestampUs = micros();
	sample.valid = isfinite(sample.altitudeMeters);
	return sample.valid;
}

void externalSensorTask(void *) {
	TickType_t wake = xTaskGetTickCount();
	float filteredAltitude = NAN;
	float filteredVelocity = 0.0f;
	float baselineSum = 0.0f;
	float baselineAltitude = NAN;
	uint32_t baselineCount = 0;
	uint32_t lastSampleUs = 0;
	for (;;) {
		vTaskDelayUntil(&wake, pdMS_TO_TICKS(BARO_SAMPLE_INTERVAL_MS));
		if (!bmpAddress) continue;
		BarometerSample sample;
		if (!readBmp388(bmpAddress, sample)) {
			portENTER_CRITICAL(&bmpSampleMux);
			++bmpRuntime.estimate.failureCount;
			portEXIT_CRITICAL(&bmpSampleMux);
			continue;
		}
		if (!isfinite(filteredAltitude)) filteredAltitude = sample.altitudeMeters;
		const uint32_t elapsedUs = lastSampleUs ? sample.timestampUs - lastSampleUs : 0;
		lastSampleUs = sample.timestampUs;
		const float previousAltitude = filteredAltitude;
		filteredAltitude += 0.20f * (sample.altitudeMeters - filteredAltitude);
		if (elapsedUs >= 20000 && elapsedUs <= 100000) {
			const float instantaneousVelocity = (filteredAltitude - previousAltitude) /
				((float)elapsedUs * 1e-6f);
			filteredVelocity += 0.15f * (instantaneousVelocity - filteredVelocity);
		}
		if (baselineCount < BARO_BASELINE_SAMPLES) {
			baselineSum += filteredAltitude;
			++baselineCount;
			if (baselineCount == BARO_BASELINE_SAMPLES)
				baselineAltitude = baselineSum / (float)BARO_BASELINE_SAMPLES;
		}
		// Track the launch surface while disarmed, then freeze the reference at
		// arming so carrying the aircraft after boot cannot create a false height.
		if (isfinite(baselineAltitude) && !__atomic_load_n(&armed, __ATOMIC_RELAXED))
			baselineAltitude = filteredAltitude;
		BarometerEstimate next;
		portENTER_CRITICAL(&bmpSampleMux);
		next = bmpRuntime.estimate;
		portEXIT_CRITICAL(&bmpSampleMux);
		next.sample = sample;
		next.relativeAltitudeMeters = isfinite(baselineAltitude) ?
			filteredAltitude - baselineAltitude : 0.0f;
		next.verticalSpeedMps = filteredVelocity;
		++next.sampleCount;
		next.valid = isfinite(baselineAltitude) && next.sampleCount >= BARO_BASELINE_SAMPLES;
		portENTER_CRITICAL(&bmpSampleMux);
		bmpRuntime.estimate = next;
		portEXIT_CRITICAL(&bmpSampleMux);
	}
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
#if !BOARD_BAROMETER_ENABLED && !BOARD_COMPASS_ENABLED
	Serial.println("EXT_SENSOR barometer=disabled compass=disabled board_config=1");
	return;
#endif
#if BOARD_I2C_SDA >= 0 && BOARD_I2C_SCL >= 0
	externalSensorI2cMutex = xSemaphoreCreateMutex();
	const bool i2cReady = Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, 100000);
#else
	const bool i2cReady = false;
#endif
	if (!i2cReady) {
		Serial.println("EXT_SENSOR bus=unavailable");
		return;
	}
	uint8_t address = 0;
#if BOARD_BAROMETER_ENABLED
	const bool bmpReady = beginBmp388(address);
	bmpAddress = bmpReady ? address : 0;
#else
	const bool bmpReady = false;
	bmpAddress = 0;
#endif
#if BOARD_COMPASS_ENABLED
	qmcReady = beginQmc5883p();
	loadMagCalibration();
#else
	qmcReady = false;
#endif
	Serial.printf("EXT_SENSOR bus=ready sda=%d scl=%d bmp388=%s addr=0x%02X qmc5883p=%s addr=0x%02X\n",
		BOARD_I2C_SDA, BOARD_I2C_SCL, bmpReady ? "ready" : "not_found", bmpAddress,
		qmcReady ? "ready" : "not_found", qmcReady ? QMC5883P_ADDR : 0);
	if (bmpReady && xTaskCreatePinnedToCore(externalSensorTask, "barometer", 3072,
		nullptr, 1, nullptr, 0) != pdPASS) {
		bmpAddress = 0;
		Serial.println("BARO bmp388 task=start_failed");
	}
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
	const CalibrationSensorAvailability sensors = {true, qmcReady};
	if (!magneticHeadingCalibrationSensorsReady(sensors) ||
		armed || motorTestActive || motorsActive()) return false;
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
	Serial.printf("MAG_CAL available=%u state=%s samples=%lu saved=%u candidate=%s", qmcReady ? 1U : 0U,
		magCalibrationActive ? "collecting" : "stopped",
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
	const CalibrationSensorAvailability sensors = {true, qmcReady};
	if (!magneticHeadingCalibrationSensorsReady(sensors) || !magCalibration.valid ||
		!isfinite(knownHeadingDegrees) || knownHeadingDegrees < 0.0f || knownHeadingDegrees >= 360.0f ||
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
	if (bmpAddress) {
		BarometerEstimate estimate;
		if (getBarometerEstimate(estimate) && estimate.sample.valid) {
			const uint32_t ageMs = (micros() - estimate.sample.timestampUs) / 1000;
			Serial.printf("BARO bmp388 pressure_pa=%.2f temperature_c=%.2f altitude_m=%.2f relative_altitude_m=%.2f vertical_speed_mps=%.2f age_ms=%lu samples=%lu failures=%lu ready=%u\n",
				estimate.sample.pressurePa, estimate.sample.temperatureC, estimate.sample.altitudeMeters,
				estimate.relativeAltitudeMeters, estimate.verticalSpeedMps, (unsigned long)ageMs,
				(unsigned long)estimate.sampleCount, (unsigned long)estimate.failureCount,
				barometerEstimateUsable(estimate, micros(), BARO_MAX_SAMPLE_AGE_US) ? 1U : 0U);
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

bool compassAvailable() {
	return qmcReady;
}

bool getBarometerEstimate(BarometerEstimate &estimate) {
	portENTER_CRITICAL(&bmpSampleMux);
	estimate = bmpRuntime.estimate;
	portEXIT_CRITICAL(&bmpSampleMux);
	return estimate.sampleCount > 0;
}
