#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "diagnostics.h"
#include "system_log.h"

#if WEB_RC_ENABLED
extern bool isUsingWebRC();
#endif

void printInvalidParameterValues();

struct DiagnosticState {
	DiagnosticFault id;
	const char *name;
	const char *severity;
	const char *advice;
	bool active;
	uint16_t occurrences;
	uint32_t firstSeen;
	uint32_t lastSeen;
	uint32_t activeSince;
	uint32_t activeDuration;
};

static DiagnosticState diagnosticStates[] = {
	{DIAG_IMU_INIT, "IMU_INIT", "CRITICAL", "检查IMU供电、SPI接线和传感器型号", false, 0, 0, 0, 0, 0},
	{DIAG_IMU_TIMEOUT, "IMU_TIMEOUT", "CRITICAL", "检查IMU通信、数据就绪中断和供电", false, 0, 0, 0, 0, 0},
	{DIAG_IMU_INVALID, "IMU_INVALID", "CRITICAL", "检查IMU数据和安装/校准参数", false, 0, 0, 0, 0, 0},
	{DIAG_MOTOR_INIT, "MOTOR_INIT", "CRITICAL", "拆桨后检查电机引脚、PWM配置和接线", false, 0, 0, 0, 0, 0},
	{DIAG_RC_LOSS, "RC_LOSS", "WARNING", "检查接收机供电、协议、串口引脚和链路", false, 0, 0, 0, 0, 0},
	{DIAG_WEB_RC_LOSS, "WEB_RC_LOSS", "WARNING", "检查遥控客户端连接和Wi-Fi链路", false, 0, 0, 0, 0, 0},
	{DIAG_BATTERY_LOW, "BATTERY_LOW", "WARNING", "检查电池电量、分压电阻和ADC引脚", false, 0, 0, 0, 0, 0},
	{DIAG_LOOP_OVERRUN, "LOOP_OVERRUN", "WARNING", "检查循环负载、通信请求和日志输出", false, 0, 0, 0, 0, 0},
	{DIAG_PARAMETER, "PARAMETER", "CRITICAL", "检查参数范围；修正后重启并重新执行diag", false, 0, 0, 0, 0, 0},
};

static uint32_t loopOverrunCount = 0;
static float worstLoopDt = 0;
static uint32_t lastLoopOverrunMs = 0;
static const char *slowStageNames[11] = {};
static uint32_t slowStageLoggedAt[11] = {};

void recordLoopStage(const char *stage, uint32_t durationUs) {
	if (!stage || durationUs < 5000) return;
	const uint32_t now = millis();
	int slot = -1;
	for (int i = 0; i < 11; ++i) {
		if (slowStageNames[i] && strcmp(slowStageNames[i], stage) == 0) {
			slot = i;
			break;
		}
		if (slot < 0 && !slowStageNames[i]) slot = i;
	}
	if (slot < 0 || (slowStageNames[slot] && (uint32_t)(now - slowStageLoggedAt[slot]) < 5000)) return;
	slowStageNames[slot] = stage;
	slowStageLoggedAt[slot] = now;
	char message[48];
	snprintf(message, sizeof(message), "stage=%s duration_us=%lu", stage, (unsigned long)durationUs);
	recordSystemLogEvent("SLOW_LOOP", message);
}

void setDiagnosticFault(DiagnosticFault fault, bool active) {
	const uint32_t now = millis();
	for (size_t i = 0; i < sizeof(diagnosticStates) / sizeof(diagnosticStates[0]); ++i) {
		DiagnosticState &state = diagnosticStates[i];
		if (state.id != fault) continue;
		if (active != state.active) {
			state.lastSeen = now;
			if (active) {
				if (state.occurrences < UINT16_MAX) state.occurrences++;
				if (state.occurrences == 1) state.firstSeen = now;
				state.activeSince = now;
			} else {
				state.activeDuration += now - state.activeSince;
			}
			state.active = active;
			char eventMessage[48];
			if (fault == DIAG_LOOP_OVERRUN && active) {
				snprintf(eventMessage, sizeof(eventMessage), "ACTIVE LOOP dt=%.4f max=%.4f n=%lu",
					dt, worstLoopDt, (unsigned long)loopOverrunCount);
			} else {
				snprintf(eventMessage, sizeof(eventMessage), "%s %s mask=0x%08lx",
					active ? "ACTIVE" : "CLEARED", state.name,
					(unsigned long)getActiveDiagnosticFaults());
			}
			recordSystemLogEvent("DIAG", eventMessage);
			// Fault transitions happen inside the flight loop. Keep them in the
			// event ring for SSE/diag instead of synchronously draining UART here.
		}
		return;
	}
}

uint32_t getActiveDiagnosticFaults() {
	uint32_t active = 0;
	for (size_t i = 0; i < sizeof(diagnosticStates) / sizeof(diagnosticStates[0]); ++i)
		if (diagnosticStates[i].active) active |= diagnosticStates[i].id;
	return active;
}

bool hasBlockingDiagnosticFault() {
	const uint32_t blocking = DIAG_IMU_INIT | DIAG_IMU_TIMEOUT | DIAG_IMU_INVALID |
		DIAG_MOTOR_INIT | DIAG_PARAMETER;
	return (getActiveDiagnosticFaults() & blocking) != 0;
}

void recordLoopTiming(float dt) {
	if (!isfinite(dt) || dt > worstLoopDt) worstLoopDt = dt;
	if (dt > 0.010f) {
		if (loopOverrunCount < UINT32_MAX) loopOverrunCount++;
		lastLoopOverrunMs = millis();
		setDiagnosticFault(DIAG_LOOP_OVERRUN, true);
	}
}

void updateDiagnostics() {
	extern bool imuOK;
	extern bool motorOutputsOK;
	extern float controlTime;
	extern float t;
	extern float rcLossTimeout;
	extern bool armed;
	extern float thrustTarget;
	extern bool batteryAlertActiveForFlight(bool flying);
	setDiagnosticFault(DIAG_IMU_INIT, !imuOK);
	setDiagnosticFault(DIAG_MOTOR_INIT, !motorOutputsOK);
	bool rcInputLost = armed && controlTime != 0 && (t - controlTime > rcLossTimeout);
#if WEB_RC_ENABLED
	// The shared RC timestamp only advances on stick packets. Web RC heartbeats
	// keep that transport alive, so don't report the unused physical RC path lost.
	if (isUsingWebRC()) rcInputLost = false;
#endif
	setDiagnosticFault(DIAG_RC_LOSS, rcInputLost);
	setDiagnosticFault(DIAG_BATTERY_LOW,
		batteryAlertActiveForFlight(armed && thrustTarget >= 0.15f));
	if (lastLoopOverrunMs && (uint32_t)(millis() - lastLoopOverrunMs) > 10000UL)
		setDiagnosticFault(DIAG_LOOP_OVERRUN, false);
}

void initializeDiagnostics() {
	updateDiagnostics();
	printDiagnostics();
}

void clearDiagnosticHistory() {
	for (size_t i = 0; i < sizeof(diagnosticStates) / sizeof(diagnosticStates[0]); ++i) {
		DiagnosticState &state = diagnosticStates[i];
		state.occurrences = state.active ? 1 : 0;
		state.firstSeen = state.active ? state.activeSince : 0;
		state.lastSeen = state.active ? state.activeSince : 0;
		state.activeDuration = 0;
	}
	loopOverrunCount = 0;
	worstLoopDt = 0;
	print("诊断历史计数已清理；当前故障仍保留。\n");
}

void printDiagnostics() {
	updateDiagnostics();
	extern bool armed, imuOK, motorOutputsOK;
	extern float t, dt, loopRate, batteryVoltage, controlTime, controlRoll, controlPitch, controlYaw, controlThrottle;
	print("DIAG_CONTEXT uptime_ms=%lu armed=%u imu_ok=%u motor_ok=%u battery_v=%.2f rc_age_s=%.3f rc=(%.2f,%.2f,%.2f,%.2f) loop_rate=%.0f dt=%.4f free_heap=%lu\n",
		(unsigned long)millis(), armed ? 1 : 0, imuOK ? 1 : 0, motorOutputsOK ? 1 : 0,
		batteryVoltage, controlTime > 0 ? t - controlTime : -1.0f,
		controlRoll, controlPitch, controlYaw, controlThrottle, loopRate, dt,
		(unsigned long)ESP.getFreeHeap());
	bool any = false;
	print("故障诊断 active=0x%08lx loop_overruns=%lu worst_dt=%.4fs\n",
		(unsigned long)getActiveDiagnosticFaults(), (unsigned long)loopOverrunCount, worstLoopDt);
	for (size_t i = 0; i < sizeof(diagnosticStates) / sizeof(diagnosticStates[0]); ++i) {
		const DiagnosticState &state = diagnosticStates[i];
		if (!state.active && state.occurrences == 0) continue;
		any = true;
		uint32_t duration = state.activeDuration;
		if (state.active) duration += millis() - state.activeSince;
		print("[%s] %s state=%s count=%u first_ms=%lu last_ms=%lu active_ms=%lu: %s\n", state.severity, state.name,
			state.active ? "ACTIVE" : "CLEARED", state.occurrences,
			(unsigned long)state.firstSeen, (unsigned long)state.lastSeen,
			(unsigned long)duration, state.advice);
	}
	if (getActiveDiagnosticFaults() & DIAG_PARAMETER) printInvalidParameterValues();
	if (!any) print("未检测到活动故障。\n");
	print("人工项目：拆桨并固定机体，依次运行 mfr/mfl/mrr/mrl，确认每次只有对应电机转动；无转速反馈，飞控不能自动确认电机本体。\n");
}
