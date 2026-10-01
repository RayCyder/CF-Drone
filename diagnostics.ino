#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "diagnostics.h"
#include "system_log.h"
#include "flight_log.h"
#include "loop_metrics.h"

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
	{DIAG_AUTO_TARGET_TIMEOUT, "AUTO_TARGET_TIMEOUT", "WARNING", "检查外部AUTO控制链路、目标频率和模式切换流程", false, 0, 0, 0, 0, 0},
	{DIAG_INVERTED, "INVERTED", "WARNING", "检查机体姿态；倒置保护触发期间保持锁定", false, 0, 0, 0, 0, 0},
};

static uint32_t loopOverrunCount = 0;
static float worstLoopDt = 0;
static uint32_t lastLoopOverrunMs = 0;
static bool haveLoopOverrun = false;
// Keep counting/reporting >1.5 ms jitter, but do not freeze the short flight
// log on a single near-budget cycle. A >5 ms cycle is a meaningful stall and
// must retain an armed snapshot even if the warning bit was already active.
static constexpr uint32_t LOOP_STALL_LOG_TRIGGER_US = 5000;
static LoopTimingMetrics loopTiming;
struct LoopStageMetrics {
    const char *name;
    uint32_t budgetUs;
    uint32_t maximumUs = 0, pendingWorstUs = 0;
    uint64_t samples = 0, overBudget = 0;
    LoopStageEma average;
};
static LoopStageMetrics loopStages[] = {
    {"imu",1200,0,0,0,0,{}}, {"imu_wait",1500,0,0,0,0,{}},
    {"imu_process",200,0,0,0,0,{}}, {"rc_web",200,0,0,0,0,{}},
    {"estimate",200,0,0,0,0,{}}, {"battery_adc",200,0,0,0,0,{}},
    {"control_law",200,0,0,0,0,{}}, {"motor_out",150,0,0,0,0,{}},
    {"control",500,0,0,0,0,{}}, {"serial_input",200,0,0,0,0,{}},
    {"mavlink",300,0,0,0,0,{}}, {"flight_log",300,0,0,0,0,{}},
    {"param_sync",200,0,0,0,0,{}}, {"led",100,0,0,0,0,{}},
    {"diagnostics",200,0,0,0,0,{}}, {"wifi_service",300,0,0,0,0,{}},
    {"loop_gap",100,0,0,0,0,{}}, {"maintenance",300,0,0,0,0,{}},
    {"whole_loop",1500,0,0,0,0,{}}
};
static uint32_t lastStageReportMs = 0;
static LoopOverrunTraceRing loopTrace;
static portMUX_TYPE loopTraceMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t currentLoopTraceStages[LOOP_TRACE_STAGE_COUNT] = {};
static uint32_t previousLoopBodyStages[LOOP_TRACE_STAGE_COUNT] = {};
static uint32_t currentLoopSequence = 0;
#if defined(CF_DRONE_CAPTURE_ARMED_LOOP_TRACE)
static bool armedLoopTraceCaptureStarted = false;
static bool loopWasArmedForTrace = false;
static bool armedLoopTraceTriggered = false;
extern bool armed;
#endif
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
static ImuWaitTrace currentImuWaitTrace = {};
#endif

static int traceStageForLoopStage(LoopStageId stage) {
    switch (stage) {
    case LOOP_STAGE_IMU_WAIT: return LOOP_TRACE_IMU_WAIT;
    case LOOP_STAGE_IMU_PROCESS: return LOOP_TRACE_IMU_PROCESS;
    case LOOP_STAGE_RC_WEB: return LOOP_TRACE_RC_WEB;
    case LOOP_STAGE_ESTIMATE: return LOOP_TRACE_ESTIMATE;
    case LOOP_STAGE_BATTERY_ADC: return LOOP_TRACE_BATTERY_ADC;
    case LOOP_STAGE_CONTROL_LAW: return LOOP_TRACE_CONTROL_LAW;
    case LOOP_STAGE_MOTOR_OUT: return LOOP_TRACE_MOTOR_OUT;
    case LOOP_STAGE_SERIAL_INPUT: return LOOP_TRACE_SERIAL_INPUT;
    case LOOP_STAGE_MAVLINK: return LOOP_TRACE_MAVLINK;
    case LOOP_STAGE_FLIGHT_LOG: return LOOP_TRACE_FLIGHT_LOG;
    case LOOP_STAGE_PARAM_SYNC: return LOOP_TRACE_PARAM_SYNC;
    case LOOP_STAGE_LED: return LOOP_TRACE_LED;
    case LOOP_STAGE_DIAGNOSTICS: return LOOP_TRACE_DIAGNOSTICS;
    case LOOP_STAGE_WIFI_SERVICE: return LOOP_TRACE_WIFI_SERVICE;
    case LOOP_STAGE_LOOP_GAP: return LOOP_TRACE_LOOP_GAP;
    default: return -1;
    }
}

static const char *const loopTraceStageNames[LOOP_TRACE_STAGE_COUNT] = {
    "imu_wait", "imu_process", "rc_web", "estimate", "battery_adc",
    "control_law", "motor_out", "serial_input", "mavlink", "flight_log",
    "param_sync", "led", "diagnostics", "wifi_service", "loop_gap", "unaccounted"
};

static_assert(sizeof(loopStages) / sizeof(loopStages[0]) == LOOP_STAGE_COUNT, "stage index/name table mismatch");
void recordLoopStage(LoopStageId stage, uint32_t durationUs) {
    if (stage >= LOOP_STAGE_COUNT) return;
    auto &entry = loopStages[stage];
    ++entry.samples;
    entry.average.update(durationUs);
    if (durationUs > entry.maximumUs) entry.maximumUs = durationUs;
    if (durationUs > entry.budgetUs) {
        ++entry.overBudget;
        if (durationUs > entry.pendingWorstUs) entry.pendingWorstUs = durationUs;
    }
    const int traceStage = traceStageForLoopStage(stage);
    if (traceStage >= 0) currentLoopTraceStages[traceStage] = durationUs;
}

#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
void recordImuWaitTrace(const ImuWaitTrace &trace) {
    currentImuWaitTrace = trace;
}
#endif

void beginLoopTraceCycle() {
    memset(currentLoopTraceStages, 0, sizeof(currentLoopTraceStages));
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
    currentImuWaitTrace = {};
#endif
}

void finishLoopTraceCycle() {
    // dt is sampled just after the next iteration's IMU read. Retain only the
    // prior iteration's body here; that next sample combines it with the
    // current gap and IMU read, matching the actual dt measurement interval.
    for (uint8_t i = LOOP_TRACE_RC_WEB; i <= LOOP_TRACE_WIFI_SERVICE; ++i)
        previousLoopBodyStages[i] = currentLoopTraceStages[i];
}

uint8_t getLoopTraceCount() {
    portENTER_CRITICAL(&loopTraceMux);
    const uint8_t result = loopTrace.count;
    portEXIT_CRITICAL(&loopTraceMux);
    return result;
}

uint32_t getLoopTraceOverwrittenCount() {
    portENTER_CRITICAL(&loopTraceMux);
    const uint32_t result = loopTrace.overwritten;
    portEXIT_CRITICAL(&loopTraceMux);
    return result;
}

uint32_t getLoopTraceOldestSequence() {
    portENTER_CRITICAL(&loopTraceMux);
    const uint32_t result = loopTrace.oldestSequence();
    portEXIT_CRITICAL(&loopTraceMux);
    return result;
}

uint32_t getLoopTraceNextSequence() {
    portENTER_CRITICAL(&loopTraceMux);
    const uint32_t result = loopTrace.nextSequence;
    portEXIT_CRITICAL(&loopTraceMux);
    return result;
}

void getLoopTraceRange(uint32_t &oldest, uint32_t &next, uint32_t &overwritten) {
    portENTER_CRITICAL(&loopTraceMux);
    oldest = loopTrace.oldestSequence();
    next = loopTrace.nextSequence;
    overwritten = loopTrace.overwritten;
    portEXIT_CRITICAL(&loopTraceMux);
}

const char *getLoopTraceStageName(uint8_t stage) {
    return stage < LOOP_TRACE_STAGE_COUNT ? loopTraceStageNames[stage] : "";
}

bool copyLoopTrace(uint32_t sequence, LoopOverrunTrace &destination) {
    portENTER_CRITICAL(&loopTraceMux);
    const bool copied = loopTrace.copy(sequence, destination);
    portEXIT_CRITICAL(&loopTraceMux);
    return copied;
}

bool copyWorstLoopTrace(LoopOverrunTrace &destination) {
    portENTER_CRITICAL(&loopTraceMux);
    const bool copied = loopTrace.copyWorst(destination);
    portEXIT_CRITICAL(&loopTraceMux);
    return copied;
}

void resetLoopTraceState() {
    portENTER_CRITICAL(&loopTraceMux);
    loopTrace.clear();
    portEXIT_CRITICAL(&loopTraceMux);
    memset(currentLoopTraceStages, 0, sizeof(currentLoopTraceStages));
    memset(previousLoopBodyStages, 0, sizeof(previousLoopBodyStages));
    currentLoopSequence = 0;
#if defined(CF_DRONE_CAPTURE_ARMED_LOOP_TRACE)
    armedLoopTraceCaptureStarted = false;
    loopWasArmedForTrace = false;
    armedLoopTraceTriggered = false;
#endif
}

void setLoopTimingSequence(uint32_t loopSequence) {
    currentLoopSequence = loopSequence;
}

static void reportLoopStages() {
    const uint32_t now = millis();
    if ((uint32_t)(now - lastStageReportMs) < 5000) return;
    lastStageReportMs = now;
    const LoopStageMetrics *worst = nullptr;
    for (const auto &entry : loopStages) {
        if (!strcmp(entry.name,"imu") || !strcmp(entry.name,"whole_loop") || !strcmp(entry.name,"control") ||
            !strcmp(entry.name,"maintenance")) continue;
        if (entry.pendingWorstUs && (!worst || entry.pendingWorstUs > worst->pendingWorstUs)) worst = &entry;
    }
    if (worst) {
        char message[48];
        snprintf(message, sizeof(message), "stage=%s duration_us=%lu", worst->name,
            (unsigned long)worst->pendingWorstUs);
        recordSystemLogEvent("SLOW_LOOP", message);
    }
    for (auto &entry : loopStages) entry.pendingWorstUs = 0;
}

void setDiagnosticFault(DiagnosticFault fault, bool active) {
	const uint32_t now = millis();
	for (size_t i = 0; i < sizeof(diagnosticStates) / sizeof(diagnosticStates[0]); ++i) {
		DiagnosticState &state = diagnosticStates[i];
		if (state.id != fault) continue;
		if (active != state.active) {
			state.lastSeen = now;
			if (active) {
				extern bool armed;
				if (armed && fault != DIAG_LOOP_OVERRUN) triggerFlightLog((uint32_t)fault);
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
		if (fault == DIAG_LOOP_OVERRUN && active && dt * 1000000.0f >= LOOP_STALL_LOG_TRIGGER_US) {
			extern bool armed;
			if (armed) triggerFlightLog((uint32_t)fault);
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
    const uint32_t us = loopTiming.observe(dt);
    worstLoopDt = loopTiming.maximumUs * .000001f;
#if defined(CF_DRONE_CAPTURE_ARMED_LOOP_TRACE)
    if (armed && !loopWasArmedForTrace) {
        portENTER_CRITICAL(&loopTraceMux);
        loopTrace.clear();
        portEXIT_CRITICAL(&loopTraceMux);
        armedLoopTraceCaptureStarted = true;
        armedLoopTraceTriggered = false;
    } else if (!armed && loopWasArmedForTrace && armedLoopTraceCaptureStarted) {
        portENTER_CRITICAL(&loopTraceMux);
        loopTrace.freeze();
        portEXIT_CRITICAL(&loopTraceMux);
    }
    loopWasArmedForTrace = armed;
    const bool captureTrace = (armed && armedLoopTraceCaptureStarted && !armedLoopTraceTriggered) ||
        (us > 1500 && !armedLoopTraceCaptureStarted);
#else
    const bool captureTrace = us > 1500;
#endif
    if (us > 1500) {
        if (loopOverrunCount < UINT32_MAX) ++loopOverrunCount;
        lastLoopOverrunMs = millis();
        haveLoopOverrun = true;
        setDiagnosticFault(DIAG_LOOP_OVERRUN, true);
    }
    if (captureTrace) {
        LoopOverrunTrace trace;
        trace.uptimeMs = millis();
        trace.dtUs = us;
        trace.loopSequence = currentLoopSequence;
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
        trace.imuWait = currentImuWaitTrace;
#endif
        trace.stageUs[LOOP_TRACE_IMU_WAIT] = currentLoopTraceStages[LOOP_TRACE_IMU_WAIT];
        trace.stageUs[LOOP_TRACE_IMU_PROCESS] = currentLoopTraceStages[LOOP_TRACE_IMU_PROCESS];
        for (uint8_t i = LOOP_TRACE_RC_WEB; i <= LOOP_TRACE_WIFI_SERVICE; ++i)
            trace.stageUs[i] = previousLoopBodyStages[i];
        trace.stageUs[LOOP_TRACE_LOOP_GAP] = currentLoopTraceStages[LOOP_TRACE_LOOP_GAP];
        uint64_t attributedUs = 0;
        for (uint8_t i = 0; i < LOOP_TRACE_UNACCOUNTED; ++i) attributedUs += trace.stageUs[i];
        trace.stageUs[LOOP_TRACE_UNACCOUNTED] = attributedUs < us ? (uint32_t)(us - attributedUs) : 0;
        portENTER_CRITICAL(&loopTraceMux);
        loopTrace.push(trace);
#if defined(CF_DRONE_CAPTURE_ARMED_LOOP_TRACE)
        // Preserve the pre-trigger window and first over-budget loop. Read it
        // after disarming so diagnostics cannot overwrite the control event.
        if (armed && us > 1500) {
            loopTrace.freeze();
            armedLoopTraceTriggered = true;
        }
#endif
        portEXIT_CRITICAL(&loopTraceMux);
    }
}

void updateDiagnostics() {
    reportLoopStages();
	extern bool imuOK;
	extern bool motorOutputsOK;
	extern double controlTime;
	extern double t;
	extern float rcLossTimeout;
	extern bool armed;
	extern bool isInverted;
	extern int mode;
	extern const int AUTO;
	extern float thrustTarget;
	extern bool batteryAlertActiveForFlight(bool flying);
	extern bool autoTargetTimedOut();
	extern bool isControlledLandingActive();
	setDiagnosticFault(DIAG_IMU_INIT, !imuOK);
	setDiagnosticFault(DIAG_MOTOR_INIT, !motorOutputsOK);
	bool rcInputLost = armed && mode != AUTO && controlTime != 0 && (t - controlTime > rcLossTimeout);
#if WEB_RC_ENABLED
	// The shared RC timestamp only advances on stick packets. Web RC heartbeats
	// keep that transport alive, so don't report the unused physical RC path lost.
	if (isUsingWebRC()) rcInputLost = false;
#endif
	setDiagnosticFault(DIAG_RC_LOSS, rcInputLost);
	bool autoTimeoutFault = false;
	if (armed && mode == AUTO) {
		autoTimeoutFault = isControlledLandingActive() ?
			((getActiveDiagnosticFaults() & DIAG_AUTO_TARGET_TIMEOUT) != 0) :
			autoTargetTimedOut();
	}
	setDiagnosticFault(DIAG_AUTO_TARGET_TIMEOUT, autoTimeoutFault);
	setDiagnosticFault(DIAG_INVERTED, armed && isInverted);
	setDiagnosticFault(DIAG_BATTERY_LOW,
		batteryAlertActiveForFlight(armed && thrustTarget >= 0.15f));
	if (haveLoopOverrun && (uint32_t)(millis() - lastLoopOverrunMs) > 10000UL) {
        setDiagnosticFault(DIAG_LOOP_OVERRUN, false);
        haveLoopOverrun = false;
    }
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
    loopTiming = {};
    for (auto &entry : loopStages) {
        entry.maximumUs = entry.pendingWorstUs = 0;
        entry.samples = entry.overBudget = 0;
        entry.average = {};
    }
    resetLoopTraceState();
    loopOverrunCount = 0;
	worstLoopDt = 0;
	print("诊断历史计数已清理；当前故障仍保留。\n");
}

static void printDiagnosticContextLine() {
	extern bool armed, imuOK, motorOutputsOK;
	extern float dt, loopRate, batteryVoltage, controlRoll, controlPitch, controlYaw, controlThrottle;
	extern double t, controlTime;
	print("DIAG_CONTEXT uptime_ms=%lu armed=%u imu_ok=%u motor_ok=%u battery_v=%.2f rc_age_s=%.3f rc=(%.2f,%.2f,%.2f,%.2f) loop_rate=%.0f dt=%.4f free_heap=%lu\n",
		(unsigned long)millis(), armed ? 1 : 0, imuOK ? 1 : 0, motorOutputsOK ? 1 : 0,
		batteryVoltage, controlTime > 0 ? t - controlTime : -1.0f,
		controlRoll, controlPitch, controlYaw, controlThrottle, loopRate, dt,
		(unsigned long)ESP.getFreeHeap());
}

static void printDiagnosticFaultSummaryLine() {
	print("故障诊断 active=0x%08lx loop_overruns=%lu worst_dt=%.4fs\n",
		(unsigned long)getActiveDiagnosticFaults(), (unsigned long)loopOverrunCount, worstLoopDt);
}

void printDiagnosticsBrief() {
	updateDiagnostics();
	extern bool armed, imuOK, motorOutputsOK;
	extern float batteryVoltage;
	const uint32_t batteryMilliVolts = isfinite(batteryVoltage) && batteryVoltage > 0.0f
		? (uint32_t)(batteryVoltage * 1000.0f) : 0;
	print("PREFLIGHT armed=%u imu_ok=%u motor_ok=%u battery_mv=%lu faults=0x%08lx\n",
		armed ? 1 : 0, imuOK ? 1 : 0, motorOutputsOK ? 1 : 0,
		(unsigned long)batteryMilliVolts, (unsigned long)getActiveDiagnosticFaults());
}

void printDiagnostics() {
	updateDiagnostics();
	printDiagnosticContextLine();
    const uint32_t p99 = loopTiming.p99UpperUs();
    char p99Label[24];
    if (!loopTiming.samples) snprintf(p99Label, sizeof(p99Label), "no_samples");
    else if (p99) snprintf(p99Label, sizeof(p99Label), "<=%lu", (unsigned long)p99);
    else snprintf(p99Label, sizeof(p99Label), ">10000");
    print("LOOP_TIMING samples=%llu invalid=%llu max_us=%lu over_1000=%llu over_1500=%llu missed_slots=%llu p99_bucket_us=%s\n",
        (unsigned long long)loopTiming.samples, (unsigned long long)loopTiming.invalid,
        (unsigned long)loopTiming.maximumUs, (unsigned long long)loopTiming.over1000,
        (unsigned long long)loopTiming.over1500, (unsigned long long)loopTiming.missedSlots, p99Label);
	// Keep the interactive diagnostic command bounded: printing one line per
	// stage on the control-loop task caused the probe itself to create overruns.
	// Detailed per-overrun measurements remain available from trace.csv.
	print("LOOP_EMA_US imu=%lu wait=%lu process=%lu rc=%lu est=%lu adc=%lu pid=%lu motor=%lu serial=%lu mav=%lu log=%lu param=%lu led=%lu diag=%lu wifi=%lu gap=%lu\n",
		(unsigned long)loopStages[LOOP_STAGE_IMU].average.roundedUs(),
		(unsigned long)loopStages[LOOP_STAGE_IMU_WAIT].average.roundedUs(),
		(unsigned long)loopStages[LOOP_STAGE_IMU_PROCESS].average.roundedUs(),
		(unsigned long)loopStages[LOOP_STAGE_RC_WEB].average.roundedUs(),
		(unsigned long)loopStages[LOOP_STAGE_ESTIMATE].average.roundedUs(),
		(unsigned long)loopStages[LOOP_STAGE_BATTERY_ADC].average.roundedUs(),
		(unsigned long)loopStages[LOOP_STAGE_CONTROL_LAW].average.roundedUs(),
		(unsigned long)loopStages[LOOP_STAGE_MOTOR_OUT].average.roundedUs(),
		(unsigned long)loopStages[LOOP_STAGE_SERIAL_INPUT].average.roundedUs(),
		(unsigned long)loopStages[LOOP_STAGE_MAVLINK].average.roundedUs(),
		(unsigned long)loopStages[LOOP_STAGE_FLIGHT_LOG].average.roundedUs(),
		(unsigned long)loopStages[LOOP_STAGE_PARAM_SYNC].average.roundedUs(),
		(unsigned long)loopStages[LOOP_STAGE_LED].average.roundedUs(),
		(unsigned long)loopStages[LOOP_STAGE_DIAGNOSTICS].average.roundedUs(),
		(unsigned long)loopStages[LOOP_STAGE_WIFI_SERVICE].average.roundedUs(),
		(unsigned long)loopStages[LOOP_STAGE_LOOP_GAP].average.roundedUs());
	print("LOOP_TRACE count=%u overwritten=%lu capacity=%u csv=/diag/trace.csv\n",
		(unsigned)getLoopTraceCount(), (unsigned long)getLoopTraceOverwrittenCount(), LOOP_TRACE_CAPACITY);
	bool any = false;
	printDiagnosticFaultSummaryLine();
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
