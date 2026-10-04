// 飞行控制

#include "vector.h"
#include "quaternion.h"
#include "pid.h"
#include "lpf.h"
#include "util.h"
#include "diagnostics.h"
#include "system_log.h"
#include "control.h"

extern bool isLevelCalibrationActive();
extern bool parameterPersistencePending();
extern bool imuRotationRestartPending();

// 参数适配118mm轴距的微型四轴飞行器
// ============== 角速率环（内环）参数 ==============
#define PITCHRATE_P 0.06 // 增大P值提高响应速度
#define PITCHRATE_I 0.1 // 中等I值补偿电机差异
#define PITCHRATE_D 0.001 // 小D值抑制震荡
#define PITCHRATE_I_LIM 0.3 // 限制积分积累
#define ROLLRATE_P PITCHRATE_P // 横滚和俯仰使用相同参数
#define ROLLRATE_I PITCHRATE_I 
#define ROLLRATE_D PITCHRATE_D
#define ROLLRATE_I_LIM PITCHRATE_I_LIM
#define YAWRATE_P 0.3 // 偏航需要更高的P值（惯性较小）
#define YAWRATE_I 0.01 // 中等I值补偿
#define YAWRATE_D 0.01 // 小D值
#define YAWRATE_I_LIM 0.05 // 偏航持续偏差的积分输出最多占电机归一化指令的 5%
// ============== 角度环（外环）参数 ==============
#define ROLL_P 6 // 较高的P值快速响应
#define ROLL_I 0 // 外环 I 项
#define ROLL_D 0 // 角度环通常不需要D项
#define ROLL_I_LIM radians(5.0f) // 外环横滚积分限幅（rad/s），约 5°/s，防止低油门/切模式时积分发散
#define PITCH_P ROLL_P // 横滚和俯仰相同
#define PITCH_I ROLL_I
#define PITCH_D ROLL_D
#define PITCH_I_LIM ROLL_I_LIM
#define YAW_P 3 // 偏航角度环增益（用于具备航向参考的 AUTO 目标）

// ============== 限制值 ==============
#define PITCHRATE_MAX radians(360) // 高转速限制（1000°/s）
#define ROLLRATE_MAX radians(360)
#define YAWRATE_MAX radians(300) // 偏航转速稍低
#define TILT_MAX radians(30) // 最大倾斜角30°
#define ALTHOLD_HOVER_THRUST 0.48f  // 实飞松杆后约上升0.10~0.20m/s，保守下调悬停前馈
#define ARM_THROTTLE_LIMIT   0.05f  // 解锁油门上限（归一化后 0~1），5%，超过此值禁止解锁
#define RATES_D_LPF_ALPHA 0.2 // cutoff frequency ~ 40 Hz

float motThrMin = 0.10f;  // 推力下限（摇杆最低位的输出），可通过参数 MOT_THR_MIN 调节
float motThrMax = 0.9f;   // 推力上限（摇杆最高位的输出），可通过参数 MOT_THR_MAX 调节；保留 10% 余量供姿态修正

const int RAW = 0, ACRO = 1, STAB = 2, ALTHOLD = 3, AUTO = 4; // flight modes
int mode = STAB;
bool armed = false;
int flightModes[] = {STAB, STAB, STAB}; // RC模式拨杆三挡对应的飞行模式，可通过参数 CTL_FLT_MODE_0/1/2 配置

#if WEB_RC_ENABLED
extern uint16_t takeWebRCButtonPressEdges(uint16_t *buttons);
#endif

PID rollRatePID(ROLLRATE_P, ROLLRATE_I, ROLLRATE_D, ROLLRATE_I_LIM, RATES_D_LPF_ALPHA);
PID pitchRatePID(PITCHRATE_P, PITCHRATE_I, PITCHRATE_D, PITCHRATE_I_LIM, RATES_D_LPF_ALPHA);
PID yawRatePID(YAWRATE_P, YAWRATE_I, YAWRATE_D, YAWRATE_I_LIM, RATES_D_LPF_ALPHA);
PID rollPID(ROLL_P, ROLL_I, ROLL_D, ROLL_I_LIM);
PID pitchPID(PITCH_P, PITCH_I, PITCH_D, PITCH_I_LIM);
PID yawPID(YAW_P, 0, 0);
Vector maxRate(ROLLRATE_MAX, PITCHRATE_MAX, YAWRATE_MAX);
float tiltMax = TILT_MAX;

Quaternion attitudeTarget;
Vector ratesTarget;
Vector ratesExtra; // feedforward rates
Vector torqueTarget;
float thrustTarget;
float motorMixScale = 1.0f;

float hoverThrottleInput() {
	const float thrustSpan = motThrMax - motThrMin;
	if (!isfinite(thrustSpan) || thrustSpan <= 0.0001f) return 0.5f;
	// Invert the manual throttle mapping used by interpretControls():
	// [0.05, 1.0] input -> [motThrMin, motThrMax] thrust.
	return constrain(0.05f + (ALTHOLD_HOVER_THRUST - motThrMin) /
		thrustSpan * 0.95f, 0.05f, 1.0f);
}

#define AUTO_TARGET_TIMEOUT_MS 500UL
#define AUTO_TARGET_READY_MS 100UL
#define AUTO_TARGET_READY_COUNT 3
#define AUTO_THRUST_MIN 0.0f
#define AUTO_THRUST_MAX 1.0f
#define AUTO_QUAT_NORM_MIN 0.5f
#define AUTO_QUAT_NORM_MAX 1.5f
#define MANUAL_SOURCE_ARBITRATION_MS 250UL

static AutoTargetKind autoTargetKind = AUTO_TARGET_NONE;
static AutoAttitudeCommand autoAttitudeCommand;
static AutoActuatorCommand autoActuatorCommand;
static uint32_t autoTargetFirstValidMs = 0;
static uint32_t autoTargetLastValidMs = 0;
static uint8_t autoTargetValidCount = 0;
static uint32_t autoTargetAppliedMs = 0;
static bool autoTargetAppliedValid = false;
static ControlSource currentControlSource = CONTROL_SOURCE_NONE;
static ControlSource latestManualControlSource = CONTROL_SOURCE_NONE;
static uint32_t physicalRCManualInputMs = 0;
static uint32_t mavlinkManualInputMs = 0;

// ============== 软件配平参数 ==============
// 用于补偿机械不对称（重心偏移、电机/桨叶推力差异、IMU 安装偏斜等）引起的固定方向漂移。
// 单位：弧度（rad）。
//
// 调整方法（串口/Web 控制台，无需重新烧录）：
//   命令格式：p <参数名> <值>
//   每次建议步长 0.005 rad（约 0.3°），逐步收敛至松杆不漂为止。
//   参数自动存储到 Flash，断电后继续生效。
//
// 漂移方向与参数对照（松杆后观察）：
//   飞机向左漂 → p CTL_TRIM_ROLL  +0.01  （正值，命令右倾以产生向右分力抵消左漂）
//   飞机向右漂 → p CTL_TRIM_ROLL  -0.01  （负值，命令左倾以产生向左分力抵消右漂）
//   飞机向前漂 → p CTL_TRIM_PITCH -0.01  （负值，命令抬头以产生向后分力抵消前漂）
//   飞机向后漂 → p CTL_TRIM_PITCH +0.01  （正值，命令低头以产生向前分力抵消后漂）
//
// 注意：电量变化、负载改变后配平值可能略有偏移，需偶尔重调。
float trimRoll  = 0.0f; // 横滚配平角（rad），参数名：CTL_TRIM_ROLL
float trimPitch = 0.0f; // 俯仰配平角（rad），参数名：CTL_TRIM_PITCH

extern const int MOTOR_REAR_LEFT, MOTOR_REAR_RIGHT, MOTOR_FRONT_RIGHT, MOTOR_FRONT_LEFT;
extern float motors[4];
extern void sendMotors();
extern float controlRoll, controlPitch, controlThrottle, controlYaw, controlMode;
extern float batteryVoltage;  // battery.ino
extern bool batteryBlocksArming();
extern bool motorTestActive;
extern bool motorTestArmInhibit;
extern bool imuOK;
extern void descend();
extern void clearControlledLanding();
extern bool isControlledLandingActive();
extern bool isAccelCalibrationActive();

static bool normalizeAutoQuaternion(Quaternion& q) {
	if (!q.finite()) return false;
	float norm = q.norm();
	if (!isfinite(norm) || norm < AUTO_QUAT_NORM_MIN || norm > AUTO_QUAT_NORM_MAX) return false;
	q.normalize();
	return q.finite();
}

static void resetAllPids() {
	rollRatePID.reset();
	pitchRatePID.reset();
	yawRatePID.reset();
	rollPID.reset();
	pitchPID.reset();
	yawPID.reset();
}

void resetControlTargets() {
	attitudeTarget.invalidate();
	ratesTarget.invalidate();
	ratesExtra = Vector(0, 0, 0);
	torqueTarget.invalidate();
	motorMixScale = 1.0f;
}

void resetAutoTargetState() {
	autoTargetKind = AUTO_TARGET_NONE;
	autoTargetFirstValidMs = 0;
	autoTargetLastValidMs = 0;
	autoTargetValidCount = 0;
	autoTargetAppliedMs = 0;
	autoTargetAppliedValid = false;
	setDiagnosticFault(DIAG_AUTO_TARGET_TIMEOUT, false);
}

ControlSource getCurrentControlSource() {
	return currentControlSource;
}

void setCurrentControlSource(ControlSource source) {
	currentControlSource = source;
}

void markManualControlInput(ControlSource source) {
	const uint32_t now = millis();
	if (source == CONTROL_SOURCE_PHYSICAL_RC) {
		physicalRCManualInputMs = now;
		latestManualControlSource = source;
	} else if (source == CONTROL_SOURCE_MAVLINK_MANUAL) {
		mavlinkManualInputMs = now;
		latestManualControlSource = source;
	}
}

static bool manualInputFresh(uint32_t timestampMs, uint32_t nowMs) {
	return timestampMs != 0 &&
		(uint32_t)(nowMs - timestampMs) <= MANUAL_SOURCE_ARBITRATION_MS;
}

static ControlSource selectedManualControlSource() {
#if WEB_RC_ENABLED
	if (isUsingWebRC()) return CONTROL_SOURCE_WEB_RC;
#endif
	if (latestManualControlSource == CONTROL_SOURCE_MAVLINK_MANUAL && mavlinkManualInputMs != 0)
		return CONTROL_SOURCE_MAVLINK_MANUAL;
	if (latestManualControlSource == CONTROL_SOURCE_PHYSICAL_RC && physicalRCManualInputMs != 0)
		return CONTROL_SOURCE_PHYSICAL_RC;
	return CONTROL_SOURCE_NONE;
}

bool canAcceptMavlinkManualControl() {
	if (mode == AUTO || isControlledLandingActive()) return false;
#if WEB_RC_ENABLED
	extern bool isLocalSequenceRunning();
	if (isLocalSequenceRunning()) return false;
	if (isUsingWebRC()) return false;
#endif
	const uint32_t now = millis();
	if (manualInputFresh(physicalRCManualInputMs, now)) return false;
	return true;
}

Vector constrainRatesToConfiguredLimits(const Vector& rates) {
	return Vector(
		constrain(rates.x, -maxRate.x, maxRate.x),
		constrain(rates.y, -maxRate.y, maxRate.y),
		constrain(rates.z, -maxRate.z, maxRate.z));
}

bool ratesWithinConfiguredLimits(const Vector& rates) {
	return rates.finite() &&
		fabsf(rates.x) <= maxRate.x &&
		fabsf(rates.y) <= maxRate.y &&
		fabsf(rates.z) <= maxRate.z;
}

static void markAutoTargetApplied() {
	autoTargetAppliedMs = millis();
	autoTargetAppliedValid = true;
}

bool isSupportedFlightMode(int requestedMode) {
	return requestedMode == RAW || requestedMode == ACRO ||
		requestedMode == STAB || requestedMode == AUTO;
}

static bool applyAutoTarget() {
	if (autoTargetKind == AUTO_TARGET_NONE || !autoTargetReady()) return false;
	if (autoTargetKind == AUTO_TARGET_ATTITUDE) {
		setCurrentControlSource(CONTROL_SOURCE_EXTERNAL_ATTITUDE);
		if (autoAttitudeCommand.useAttitude) {
			attitudeTarget = autoAttitudeCommand.attitude;
			ratesTarget.invalidate();
			ratesExtra = autoAttitudeCommand.useRates ? autoAttitudeCommand.rates : Vector(0, 0, 0);
		} else {
			attitudeTarget.invalidate();
			ratesTarget = autoAttitudeCommand.rates;
			ratesExtra = Vector(0, 0, 0);
		}
		torqueTarget.invalidate();
		thrustTarget = autoAttitudeCommand.thrust;
		markAutoTargetApplied();
		return true;
	}
	if (autoTargetKind == AUTO_TARGET_ACTUATOR) {
		setCurrentControlSource(CONTROL_SOURCE_EXTERNAL_MOTORS);
		attitudeTarget.invalidate();
		ratesTarget.invalidate();
		torqueTarget.invalidate();
		ratesExtra = Vector(0, 0, 0);
		thrustTarget = (autoActuatorCommand.motors[0] + autoActuatorCommand.motors[1] +
			autoActuatorCommand.motors[2] + autoActuatorCommand.motors[3]) * 0.25f;
		if (armed) {
			for (int i = 0; i < 4; ++i) motors[i] = autoActuatorCommand.motors[i];
		}
		markAutoTargetApplied();
		return true;
	}
	return false;
}

bool setFlightMode(int requestedMode) {
	if (!isSupportedFlightMode(requestedMode)) return false;
	#if WEB_RC_ENABLED
	extern bool isLocalSequenceReadyForAuto();
	const bool localAutoReady = requestedMode == AUTO && isLocalSequenceReadyForAuto();
	#else
	const bool localAutoReady = false;
	#endif
	if (requestedMode == AUTO && !localAutoReady && !autoTargetReady()) return false;
	#if WEB_RC_ENABLED
	if (requestedMode == STAB || requestedMode == ACRO) {
		extern bool isLocalSequenceRunning();
		extern void cancelLocalSequenceForManualMode();
		if (isLocalSequenceRunning() || isControlledLandingActive()) cancelLocalSequenceForManualMode();
	}
	#endif

	if (isControlledLandingActive()) {
		if (requestedMode == STAB || requestedMode == ACRO) {
			clearControlledLanding();
		} else {
			return mode == requestedMode;
		}
	}

	if (mode == requestedMode) return true;

	mode = requestedMode;
	resetAllPids();
	resetControlTargets();
	if (mode != AUTO) setDiagnosticFault(DIAG_AUTO_TARGET_TIMEOUT, false);
	if (mode == AUTO && !localAutoReady) applyAutoTarget();
	return true;
}

static void markAutoTargetValid(AutoTargetKind kind) {
	const uint32_t now = millis();
	if (autoTargetValidCount == 0 ||
		(uint32_t)(now - autoTargetLastValidMs) > AUTO_TARGET_TIMEOUT_MS ||
		autoTargetKind != kind) {
		autoTargetFirstValidMs = now;
		autoTargetValidCount = 0;
	}
	autoTargetKind = kind;
	autoTargetLastValidMs = now;
	if (autoTargetValidCount < UINT8_MAX) ++autoTargetValidCount;
	if (!isControlledLandingActive()) setDiagnosticFault(DIAG_AUTO_TARGET_TIMEOUT, false);
}

bool submitAutoAttitudeTarget(const AutoAttitudeCommand& target) {
	AutoAttitudeCommand normalized = target;
	if (!isfinite(normalized.thrust) || normalized.thrust < AUTO_THRUST_MIN || normalized.thrust > AUTO_THRUST_MAX)
		return false;
	if (!normalized.useAttitude && !normalized.useRates) return false;
	if (normalized.useAttitude && !normalizeAutoQuaternion(normalized.attitude)) return false;
	if (normalized.useRates && !ratesWithinConfiguredLimits(normalized.rates)) return false;

	autoAttitudeCommand = normalized;
	markAutoTargetValid(AUTO_TARGET_ATTITUDE);
	#if WEB_RC_ENABLED
	extern bool isLocalSequenceRunning();
	extern bool isLocalSequenceReadyForAuto();
	if (mode == AUTO && !isControlledLandingActive() &&
		!isLocalSequenceRunning() && !isLocalSequenceReadyForAuto()) applyAutoTarget();
	#else
	if (mode == AUTO && !isControlledLandingActive()) applyAutoTarget();
	#endif
	return true;
}

bool submitAutoActuatorTarget(const AutoActuatorCommand& target) {
	AutoActuatorCommand normalized = target;
	for (int i = 0; i < 4; ++i) {
		if (!isfinite(normalized.motors[i]) ||
			normalized.motors[i] < 0.0f || normalized.motors[i] > 1.0f)
			return false;
	}
	autoActuatorCommand = normalized;
	markAutoTargetValid(AUTO_TARGET_ACTUATOR);
	#if WEB_RC_ENABLED
	extern bool isLocalSequenceRunning();
	extern bool isLocalSequenceReadyForAuto();
	if (mode == AUTO && !isControlledLandingActive() &&
		!isLocalSequenceRunning() && !isLocalSequenceReadyForAuto()) applyAutoTarget();
	#else
	if (mode == AUTO && !isControlledLandingActive()) applyAutoTarget();
	#endif
	return true;
}

bool autoTargetReady() {
	if (autoTargetKind == AUTO_TARGET_NONE || autoTargetValidCount == 0) return false;
	const uint32_t now = millis();
	if ((uint32_t)(now - autoTargetLastValidMs) > AUTO_TARGET_TIMEOUT_MS) return false;
	return autoTargetValidCount >= AUTO_TARGET_READY_COUNT &&
		(uint32_t)(now - autoTargetFirstValidMs) >= AUTO_TARGET_READY_MS;
}

bool autoTargetTimedOut() {
	if (mode == AUTO && armed) {
		if (!autoTargetAppliedValid) return true;
		return (uint32_t)(millis() - autoTargetAppliedMs) > AUTO_TARGET_TIMEOUT_MS;
	}
	if (autoTargetKind == AUTO_TARGET_NONE || autoTargetValidCount == 0) return false;
	return (uint32_t)(millis() - autoTargetLastValidMs) > AUTO_TARGET_TIMEOUT_MS;
}

static uint32_t webStopLastMs = 0;

const char* armBlockReason() {
	if (armed) return "";
	if (webStopLastMs && (uint32_t)(millis() - webStopLastMs) < 2000)
		return "网页上锁后请稍候再解锁";
	if (motorTestArmInhibit) return "电机测试后请先释放解锁输入";
	#if WEB_RC_ENABLED
	extern bool isLocalSequenceReadyForAuto();
	const bool localAutoReady = isLocalSequenceReadyForAuto();
	#else
	const bool localAutoReady = false;
	#endif
	if (mode == AUTO && !localAutoReady && !autoTargetReady()) return "AUTO 模式尚无有效目标，请切回 STAB 或等待目标就绪";
	if (motorTestActive) return "电机测试正在运行";
	if (isAccelCalibrationActive()) return "加速度计校准正在运行";
	if (isLevelCalibrationActive()) return "水平校准进行中或已保存安装角，重启飞控后才可解锁";
	if (imuRotationRestartPending()) return "IMU 安装角已改变，重启飞控后才可解锁";
	if (controlThrottle > ARM_THROTTLE_LIMIT) return "油门高于解锁上限 5%";
	if (!imuOK) return "IMU 未就绪";
	if (batteryBlocksArming()) return "电池电压低于解锁门槛 3.5 V";
	if (hasBlockingDiagnosticFault()) return "存在阻止解锁的诊断故障，请查看 diag";
	if (parameterPersistencePending()) return "参数尚未写入闪存，暂不可解锁";
	if (systemLogArmingBlocked()) return "系统日志或参数正在写入，或写入后保护等待尚未结束";
	return nullptr;
}

bool requestArm() {
	if (armed) return true;
	updateDiagnostics();
	if (armBlockReason()) return false;
	extern bool motorEmergencyCutoffReadyForArm();
	extern bool clearMotorEmergencyCutoffIfAcknowledged();
	if (!motorEmergencyCutoffReadyForArm() || !tryArmWithSystemLog()) return false;
	if (clearMotorEmergencyCutoffIfAcknowledged()) return true;
	// A new fast-stop arrived between the readiness check and arming.
	disarm(DISARM_REASON_WEB_EMERGENCY);
	return false;
}

static uint8_t lastDisarmReason = DISARM_REASON_UNKNOWN;

DisarmReason getLastDisarmReason() {
	return (DisarmReason)__atomic_load_n(&lastDisarmReason, __ATOMIC_RELAXED);
}

void disarm(DisarmReason reason) {
	if (reason == DISARM_REASON_WEB_LOCK || reason == DISARM_REASON_WEB_EMERGENCY)
		webStopLastMs = millis() ? millis() : 1;
	// Every disarm source is a motor-stop command. This also covers the
	// disarmed boot self-check, where armed is already false and a CLI disarm
	// must still cancel the active pulse sequence immediately.
	extern void cancelMotorTest();
	extern void abortVibrationCalibrationForDisarm();
	if (motorTestActive) cancelMotorTest();
	abortVibrationCalibrationForDisarm();
	bool outputWasActive = armed;
	if (armed) __atomic_store_n(&lastDisarmReason, (uint8_t)reason, __ATOMIC_RELAXED);
	for (int i = 0; i < 4; ++i) outputWasActive = outputWasActive || motors[i] != 0.0f;
	armed = false;
	clearControlledLanding();
	thrustTarget = 0.0f;
	memset(motors, 0, sizeof(float) * 4);
	torqueTarget.invalidate();
	resetAutoTargetState();
	if (outputWasActive) sendMotors();
}

void control() {
	interpretControls();
#if WEB_RC_ENABLED
	interpretWebRC();
#endif
	failsafe();
	controlAttitude();
	controlRates();
	controlTorque();
}

void interpretControls() {
	// A locally uploaded sequence owns the stick targets until an explicit mode
	// change/takeover or disarm. Its values still need the normal STAB mapping.
#if WEB_RC_ENABLED
	extern bool isLocalSequenceRunning();
	const bool localSequenceRunning = isLocalSequenceRunning();
	if (!localSequenceRunning) {
#else
	const bool localSequenceRunning = false;
#endif
	if (motorTestArmInhibit && (controlThrottle >= 0.05f || controlYaw <= 0.95f))
		motorTestArmInhibit = false;
	static int lastControlModeSlot = -1;
	int controlModeSlot = -1;
	if (isfinite(controlMode) && controlMode >= 0.0f && controlMode <= 1.0f) {
		if (controlMode < 0.25f) controlModeSlot = 0;
		else if (controlMode <= 0.75f) controlModeSlot = 1;
		else controlModeSlot = 2;
	}
	if (controlModeSlot >= 0 && controlModeSlot != lastControlModeSlot) {
		int requestedMode = flightModes[controlModeSlot];
		if (requestedMode == ALTHOLD) requestedMode = STAB;
		setFlightMode(requestedMode);
		lastControlModeSlot = controlModeSlot;
	}

#if WEB_RC_ENABLED
	if (!isUsingWebRC()) { // SBUS手势解锁仅当WebRC未活跃时生效
#endif
	extern bool imuOK;
	static bool armWarnNotified = false;  // 防刷屏：低电/IMU故障禁止解锁提示
	if (controlThrottle < 0.05 && controlYaw > 0.95) { // arm gesture
		if (!imuOK) {
			// IMU 故障：禁止解锁
			if (!armWarnNotified) {
				print("IMU故障，禁止解锁！\n");
#if WEB_RC_ENABLED
				setWebRCWarn("IMU故障 禁止解锁");
#endif
				armWarnNotified = true;
			}
		} else if (batteryBlocksArming()) {
			// L1 及以下：禁止解锁，状态变化时提示
			if (!armWarnNotified) {
				print("电量低(%.2fV)，禁止解锁\n", batteryVoltage);
#if WEB_RC_ENABLED
				char warnBuf[64];
				snprintf(warnBuf, sizeof(warnBuf), "电量低(%.2fV) 禁止解锁", batteryVoltage);
				setWebRCWarn(warnBuf);
#endif
				armWarnNotified = true;
			}
		} else {
			if (hasBlockingDiagnosticFault()) {
				if (!armWarnNotified) print("系统诊断存在阻止解锁的故障，请运行 diag 查看。\n");
				armWarnNotified = true;
			} else {
				if (requestArm()) {
					armWarnNotified = false;
				} else {
					if (!armWarnNotified) {
						const char *reason = armBlockReason();
						print("禁止解锁：%s\n", reason ? reason : "解锁状态刚发生变化，请重试");
					}
					armWarnNotified = true;
				}
			}
		}
	}
	if (controlThrottle < 0.05 && controlYaw < -0.95) disarm(DISARM_REASON_RC_GESTURE); // disarm gesture
#if WEB_RC_ENABLED
	}
	} // Local sequence values must not trigger RC mode changes or arm gestures.
#endif

	if ((mode == AUTO && !localSequenceRunning) || isControlledLandingActive()) return;

#if WEB_RC_ENABLED
	if (!localSequenceRunning) {
#endif
	const ControlSource manualSource = selectedManualControlSource();
	setCurrentControlSource(manualSource == CONTROL_SOURCE_NONE ? CONTROL_SOURCE_PHYSICAL_RC : manualSource);
#if WEB_RC_ENABLED
	}
#endif

	if (abs(controlYaw) < 0.1) controlYaw = 0; // yaw dead zone

	if (controlThrottle < 0.05f) {
		thrustTarget = 0.0f;   // 底部死区 → 怠速，PID 不运行
	} else {
		thrustTarget = mapf(controlThrottle, 0.05f, 1.0f, motThrMin, motThrMax);
	}

	if (mode == STAB || (mode == AUTO && localSequenceRunning)) {
		float yawTarget = attitudeTarget.getYaw();
		if (!armed || invalid(yawTarget) || controlYaw != 0) yawTarget = attitude.getYaw(); // reset yaw target
		// trimRoll/trimPitch 叠加到摇杆指令上，补偿机械不对称引起的固定漂移
		// 调整方式见变量声明处注释，或通过 CLI: set CTL_TRIM_ROLL / CTL_TRIM_PITCH
		attitudeTarget = Quaternion::fromEuler(Vector(controlRoll * tiltMax + trimRoll, controlPitch * tiltMax + trimPitch, yawTarget));
		ratesExtra = Vector(0, 0, -controlYaw * maxRate.z); // positive yaw stick means clockwise rotation in FLU
	}

	if (mode == ACRO) {
		attitudeTarget.invalidate(); // skip attitude control
		ratesTarget.x = controlRoll * maxRate.x;
		ratesTarget.y = controlPitch * maxRate.y;
		ratesTarget.z = -controlYaw * maxRate.z; // positive yaw stick means clockwise rotation in FLU
	}

	if (mode == RAW) { // direct torque control
		attitudeTarget.invalidate(); // skip attitude control
		ratesTarget.invalidate(); // skip rate control
		torqueTarget = Vector(controlRoll, controlPitch, -controlYaw) * 0.1;
	}
}

void controlAttitude() {
	if (!armed || attitudeTarget.invalid() || thrustTarget < motThrMin) {
		rollPID.reset(); pitchPID.reset(); yawPID.reset();
		return;
	}

	const Vector up(0, 0, 1);
	Vector upActual = Quaternion::rotateVector(up, attitude);
	Vector upTarget = Quaternion::rotateVector(up, attitudeTarget);

	Vector error = Vector::rotationVectorBetween(upTarget, upActual);

	ratesTarget.x = rollPID.update(error.x) + ratesExtra.x;
	ratesTarget.y = pitchPID.update(error.y) + ratesExtra.y;

	#if WEB_RC_ENABLED
	extern bool isLocalSequenceRunning();
	const bool localSequenceRunning = isLocalSequenceRunning();
	#else
	const bool localSequenceRunning = false;
	#endif
	if (mode == STAB || (mode == AUTO && localSequenceRunning)) {
		// There is no magnetometer heading correction in this estimator, so
		// STAB's integrated yaw drifts under gyro bias/vibration. Command yaw
		// rate from the pilot; do not turn that unobservable drift into torque.
		ratesTarget.z = ratesExtra.z;
	} else {
		// Retain absolute yaw targets for external AUTO attitude commands.
		const float yawError = wrapAngle(attitudeTarget.getYaw() - attitude.getYaw());
		ratesTarget.z = yawPID.update(yawError) + ratesExtra.z;
	}
	ratesTarget = constrainRatesToConfiguredLimits(ratesTarget);
}


void controlRates() {
	if (!armed || ratesTarget.invalid() || thrustTarget < motThrMin) {
		rollRatePID.reset(); pitchRatePID.reset(); yawRatePID.reset();
		motorMixScale = 1.0f;
		return;
	}

	Vector error = ratesTarget - rates;

	// Calculate desired torque, where 0 - no torque, 1 - maximum possible torque
	torqueTarget.x = rollRatePID.update(error.x, dt, motorMixScale >= 0.999f || error.x * torqueTarget.x <= 0);
	torqueTarget.y = pitchRatePID.update(error.y, dt, motorMixScale >= 0.999f || error.y * torqueTarget.y <= 0);
	torqueTarget.z = yawRatePID.update(error.z, dt, motorMixScale >= 0.999f || error.z * torqueTarget.z <= 0);
}

void controlTorque() {
	if (!armed) {
		memset(motors, 0, sizeof(motors)); // stop motors if disarmed
		return;
	}
	// Armed zero-throttle is a distinct idle state. Attitude/rate control keeps
	// torqueTarget invalid here, so idle output must be applied before that guard.
	if (getCurrentControlSource() != CONTROL_SOURCE_EXTERNAL_MOTORS && thrustTarget < motThrMin) {
		for (int i = 0; i < 4; i++) motors[i] = motThrMin; // idle thrust
		return;
	}
	if (!torqueTarget.valid()) return; // skip torque control

	motors[MOTOR_FRONT_LEFT] = thrustTarget + torqueTarget.x - torqueTarget.y + torqueTarget.z;
	motors[MOTOR_FRONT_RIGHT] = thrustTarget - torqueTarget.x - torqueTarget.y - torqueTarget.z;
	motors[MOTOR_REAR_LEFT] = thrustTarget + torqueTarget.x + torqueTarget.y - torqueTarget.z;
	motors[MOTOR_REAR_RIGHT] = thrustTarget - torqueTarget.x + torqueTarget.y + torqueTarget.z;

	desaturate(motors[MOTOR_FRONT_LEFT], motors[MOTOR_FRONT_RIGHT], motors[MOTOR_REAR_LEFT], motors[MOTOR_REAR_RIGHT]);

	motors[0] = constrain(motors[0], 0, 1);
	motors[1] = constrain(motors[1], 0, 1);
	motors[2] = constrain(motors[2], 0, 1);
	motors[3] = constrain(motors[3], 0, 1);
}

void desaturate(float& a, float& b, float& c, float& d) {
	// Preserve attitude torque first. At high collective, lower the average
	// motor command enough to keep the requested torque span; at low collective,
	// do not raise thrust just to fit torque, so scale torque to stay in range.
	const float avg = (a + b + c + d) * 0.25f;
	const float da = a - avg, db = b - avg, dc = c - avg, dd = d - avg;
	const float minDev = min(min(da, db), min(dc, dd));
	const float maxDev = max(max(da, db), max(dc, dd));
	const float span = maxDev - minDev;
	float scale = span > 1.0f ? 1.0f / span : 1.0f;
	const float minCollective = max(0.0f, -minDev * scale);
	const float maxCollective = min(1.0f, 1.0f - maxDev * scale);
	float collective = constrain(avg, minCollective, maxCollective);
	if (avg < minCollective) {
		// Low thrust has insufficient lower rail headroom. Preserve the requested
		// collective and reduce torque symmetrically instead of raising thrust.
		if (minDev < 0.0f) scale = min(scale, avg / -minDev);
		if (maxDev > 0.0f) scale = min(scale, (1.0f - avg) / maxDev);
		collective = avg;
	}
	motorMixScale = constrain(scale, 0.0f, 1.0f);
	a = collective + da * motorMixScale;
	b = collective + db * motorMixScale;
	c = collective + dc * motorMixScale;
	d = collective + dd * motorMixScale;
}

const char* getModeName() {
	switch (mode) {
		case RAW:     return "RAW";
		case ACRO:    return "ACRO";
		case STAB:    return "STAB";
		case ALTHOLD: return "ALTHOLD";
		case AUTO:    return "AUTO";
		default:      return "UNKNOWN";
	}
}

#if WEB_RC_ENABLED
// Web遥控器按钮处理（仅负责ARM/DISARM和模式切换）
void interpretWebRC() {
	if (!isUsingWebRC()) return;

	// 处理解锁/上锁按钮（上升沿检测，避免每个控制周期重复触发）
	static uint16_t lastWebRCButtons = 0;
	uint16_t currentButtons = 0;
	uint16_t risingEdge = takeWebRCButtonPressEdges(&currentButtons) | (currentButtons & ~lastWebRCButtons);
	lastWebRCButtons = currentButtons;
	if (motorTestArmInhibit && !(currentButtons & 0x0001)) motorTestArmInhibit = false;

	// 处理解锁/上锁状态变化日志
	static bool lastArmedState = false;
	if (armed != lastArmedState) {
		recordSystemLogEvent("FLIGHT_STATE", armed ? "Web RC: 已解锁" : "Web RC: 已上锁");
		lastArmedState = armed;
	}

	// 按鈕。0：解锁（上升沿）
	if (risingEdge & 0x0001) {
		extern bool imuOK;
		if (motorTestArmInhibit) {
			setWebRCWarn("电机测试后请先释放解锁输入");
		} else if (hasBlockingDiagnosticFault()) {
			setWebRCWarn("系统故障 禁止解锁，请查看diag");
		} else if (!imuOK) {
			setWebRCWarn("IMU故障 禁止解锁");
		} else if (batteryBlocksArming()) {
			// 低电量：禁止解锁（与 SBUS 路径 interpretControls() 对齐）
			char warnBuf[64];
			snprintf(warnBuf, sizeof(warnBuf), "电量低(%.2fV) 禁止解锁", batteryVoltage);
			setWebRCWarn(warnBuf);
		} else if (controlThrottle > ARM_THROTTLE_LIMIT) {
			setWebRCWarn("油门过高，无法解锁");
		} else {
			if (requestArm()) {
				clearWebRCWarn(); // 解锁成功，清除上次遗留的警告
			} else {
				const char *reason = armBlockReason();
				setWebRCWarn(reason ? reason : "解锁状态刚发生变化，请重试");
			}
		}
	}

	// 按钮1：上锁（上升沿）
	if (risingEdge & 0x0002) {
		disarm(DISARM_REASON_WEB_LOCK);
	}

	// 按钮2：急停（上升沿）
	if (risingEdge & 0x0004) {
		disarm(DISARM_REASON_WEB_EMERGENCY);
	}

	// 按钮3：迫降（上升沿）；复用 RC 失联/低电时的受控下降流程。
	if (risingEdge & 0x0008) {
		if (armed) {
			descend();
			clearWebRCWarn();
		} else {
			setWebRCWarn("迫降未启动：飞控当前已上锁");
		}
	}

	// 按钮6：STAB模式（上升沿）
	if (risingEdge & 0x0040) {
		setFlightMode(STAB);
	}

	// 按钮7：ACRO模式（上升沿）
	if (risingEdge & 0x0080) {
		setFlightMode(ACRO);
	}

	// 按钮8保留给ALTHOLD；当前六轴硬件不支持，入口只告警，不切模式。
	if (risingEdge & 0x0100) {
		setWebRCWarn("定高模式暂不支持");
	}

	// 按钮9：已上传的单条序列进入 AUTO 后自动启动。
	if (risingEdge & 0x0200) {
		if (!setFlightMode(AUTO)) setWebRCWarn("AUTO 序列未就绪或连接无效");
	}

	// 模式切换日志
	static int lastMode = STAB;
	if (mode != lastMode) {
		char event[64];
		snprintf(event, sizeof(event), "Web RC: 模式切换到 %s", getModeName());
		recordSystemLogEvent("FLIGHT_MODE", event);
		lastMode = mode;
	}
}
#endif
