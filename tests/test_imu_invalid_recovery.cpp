#include "Arduino.h"
#include "SPI.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cmath>

#include "../vector.h"
#include "../quaternion.h"
#include "../diagnostics.h"

SPIClass SPI;

#define INVENSENSE_IMU_SRC_MPU9250_H_
class MPU9250 {
public:
	enum AccelRange { ACCEL_RANGE_2G, ACCEL_RANGE_4G };
	enum GyroRange { GYRO_RANGE_2000DPS };
	enum DLPF { DLPF_MAX };
	enum Rate { RATE_1KHZ_APPROX };

	MPU9250(SPIClass &, int) {}

	bool begin() { return true; }
	bool setAccelRange(AccelRange) { return true; }
	bool setGyroRange(GyroRange) { return true; }
	bool setDLPF(DLPF) { return true; }
	bool setRate(Rate) { return true; }
	bool setupInterrupt() { return true; }
	bool waitForData(uint32_t) { return ready; }
	void getGyro(float &x, float &y, float &z) const { x = gyroSample.x; y = gyroSample.y; z = gyroSample.z; }
	void getAccel(float &x, float &y, float &z) const { x = accSample.x; y = accSample.y; z = accSample.z; }
	float getTemp() const { return temperature; }
	int status() const { return 0; }
	uint8_t whoAmI() const { return 0x70; }
	const char *getModel() const { return "host-fake"; }

	void setSample(const Vector &gyro, const Vector &acc, bool dataReady = true) {
		gyroSample = gyro;
		accSample = acc;
		ready = dataReady;
	}

	bool ready = true;
	Vector gyroSample;
	Vector accSample;
	float temperature = 25.0f;
};

double t = 0.0;
float dt = 0.001f;
float loopRate = 1000.0f;
float controlRoll = 0.0f;
float controlPitch = 0.0f;
Vector rates, gyro, acc;
Quaternion attitude;
bool landed = false;
bool armed = false;
static bool motorOutputActive = false;
static uint32_t nowUs = 0;
static uint32_t diagnosticFaults = 0;

uint32_t micros() { nowUs += 1000; return nowUs; }
uint32_t millis() { return nowUs / 1000; }
bool motorsActive() { return motorOutputActive; }
bool saveParameterNow(const char *) { return true; }
void print(const char *, ...) {}
void recordLoopStage(LoopStageId, uint32_t) {}
void setDiagnosticFault(DiagnosticFault fault, bool active) {
	if (active) diagnosticFaults |= fault;
	else diagnosticFaults &= ~static_cast<uint32_t>(fault);
}
uint32_t getActiveDiagnosticFaults() { return diagnosticFaults; }
bool hasBlockingDiagnosticFault() {
	return diagnosticFaults & (DIAG_IMU_INIT | DIAG_IMU_TIMEOUT | DIAG_IMU_INVALID | DIAG_MOTOR_INIT |
		DIAG_RC_LOSS | DIAG_WEB_RC_LOSS | DIAG_BATTERY_LOW | DIAG_PARAMETER);
}

void applyGyro();
void applyAcc();
void applyLevel();
bool configureIMU();
void calibrateGyroOnce(const Vector &rawGyroSensor, const Vector &rawAccSensor);
void printIMUCalibration();

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdynamic-class-memaccess"
#endif
#include "../imu.ino"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#define ESTIMATOR_REPORT_IMU_INVALID(active) setDiagnosticFault(DIAG_IMU_INVALID, (active))
#define ESTIMATOR_DISARM_CRITICAL() do { armed = false; } while (0)
#define ESTIMATOR_IMU_SAMPLE_VALID() imuSampleValid
#include "../estimate.ino"

static void resetHostState() {
	t = 0.0;
	dt = 0.001f;
	nowUs = 0;
	diagnosticFaults = 0;
	imuOK = true;
	imuSampleValid = false;
	landed = false;
	armed = false;
	motorOutputActive = false;
	gyro = Vector(1.0f, 2.0f, 3.0f);
	acc = Vector(4.0f, 5.0f, 6.0f);
	rates = Vector();
	attitude = Quaternion();
	accBias = Vector();
	accScale = Vector(1.0f, 1.0f, 1.0f);
	gyroBias = Vector();
	imuRotation = Vector(0.0f, 0.0f, 0.0f);
	ratesFilter.reset();
	accelerationFusionFilter.reset();
}

static void assertVectorEquals(const Vector &actual, const Vector &expected) {
	assert(actual.x == expected.x);
	assert(actual.y == expected.y);
	assert(actual.z == expected.z);
}

static void assertQuaternionNear(const Quaternion &actual, const Quaternion &expected) {
	assert(fabsf(actual.w - expected.w) < 1e-7f);
	assert(fabsf(actual.x - expected.x) < 1e-7f);
	assert(fabsf(actual.y - expected.y) < 1e-7f);
	assert(fabsf(actual.z - expected.z) < 1e-7f);
}

static void assertVectorNear(const Vector &actual, const Vector &expected) {
	assert(fabsf(actual.x - expected.x) < 1e-7f);
	assert(fabsf(actual.y - expected.y) < 1e-7f);
	assert(fabsf(actual.z - expected.z) < 1e-7f);
}

int main() {
	resetHostState();
	const Vector lastGyro = gyro;
	const Vector lastAcc = acc;
	imu.setSample(Vector(NAN, 0.0f, 0.0f), Vector(0.0f, 0.0f, ONE_G));
	readIMU();
	assert(getActiveDiagnosticFaults() & DIAG_IMU_INVALID);
	assertVectorEquals(gyro, lastGyro);
	assertVectorEquals(acc, lastAcc);

	resetHostState();
	accScale = Vector(0.0f, 1.0f, 1.0f);
	imu.setSample(Vector(0.0f, 0.0f, 0.0f), Vector(0.0f, 0.0f, ONE_G));
	readIMU();
	assert(getActiveDiagnosticFaults() & DIAG_IMU_INVALID);
	assertVectorEquals(gyro, Vector(1.0f, 2.0f, 3.0f));
	assertVectorEquals(acc, Vector(4.0f, 5.0f, 6.0f));

	resetHostState();
	armed = true;
	imu.setSample(Vector(1.0f, 0.0f, 0.0f), Vector(0.0f, 0.0f, ONE_G));
	readIMU();
	assert(imuSampleValid);
	estimate();
	const Quaternion attitudeAfterGood = attitude;
	const Vector ratesAfterGood = rates;
	assert(attitudeAfterGood.valid());
	assert(ratesAfterGood.valid());
	imu.setSample(Vector(NAN, 0.0f, 0.0f), Vector(0.0f, 0.0f, ONE_G));
	readIMU();
	assert(!imuSampleValid);
	assert(getActiveDiagnosticFaults() & DIAG_IMU_INVALID);
	estimate();
	// A single bad frame keeps the last finite estimate. The safety layer owns
	// the 100 ms outage debounce and bounded landing transition.
	assert(armed);
	assertQuaternionNear(attitude, attitudeAfterGood);
	assertVectorNear(rates, ratesAfterGood);

	resetHostState();
	gyro.invalidate();
	acc.invalidate();
	rates.invalidate();
	attitude.invalidate();
	armed = true;
	motorOutputActive = true;
	setDiagnosticFault(DIAG_IMU_INVALID, true);
	imu.setSample(Vector(0.0f, 0.0f, 0.0f), Vector(0.0f, 0.0f, ONE_G));
	for (int i = 0; i < 100; ++i) {
		t += dt;
		readIMU();
		estimate();
	}
	assert(!armed);
	assert(!(getActiveDiagnosticFaults() & DIAG_IMU_INVALID));
	assert(!hasBlockingDiagnosticFault());
	assert(gyro.valid());
	assert(acc.valid());
	assert(rates.valid());
	assert(attitude.valid() && isfinite(attitude.norm()) && attitude.norm() > 0.99f && attitude.norm() < 1.01f);

	puts("IMU invalid sample recovery regression: PASS");
}
