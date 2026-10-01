// 基于陀螺仪和加速度计的姿态计算
// Attitude estimation from gyro and accelerometer

#include "quaternion.h"
#include "vector.h"
#include "lpf.h"
#include "util.h"
#include "attitude_vqf.h"

float accWeight = 0.0005f;
// Last-sample estimator evidence copied into the bounded flight log. This
// value is diagnostic only and does not feed the control calculation.
float accelCorrectionConfidence = 0.0f;
static const float ESTIMATE_NOMINAL_DT = 0.001f;
// A long scheduler stall yields only one fresh accelerometer sample, not a
// history representative of the whole gap. Bound gravity feedback to 5 ms.
static const float ESTIMATE_MAX_ACCEL_CORRECTION_DT = 0.005f;
// Fade gravity feedback between 5 degrees and a compile-time upper threshold;
// keep a small gain floor to limit gyro-bias drift.
static const float ESTIMATE_ACCEL_INNOVATION_MIN_RAD = 0.08726646f; // 5 deg
#ifndef ESTIMATE_ACCEL_INNOVATION_MAX_DEG
#define ESTIMATE_ACCEL_INNOVATION_MAX_DEG 15.0f
#endif
static_assert(ESTIMATE_ACCEL_INNOVATION_MAX_DEG > 5.0f && ESTIMATE_ACCEL_INNOVATION_MAX_DEG <= 180.0f,
	"ESTIMATE_ACCEL_INNOVATION_MAX_DEG must be in (5, 180]");
static const float ESTIMATE_ACCEL_INNOVATION_MAX_RAD = radians(ESTIMATE_ACCEL_INNOVATION_MAX_DEG);
#ifndef ESTIMATE_ACCEL_MIN_ADAPTIVE_WEIGHT
#define ESTIMATE_ACCEL_MIN_ADAPTIVE_WEIGHT 0.0005f
#endif
static_assert(ESTIMATE_ACCEL_MIN_ADAPTIVE_WEIGHT >= 0.0f && ESTIMATE_ACCEL_MIN_ADAPTIVE_WEIGHT <= 1.0f,
	"ESTIMATE_ACCEL_MIN_ADAPTIVE_WEIGHT must be in [0, 1]");
#ifndef EST_RAW_ACCEL_NORM_TOLERANCE
#define EST_RAW_ACCEL_NORM_TOLERANCE 0.05f
#endif
static_assert(EST_RAW_ACCEL_NORM_TOLERANCE > 0.0f && EST_RAW_ACCEL_NORM_TOLERANCE <= 1.0f,
	"EST_RAW_ACCEL_NORM_TOLERANCE must be in (0, 1]");

static float adaptiveAccelerationWeight(float configuredWeight, float gravityAlignment) {
	const float innovation = acosf(constrain(gravityAlignment, -1.0f, 1.0f));
	const float innovationRatio = constrain((innovation - ESTIMATE_ACCEL_INNOVATION_MIN_RAD) /
		(ESTIMATE_ACCEL_INNOVATION_MAX_RAD - ESTIMATE_ACCEL_INNOVATION_MIN_RAD), 0.0f, 1.0f);
	const float minimumWeight = min(configuredWeight, ESTIMATE_ACCEL_MIN_ADAPTIVE_WEIGHT);
	return configuredWeight + (minimumWeight - configuredWeight) * innovationRatio;
}

// ============== 水平修正 P 项 ==============
float levelWeight = 0;  // 水平修正 P 项权重（关闭，无法区分陀螺温漂与机械不对称时会起负作用）
float levelMaxTilt = radians(30); // rad, level correction fades out at this tilt angle (matches TILT_MAX)
// 摇杆感知门控阈值
// 当飞手摇杆偏转量（横滚或俯仰取最大值，范围 0~1）超过此阈值时，
// applyLevel 的修正权重线性衰减至 0，避免与 PID 积分项产生耦合导致松杆后漂移。
// 调整方法：
//   - 值越小，门控越灵敏（轻微打杆即停止修正），悬停抑漂效果减弱。
//   - 值越大，打杆时 applyLevel 干扰更久，现象三改善减弱。
//   - 推荐范围：0.2（灵敏）~ 0.4（宽松），默认 0.2（摇杆 20% 行程时权重归零）。
//   - 可通过 CLI 在线修改：p EST_LVL_GATE_THR <值>
float levelGateThreshold = 0.2f; // 摇杆门控阈值，参数名：EST_LVL_GATE_THR

// ============== Mahony 风格水平修正 I 项 ==============
// levelBiasGain：重力误差向虚拟陀螺偏置的积分增益（I 项）。
// 积分量在 applyGyro() 中从陀螺读数中减去，不直接修改 attitude 四元数，
// 因此 PID 控制回路感知不到修正过程，消除了与内环 I 项的耦合振荡。
// 调整方法：越大收敛越快，但可能过积分；约 30s 收敛至稳态。
// 参数名：EST_LVL_BIAS_GAIN
float levelBiasGain = 0;  // Mahony I 项增益（关闭，无法区分陀螺温漂与机械不对称时会起负作用）
Vector levelGyroBias(0, 0, 0); // 由 applyLevel() 估计的虚拟陀螺偏置（rad/s）

extern float controlRoll, controlPitch; // 飞手摇杆输入，定义于 CF-Drone.ino
extern bool armed;
LowPassFilter<Vector> ratesFilter(0.2f); // 1 ms reference coefficient; about 35.5 Hz
// Use a separate low-pass path for gravity fusion. The raw MPU9250 acceleration
// remains available to logging and calibration, while motor vibration is
// attenuated before the 1 g confidence gate so aliased high-frequency vibration
// does not disable attitude correction for most of a motor run.
#ifndef EST_ACCEL_FUSION_FILTER_ALPHA
#define EST_ACCEL_FUSION_FILTER_ALPHA 0.2f
#endif
static_assert(EST_ACCEL_FUSION_FILTER_ALPHA > 0.0f && EST_ACCEL_FUSION_FILTER_ALPHA <= 1.0f,
	"EST_ACCEL_FUSION_FILTER_ALPHA must be in (0, 1]");
LowPassFilter<Vector> accelerationFusionFilter(EST_ACCEL_FUSION_FILTER_ALPHA); // 35.5 Hz at 1 kHz by default
#if ATTITUDE_ESTIMATOR_VQF
static VqfAttitudeEstimator vqfAttitudeEstimator;
#endif

void estimate() {
	#if ATTITUDE_ESTIMATOR_VQF
	const float accNorm = acc.norm();
	if (!isfinite(accNorm) || accNorm < 1e-3f) {
		accelCorrectionConfidence = 0.0f;
		landed = false;
		rates = ratesFilter.update(gyro, dt, ESTIMATE_NOMINAL_DT);
		vqfAttitudeEstimator.update(attitude, gyro, acc, dt, false);
		applyLevel();
		return;
	}
	landed = isfinite(accNorm) && !motorsActive() && fabsf(accNorm - ONE_G) < ONE_G * 0.1f;
	const Vector gravityReference = accelerationFusionFilter.update(acc, dt, ESTIMATE_NOMINAL_DT);
	float correctionConfidence = 1.0f;
	if (!landed) {
		correctionConfidence = 0.0f;
		if (armed && isfinite(accNorm) && accNorm >= 1e-3f && gravityReference.valid()) {
			const float gravityNorm = gravityReference.norm();
			if (isfinite(gravityNorm) && gravityNorm >= 1e-3f) {
				const float normTolerance = ONE_G * 0.15f;
				correctionConfidence = constrain(1.0f - fabsf(gravityNorm - ONE_G) / normTolerance,
					0.0f, 1.0f);
				const float stickDeflection = max(fabsf(controlRoll), fabsf(controlPitch));
				const float stickGate = levelGateThreshold > 0.0f
					? constrain(1.0f - stickDeflection / levelGateThreshold, 0.0f, 1.0f)
					: (stickDeflection == 0.0f ? 1.0f : 0.0f);
				correctionConfidence *= stickGate;
			}
		}
	}
	accelCorrectionConfidence = correctionConfidence;
	rates = ratesFilter.update(gyro, dt, ESTIMATE_NOMINAL_DT);
	vqfAttitudeEstimator.update(attitude, gyro, acc, dt,
		landed || correctionConfidence > 0.0f);
	applyLevel();
	#else
	applyGyro();
	applyAcc();
	applyLevel();
	#endif
}

void applyGyro() {
	// Mahony 风格水平修正 I 项 从陀螺读数中减去 Mahony I 项估计的虚拟偏置，再滤波积分
	// 这样水平修正的长期影响通过偏置路径而非 attitude 直接反映，PID 不感知
	rates = ratesFilter.update(gyro - levelGyroBias, dt, ESTIMATE_NOMINAL_DT);

	// apply rates to attitude
	attitude = Quaternion::rotate(attitude, Quaternion::fromRotationVector(rates * dt));
}

void applyAcc() {
	accelCorrectionConfidence = 0.0f;
	float accNorm = acc.norm();
	landed = isfinite(accNorm) && !motorsActive() && fabsf(accNorm - ONE_G) < ONE_G * 0.1f;
	if (!isfinite(accNorm) || accNorm < 1e-3f) return;
	const Vector gravityReference = accelerationFusionFilter.update(acc, dt, ESTIMATE_NOMINAL_DT);
	const float gravityNorm = gravityReference.norm();
	if (!isfinite(gravityNorm) || gravityNorm < 1e-3f) return;

	float correctionConfidence = 1.0f;
	if (!landed) {
		// Accelerometer direction is a gravity reference only when the measured
		// specific-force magnitude is near 1g. Never use it during disarmed motor
		// tests, and fade its influence during deliberate roll/pitch commands.
		if (!armed) return;
		const float normError = fabsf(gravityNorm - ONE_G);
		const float normTolerance = ONE_G * 0.15f;
		correctionConfidence = constrain(1.0f - normError / normTolerance, 0.0f, 1.0f);

		const float stickDeflection = max(fabsf(controlRoll), fabsf(controlPitch));
		const float stickGate = levelGateThreshold > 0.0f
			? constrain(1.0f - stickDeflection / levelGateThreshold, 0.0f, 1.0f)
			: (stickDeflection == 0.0f ? 1.0f : 0.0f);
		correctionConfidence *= stickGate;
		// The filtered norm protects against translational acceleration, but can
		// hide short motor-vibration peaks. Fade gravity correction using the raw
		// specific-force magnitude as an independent confidence signal.
		const float rawNormTolerance = ONE_G * EST_RAW_ACCEL_NORM_TOLERANCE;
		const float rawNormConfidence = constrain(1.0f - fabsf(accNorm - ONE_G) / rawNormTolerance,
			0.0f, 1.0f);
		correctionConfidence *= rawNormConfidence;
	}
	accelCorrectionConfidence = correctionConfidence;
	if (correctionConfidence <= 0.0f) return;

	// calculate accelerometer correction
	Vector up = Quaternion::rotateVector(Vector(0, 0, 1), attitude);
	const float gravityAlignment = Vector::dot(gravityReference / gravityNorm, up);
	const float correctionWeight = adaptiveAccelerationWeight(accWeight, gravityAlignment);
	Vector correction = Vector::rotationVectorBetween(gravityReference, up) *
		(correctionWeight * correctionConfidence *
			(min(dt, ESTIMATE_MAX_ACCEL_CORRECTION_DT) / ESTIMATE_NOMINAL_DT));

	// apply correction
	attitude = Quaternion::rotate(attitude, Quaternion::fromRotationVector(correction));
}

// 水平修正（Mahony 风格：P 项直接修正 attitude，I 项积分进虚拟陀螺偏置）
// 架构说明：
//   - P 项（levelWeight）：对 attitude 施加小修正，提供快速响应，量级已减半以降低踢扰。
//   - I 项（levelBiasGain）：将重力参考误差积分到 levelGyroBias，
//     在 applyGyro() 中从陀螺读数减去。修正路径绕开 PID 误差输入，
//     消除原来直接改 attitude 导致的内环 I 项耦合振荡。
void applyLevel() {
	if (landed) {
		levelGyroBias = Vector(0, 0, 0); // 落地后清零偏置，下次起飞重新学习
		return;
	}
	if (levelWeight == 0.0f && levelBiasGain == 0.0f &&
		levelGyroBias.x == 0.0f && levelGyroBias.y == 0.0f && levelGyroBias.z == 0.0f) return;

	Vector up = Quaternion::rotateVector(Vector(0, 0, 1), attitude);
	float tilt = acos(constrain(up.z, -1.0f, 1.0f));

	// 极近水平时跳过：tilt→0 时叉积方向对振动噪声极敏感，方向随机会引入无规律漂移
	if (tilt < radians(0.1f)) return;

	// P 项权重：倾角越接近 levelMaxTilt，权重越小
	float dynamicWeight = levelWeight * constrain(1.0f - tilt / levelMaxTilt, 0.0f, 1.0f) *
		(dt / ESTIMATE_NOMINAL_DT);

	// ---- 摇杆感知门控 ----
	float stickDeflection = max(abs(controlRoll), abs(controlPitch));
	float stickGate = constrain(1.0f - stickDeflection / levelGateThreshold, 0.0f, 1.0f);
	dynamicWeight *= stickGate;
	// ------------------------------------

	// 重力参考误差：机体 Z 轴与世界 Z 轴之间的旋转向量
	Vector error = Vector::rotationVectorBetween(Vector(0, 0, 1), up);

	// P 项：直接修正 attitude（量级极小，保留快速漂移抑制响应）
	attitude = Quaternion::rotate(attitude, Quaternion::fromRotationVector(error * dynamicWeight));

	// I 项：积分进虚拟陀螺偏置（Mahony 风格）
	// 打杆期间（stickGate=0）暂停积分，避免积分方向因主动操纵而错误累积
	levelGyroBias += error * (levelBiasGain * stickGate * (dt / ESTIMATE_NOMINAL_DT));

	// 限幅：等效最大补偿 3 deg/s，防止偏置发散
	float biasNorm = levelGyroBias.norm();
	const float biasLimit = radians(3.0f);
	if (biasNorm > biasLimit) levelGyroBias = levelGyroBias * (biasLimit / biasNorm);
}
