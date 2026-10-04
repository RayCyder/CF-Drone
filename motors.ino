// 使用MOSFET的电机输出控制
// 如果使用ESC，将pwmStop、pwmMin和pwmMax更改为适当的μs值，将pwmFrequency减小到400
// Motors output control using MOSFETs
// In case of using ESCs, change pwmStop, pwmMin and pwmMax to appropriate values in μs, decrease pwmFrequency (to 400)

#include "util.h"
#include "board_config.h"
#include "diagnostics.h"
#include "motor_test_timer.h"
#include "pwm_config.h"
#include <string.h>

float motors[4]; // normalized motor thrusts in range [0..1]

// 电机引脚（对应 MOTOR_REAR_LEFT=0, MOTOR_REAR_RIGHT=1, MOTOR_FRONT_RIGHT=2, MOTOR_FRONT_LEFT=3）
int motorPins[4] = BOARD_MOTOR_PINS; // RL, RR, FR, FL
static int configuredMotorPins[4] = {};
static bool motorPinConfigInitialized = false;

int pwmFrequency = 25000; // ESP32-C3 LEDC 默认 XTAL 时钟 40MHz，10-bit 上限 39062Hz；
                          // ESP32 APB 80MHz，10-bit 上限 78125Hz；
                          // 25kHz 在两者 XTAL/APB 下 prescaler 均可精确表示，安全余量充足
int pwmResolution = 10;
int pwmStop = 0;
int pwmMin = 0;
int pwmMax = -1; // -1 表示纯占空比模式（接 MOSFET 直驱）；接 ESC 时设为实际 PWM 最大值（μs）
bool motorOutputsOK = false;
bool motorTestActive = false;
bool motorTestArmInhibit = false;
static uint32_t motorTestDeadlineMs = 0;
static bool motorTestVerbose = false;
static const uint32_t MOTOR_TEST_DURATION_MS = 3000;
static constexpr uint32_t MOTOR_CUTOFF_LATCHED = 0x80000000UL;
static constexpr uint32_t MOTOR_CUTOFF_GENERATION_MASK = 0x7FFFFFFFUL;
static uint32_t motorEmergencyCutoffState = 0;
static uint32_t motorEmergencyCutoffAcknowledgedGeneration = 0;

// Motors array indexes:
const int MOTOR_REAR_LEFT = 0;
const int MOTOR_REAR_RIGHT = 1;
const int MOTOR_FRONT_RIGHT = 2;
const int MOTOR_FRONT_LEFT = 3;

void setupMotors() {
	print("Setup Motors\n");
	motorOutputsOK = true;

	// 先解绑所有引脚（重复调用时清理旧 LEDC 通道），再拉低防止误转
	for (int i = 0; i < 4; i++) {
		const int pin = motorPinConfigInitialized ? configuredMotorPins[i] : motorPins[i];
		ledcDetach(pin);                  // 首次调用时无绑定会静默返回 false，无副作用
		pinMode(pin, OUTPUT);
		digitalWrite(pin, LOW);
	}

	// configure pins
	for (int i = 0; i < 4; i++) {
		bool duplicatePin = false;
		for (int j = 0; j < i; ++j) duplicatePin = duplicatePin || (motorPins[i] == motorPins[j]);
		if (motorPins[i] < 0 || duplicatePin) {
			motorOutputsOK = false;
			print("  motor%d pin=%d invalid or duplicated\n", i, motorPins[i]);
			continue;
		}
		bool ok = ledcAttach(motorPins[i], pwmFrequency, pwmResolution);
		if (!ok) motorOutputsOK = false;
		if (ok) {
			double actual = ledcChangeFrequency(motorPins[i], pwmFrequency, pwmResolution);
			if (actual > 0) pwmFrequency = (int)round(actual); // 用 double 接收返回值，避免精度损失
		}
		print("  motor%d pin=%d ledcAttach=%s\n", i, motorPins[i], ok ? "OK" : "FAIL");
	}
	if (!motorPwmConfigurationValid(pwmFrequency, pwmStop, pwmMin, pwmMax)) {
		motorOutputsOK = false;
		print("  PWM invalid after LEDC frequency update: freq=%d stop=%d min=%d max=%d\n",
			pwmFrequency, pwmStop, pwmMin, pwmMax);
	}
	memcpy(configuredMotorPins, motorPins, sizeof(configuredMotorPins));
	motorPinConfigInitialized = true;
	setDiagnosticFault(DIAG_MOTOR_INIT, !motorOutputsOK);

	sendMotors();
	print("Motors initialized\n");
}

int getDutyCycle(float value) {
	return motorPwmDutyFromValue(value, pwmFrequency, pwmResolution, pwmStop, pwmMin, pwmMax);
}

static void writeMotorStopOutputs() {
	const int stopDuty = getDutyCycle(0.0f);
	for (int i = 0; i < 4; ++i) ledcWrite(motorPins[i], stopDuty);
}

bool motorEmergencyCutoffLatched() {
	return (__atomic_load_n(&motorEmergencyCutoffState, __ATOMIC_ACQUIRE) & MOTOR_CUTOFF_LATCHED) != 0;
}

uint32_t motorEmergencyCutoffGeneration() {
	return __atomic_load_n(&motorEmergencyCutoffState, __ATOMIC_ACQUIRE) & MOTOR_CUTOFF_GENERATION_MASK;
}

void latchMotorEmergencyCutoff() {
	uint32_t oldState = __atomic_load_n(&motorEmergencyCutoffState, __ATOMIC_RELAXED);
	for (;;) {
		uint32_t generation = (oldState & MOTOR_CUTOFF_GENERATION_MASK) + 1;
		generation &= MOTOR_CUTOFF_GENERATION_MASK;
		if (generation == 0) generation = 1;
		const uint32_t newState = MOTOR_CUTOFF_LATCHED | generation;
		if (__atomic_compare_exchange_n(&motorEmergencyCutoffState, &oldState, newState,
			false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) break;
	}
	// This runs on the independent fast-stop task, so a stalled flight loop
	// cannot delay the physical zero-duty writes.
	writeMotorStopOutputs();
}

void acknowledgeMotorEmergencyCutoff(uint32_t generation) {
	const uint32_t state = __atomic_load_n(&motorEmergencyCutoffState, __ATOMIC_ACQUIRE);
	if ((state & MOTOR_CUTOFF_LATCHED) &&
		(state & MOTOR_CUTOFF_GENERATION_MASK) == generation) {
		__atomic_store_n(&motorEmergencyCutoffAcknowledgedGeneration, generation, __ATOMIC_RELEASE);
	}
}

bool clearMotorEmergencyCutoffIfAcknowledged() {
	uint32_t expected = __atomic_load_n(&motorEmergencyCutoffState, __ATOMIC_ACQUIRE);
	if ((expected & MOTOR_CUTOFF_LATCHED) == 0) return true;
	const uint32_t generation = expected & MOTOR_CUTOFF_GENERATION_MASK;
	if (__atomic_load_n(&motorEmergencyCutoffAcknowledgedGeneration, __ATOMIC_ACQUIRE) != generation)
		return false;
	return __atomic_compare_exchange_n(&motorEmergencyCutoffState, &expected, generation,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

void sendMotors() {
	const bool cutoff = motorEmergencyCutoffLatched();
	for (int i = 0; i < 4; i++) {
		ledcWrite(motorPins[i], getDutyCycle(cutoff ? 0.0f : motors[i]));
	}
	// Close the race where the fast-stop task latches between the first check
	// and the final motor write. The last writer is always a zero-duty pass.
	if (!cutoff && motorEmergencyCutoffLatched()) writeMotorStopOutputs();
}

void serviceMotorTest() {
	if (!motorTestActive || !motorTestDeadlineReached(millis(), motorTestDeadlineMs)) return;
	memset(motors, 0, sizeof(motors));
	motorTestActive = false;
	sendMotors();
	if (motorTestVerbose) print("电机测试结束，全部输出已归零。\n");
	motorTestVerbose = false;
}

void cancelMotorTest() {
	if (!motorTestActive) return;
	memset(motors, 0, sizeof(motors));
	motorTestActive = false;
	motorTestArmInhibit = true;
	sendMotors();
	motorTestVerbose = false;
}

bool motorsActive() {
	return motors[0] != 0 || motors[1] != 0 || motors[2] != 0 || motors[3] != 0;
}

bool startMotorTest(int n, float output, uint32_t durationMs) {
	extern bool armed;
	extern bool isAccelCalibrationActive();
	extern bool batteryBlocksArming();
	extern bool hasBlockingDiagnosticFault();
	if (!motorOutputsOK || n < 0 || n >= 4 || !isfinite(output) ||
		output < 0.05f || output > 0.3f || durationMs < 50 || durationMs > MOTOR_TEST_DURATION_MS) {
		print("电机输出未就绪或试转配置无效，拒绝测试。\n");
		return false;
	}
	if (armed || motorTestActive || isAccelCalibrationActive() ||
		batteryBlocksArming() || hasBlockingDiagnosticFault()) {
		print("电机测试仅允许在已上锁时执行；当前状态不安全，拒绝测试。\n");
		return false;
	}
	// Print before output starts so serial TX never stalls the motor-sampling loop.
	const bool verbose = durationMs >= 500;
	if (verbose) print("电机 %d 将以 %.0f%% 输出运行 %lu ms。确认已拆桨并固定机体。\n",
		n, output * 100.0f, (unsigned long)durationMs);
	// 电机测试期间清空所有输出，只给目标电机输出，避免遗留控制量带动其他电机。
	memset(motors, 0, sizeof(motors));
	motorTestActive = true;
	motorTestArmInhibit = true;
	motorTestVerbose = verbose;
	motorTestDeadlineMs = millis() + durationMs;
	motors[n] = output;
	sendMotors();
	return true;
}

void testMotor(int n) {
	startMotorTest(n, 0.3f, MOTOR_TEST_DURATION_MS);
}
