// 故障安全保护
// Fail-safe functions

#include "diagnostics.h"
#include "control.h"

bool isInverted = false;  // 当前机身是否处于倒置（Z轴cos < INVERTED_COS_THRESHOLD）

float rcLossTimeout = 1;        // RC丢失超时时间（秒），可通过参数 SF_RC_LOSS_TIME 配置
float descendTime = 3;          // 过渡到目标下降推力的时间（秒）
float descendThrust = 0.35f;     // 自动下降目标推力（归一化指令，需按机体实测调整）
static bool controlledLandingActive = false;
static double lastDescendUpdateTime = NAN;
#define WEB_RC_LOSS_TIMEOUT_MS 8000UL  // Web遥控器失联阈值(ms)，必须大于心跳间隔2000ms

// 倒置保护参数
#define INVERTED_COS_THRESHOLD -0.7f   // cos(134°)，倾角超过134°视为倒置（留出陀螺漂移裕量）
#define INVERTED_TIMEOUT        1.5f   // 持续倒置超过1.5秒触发停机

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

void failsafe() {
	updateDiagnostics();
	if (armed && (getActiveDiagnosticFaults() &
		(DIAG_IMU_INIT | DIAG_IMU_TIMEOUT | DIAG_IMU_INVALID | DIAG_MOTOR_INIT))) {
		// 关键传感器或输出异常时立即停止输出，不能继续依赖常规控制计算。
		disarm(DISARM_REASON_CRITICAL_FAULT);
		extern float motors[4];
		print("严重故障，立即停机；运行 diag 查看原因。\n");
	}
	rcLossFailsafe();
#if WEB_RC_ENABLED
	webRCLossFailsafe();
#endif
	autoFailsafe();
	invertedFailsafe();
	batteryFailsafe();
	if (armed && controlledLandingActive) descend();
}

// RC loss failsafe
void rcLossFailsafe() {
	if (controlTime == 0) return; // no RC at all
	if (!armed) return;
	if (mode == AUTO) return; // AUTO has an independent external-target timeout.
#if WEB_RC_ENABLED
	if (isUsingWebRC()) return; // WebRC独立负责其超时（webRCLossFailsafe）
#endif
	if (t - controlTime > rcLossTimeout) {
		descend();
	}
}

// Smooth descend on RC lost. Without a height/vertical-speed sensor this is
// only a conservative fixed-thrust descent, not closed-loop speed control.
void descend() {
	const bool firstLandingFrame = !controlledLandingActive;
	controlledLandingActive = true;
	setCurrentControlSource(CONTROL_SOURCE_LANDING);
	if (firstLandingFrame) {
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
	// 每帧跟随实际偏航，防止偏航PID在降落过程中重新累积误差
	float currentYaw = attitude.getYaw();
	attitudeTarget = Quaternion::fromEuler(Vector(0, 0, currentYaw));
	if (!isfinite(t) || !isfinite(dt) || dt <= 0 || lastDescendUpdateTime == t) return;
	lastDescendUpdateTime = t;

	float targetThrust = max(motThrMin, min(descendThrust, ALTHOLD_HOVER_THRUST));
	float maxStep = dt / max(descendTime, 0.1f) * ALTHOLD_HOVER_THRUST;
	if (thrustTarget > targetThrust) thrustTarget = max(targetThrust, thrustTarget - maxStep);
	else if (thrustTarget < targetThrust) thrustTarget = min(targetThrust, thrustTarget + maxStep);
}

bool isControlledLandingActive() {
	return controlledLandingActive;
}

void clearControlledLanding() {
	controlledLandingActive = false;
	lastDescendUpdateTime = NAN;
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
	if (!webRCEnabled || !useWebRC) return;
	if (!armed) return;

	// 使用毫秒直接比较，避免整数除法引入的最大1秒误差
	const unsigned long nowMs = millis();
	const unsigned long linkAgeMs = nowMs - webRCLastUpdate;
	const unsigned long stickAgeMs = nowMs - webRCLastStickUpdate;
	if (linkAgeMs <= WEB_RC_LOSS_TIMEOUT_MS && stickAgeMs <= WEB_RC_LOSS_TIMEOUT_MS) {
		timeoutHandled = false;
		return;
	}
	// The 10 s active-source timeout is longer than this failsafe threshold. Without a latch,
	// readWebRC() re-enables the stale link in that gap and repeats this work
	// on every control-loop iteration until the longer timeout expires.
	if (timeoutHandled) return;
	timeoutHandled = true;
	setDiagnosticFault(DIAG_WEB_RC_LOSS, true);
	if (mode == AUTO && autoTargetReady()) {
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

// 倒置保护：机体Z轴与世界Z轴夹角超过120°持续1.5秒则停机
void invertedFailsafe() {
	if (!armed) {
		isInverted = false;
		return;
	}

	// 取机体Z轴在世界系的Z分量：正立时≈+1，倒置时≈-1
	Vector worldUp = Quaternion::rotateVector(Vector(0, 0, 1), attitude);

	static double invertedStartTime = 0;
	if (worldUp.z < INVERTED_COS_THRESHOLD) {
		isInverted = true;
		if (invertedStartTime == 0) invertedStartTime = t;
		if (t - invertedStartTime > INVERTED_TIMEOUT) {
			disarm(DISARM_REASON_INVERTED);
			invertedStartTime = 0;
			print("倒置保护：停机\n");
		}
	} else {
		isInverted = false;
		invertedStartTime = 0;
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
