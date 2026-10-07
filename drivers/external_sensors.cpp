#include "external_sensors.h"

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <math.h>
#include "board_config.h"
#include "bmp388_compensation.h"
#include "calibration_sensor_policy.h"
#include "flight_sensor_interfaces.h"
#include "navigation_heading.h"
#include "quaternion.h"
#include "system_log.h"

extern Preferences storage;
extern bool armed;
extern bool motorTestActive;
extern Quaternion attitude;
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
constexpr uint32_t MAG_CAL_MAGIC_V2 = 0x4D414732; // MAG2
constexpr uint32_t MAG_MAX_SAMPLE_AGE_US = 100000;
constexpr float MAG_FIELD_NORM_TOLERANCE = 0.20f;
constexpr float MAG_MAX_TILT_RADIANS = 1.04719755f; // 60 degrees
constexpr uint32_t MAG_CAL_MIN_SAMPLES = 300;
constexpr uint32_t MAG_CAL_TARGET_SAMPLES = 600;
constexpr int32_t MAG_CAL_MIN_AXIS_SPAN = 500;

struct MagCalibration {
	float offset[3] = {};
	float scale[3] = {1.0f, 1.0f, 1.0f};
	float headingOffset = 0.0f;
	float fieldNorm = 0.0f;
	float quality = 0.0f;
	bool valid = false;
};

struct PersistedMagCalibration {
	uint32_t magic;
	float offset[3];
	float scale[3];
	float headingOffset;
};

struct PersistedMagCalibrationV2 {
	uint32_t magic;
	uint16_t version;
	uint16_t reserved;
	float offset[3];
	float scale[3];
	float headingOffset;
	float fieldNorm;
	float quality;
};

struct MagRuntimeState {
	MagCalibration calibration;
	int16_t minimum[3] = {INT16_MAX, INT16_MAX, INT16_MAX};
	int16_t maximum[3] = {INT16_MIN, INT16_MIN, INT16_MIN};
	uint32_t samples = 0;
	uint32_t lastSampleMs = 0;
	uint32_t calibrationStartedMs = 0;
	uint32_t calibrationStoppedMs = 0;
	bool active = false;
	bool calibrationAttemptStarted = false;
	bool compassReady = false;
	uint8_t barometerAddress = 0;
	uint8_t consecutiveSuccesses = 0;
	uint8_t consecutiveFailures = 0;
	uint16_t baselineSamples = 0;
	float baselineMean = 0.0f;
	float baselineM2 = 0.0f;
};

struct Bmp388RuntimeState {
	Bmp388Calibration calibration = {};
	BarometerEstimate estimate;
	bool calibrationReady = false;
};

static_assert(sizeof(MagRuntimeState) <= 96, "Magnetometer runtime RAM budget");
MagRuntimeState magRuntime;
Bmp388RuntimeState bmpRuntime;
portMUX_TYPE bmpSampleMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE magSampleMux = portMUX_INITIALIZER_UNLOCKED;
MagnetometerEstimate magEstimate;
NavigationHeadingEstimator headingEstimator;
uint32_t lastHeadingSequence = 0;
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

bool readQmc5883p(int16_t &x, int16_t &y, int16_t &z);
bool calibratedMag(int16_t rawX, int16_t rawY, int16_t rawZ, float corrected[3]);

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
	uint32_t lastBarometerMs = 0;
	for (;;) {
		vTaskDelayUntil(&wake, pdMS_TO_TICKS(MAG_SAMPLE_INTERVAL_MS));
		pollDownwardRangeSensor();
		if (qmcReady) {
			int16_t x = 0, y = 0, z = 0;
			if (readQmc5883p(x, y, z)) {
				MagnetometerEstimate next;
				portENTER_CRITICAL(&magSampleMux);
				next = magEstimate;
				portEXIT_CRITICAL(&magSampleMux);
				next.raw[0] = x; next.raw[1] = y; next.raw[2] = z;
				next.timestampUs = micros();
				next.detected = true;
				next.calibrated = magCalibration.valid;
				if (magCalibration.valid) {
					calibratedMag(x, y, z, next.corrected);
					next.fieldNorm = sqrtf(next.corrected[0] * next.corrected[0] +
						next.corrected[1] * next.corrected[1] + next.corrected[2] * next.corrected[2]);
					if (magCalibration.fieldNorm < 250.0f && !armed && !motorTestActive &&
						isfinite(next.fieldNorm) && next.fieldNorm >= 250.0f && magRuntime.baselineSamples < 100) {
						++magRuntime.baselineSamples;
						const float delta = next.fieldNorm - magRuntime.baselineMean;
						magRuntime.baselineMean += delta / (float)magRuntime.baselineSamples;
						magRuntime.baselineM2 += delta * (next.fieldNorm - magRuntime.baselineMean);
						if (magRuntime.baselineSamples == 100) {
							const float variance = magRuntime.baselineM2 / 99.0f;
							if (variance >= 0.0f && sqrtf(variance) <= magRuntime.baselineMean * 0.05f) {
								magCalibration.fieldNorm = magRuntime.baselineMean;
								magCalibration.quality = 0.5f; // Runtime migration of a valid MAG1 record.
							}
						}
					}
					next.fieldNormReference = magCalibration.fieldNorm;
				}
				++next.sequence;
				++next.sampleCount;
				if (magRuntime.consecutiveSuccesses < UINT8_MAX) ++magRuntime.consecutiveSuccesses;
				magRuntime.consecutiveFailures = 0;
				next.ready = magRuntime.consecutiveSuccesses >= 3;
				portENTER_CRITICAL(&magSampleMux);
				magEstimate = next;
				if (magCalibrationActive && !armed && !motorTestActive) {
					const int16_t sample[3] = {x, y, z};
					for (int axis = 0; axis < 3; ++axis) {
						if (sample[axis] < magMinimum[axis]) magMinimum[axis] = sample[axis];
						if (sample[axis] > magMaximum[axis]) magMaximum[axis] = sample[axis];
					}
					++magCalibrationSamples;
					lastMagSampleMs = millis();
				}
				portEXIT_CRITICAL(&magSampleMux);
			} else {
				if (magRuntime.consecutiveFailures < UINT8_MAX) ++magRuntime.consecutiveFailures;
				if (magRuntime.consecutiveFailures >= 3) magRuntime.consecutiveSuccesses = 0;
				portENTER_CRITICAL(&magSampleMux);
				++magEstimate.failureCount;
				if (magRuntime.consecutiveFailures >= 3) {
					magEstimate.ready = false;
					magEstimate.trusted = false;
				}
				magEstimate.rejectReasons |= MAG_REJECT_BUS;
				portEXIT_CRITICAL(&magSampleMux);
			}
		}
		const uint32_t nowMs = millis();
		if ((uint32_t)(nowMs - lastBarometerMs) < BARO_SAMPLE_INTERVAL_MS) continue;
		lastBarometerMs = nowMs;
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

bool calculateMagCalibration(float offset[3], float scale[3], float &fieldNorm, float &quality) {
	int16_t minimum[3], maximum[3];
	uint32_t sampleCount = 0;
	portENTER_CRITICAL(&magSampleMux);
	memcpy(minimum, magMinimum, sizeof(minimum));
	memcpy(maximum, magMaximum, sizeof(maximum));
	sampleCount = magCalibrationSamples;
	portEXIT_CRITICAL(&magSampleMux);
	if (sampleCount < MAG_CAL_MIN_SAMPLES) return false;
	float halfRange[3];
	float meanHalfRange = 0.0f;
	float minimumHalfRange = INFINITY;
	float maximumHalfRange = 0.0f;
	for (int axis = 0; axis < 3; ++axis) {
		const int32_t span = (int32_t)maximum[axis] - (int32_t)minimum[axis];
		if (span < MAG_CAL_MIN_AXIS_SPAN) return false; // Require rotation that excites every sensor axis.
		offset[axis] = ((float)maximum[axis] + (float)minimum[axis]) * 0.5f;
		halfRange[axis] = (float)span * 0.5f;
		meanHalfRange += halfRange[axis] / 3.0f;
		if (halfRange[axis] < minimumHalfRange) minimumHalfRange = halfRange[axis];
		if (halfRange[axis] > maximumHalfRange) maximumHalfRange = halfRange[axis];
	}
	if (!isfinite(meanHalfRange) || meanHalfRange < 250.0f) return false;
	for (int axis = 0; axis < 3; ++axis) {
		scale[axis] = meanHalfRange / halfRange[axis];
		if (!isfinite(scale[axis]) || scale[axis] < 0.33f || scale[axis] > 3.0f) return false;
	}
	fieldNorm = meanHalfRange;
	quality = maximumHalfRange > 0.0f ? minimumHalfRange / maximumHalfRange : 0.0f;
	if (sampleCount < MAG_CAL_TARGET_SAMPLES) quality *= (float)sampleCount / (float)MAG_CAL_TARGET_SAMPLES;
	return true;
}

bool calibratedMag(int16_t rawX, int16_t rawY, int16_t rawZ, float corrected[3]) {
	MagCalibration calibration;
	portENTER_CRITICAL(&magSampleMux);
	calibration = magCalibration;
	portEXIT_CRITICAL(&magSampleMux);
	if (!calibration.valid) return false;
	const int16_t raw[3] = {rawX, rawY, rawZ};
	for (int axis = 0; axis < 3; ++axis) {
		corrected[axis] = ((float)raw[axis] - calibration.offset[axis]) * calibration.scale[axis];
	}
	return true;
}

float calculateHeadingDegrees(const float mag[3], float roll, float pitch) {
	// Rotate the calibrated magnetic vector into the level plane before atan2.
	const float horizontalX = mag[0] * cosf(pitch) + mag[2] * sinf(pitch);
	const float horizontalY = mag[0] * sinf(roll) * sinf(pitch) +
		mag[1] * cosf(roll) - mag[2] * sinf(roll) * cosf(pitch);
	float headingOffset = 0.0f;
	portENTER_CRITICAL(&magSampleMux);
	headingOffset = magCalibration.headingOffset;
	portEXIT_CRITICAL(&magSampleMux);
	float heading = atan2f(-horizontalY, horizontalX) * 57.2957795f + headingOffset;
	while (heading < 0.0f) heading += 360.0f;
	while (heading >= 360.0f) heading -= 360.0f;
	return heading;
}

void loadMagCalibration() {
	PersistedMagCalibrationV2 storedV2 = {};
	PersistedMagCalibration storedV1 = {};
	const size_t length = storage.getBytesLength("MAG_CAL");
	bool v2 = length == sizeof(storedV2) &&
		storage.getBytes("MAG_CAL", &storedV2, sizeof(storedV2)) == sizeof(storedV2) &&
		storedV2.magic == MAG_CAL_MAGIC_V2 && storedV2.version == 2;
	if (!v2 && (length != sizeof(storedV1) ||
		storage.getBytes("MAG_CAL", &storedV1, sizeof(storedV1)) != sizeof(storedV1) ||
		storedV1.magic != MAG_CAL_MAGIC)) return;
	for (int axis = 0; axis < 3; ++axis) {
		const float offset = v2 ? storedV2.offset[axis] : storedV1.offset[axis];
		const float scale = v2 ? storedV2.scale[axis] : storedV1.scale[axis];
		if (!isfinite(offset) || !isfinite(scale) || scale < 0.33f || scale > 3.0f) return;
		magCalibration.offset[axis] = offset;
		magCalibration.scale[axis] = scale;
	}
	const float headingOffset = v2 ? storedV2.headingOffset : storedV1.headingOffset;
	if (!isfinite(headingOffset)) return;
	magCalibration.headingOffset = headingOffset;
	if (v2 && isfinite(storedV2.fieldNorm) && storedV2.fieldNorm >= 250.0f &&
		isfinite(storedV2.quality) && storedV2.quality >= 0.0f && storedV2.quality <= 1.0f) {
		magCalibration.fieldNorm = storedV2.fieldNorm;
		magCalibration.quality = storedV2.quality;
	}
	magCalibration.valid = true;
}
} // namespace

void setupExternalSensors() {
#if !BOARD_BAROMETER_ENABLED && !BOARD_COMPASS_ENABLED && \
    !BOARD_OPTICAL_FLOW_ENABLED && !BOARD_DOWNWARD_RANGE_ENABLED
    Serial.println("EXT_SENSOR all_optional_sensors=disabled board_config=1");
	return;
#endif
#if BOARD_I2C_SDA >= 0 && BOARD_I2C_SCL >= 0
	externalSensorI2cMutex = xSemaphoreCreateMutex();
	const bool i2cReady = Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, 100000);
	Wire.setTimeOut(20);
#else
	const bool i2cReady = false;
#endif
	// Initialize optional devices before background I2C sampling starts. PMW3901
	// remains on the control-loop task so it never races the IMU on shared SPI.
	setupSupplementarySensors(i2cReady, externalSensorI2cMutex);
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
	portENTER_CRITICAL(&magSampleMux);
	magEstimate.detected = qmcReady;
	magEstimate.calibrated = magCalibration.valid;
	magEstimate.rejectReasons = qmcReady ? (magCalibration.valid ? MAG_REJECT_NOT_READY : MAG_REJECT_NOT_CALIBRATED) :
		MAG_REJECT_NOT_DETECTED;
	portEXIT_CRITICAL(&magSampleMux);
	Serial.printf("EXT_SENSOR bus=ready sda=%d scl=%d bmp388=%s addr=0x%02X qmc5883p=%s addr=0x%02X\n",
		BOARD_I2C_SDA, BOARD_I2C_SCL, bmpReady ? "ready" : "not_found", bmpAddress,
		qmcReady ? "ready" : "not_found", qmcReady ? QMC5883P_ADDR : 0);
	if ((bmpReady || downwardRangeAvailable() || qmcReady) && xTaskCreatePinnedToCore(externalSensorTask, "ext-i2c", 3584,
		nullptr, 1, nullptr, 0) != pdPASS) {
		bmpAddress = 0;
		qmcReady = false;
		markDownwardRangeUnavailable();
		Serial.println("EXT_SENSOR task=start_failed");
	}
}

void updateExternalSensors() {
	updateSupplementarySensors();
	if (armed || motorTestActive || motorsActive()) {
		if (magCalibrationActive) {
			portENTER_CRITICAL(&magSampleMux);
			magCalibrationActive = false;
			magRuntime.calibrationStoppedMs = millis();
			portEXIT_CRITICAL(&magSampleMux);
			Serial.println("MAG_CAL state=aborted reason=outputs_active");
		}
	}
	MagnetometerEstimate estimate;
	portENTER_CRITICAL(&magSampleMux);
	estimate = magEstimate;
	portEXIT_CRITICAL(&magSampleMux);
	const uint32_t nowUs = micros();
	const float roll = attitude.getRoll();
	const float pitch = attitude.getPitch();
	const float gyroYaw = attitude.getYaw();
	headingEstimator.predict(gyroYaw, nowUs);
	uint16_t reasons = MAG_REJECT_NONE;
	if (!estimate.detected) reasons |= MAG_REJECT_NOT_DETECTED;
	if (!estimate.ready) reasons |= MAG_REJECT_NOT_READY;
	if (!estimate.calibrated) reasons |= MAG_REJECT_NOT_CALIBRATED;
	const bool fresh = estimate.timestampUs != 0 &&
		(uint32_t)(nowUs - estimate.timestampUs) <= MAG_MAX_SAMPLE_AGE_US;
	if (!fresh) reasons |= MAG_REJECT_STALE;
	if (estimate.failureCount && !estimate.ready) reasons |= MAG_REJECT_BUS;
	const bool normReady = estimate.fieldNormReference >= 250.0f && isfinite(estimate.fieldNorm);
	const bool normValid = normReady && fabsf(estimate.fieldNorm - estimate.fieldNormReference) <=
		estimate.fieldNormReference * MAG_FIELD_NORM_TOLERANCE;
	if (!normValid) reasons |= MAG_REJECT_FIELD_NORM;
	const bool tiltValid = isfinite(roll) && isfinite(pitch) && fabsf(roll) <= MAG_MAX_TILT_RADIANS &&
		fabsf(pitch) <= MAG_MAX_TILT_RADIANS;
	if (!tiltValid) reasons |= MAG_REJECT_TILT;
	const bool baseTrusted = reasons == MAG_REJECT_NONE;
	if (estimate.sequence != lastHeadingSequence && estimate.calibrated) {
		estimate.magneticHeadingRadians = radians(calculateHeadingDegrees(estimate.corrected, roll, pitch));
		const bool accepted = headingEstimator.observe(estimate.magneticHeadingRadians, baseTrusted,
			estimate.timestampUs, !armed);
		if (!accepted && baseTrusted) reasons |= MAG_REJECT_INNOVATION;
		lastHeadingSequence = estimate.sequence;
	}
	headingEstimator.predict(gyroYaw, nowUs);
	const NavigationHeadingState &heading = headingEstimator.state();
	estimate.navigationHeadingRadians = heading.fusedYawRadians;
	estimate.innovationRadians = heading.innovationRadians;
	estimate.fresh = fresh;
	estimate.trusted = baseTrusted && heading.trusted;
	if (baseTrusted && !heading.trusted) reasons |= MAG_REJECT_INNOVATION;
	estimate.rejectReasons = reasons;
	portENTER_CRITICAL(&magSampleMux);
	// Preserve a newer raw sample that may have arrived while fusion was running.
	if (magEstimate.sequence == estimate.sequence) magEstimate = estimate;
	else {
		magEstimate.navigationHeadingRadians = estimate.navigationHeadingRadians;
		magEstimate.innovationRadians = estimate.innovationRadians;
		magEstimate.trusted = false;
	}
	portEXIT_CRITICAL(&magSampleMux);
	static bool healthInitialized = false;
	static bool previousTrusted = false;
	if (!healthInitialized || previousTrusted != estimate.trusted) {
		char details[64];
		snprintf(details, sizeof(details), "trusted=%u reasons=0x%04X age_ms=%lu",
			estimate.trusted ? 1U : 0U, (unsigned)estimate.rejectReasons,
			(unsigned long)(estimate.timestampUs ? (uint32_t)(nowUs - estimate.timestampUs) / 1000U : UINT32_MAX));
		recordSystemLogEvent("MAG_HEALTH", details);
		previousTrusted = estimate.trusted;
		healthInitialized = true;
	}
}

bool startMagCalibration() {
	const CalibrationSensorAvailability sensors = {true, qmcReady};
	if (!magneticHeadingCalibrationSensorsReady(sensors) ||
		armed || motorTestActive || motorsActive() || magCalibrationActive) return false;
	portENTER_CRITICAL(&magSampleMux);
	for (int axis = 0; axis < 3; ++axis) {
		magMinimum[axis] = INT16_MAX;
		magMaximum[axis] = INT16_MIN;
	}
	magCalibrationSamples = 0;
	magRuntime.calibrationStartedMs = millis();
	magRuntime.calibrationStoppedMs = 0;
	lastMagSampleMs = magRuntime.calibrationStartedMs;
	magRuntime.calibrationAttemptStarted = true;
	magCalibrationActive = true;
	portEXIT_CRITICAL(&magSampleMux);
	return true;
}

void stopMagCalibration() {
	portENTER_CRITICAL(&magSampleMux);
	magCalibrationActive = false;
	magRuntime.calibrationStoppedMs = millis();
	portEXIT_CRITICAL(&magSampleMux);
}

bool getMagCalibrationStatus(MagnetometerCalibrationStatus &status) {
	status = MagnetometerCalibrationStatus();
	uint32_t startedMs = 0, stoppedMs = 0, lastSampleMs = 0;
	portENTER_CRITICAL(&magSampleMux);
	status.available = qmcReady;
	status.collecting = magCalibrationActive;
	status.saved = magCalibration.valid;
	status.attemptStarted = magRuntime.calibrationAttemptStarted;
	status.samples = magCalibrationSamples;
	startedMs = magRuntime.calibrationStartedMs;
	stoppedMs = magRuntime.calibrationStoppedMs;
	lastSampleMs = lastMagSampleMs;
	for (int axis = 0; axis < 3; ++axis) {
		status.minimum[axis] = magMinimum[axis];
		status.maximum[axis] = magMaximum[axis];
	}
	portEXIT_CRITICAL(&magSampleMux);

	const uint32_t nowMs = millis();
	if (status.attemptStarted && startedMs) {
		const uint32_t endedMs = status.collecting || !stoppedMs ? nowMs : stoppedMs;
		status.elapsedMs = endedMs - startedMs;
	}
	status.lastSampleAgeMs = status.samples && lastSampleMs ? nowMs - lastSampleMs : UINT32_MAX;
	status.sampleProgressPct = status.samples >= MAG_CAL_MIN_SAMPLES ? 100U :
		(uint8_t)(status.samples * 100UL / MAG_CAL_MIN_SAMPLES);
	status.overallProgressPct = status.sampleProgressPct;
	for (int axis = 0; axis < 3; ++axis) {
		const int32_t rawSpan = status.maximum[axis] >= status.minimum[axis] ?
			status.maximum[axis] - status.minimum[axis] : 0;
		status.span[axis] = (uint16_t)min((int32_t)UINT16_MAX, rawSpan);
		status.axisProgressPct[axis] = (uint8_t)min(100L,
			(long)(rawSpan * 100L / MAG_CAL_MIN_AXIS_SPAN));
		if (status.axisProgressPct[axis] < status.overallProgressPct)
			status.overallProgressPct = status.axisProgressPct[axis];
	}
	float offsets[3], scales[3];
	status.candidateReady = calculateMagCalibration(offsets, scales, status.fieldNorm, status.quality);
	if (status.candidateReady) status.overallProgressPct = 100;
	return status.available;
}

void printMagCalibrationStatus() {
	MagnetometerCalibrationStatus status;
	getMagCalibrationStatus(status);
	float offsets[3], scales[3], fieldNorm = 0.0f, quality = 0.0f;
	const bool candidateValid = calculateMagCalibration(offsets, scales, fieldNorm, quality);
	Serial.printf("MAG_CAL available=%u state=%s samples=%lu elapsed_ms=%lu progress=%u sample_progress=%u axis_progress=%u,%u,%u saved=%u candidate=%s",
		status.available ? 1U : 0U, status.collecting ? "collecting" : "stopped",
		(unsigned long)status.samples, (unsigned long)status.elapsedMs,
		(unsigned)status.overallProgressPct, (unsigned)status.sampleProgressPct,
		(unsigned)status.axisProgressPct[0], (unsigned)status.axisProgressPct[1],
		(unsigned)status.axisProgressPct[2], status.saved ? 1U : 0U,
		candidateValid ? "ready" : "insufficient_coverage");
	for (int axis = 0; axis < 3; ++axis) {
		Serial.printf(" min%d=%ld max%d=%ld span%d=%u", axis, (long)status.minimum[axis],
			axis, (long)status.maximum[axis], axis, (unsigned)status.span[axis]);
		if (candidateValid) Serial.printf(" off%d=%.1f scale%d=%.3f", axis, offsets[axis], axis, scales[axis]);
	}
	Serial.printf(" heading_offset_deg=%.2f field_norm=%.1f quality=%.3f\n",
		magCalibration.headingOffset, candidateValid ? fieldNorm : magCalibration.fieldNorm,
		candidateValid ? quality : magCalibration.quality);
}

bool saveMagCalibration() {
	if (magCalibrationActive || armed || motorTestActive || motorsActive()) return false;
	float offsets[3], scales[3], fieldNorm = 0.0f, quality = 0.0f;
	const bool haveNewCalibration = calculateMagCalibration(offsets, scales, fieldNorm, quality);
	MagCalibration current;
	portENTER_CRITICAL(&magSampleMux);
	current = magCalibration;
	const bool attemptedCollection = magRuntime.calibrationAttemptStarted;
	portEXIT_CRITICAL(&magSampleMux);
	if (attemptedCollection && !haveNewCalibration) return false;
	if (!haveNewCalibration && !current.valid) return false;
	PersistedMagCalibrationV2 record = {};
	record.magic = MAG_CAL_MAGIC_V2;
	record.version = 2;
	if (haveNewCalibration) {
		for (int axis = 0; axis < 3; ++axis) {
			record.offset[axis] = offsets[axis];
			record.scale[axis] = scales[axis];
		}
	} else {
		memcpy(record.offset, current.offset, sizeof(record.offset));
		memcpy(record.scale, current.scale, sizeof(record.scale));
	}
	record.headingOffset = current.headingOffset;
	record.fieldNorm = haveNewCalibration ? fieldNorm : current.fieldNorm;
	record.quality = haveNewCalibration ? quality : current.quality;
	if (!beginPersistentWriteBatch()) return false;
	PersistedMagCalibrationV2 readBack = {};
	const bool wroteAny = storage.putBytes("MAG_CAL", &record, sizeof(record)) == sizeof(record);
	const bool ok = wroteAny && storage.getBytes("MAG_CAL", &readBack, sizeof(readBack)) == sizeof(readBack) &&
		memcmp(&record, &readBack, sizeof(record)) == 0;
	finishPersistentWriteBatch(wroteAny);
	if (!ok) return false;
	MagCalibration published = current;
	if (haveNewCalibration) {
		memcpy(published.offset, offsets, sizeof(published.offset));
		memcpy(published.scale, scales, sizeof(published.scale));
		published.fieldNorm = fieldNorm;
		published.quality = quality;
	}
	published.valid = true;
	portENTER_CRITICAL(&magSampleMux);
	magCalibration = published;
	magEstimate.calibrated = true;
	magEstimate.fieldNormReference = published.fieldNorm;
	portEXIT_CRITICAL(&magSampleMux);
	return true;
}

bool resetMagCalibration() {
	if (armed || motorTestActive || motorsActive() || magCalibrationActive || !beginPersistentWriteBatch()) return false;
	const bool wasPresent = storage.isKey("MAG_CAL");
	const bool removed = !wasPresent || storage.remove("MAG_CAL");
	const bool wroteAny = wasPresent && removed;
	finishPersistentWriteBatch(wroteAny);
	if (!removed) return false;
	portENTER_CRITICAL(&magSampleMux);
	magCalibration = MagCalibration();
	magRuntime.calibrationAttemptStarted = false;
	magRuntime.calibrationStartedMs = 0;
	magRuntime.calibrationStoppedMs = 0;
	magCalibrationSamples = 0;
	for (int axis = 0; axis < 3; ++axis) {
		magMinimum[axis] = INT16_MAX;
		magMaximum[axis] = INT16_MIN;
	}
	headingEstimator.reset();
	magEstimate.calibrated = false;
	magEstimate.trusted = false;
	portEXIT_CRITICAL(&magSampleMux);
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
	portENTER_CRITICAL(&magSampleMux);
	magCalibration.headingOffset += knownHeadingDegrees - current;
	while (magCalibration.headingOffset < -360.0f) magCalibration.headingOffset += 360.0f;
	while (magCalibration.headingOffset > 360.0f) magCalibration.headingOffset -= 360.0f;
	portEXIT_CRITICAL(&magSampleMux);
	return true;
}

void printExternalSensorReadings(float rollRadians, float pitchRadians) {
	const uint32_t nowUs = micros();
	OpticalFlowSample flow;
	DownwardRangeSample range;
	uint32_t flowSamples = 0, flowFailures = 0, rangeSamples = 0, rangeFailures = 0;
	const bool haveFlow = getOpticalFlowSample(flow, flowSamples, flowFailures);
	const bool haveRange = getDownwardRangeSample(range, rangeSamples, rangeFailures);
	Serial.printf("EXT_CAPABILITY optical_flow_detected=%u optical_flow_ready=%u downward_range_detected=%u downward_range_ready=%u\n",
		opticalFlowDetected(), opticalFlowAvailable(), downwardRangeDetected(), downwardRangeAvailable());
	if (haveFlow) {
		Serial.printf("FLOW pmw3901 dx=%d dy=%d delta_x_rad=%.6f delta_y_rad=%.6f quality=%u motion=%u valid=%u age_ms=%lu samples=%lu failures=%lu\n",
			flow.deltaX, flow.deltaY, flow.deltaXAngularRadians, flow.deltaYAngularRadians,
			flow.quality, flow.motionDetected, flow.valid,
			(unsigned long)((uint32_t)(nowUs - flow.timestampUs) / 1000U),
			(unsigned long)flowSamples, (unsigned long)flowFailures);
	} else {
		Serial.printf("FLOW pmw3901 read=%s\n", opticalFlowAvailable() ? "waiting_for_sample" : "unavailable");
	}
	if (haveRange) {
		Serial.printf("RANGE vl53l1x distance_m=%.3f status=%u raw_status=%u quality=%u valid=%u age_ms=%lu samples=%lu failures=%lu\n",
			range.distanceMeters, range.rangeStatus, range.rawRangeStatus, range.quality, range.valid,
			(unsigned long)((uint32_t)(nowUs - range.timestampUs) / 1000U),
			(unsigned long)rangeSamples, (unsigned long)rangeFailures);
	} else {
		Serial.printf("RANGE vl53l1x read=%s\n", downwardRangeAvailable() ? "waiting_for_sample" : "unavailable");
	}
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
	MagnetometerEstimate magnetic;
	if (getMagnetometerEstimate(magnetic)) {
		Serial.printf("COMPASS qmc5883p detected=%u ready=%u calibrated=%u fresh=%u trusted=%u raw_x=%d raw_y=%d raw_z=%d calibrated_x=%.1f calibrated_y=%.1f calibrated_z=%.1f field_norm=%.1f field_reference=%.1f magnetic_heading_deg=%.1f navigation_heading_deg=%.1f innovation_deg=%.1f age_ms=%lu samples=%lu failures=%lu reject=0x%04X\n",
			magnetic.detected, magnetic.ready, magnetic.calibrated, magnetic.fresh, magnetic.trusted,
			magnetic.raw[0], magnetic.raw[1], magnetic.raw[2], magnetic.corrected[0], magnetic.corrected[1],
			magnetic.corrected[2], magnetic.fieldNorm, magnetic.fieldNormReference,
			degrees(magnetic.magneticHeadingRadians), degrees(magnetic.navigationHeadingRadians),
			degrees(magnetic.innovationRadians),
			(unsigned long)((uint32_t)(nowUs - magnetic.timestampUs) / 1000U),
			(unsigned long)magnetic.sampleCount, (unsigned long)magnetic.failureCount,
			(unsigned)magnetic.rejectReasons);
	} else {
		Serial.println("COMPASS qmc5883p=not_found");
	}
}

bool compassAvailable() {
	return qmcReady;
}

bool getMagnetometerEstimate(MagnetometerEstimate &estimate) {
	portENTER_CRITICAL(&magSampleMux);
	estimate = magEstimate;
	portEXIT_CRITICAL(&magSampleMux);
	return estimate.detected;
}

bool magHeadingTrusted() {
	MagnetometerEstimate estimate;
	return getMagnetometerEstimate(estimate) && estimate.trusted;
}

float navigationHeadingRadians(float fallbackYawRadians) {
	const NavigationHeadingState &heading = headingEstimator.state();
	// Once aligned, retain the learned gyro-to-magnetic offset even while the
	// magnetic sample is unhealthy. Trust is reported separately so AUTO can
	// exit deliberately without creating a discontinuous yaw error.
	return heading.initialized && isfinite(heading.fusedYawRadians) ?
		heading.fusedYawRadians : fallbackYawRadians;
}

bool barometerAvailable() {
	return bmpAddress != 0;
}

bool getBarometerEstimate(BarometerEstimate &estimate) {
	portENTER_CRITICAL(&bmpSampleMux);
	estimate = bmpRuntime.estimate;
	portEXIT_CRITICAL(&bmpSampleMux);
	return estimate.sampleCount > 0;
}
