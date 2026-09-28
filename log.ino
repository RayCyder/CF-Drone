// RAM日志记录
// In-RAM logging

#include "vector.h"
#include "util.h"

#include "board_config.h"
#include "diagnostics.h"

#define LOG_RATE 100
#define LOG_DURATION BOARD_LOG_DURATION  // 各板型保留最近4秒数据，包含上锁阶段
#define LOG_SIZE LOG_DURATION * LOG_RATE

extern bool armed;
extern float batteryVoltage, controlRoll, controlPitch, controlYaw, controlThrottle, controlMode, controlTime;
extern float motors[4];
extern float dt;
extern Vector gyro, acc;
extern float thrustTarget;
extern int mode;

static int logPointer = 0;
static int logCount = 0;
static uint32_t logSequence = 0;
static float logArmed = 0;
static float logFaultMask = 0;
static float logRcAge = -1;
static float logMode = 0;

Vector attitudeEuler;
Vector attitudeTargetEuler;

struct LogEntry {
	const char *name;
	float *value;
};

LogEntry logEntries[] = {
	{"t", &t},
	{"dt_s", &dt},
	{"gyro_x", &gyro.x},
	{"gyro_y", &gyro.y},
	{"gyro_z", &gyro.z},
	{"acc_x", &acc.x},
	{"acc_y", &acc.y},
	{"acc_z", &acc.z},
	{"rates.x", &rates.x},
	{"rates.y", &rates.y},
	{"rates.z", &rates.z},
	{"ratesTarget.x", &ratesTarget.x},
	{"ratesTarget.y", &ratesTarget.y},
	{"ratesTarget.z", &ratesTarget.z},
	{"attitude.x", &attitudeEuler.x},
	{"attitude.y", &attitudeEuler.y},
	{"attitude.z", &attitudeEuler.z},
	{"attitudeTarget.x", &attitudeTargetEuler.x},
	{"attitudeTarget.y", &attitudeTargetEuler.y},
	{"attitudeTarget.z", &attitudeTargetEuler.z},
	{"thrustTarget", &thrustTarget},
	{"battery_v", &batteryVoltage},
	{"rc_roll", &controlRoll},
	{"rc_pitch", &controlPitch},
	{"rc_yaw", &controlYaw},
	{"rc_throttle", &controlThrottle},
	{"rc_mode", &controlMode},
	{"flight_mode", &logMode},
	{"rc_age_s", &logRcAge},
	{"armed", &logArmed},
	{"fault_mask", &logFaultMask},
	{"motor_rl", &motors[0]},
	{"motor_rr", &motors[1]},
	{"motor_fr", &motors[2]},
	{"motor_fl", &motors[3]}
};

const int logColumns = sizeof(logEntries) / sizeof(logEntries[0]);
float logBuffer[LOG_SIZE][logColumns];
static portMUX_TYPE logBufferMux = portMUX_INITIALIZER_UNLOCKED;

int getLogColumnCount() {
	return logColumns;
}

const char* getLogColumnName(int column) {
	return column >= 0 && column < logColumns ? logEntries[column].name : "";
}

bool copyLatestLogRow(float *destination, int capacity, uint32_t *sequence) {
	if (!destination || capacity < logColumns) return false;
	portENTER_CRITICAL(&logBufferMux);
	if (logCount == 0) {
		portEXIT_CRITICAL(&logBufferMux);
		return false;
	}
	const int latest = (logPointer - 1 + LOG_SIZE) % LOG_SIZE;
	memcpy(destination, logBuffer[latest], sizeof(float) * logColumns);
	if (sequence) *sequence = logSequence;
	portEXIT_CRITICAL(&logBufferMux);
	return true;
}

void prepareLogData() {
	attitudeEuler = attitude.toEuler();
	attitudeTargetEuler = attitudeTarget.toEuler();
	logMode = (float)mode;
	logArmed = armed ? 1.0f : 0.0f;
	logFaultMask = (float)getActiveDiagnosticFaults();
	logRcAge = controlTime > 0 ? t - controlTime : -1.0f;
}

void logData() {
	static Rate period(LOG_RATE);
	if (!period) return;

	prepareLogData();

	portENTER_CRITICAL(&logBufferMux);
	for (int i = 0; i < logColumns; i++) {
		logBuffer[logPointer][i] = *logEntries[i].value;
	}

	logPointer++;
	if (logPointer >= LOG_SIZE) {
		logPointer = 0;
	}
	if (logCount < LOG_SIZE) logCount++;
	logSequence++;
	portEXIT_CRITICAL(&logBufferMux);
}

void printLogHeader() {
	for (int i = 0; i < logColumns; i++) {
		print("%s%s", logEntries[i].name, i < logColumns - 1 ? "," : "\n");
	}
}

void printLogData() {
	const int first = (logPointer - logCount + LOG_SIZE) % LOG_SIZE;
	for (int row = 0; row < logCount; row++) {
		const int i = (first + row) % LOG_SIZE;
		for (int j = 0; j < logColumns; j++) {
			print("%g%s", logBuffer[i][j], j < logColumns - 1 ? "," : "\n");
		}
	}
}
