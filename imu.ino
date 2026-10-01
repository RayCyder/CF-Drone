// 陀螺仪加速度计相关设置
// Work with the IMU sensor

#include <SPI.h>
#include "drivers/MPU9250.h"
#include "vector.h"
#include "lpf.h"
#include "util.h"
#include "board_config.h"
#include "diagnostics.h"
#include "imu_capture.h"
#include "stationary_imu_detector.h"

MPU9250 imu(SPI, BOARD_SPI_CS);
ImuCaptureBuffer imuCapture;

bool imuOK = false; // IMU 初始化是否成功；false 时禁止解锁，readIMU() 跳过等待
bool imuSampleValid = false; // 当前主循环是否发布了新的有效 IMU 样本
extern bool saveParameterNow(const char *name);

// IMU 安装方向（欧拉角，单位 rad）。默认值 (0, 0, -PI/2) 对应本 PCB 的安装方式：
// 芯片正面朝上，X 丝印→飞行器右侧，Y 丝印→飞行器前方 → 转换公式 Vector(data.y, -data.x, data.z)
// 如需适配其他安装方向，修改此值并执行 `preset` 重置参数存储。
Vector imuRotation(0, 0, -PI / 2);

Vector accBias;
Vector accScale(1, 1, 1);
Vector gyroBias;
LowPassFilter<Vector> gyroBiasFilter(0.001); // 陀螺仪偏置低通估计滤波器

static Vector calibrationRawAcc;
static uint32_t calibrationRawAccSequence;
static Vector calibrationFaces[6];
static Vector calibrationSum;
static int calibrationFace;
static int calibrationSampleCount;
static uint32_t calibrationLastSequence;
static uint32_t calibrationPhaseStarted;
static bool calibrationProgressShown;
enum AccelCalibrationPhase { CAL_IDLE, CAL_SETTLE, CAL_SAMPLE, CAL_GAP };
static AccelCalibrationPhase calibrationPhase = CAL_IDLE;
static const char *calibrationInstructions[6] = {
	"水平放置：机头朝前，底部朝下，确保水平且静止。",
	"机头朝上：机头指向天空，机身与地面垂直。",
	"机头朝下：机头指向地面，机身与地面垂直。",
	"右侧朝下：机身右侧接触支撑面。",
	"左侧朝下：机身左侧接触支撑面。",
	"倒置放置：顶部朝下，底部朝上。"
};

static void printCalibrationFace();
static void abortAccelCalibration(const char *reason);
static bool finishAccelCalibration();
bool isAccelCalibrationActive();
void updateAccelCalibration();

void setupIMU() {
	print("Setup IMU\n");
	SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI, BOARD_SPI_CS);
	if (!imu.begin()) {
		print("IMU_INIT_DETAIL result=FAIL driver_status=%d whoami=0x%02X model=%s\n",
			imu.status(), imu.whoAmI(), imu.getModel());
		print("⚠ IMU初始化失败！请检查 IMU 硬件连接！\n");
		setDiagnosticFault(DIAG_IMU_INIT, true);
		return; // 不调用 configureIMU()，不启动定时器中断，imuOK 保持 false
	}
	imuOK = configureIMU();
	if (!imuOK) {
		setDiagnosticFault(DIAG_IMU_INIT, true);
		print("IMU_CONFIG_DETAIL result=FAIL driver_status=%d whoami=0x%02X model=%s\n",
			imu.status(), imu.whoAmI(), imu.getModel());
		print("⚠ IMU配置失败，禁止解锁。\n");
	}
}

bool configureIMU() {
	bool ok = imu.setAccelRange(imu.ACCEL_RANGE_4G);
	ok = imu.setGyroRange(imu.GYRO_RANGE_2000DPS) && ok;
	ok = imu.setDLPF(imu.DLPF_MAX) && ok;
	ok = imu.setRate(imu.RATE_1KHZ_APPROX) && ok;
	ok = imu.setupInterrupt() && ok;
	return ok;
}

void readIMU() {
	imuSampleValid = false;
	if (!imuOK) return; // IMU 故障时跳过，gyro/acc 保持零值，主循环继续运行
	static uint8_t consecutiveGoodFrames = 0;
	const uint32_t waitStarted = micros();
	const bool imuDataReady = imu.waitForData(5);
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
	recordImuWaitTrace(imu.lastWaitTrace());
#endif
	if (!imuDataReady) {
		recordLoopStage(LOOP_STAGE_IMU_WAIT, micros() - waitStarted);
		consecutiveGoodFrames = 0;
		setDiagnosticFault(DIAG_IMU_TIMEOUT, true);
		return;
	}
	recordLoopStage(LOOP_STAGE_IMU_WAIT, micros() - waitStarted);
	const uint32_t processStarted = micros();
	Vector sampledGyro;
	Vector sampledAcc;
	imu.getGyro(sampledGyro.x, sampledGyro.y, sampledGyro.z);
	imu.getAccel(sampledAcc.x, sampledAcc.y, sampledAcc.z);
	if (!sampledGyro.valid() || !sampledAcc.valid()) {
		recordLoopStage(LOOP_STAGE_IMU_PROCESS, micros() - processStarted);
		consecutiveGoodFrames = 0;
		setDiagnosticFault(DIAG_IMU_INVALID, true);
		return;
	}
	// Keep the calibrated sensor-frame reading before online bias subtraction
	// and body-axis rotation for optional, disarmed temperature diagnostics.
	const Vector rawGyroSensor = sampledGyro;
	calibrateGyroOnce(sampledGyro, sampledAcc);
	// apply scale and bias
	Vector bodyAcc = (sampledAcc - accBias) / accScale;
	Vector bodyGyro = sampledGyro - gyroBias;
	if (!bodyAcc.valid() || !bodyGyro.valid() || !imuRotation.valid()) {
		recordLoopStage(LOOP_STAGE_IMU_PROCESS, micros() - processStarted);
		consecutiveGoodFrames = 0;
		setDiagnosticFault(DIAG_IMU_INVALID, true);
		return;
	}
	// rotate to body frame using imuRotation Euler angles
	// 旋转四元数缓存：imuRotation 为运行时常量（仅参数变更时改变），
	// 缓存后每帧仅做 3 次浮点比较，消除原先每帧 6 次 sinf/cosf 调用。
	// 参数更新后首次 readIMU() 调用自动重建缓存，延迟仅 1 帧（~1 ms）。
	static Quaternion _imuRotQuat;
	static float _cachedRotX = NAN, _cachedRotY = NAN, _cachedRotZ = NAN;
	if (imuRotation.x != _cachedRotX || imuRotation.y != _cachedRotY || imuRotation.z != _cachedRotZ) {
		_imuRotQuat = Quaternion::fromEuler(imuRotation).inversed();
		_cachedRotX = imuRotation.x;
		_cachedRotY = imuRotation.y;
		_cachedRotZ = imuRotation.z;
	}
	bodyAcc  = Quaternion::rotateVector(bodyAcc,  _imuRotQuat);
	bodyGyro = Quaternion::rotateVector(bodyGyro, _imuRotQuat);
	if (!bodyAcc.valid() || !bodyGyro.valid()) {
		recordLoopStage(LOOP_STAGE_IMU_PROCESS, micros() - processStarted);
		consecutiveGoodFrames = 0;
		setDiagnosticFault(DIAG_IMU_INVALID, true);
		return;
	}
	calibrationRawAcc = sampledAcc;
	++calibrationRawAccSequence;
	gyro = bodyGyro;
	acc = bodyAcc;
	imuSampleValid = true;
	if (consecutiveGoodFrames < 100) ++consecutiveGoodFrames;
	if (consecutiveGoodFrames >= 100) {
		setDiagnosticFault(DIAG_IMU_TIMEOUT, false);
		setDiagnosticFault(DIAG_IMU_INVALID, false);
	}
	imuCapture.append(micros(), gyro.x, gyro.y, gyro.z, acc.x, acc.y, acc.z, imu.getTemp(),
		rawGyroSensor.x, rawGyroSensor.y, rawGyroSensor.z);
	recordLoopStage(LOOP_STAGE_IMU_PROCESS, micros() - processStarted);
}

void calibrateGyroOnce(const Vector &rawGyroSensor, const Vector &rawAccSensor) {
	static Delay landedDelay(2);
	static StationaryImuDetector stationaryDetector;
	static bool gyroBiasInitialized = false;
	extern bool armed;
	if (armed || !landed) {
		stationaryDetector.reset();
		landedDelay.update(false);
		gyroBiasFilter.reset();
		return;
	}
	Vector stationaryGyroMean;
	// Before the first calibration, the raw sensor bias can approach 0.05 rad/s
	// on this MPU-6500, so use a wider bootstrap gate. Afterwards detect motion
	// relative to the learned bias with a tighter limit; otherwise a slow steady
	// rotation can be learned as bias and then subtracted from the flight rate.
	const Vector gyroBiasResidual = rawGyroSensor - gyroBias;
	const float maxGyroMean = gyroBiasInitialized
		? StationaryImuDetector::MAX_GYRO_MEAN_RAD_S
		: StationaryImuDetector::BOOTSTRAP_GYRO_MEAN_RAD_S;
	const StationaryImuDetector::Result stationarity = stationaryDetector.update(
		gyroBiasResidual, rawAccSensor, stationaryGyroMean, maxGyroMean);
	if (stationarity == StationaryImuDetector::WINDOW_COLLECTING) return;
	if (stationarity != StationaryImuDetector::STATIONARY) {
		landedDelay.update(false);
		gyroBiasFilter.reset();
		return;
	}
	if (!landedDelay.update(true)) return; // require 2 seconds of stable windows

	// The detector returns residual rate relative to the current estimate.
	// Integrate only the residual from a complete stationary window.
	gyroBias = gyroBiasFilter.update(gyroBias + stationaryGyroMean,
		StationaryImuDetector::WINDOW_SAMPLES * 0.001f, 0.001f);
	gyroBiasInitialized = true;
}

void calibrateAccel() {
	print("六面校准加速度计 Calibrating accelerometer\n");
	extern bool armed;
	extern bool motorsActive();
	if (calibrationPhase != CAL_IDLE) {
		print("六面校准已在进行中，请按当前提示操作。\n");
		return;
	}
	if (armed || motorsActive() || !imuOK) {
		print("电机必须停止且 IMU 正常，才能执行加速度计校准。\n");
		return;
	}
	if (!imu.setAccelRange(imu.ACCEL_RANGE_2G)) {
		imuOK = configureIMU();
		setDiagnosticFault(DIAG_IMU_INIT, !imuOK);
		print("无法切换 IMU 加速度量程，校准已取消。\n");
		return;
	}
	memset(calibrationFaces, 0, sizeof(calibrationFaces));
	calibrationFace = 0;
	calibrationPhase = CAL_SETTLE;
	calibrationPhaseStarted = millis();
	calibrationLastSequence = calibrationRawAccSequence;
	printCalibrationFace();
}

bool isAccelCalibrationActive() {
	return calibrationPhase != CAL_IDLE;
}

static void printCalibrationFace() {
	print("%d/6 %s 放稳后等待8秒，随后采样；期间飞控循环继续运行。\n",
		calibrationFace + 1, calibrationInstructions[calibrationFace]);
}

static void abortAccelCalibration(const char *reason) {
	calibrationPhase = CAL_IDLE;
	if (!configureIMU()) {
		imuOK = false;
		setDiagnosticFault(DIAG_IMU_INIT, true);
	}
	print("加速度计校准取消：%s；原校准参数未更改。\n", reason);
}

static bool finishAccelCalibration() {
	Vector accMin = calibrationFaces[0];
	Vector accMax = calibrationFaces[0];
	for (int i = 1; i < 6; ++i) {
		accMin.x = min(accMin.x, calibrationFaces[i].x);
		accMin.y = min(accMin.y, calibrationFaces[i].y);
		accMin.z = min(accMin.z, calibrationFaces[i].z);
		accMax.x = max(accMax.x, calibrationFaces[i].x);
		accMax.y = max(accMax.y, calibrationFaces[i].y);
		accMax.z = max(accMax.z, calibrationFaces[i].z);
	}
	Vector newScale = (accMax - accMin) / (2.0f * ONE_G);
	Vector newBias = (accMax + accMin) / 2.0f;
	if (!newScale.valid() || !newBias.valid() ||
		newScale.x < 0.8f || newScale.x > 1.2f ||
		newScale.y < 0.8f || newScale.y > 1.2f ||
		newScale.z < 0.8f || newScale.z > 1.2f ||
		fabsf(newBias.x) > 2.0f || fabsf(newBias.y) > 2.0f || fabsf(newBias.z) > 2.0f) {
		print("六面结果异常：bias=(%.3f,%.3f,%.3f) scale=(%.3f,%.3f,%.3f)。\n",
			newBias.x, newBias.y, newBias.z, newScale.x, newScale.y, newScale.z);
		abortAccelCalibration("结果异常，请检查六个面的方向与静止状态后重新运行 ca 校准");
		return false;
	}
	if (!configureIMU()) {
		imuOK = false;
		setDiagnosticFault(DIAG_IMU_INIT, true);
		calibrationPhase = CAL_IDLE;
		print("IMU重新配置失败，原校准参数未更改并禁止解锁。\n");
		return false;
	}
	accScale = newScale;
	accBias = newBias;
	setDiagnosticFault(DIAG_IMU_INIT, false);
	printIMUCalibration();
	bool saved = true;
	saved = saveParameterNow("IMU_ACC_BIAS_X") && saved;
	saved = saveParameterNow("IMU_ACC_BIAS_Y") && saved;
	saved = saveParameterNow("IMU_ACC_BIAS_Z") && saved;
	saved = saveParameterNow("IMU_ACC_SCALE_X") && saved;
	saved = saveParameterNow("IMU_ACC_SCALE_Y") && saved;
	saved = saveParameterNow("IMU_ACC_SCALE_Z") && saved;
	calibrationPhase = CAL_IDLE;
	if (saved) print("✓加速度计六面校准完成，参数已加入锁定后的 NVS 统一保存队列；等待保存完成后再断电。放正机身后执行 ps 查看姿态。\n");
	else print("⚠加速度计校准已应用于本次运行，但参数未能加入 NVS 保存队列；重新运行 ca。\n");
	return true;
}

void updateAccelCalibration() {
	if (calibrationPhase == CAL_IDLE) return;
	extern bool armed;
	extern bool motorsActive();
	if (armed || motorsActive()) {
		abortAccelCalibration("检测到电机已启动");
		return;
	}
	const uint32_t now = millis();
	switch (calibrationPhase) {
	case CAL_SETTLE:
		if ((uint32_t)(now - calibrationPhaseStarted) >= 8000) {
			calibrationPhase = CAL_SAMPLE;
			calibrationPhaseStarted = now;
			calibrationLastSequence = calibrationRawAccSequence;
			calibrationSum = Vector(0, 0, 0);
			calibrationSampleCount = 0;
			calibrationProgressShown = false;
			print("第%d面开始采样。\n", calibrationFace + 1);
		}
		break;
	case CAL_SAMPLE:
		if (calibrationRawAccSequence != calibrationLastSequence) {
			calibrationLastSequence = calibrationRawAccSequence;
			if (calibrationRawAcc.valid()) {
				calibrationSum = calibrationSum + calibrationRawAcc;
				++calibrationSampleCount;
				if (!calibrationProgressShown && calibrationSampleCount >= 500) {
					print("第%d面采样进度：%d/1000。\n", calibrationFace + 1, calibrationSampleCount);
					calibrationProgressShown = true;
				}
			}
		}
		if (calibrationSampleCount >= 1000 || (uint32_t)(now - calibrationPhaseStarted) >= 2500) {
			if (calibrationSampleCount < 950) {
				char reason[64];
				snprintf(reason, sizeof(reason), "第%d面有效样本不足（%d/1000）",
					calibrationFace + 1, calibrationSampleCount);
				abortAccelCalibration(reason);
				return;
			}
			calibrationFaces[calibrationFace] = calibrationSum / (float)calibrationSampleCount;
			print("第%d面采样完成（%d个有效样本）。\n", calibrationFace + 1, calibrationSampleCount);
			if (calibrationFace == 5) finishAccelCalibration();
			else {
				calibrationPhase = CAL_GAP;
				calibrationPhaseStarted = now;
			}
		}
		break;
	case CAL_GAP:
		if ((uint32_t)(now - calibrationPhaseStarted) >= 1000) {
			++calibrationFace;
			calibrationPhase = CAL_SETTLE;
			calibrationPhaseStarted = now;
			printCalibrationFace();
		}
		break;
	case CAL_IDLE:
		break;
	}
}

void printIMUCalibration() {
	print("gyro bias: %f %f %f\n", gyroBias.x, gyroBias.y, gyroBias.z);
	print("accel bias: %f %f %f\n", accBias.x, accBias.y, accBias.z);
	print("accel scale: %f %f %f\n", accScale.x, accScale.y, accScale.z);
}

void printIMUInfo() {
	imu.status() ? print("status: ERROR %d\n", imu.status()) : print("status: OK\n");
	print("model: %s\n", imu.getModel());
	print("who am I: 0x%02X\n", imu.whoAmI());
	print("rate: %.0f\n", loopRate);
	print("gyro: %f %f %f\n", gyro.x, gyro.y, gyro.z);
	print("acc: %f %f %f\n", acc.x, acc.y, acc.z);
	if (!imu.waitForData(10)) {
		print("IMU无新数据，无法读取原始值。\n");
		return;
	}
	Vector rawGyro, rawAcc;
	imu.getGyro(rawGyro.x, rawGyro.y, rawGyro.z);
	imu.getAccel(rawAcc.x, rawAcc.y, rawAcc.z);
	print("raw gyro: %f %f %f\n", rawGyro.x, rawGyro.y, rawGyro.z);
	print("raw acc: %f %f %f\n", rawAcc.x, rawAcc.y, rawAcc.z);
}
