// 故障安全保护
// Fail-safe functions

#include "diagnostics.h"
#include "control.h"
#include "external_sensors.h"
#include "landing_barometer_guard.h"
#include "system_log.h"

#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif

bool isInverted = false;  // 当前机身是否处于倒置（Z轴cos < INVERTED_COS_THRESHOLD）

float rcLossTimeout = 1;        // RC丢失超时时间（秒），可通过参数 SF_RC_LOSS_TIME 配置
float descendTime = 3;          // 过渡到目标下降推力的时间（秒）
float descendThrust = 0.42f;     // 自动下降目标推力；按实飞将悬停推力缺口约减半
static bool controlledLandingActive = false;
static double lastDescendUpdateTime = NAN;
static float recentPoweredThrust = NAN;
static uint32_t recentPoweredAtMs = 0;
static constexpr uint32_t LANDING_THRUST_HANDOFF_MAX_AGE_MS = 200;
static constexpr float LANDING_THRUST_HANDOFF_MIN = 0.15f;
static constexpr float LANDING_IDLE_THRUST_MAX = 0.01f;
static constexpr uint32_t CONTROLLED_LANDING_MAX_MS = 8000;
#define WEB_RC_LOSS_TIMEOUT_MS 8000UL  // Web遥控器失联阈值(ms)，必须大于心跳间隔2000ms
static constexpr uint32_t WEB_RC_HARD_STOP_TIMEOUT_MS =
	WEB_RC_LOSS_TIMEOUT_MS + CONTROLLED_LANDING_MAX_MS;
static constexpr uint32_t MOTOR_OUTPUT_STALE_TIMEOUT_MS = 250;
static constexpr uint32_t IMU_LOSS_LANDING_TIMEOUT_MS = 100;
static const float FREE_FALL_ACCEL_THRESHOLD = ONE_G * 0.30f;
static constexpr uint32_t FREE_FALL_CONFIRM_MS = 50;

// 倒置保护参数
#define INVERTED_COS_THRESHOLD -0.7f   // cos(134°)，倾角超过134°视为倒置（留出陀螺漂移裕量）
#define INVERTED_TIMEOUT_MS     500U   // 持续倒置0.5秒即停机，避免坠地堵转烧机

RTC_DATA_ATTR static uint32_t controlledLandingStartedMs = 0;
RTC_DATA_ATTR static uint32_t landingHardStopDeadlineMs = 0;
RTC_DATA_ATTR static uint32_t webRcHardStopIssuedForUpdateMs = 0;
RTC_DATA_ATTR static uint32_t loopStallFailsafeRequested = 0;
RTC_DATA_ATTR static uint32_t loopStallFailsafeIssued = 0;
RTC_DATA_ATTR static uint32_t loopStallHardStopDeadlineMs = 0;
#if defined(ARDUINO_ARCH_ESP32)
RTC_DATA_ATTR static bool safetyHardStopTaskReady = false;
#else
static bool safetyHardStopTaskReady = true;
#endif
extern bool armed;

[[maybe_unused]] static bool takeExpiredSafetyDeadline(uint32_t *deadline, uint32_t nowMs) {
	uint32_t expected = __atomic_load_n(deadline, __ATOMIC_ACQUIRE);
	if (!expected || (int32_t)(nowMs - expected) < 0) return false;
	return __atomic_compare_exchange_n(deadline, &expected, 0, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

bool setupSafetyHardStopTask() {
	controlledLandingStartedMs = 0;
	landingHardStopDeadlineMs = 0;
	webRcHardStopIssuedForUpdateMs = 0;
	loopStallFailsafeRequested = 0;
	loopStallFailsafeIssued = 0;
	loopStallHardStopDeadlineMs = 0;
	safetyHardStopTaskReady = false;
#if defined(ARDUINO_ARCH_ESP32)
	extern void latchMotorEmergencyCutoff(DisarmReason reason);
	extern bool motorEmergencyCutoffLatched();
	extern bool motorOutputRefreshExpired(uint32_t nowMs, uint32_t timeoutMs);
	const BaseType_t created = xTaskCreatePinnedToCore([](void*) {
		for (;;) {
			const uint32_t nowMs = millis();
			if (armed && takeExpiredSafetyDeadline(&landingHardStopDeadlineMs, nowMs))
				latchMotorEmergencyCutoff(DISARM_REASON_LANDING_TIMEOUT);
			if (armed && takeExpiredSafetyDeadline(&loopStallHardStopDeadlineMs, nowMs))
				latchMotorEmergencyCutoff(DISARM_REASON_LOOP_STALL);
#if WEB_RC_ENABLED
			extern bool useWebRC;
			extern unsigned long webRCLastUpdate;
			const uint32_t lastWebUpdate = __atomic_load_n(&webRCLastUpdate, __ATOMIC_ACQUIRE);
			if (armed && useWebRC && lastWebUpdate &&
				(uint32_t)(nowMs - lastWebUpdate) >= WEB_RC_HARD_STOP_TIMEOUT_MS &&
				webRcHardStopIssuedForUpdateMs != lastWebUpdate) {
				webRcHardStopIssuedForUpdateMs = lastWebUpdate;
				latchMotorEmergencyCutoff(DISARM_REASON_WEB_RC_LOSS);
			} else if (!armed) {
				webRcHardStopIssuedForUpdateMs = 0;
			}
#endif
			const bool outputStale = armed && !motorEmergencyCutoffLatched() &&
				motorOutputRefreshExpired(nowMs, MOTOR_OUTPUT_STALE_TIMEOUT_MS);
			if (outputStale) {
				if (__atomic_exchange_n(&loopStallFailsafeIssued, 1, __ATOMIC_ACQ_REL) == 0) {
					__atomic_store_n(&loopStallFailsafeRequested, 1, __ATOMIC_RELEASE);
					__atomic_store_n(&loopStallHardStopDeadlineMs,
						nowMs + CONTROLLED_LANDING_MAX_MS, __ATOMIC_RELEASE);
				}
			} else if (!armed) {
				__atomic_store_n(&loopStallFailsafeRequested, 0, __ATOMIC_RELEASE);
				__atomic_store_n(&loopStallFailsafeIssued, 0, __ATOMIC_RELEASE);
				__atomic_store_n(&loopStallHardStopDeadlineMs, 0, __ATOMIC_RELEASE);
			} else {
				__atomic_store_n(&loopStallFailsafeIssued, 0, __ATOMIC_RELEASE);
			}
			vTaskDelay(pdMS_TO_TICKS(5));
		}
	}, "flight_hard_stop", 2048, nullptr, 3, nullptr, 0);
	const bool ready = created == pdPASS;
#else
	const bool ready = true;
#endif
	safetyHardStopTaskReady = ready;
	return ready;
}

bool safetyHardStopReady() {
	return safetyHardStopTaskReady;
}

void clearSafetyHardStopDeadlines() {
	__atomic_store_n(&landingHardStopDeadlineMs, 0, __ATOMIC_RELEASE);
	__atomic_store_n(&loopStallFailsafeRequested, 0, __ATOMIC_RELEASE);
	__atomic_store_n(&loopStallFailsafeIssued, 0, __ATOMIC_RELEASE);
	__atomic_store_n(&loopStallHardStopDeadlineMs, 0, __ATOMIC_RELEASE);
}

static bool takeLoopStallFailsafeRequest() {
	return __atomic_exchange_n(&loopStallFailsafeRequested, 0, __ATOMIC_ACQ_REL) != 0;
}

extern double controlTime;
extern float controlRoll, controlPitch, controlThrottle, controlYaw;

#if WEB_RC_ENABLED
// Web RC变量声明
extern bool webRCEnabled;
extern bool useWebRC;
extern unsigned long webRCLastUpdate;
extern unsigned long webRCLastStickUpdate;
bool isUsingWebRC();
#endif

extern bool armed;
extern int mode;
extern float dt;
extern float thrustTarget;
extern double t;
extern float batteryVoltage;  // battery.ino
extern Quaternion attitudeTarget;
extern Quaternion attitude;
extern Vector ratesExtra;      // control.ino
extern PID rollRatePID, pitchRatePID, yawRatePID;  // control.ino
extern PID rollPID, pitchPID, yawPID;              // control.ino
extern float motThrMin;        // control.ino

void freeFallFailsafe();
void imuLossFailsafe();

void failsafe() {
	extern bool motorEmergencyCutoffLatched();
	if (armed && motorEmergencyCutoffLatched()) {
		extern DisarmReason getMotorEmergencyCutoffReason();
		const DisarmReason reason = getMotorEmergencyCutoffReason();
		disarm(reason == DISARM_REASON_UNKNOWN ? DISARM_REASON_CRITICAL_FAULT : reason);
		print("独立安全停机已触发，reason=%u，飞控状态同步上锁\n", (unsigned)reason);
		return;
	}
	if (armed && takeLoopStallFailsafeRequest()) {
		recordSystemLogEvent("LOOP_STALL", "main_loop_resumed_controlled_landing");
		print("主循环停顿后恢复，进入受控迫降\n");
		descend();
		// descend() has installed its own independent maximum-duration cutoff.
		// Cancel the fallback used only while the main loop remained frozen.
		if (armed && controlledLandingActive)
			__atomic_store_n(&loopStallHardStopDeadlineMs, 0, __ATOMIC_RELEASE);
		if (!armed) return;
	}
	// Keep the last powered command briefly so releasing the Web throttle to
	// press Land cannot make the landing ramp start from zero.
	if (armed && !controlledLandingActive && isfinite(thrustTarget) &&
		thrustTarget >= LANDING_THRUST_HANDOFF_MIN) {
		recentPoweredThrust = thrustTarget;
		recentPoweredAtMs = millis();
	}
	updateDiagnostics();
	const uint32_t activeFaults = getActiveDiagnosticFaults();
	if (armed && (activeFaults & (DIAG_IMU_INIT | DIAG_MOTOR_INIT))) {
		// 初始化能力或电机输出层失效时无法执行受控动作。
		disarm(DISARM_REASON_CRITICAL_FAULT);
		print("IMU初始化或电机输出故障，立即停机；运行 diag 查看原因。\n");
		return;
	}
	imuLossFailsafe();
	rcLossFailsafe();
#if WEB_RC_ENABLED
	webRCLossFailsafe();
#endif
	autoFailsafe();
	freeFallFailsafe();
	invertedFailsafe();
	batteryFailsafe();
	if (armed && controlledLandingActive) descend();
}

void imuLossFailsafe() {
	if (!armed) return;
	extern uint32_t imuValidSampleAgeMs(uint32_t nowMs);
	const uint32_t ageMs = imuValidSampleAgeMs(millis());
	if (ageMs < IMU_LOSS_LANDING_TIMEOUT_MS) return;
	if (!controlledLandingActive) {
		char event[40];
		snprintf(event, sizeof(event), "sample_age_ms=%lu", (unsigned long)ageMs);
		recordSystemLogEvent("IMU_LOSS", event);
		print("IMU有效样本持续中断，进入有时限迫降\n");
	}
	descend();
}

// Near-zero specific force means the airframe is in ballistic motion. Require
// a short continuous window to reject isolated IMU samples, then immediately
// hand control to the existing bounded landing path.
void freeFallFailsafe() {
	static uint32_t freeFallStartedMs = 0;
	extern Vector acc;
	extern bool imuSampleValid;
	if (!armed || controlledLandingActive || !imuSampleValid || !acc.valid()) {
		freeFallStartedMs = 0;
		return;
	}
	const float acceleration = acc.norm();
	if (!isfinite(acceleration) || acceleration > FREE_FALL_ACCEL_THRESHOLD) {
		freeFallStartedMs = 0;
		return;
	}
	const uint32_t nowMs = millis();
	if (!freeFallStartedMs) {
		freeFallStartedMs = nowMs ? nowMs : 1;
		return;
	}
	if ((uint32_t)(nowMs - freeFallStartedMs) < FREE_FALL_CONFIRM_MS) return;
	char event[48];
	snprintf(event, sizeof(event), "accel_milli=%d confirm_ms=%lu",
		(int)(acceleration * 1000.0f),
		(unsigned long)(nowMs - freeFallStartedMs));
	recordSystemLogEvent("FREE_FALL", event);
	print("检测到自由落体，进入受控迫降\n");
	freeFallStartedMs = 0;
	descend();
}

// RC loss failsafe
void rcLossFailsafe() {
	static bool freshRCSeenWhileArmed = false;
	if (!armed) {
		freshRCSeenWhileArmed = false;
		return;
	}
	if (controlTime == 0) return; // no RC at all
	if (mode == AUTO) return; // AUTO has an independent external-target timeout.
	const ControlSource source = getCurrentControlSource();
	if (source == CONTROL_SOURCE_LOCAL_SEQUENCE ||
		source == CONTROL_SOURCE_EXTERNAL_ATTITUDE ||
		source == CONTROL_SOURCE_EXTERNAL_MOTORS ||
		source == CONTROL_SOURCE_LANDING)
		return;
#if WEB_RC_ENABLED
	if (isUsingWebRC()) return; // WebRC独立负责其超时（webRCLossFailsafe）
#endif
	const double rcAge = t - controlTime;
	if (rcAge <= rcLossTimeout) {
		freshRCSeenWhileArmed = true;
		return;
	}
	const uint32_t handoffAgeMs = millis() - recentPoweredAtMs;
	const bool recentPoweredHandoff =
		isfinite(recentPoweredThrust) &&
		handoffAgeMs <= LANDING_THRUST_HANDOFF_MAX_AGE_MS;
	if (!freshRCSeenWhileArmed && !recentPoweredHandoff) {
		if (isfinite(thrustTarget) && thrustTarget > LANDING_IDLE_THRUST_MAX) {
			disarm(DISARM_REASON_UNKNOWN);
			print("RC输入为解锁前旧数据，非零输出已安全上锁\n");
			return;
		}
		controlTime = 0; // discard stale pre-arm RC time so idle arm cannot ramp into landing
		return;
	}
	if (isfinite(thrustTarget) && thrustTarget <= LANDING_IDLE_THRUST_MAX &&
		!recentPoweredHandoff) {
		controlTime = 0; // zero-throttle idle loss is not a landing handoff
		return;
	}
	descend();
}

// Smooth descend on RC loss. Fixed thrust remains the fallback; fresh
// barometer feedback can only add a small, bounded fast-descent correction.
void descend() {
	const bool firstLandingFrame = !controlledLandingActive;
	if (firstLandingFrame) {
		const float entryThrust = thrustTarget;
		const uint32_t handoffAgeMs = millis() - recentPoweredAtMs;
		const bool poweredEntry = isfinite(entryThrust) && entryThrust > LANDING_IDLE_THRUST_MAX;
		const bool recentPoweredHandoff =
			isfinite(recentPoweredThrust) &&
			recentPoweredThrust >= LANDING_THRUST_HANDOFF_MIN &&
			handoffAgeMs <= LANDING_THRUST_HANDOFF_MAX_AGE_MS;
		if (armed && !poweredEntry && !recentPoweredHandoff) {
			recordSystemLogEvent("LANDING", "idle_entry_disarm");
			disarm(DISARM_REASON_FAILSAFE_IDLE);
			print("下降请求发生在零油门怠速，已立即停机上锁\n");
			return;
		}
		controlledLandingActive = true;
		controlledLandingStartedMs = millis();
		__atomic_store_n(&landingHardStopDeadlineMs,
			controlledLandingStartedMs + CONTROLLED_LANDING_MAX_MS, __ATOMIC_RELEASE);
		setCurrentControlSource(CONTROL_SOURCE_LANDING);
		bool restored = false;
		if (armed && isfinite(entryThrust) && entryThrust < LANDING_THRUST_HANDOFF_MIN &&
			recentPoweredHandoff) {
			thrustTarget = min(recentPoweredThrust, ALTHOLD_HOVER_THRUST);
			restored = true;
		}
		char event[96];
		snprintf(event, sizeof(event), "entry_milli=%d start_milli=%d restored=%u age_ms=%lu",
			isfinite(entryThrust) ? (int)(entryThrust * 1000.0f) : -1,
			isfinite(thrustTarget) ? (int)(thrustTarget * 1000.0f) : -1,
			restored ? 1U : 0U,
			(unsigned long)(restored ? handoffAgeMs : 0));
		recordSystemLogEvent("LANDING", event);
		BarometerEstimate landingEstimate;
		const bool haveLandingEstimate = getBarometerEstimate(landingEstimate);
		char barometerEvent[44];
		snprintf(barometerEvent, sizeof(barometerEvent), "ready=%u rel_cm=%d vz_cms=%d",
			haveLandingEstimate && barometerEstimateUsable(landingEstimate, micros(),
				LANDING_BARO_MAX_AGE_US) ? 1U : 0U,
			haveLandingEstimate && isfinite(landingEstimate.relativeAltitudeMeters) ?
				(int)(landingEstimate.relativeAltitudeMeters * 100.0f) : 0,
			haveLandingEstimate && isfinite(landingEstimate.verticalSpeedMps) ?
				(int)(landingEstimate.verticalSpeedMps * 100.0f) : 0);
		recordSystemLogEvent("LAND_BARO", barometerEvent);
		// 首次进入：保持当前偏航（仅强制机体水平），清零速率前馈，重置PID积分
		float currentYaw = attitude.getYaw();
		attitudeTarget = Quaternion::fromEuler(Vector(0, 0, currentYaw));
		ratesExtra = Vector(0, 0, 0);
		rollRatePID.reset();
		pitchRatePID.reset();
		yawRatePID.reset();
		rollPID.reset();
		pitchPID.reset();
		yawPID.reset();
		mode = AUTO;
	}
	if (armed && controlledLandingStartedMs &&
		(uint32_t)(millis() - controlledLandingStartedMs) >= CONTROLLED_LANDING_MAX_MS) {
		recordSystemLogEvent("LANDING", "maximum_duration_disarm");
		disarm(DISARM_REASON_LANDING_TIMEOUT);
		print("迫降达到最大持续时间，已强制停机\n");
		return;
	}
	// 每帧跟随实际偏航，防止偏航PID在降落过程中重新累积误差
	float currentYaw = attitude.getYaw();
	attitudeTarget = Quaternion::fromEuler(Vector(0, 0, currentYaw));
	if (!isfinite(t) || !isfinite(dt) || dt <= 0 || lastDescendUpdateTime == t) return;
	lastDescendUpdateTime = t;

	float targetThrust = max(motThrMin, min(descendThrust, ALTHOLD_HOVER_THRUST));
	BarometerEstimate barometerEstimate;
	if (getBarometerEstimate(barometerEstimate)) {
		targetThrust = min(ALTHOLD_HOVER_THRUST, targetThrust +
			landingBarometerThrustCorrection(barometerEstimate, micros()));
	}
	float maxStep = dt / max(descendTime, 0.1f) * ALTHOLD_HOVER_THRUST;
	if (thrustTarget > targetThrust) thrustTarget = max(targetThrust, thrustTarget - maxStep);
	else if (thrustTarget < targetThrust) thrustTarget = min(targetThrust, thrustTarget + maxStep);
}

bool isControlledLandingActive() {
	return controlledLandingActive;
}

void clearControlledLanding() {
	controlledLandingActive = false;
	controlledLandingStartedMs = 0;
	__atomic_store_n(&landingHardStopDeadlineMs, 0, __ATOMIC_RELEASE);
	lastDescendUpdateTime = NAN;
	recentPoweredThrust = NAN;
	recentPoweredAtMs = 0;
}

// Allow pilot to interrupt automatic flight
void autoFailsafe() {
	if (!armed || mode != AUTO) {
		setDiagnosticFault(DIAG_AUTO_TARGET_TIMEOUT, false);
		return;
	}
	if (controlledLandingActive) {
		return;
	}
	#if WEB_RC_ENABLED
	extern bool isLocalSequenceRunning();
	extern bool isLocalSequenceReadyForAuto();
	if (isLocalSequenceRunning() || isLocalSequenceReadyForAuto()) {
		setDiagnosticFault(DIAG_AUTO_TARGET_TIMEOUT, false);
		return;
	}
	#endif
	if (autoTargetTimedOut()) {
		setDiagnosticFault(DIAG_AUTO_TARGET_TIMEOUT, true);
		descend();
	} else {
		setDiagnosticFault(DIAG_AUTO_TARGET_TIMEOUT, false);
	}
}

#if WEB_RC_ENABLED
// Web遥控器丢失保护
void webRCLossFailsafe() {
	static bool timeoutHandled = false;
	static bool freshStickSeenWhileArmed = false;
	if (!armed) {
		freshStickSeenWhileArmed = false;
		timeoutHandled = false;
		return;
	}
	if (!webRCEnabled || !useWebRC) {
		freshStickSeenWhileArmed = false;
		timeoutHandled = false;
		return;
	}

	// 使用毫秒直接比较，避免整数除法引入的最大1秒误差
	const unsigned long nowMs = millis();
	const unsigned long linkAgeMs = nowMs - webRCLastUpdate;
	const unsigned long stickAgeMs = nowMs - webRCLastStickUpdate;
	if (linkAgeMs <= WEB_RC_LOSS_TIMEOUT_MS && stickAgeMs <= WEB_RC_LOSS_TIMEOUT_MS) {
		timeoutHandled = false;
		freshStickSeenWhileArmed = true;
		return;
	}
	// The 10 s active-source timeout is longer than this failsafe threshold. Without a latch,
	// readWebRC() re-enables the stale link in that gap and repeats this work
	// on every control-loop iteration until the longer timeout expires.
	if (timeoutHandled) return;
	timeoutHandled = true;
	if (!freshStickSeenWhileArmed) {
		print("Web RC摇杆输入已过期，忽略本次旧连接\n");
		webRCEnabled = false;
		useWebRC = false;
		return;
	}
	setDiagnosticFault(DIAG_WEB_RC_LOSS, true);
	extern bool isLocalSequenceRunning();
	extern bool isLocalSequenceReadyForAuto();
	if (mode == AUTO && !isLocalSequenceRunning() &&
		!isLocalSequenceReadyForAuto() && autoTargetReady()) {
		print("Web RC连接丢失，外部AUTO目标有效，保持AUTO控制\n");
		webRCEnabled = false;
		useWebRC = false;
		return;
	}
	print("Web RC连接或摇杆输入丢失，启动下降\n");
	descend();
	webRCEnabled = false;
	useWebRC = false;
}
#endif

// 倒置保护：持续倒置后请求水平恢复和有时限迫降，不在空中直接停桨。
void invertedFailsafe() {
	static uint32_t invertedStartedMs = 0;
	static bool invertedActionLogged = false;
	if (!armed) {
		isInverted = false;
		invertedStartedMs = 0;
		invertedActionLogged = false;
		return;
	}

	// 取机体Z轴在世界系的Z分量：正立时≈+1，倒置时≈-1
	Vector worldUp = Quaternion::rotateVector(Vector(0, 0, 1), attitude);

	if (worldUp.z < INVERTED_COS_THRESHOLD) {
		isInverted = true;
		if (invertedStartedMs == 0) {
			invertedStartedMs = millis() ? millis() : 1;
		}
		if ((uint32_t)(millis() - invertedStartedMs) >= INVERTED_TIMEOUT_MS) {
			if (!invertedActionLogged) {
				recordSystemLogEvent("INVERTED", "controlled_landing");
				print("持续倒置，尝试水平恢复并进入迫降\n");
				invertedActionLogged = true;
			}
			if (!controlledLandingActive) descend();
		}
	} else {
		isInverted = false;
		invertedStartedMs = 0;
		invertedActionLogged = false;
	}
}

// 电池电压保护
// L1（3.4V）：未解锁禁止解锁（在 control.ino 处理），怠速时自动上锁
// L2（2.8V）：飞行中仅 LED 快闪告警
// L3（2.6V）：飞行中自动降落（复用固定目标推力的 descend()）
void batteryFailsafe() {
	static bool l3Latched = false;
	static double lowSince = 0.0f;
	static double l3LastNotify = 0.0f;

	if (batteryVoltage < VBAT_ABSENT_THRESHOLD) return; // 未接电池，忽略
	if (!armed) {
		l3Latched = false;
		lowSince = 0.0f;
		l3LastNotify = 0.0f;
		return;
	}

	// L3 已触发后保持降落，直到上锁，避免阈值附近反复进出
	if (l3Latched) {
		if (t - l3LastNotify >= 2.0f) {
			print("电池电量告急(%.2fV)，持续降落，推力%.0f%%\n",
			      batteryVoltage, thrustTarget * 100.0f);
#if WEB_RC_ENABLED
			char warnBuf[64];
			snprintf(warnBuf, sizeof(warnBuf),
			         "电池电量告急(%.2fV) 持续降落 推力%.0f%%",
			         batteryVoltage, thrustTarget * 100.0f);
			setWebRCWarn(warnBuf);
#endif
			l3LastNotify = t;
		}
		descend();
		return;
	}

	bool flying = thrustTarget >= BATTERY_FLYING_THRUST_MIN;

	bool actionCondition = false;
	bool criticalAction = false;

	// 飞行中：仅 L3 触发强动作；L2 继续由 LED 告警
	if (flying) {
		if (batteryVoltage < VBAT_CRITICAL_THRESHOLD) {
			actionCondition = true;
			criticalAction = true;
		}
	} else {
		// 解锁怠速：L1 触发自动上锁
		if (batteryVoltage < VBAT_WARN_THRESHOLD) {
			actionCondition = true;
		}
	}

	if (!actionCondition) {
		lowSince = 0.0f;
		return;
	}

	if (lowSince == 0.0f) lowSince = t;
	if (t - lowSince < BATTERY_ACTION_DEBOUNCE_TIME) return;

	lowSince = 0.0f;
	if (criticalAction) {
		l3Latched = true;
		l3LastNotify = 0.0f;
		print("电池电量告急(%.2fV)，进入自动降落\n", batteryVoltage);
#if WEB_RC_ENABLED
		char warnBuf[64];
		snprintf(warnBuf, sizeof(warnBuf),
		         "电池电量告急(%.2fV) 进入自动降落",
		         batteryVoltage);
		setWebRCWarn(warnBuf);
#endif
		descend();
		return;
	}

	disarm(DISARM_REASON_BATTERY_IDLE_LOW);
	print("电量低(%.2fV)，自动上锁\n", batteryVoltage);
#if WEB_RC_ENABLED
	char warnBuf[64];
	snprintf(warnBuf, sizeof(warnBuf), "电量低(%.2fV) 已自动上锁", batteryVoltage);
	setWebRCWarn(warnBuf);
#endif
}
