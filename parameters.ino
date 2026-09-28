// 参数存储在闪存
// Parameters storage in flash memory

#include <Preferences.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "board_config.h"
#include "diagnostics.h"
#include "system_log.h"

extern float channelZero[16];
extern float channelMax[16];
extern float rollChannel, pitchChannel, throttleChannel, yawChannel, modeChannel;
#if BOARD_WIFI_ENABLED
extern int wifiMode, udpLocalPort, udpRemotePort;
#endif
extern int motorPins[4];
extern int pwmFrequency, pwmResolution, pwmStop, pwmMin, pwmMax;
extern float motThrMin;
extern float motThrMax;
extern bool armed;
#if BOARD_WIFI_ENABLED
extern int mavlinkSysId;
extern Rate telemetrySlow, telemetryFast;
#endif
extern float rcLossTimeout, descendTime, descendThrust;
extern int flightModes[3];
extern const int ALTHOLD;
extern Vector accBias, accScale;
extern Vector imuRotation;
extern LowPassFilter<Vector> gyroBiasFilter;
extern int rcRxPin;
extern int rcProtocol;   // 遥控协议：0=SBUS, 1=CRSF(ELRS)
extern int rcTxPin;      // 遥测回传 TX 引脚，-1=不启用
extern int rcBaud;       // 串口波特率
extern float trimRoll, trimPitch;         // 软件配平参数，定义于 control.ino
extern float levelGateThreshold;         // 摇杆门控阈值，定义于 estimate.ino
extern float levelBiasGain;              // Mahony I 项增益，定义于 estimate.ino
extern Vector levelGyroBias;             // Mahony 虚拟陀螺偏置，定义于 estimate.ino

Preferences storage;
static bool parameterStorageReady = false;
static uint16_t dirtyParameterCount = 0;

struct Parameter {
	const char *name; // max length is 15 (Preferences key limit)
	bool integer;
	union { float *f; int *i; };
	float cache; // what's stored in flash
	bool dirty = false;
	void (*callback)(); // called after parameter change
	Parameter(const char *name, float *variable, void (*callback)() = nullptr) : name(name), integer(false), f(variable), cache(0), callback(callback) {};
	Parameter(const char *name, int *variable, void (*callback)() = nullptr) : name(name), integer(true), i(variable), cache(0), callback(callback) {};
	float getValue() const { return integer ? *i : *f; }
	void setValue(const float value) { if (integer) *i = (int)value; else *f = value; }
};

Parameter parameters[] = {
	// ===== 控制（角速率内环 PID）=====
	// 角速率环直接控制机身旋转速度，是最内层控制回路。
	// P：比例增益，值越大响应越快，过大会振荡；
	// I：积分增益，消除稳态误差（如电机差异），过大会产生积分饱和；
	// D：微分增益，抑制超调，过大会放大高频噪声；
	// WU（windup）：积分饱和限制幅值，防止 I 项累积过大。
	{"CTL_R_RATE_P",  &rollRatePID.p},      // 横滚角速率 P 增益（rad/s → 力矩）
	{"CTL_R_RATE_I",  &rollRatePID.i},      // 横滚角速率 I 增益
	{"CTL_R_RATE_D",  &rollRatePID.d},      // 横滚角速率 D 增益
	{"CTL_R_RATE_WU", &rollRatePID.windup}, // 横滚角速率积分限幅（±windup）
	{"CTL_P_RATE_P",  &pitchRatePID.p},     // 俯仰角速率 P 增益
	{"CTL_P_RATE_I",  &pitchRatePID.i},     // 俯仰角速率 I 增益
	{"CTL_P_RATE_D",  &pitchRatePID.d},     // 俯仰角速率 D 增益
	{"CTL_P_RATE_WU", &pitchRatePID.windup},// 俯仰角速率积分限幅
	{"CTL_Y_RATE_P",  &yawRatePID.p},       // 偏航角速率 P 增益（偏航惯量小，通常需更大值）
	{"CTL_Y_RATE_I",  &yawRatePID.i},       // 偏航角速率 I 增益
	{"CTL_Y_RATE_D",  &yawRatePID.d},       // 偏航角速率 D 增益

	// ===== 控制（角度外环 PID）=====
	// 角度环将目标姿态角误差转换为角速率指令，输出给内环。
	// I 项用于自动补偿物理不对称引起的固定悬停偏差，需同时配置 WU 积分限幅才能生效。
	{"CTL_R_P",  &rollPID.p},      // 横滚角度 P 增益（角度误差 rad → 角速率指令 rad/s）
	{"CTL_R_I",  &rollPID.i},      // 横滚角度 I 增益，用于补偿物理不对称固定偏差（默认 0.5）
	{"CTL_R_D",  &rollPID.d},      // 横滚角度 D 增益（一般为 0）
	{"CTL_R_WU", &rollPID.windup}, // 横滚角度积分限幅（rad/s），I 项非 0 时必须配置，防止积分发散
	{"CTL_P_P",  &pitchPID.p},     // 俯仰角度 P 增益
	{"CTL_P_I",  &pitchPID.i},     // 俯仰角度 I 增益（默认 0.5）
	{"CTL_P_D",  &pitchPID.d},     // 俯仰角度 D 增益（一般为 0）
	{"CTL_P_WU", &pitchPID.windup},// 俯仰角度积分限幅（rad/s）
	{"CTL_Y_P",  &yawPID.p},       // 偏航角度 P 增益

	// ===== 控制（限制值）=====
	{"CTL_P_RATE_MAX", &maxRate.y}, // 俯仰最大角速率限制（rad/s），超出摇杆指令会被截断
	{"CTL_R_RATE_MAX", &maxRate.x}, // 横滚最大角速率限制（rad/s）
	{"CTL_Y_RATE_MAX", &maxRate.z}, // 偏航最大角速率限制（rad/s）
	{"CTL_TILT_MAX",   &tiltMax},   // 稳定模式最大允许倾斜角（rad），超出时角度外环被限幅

	// ===== 控制（软件配平）=====
	// 用于补偿飞机重心偏移或电机推力不一致导致的悬停偏移，无需物理调整。
	{"CTL_TRIM_ROLL",  &trimRoll},  // 横滚配平角（rad），正值向右滚，调整步长建议 0.005 rad
	{"CTL_TRIM_PITCH", &trimPitch}, // 俯仰配平角（rad），正值向后仰，调整步长建议 0.005 rad

	// ===== 控制（飞行模式）=====
	// 遥控器模式通道（RC_MODE）将摇杆三挡位映射到对应飞行模式枚举值：
	// 0=RAW（直通），1=ACRO（特技），2=STAB（自稳），4=AUTO；3=ALTHOLD需气压计，当前六轴硬件不支持
	{"CTL_FLT_MODE_0", &flightModes[0]}, // 模式通道第 0 挡对应的飞行模式
	{"CTL_FLT_MODE_1", &flightModes[1]}, // 模式通道第 1 挡对应的飞行模式
	{"CTL_FLT_MODE_2", &flightModes[2]}, // 模式通道第 2 挡对应的飞行模式

	// ===== IMU（传感器安装旋转补偿）=====
	// 若 IMU 安装方向与机体坐标系不一致，在此设置旋转角（rad）进行软件补偿。
	{"IMU_ROT_ROLL",  &imuRotation.x}, // IMU 绕机体 X 轴（横滚轴）的安装偏转角（rad）
	{"IMU_ROT_PITCH", &imuRotation.y}, // IMU 绕机体 Y 轴（俯仰轴）的安装偏转角（rad）
	{"IMU_ROT_YAW",   &imuRotation.z}, // IMU 绕机体 Z 轴（偏航轴）的安装偏转角（rad）

	// ===== IMU（陀螺仪偏置滤波）=====
	// 陀螺仪静态偏置通过低通滤波器（指数移动平均）在上电初始化时估计。
	// alpha 越小滤波越强（收敛越慢），越大收敛越快但对振动噪声更敏感。
	{"IMU_GYRO_BIAS_A", &gyroBiasFilter.alpha}, // 陀螺偏置低通滤波器系数 alpha（0~1）

	// ===== IMU（加速度计标定）=====
	// 出厂或重新安装后需对加速度计进行六面标定，结果存入以下参数。
	// 标定公式：acc_corrected = (acc_raw - bias) / scale
	{"IMU_ACC_BIAS_X", &accBias.x},  // 加速度计 X 轴零偏（m/s²）
	{"IMU_ACC_BIAS_Y", &accBias.y},  // 加速度计 Y 轴零偏（m/s²）
	{"IMU_ACC_BIAS_Z", &accBias.z},  // 加速度计 Z 轴零偏（m/s²）
	{"IMU_ACC_SCALE_X", &accScale.x}, // 加速度计 X 轴增益校正因子（无量纲，理想值为 1.0）
	{"IMU_ACC_SCALE_Y", &accScale.y}, // 加速度计 Y 轴增益校正因子
	{"IMU_ACC_SCALE_Z", &accScale.z}, // 加速度计 Z 轴增益校正因子

	// ===== 姿态估计 =====
	// 采用互补滤波：陀螺仪积分提供高频动态，加速度计提供低频重力修正。
	{"EST_ACC_WEIGHT",   &accWeight},          // 加速度计对姿态的修正权重（0~1），越大修正越强，但对振动越敏感
	{"EST_RATES_LPF_A",  &ratesFilter.alpha},  // 角速率低通滤波器系数 alpha（0~1），越小截止频率越低（约 40Hz@0.2）
	{"EST_LVL_GATE_THR", &levelGateThreshold}, // 摇杆门控阈值（0~1），摇杆偏转超过此比例时 applyLevel 权重渐变为零，防止打杆后松杆漂移
	{"EST_LVL_BIAS_GAIN",&levelBiasGain},      // Mahony I 项增益：重力误差积分进虚拟陀螺偏置的速率，越大收敛越快（约 30s@0.00002）

	// ===== 电机（引脚配置）=====
	// 修改后立即调用 setupMotors() 重新初始化 LEDC 通道，无需重启。
	{"MOT_PIN_FL", &motorPins[MOTOR_FRONT_LEFT],  setupMotors}, // 左前电机 GPIO 引脚号
	{"MOT_PIN_FR", &motorPins[MOTOR_FRONT_RIGHT], setupMotors}, // 右前电机 GPIO 引脚号
	{"MOT_PIN_RL", &motorPins[MOTOR_REAR_LEFT],   setupMotors}, // 左后电机 GPIO 引脚号
	{"MOT_PIN_RR", &motorPins[MOTOR_REAR_RIGHT],  setupMotors}, // 右后电机 GPIO 引脚号

	// ===== 电机（PWM 配置）=====
	// 直驱 MOSFET 模式：pwmMax=-1，pwmStop/pwmMin=0，使用纯占空比控制。
	// ESC 模式：pwmMax 设为脉宽上限（μs），pwmMin 设为脉宽下限，pwmFrequency 降至 400Hz。
	{"MOT_PWM_FREQ", &pwmFrequency, setupMotors}, // PWM 频率（Hz），MOSFET 直驱建议 25000，ESC 建议 400
	{"MOT_PWM_RES",  &pwmResolution, setupMotors},// PWM 分辨率（bit），决定 LEDC 计数上限为 2^n-1，常用 10bit
	{"MOT_PWM_STOP", &pwmStop},                   // 电机停转时的 PWM 值；纯占空比模式下为 0，ESC 模式下为怠速脉宽（μs）
	{"MOT_PWM_MIN",  &pwmMin},                    // 电机最小 PWM 值；纯占空比模式下为 0，ESC 模式下为最低脉宽（μs）
	{"MOT_PWM_MAX",  &pwmMax},                    // 电机最大 PWM 值；-1=纯占空比模式（MOSFET），正整数=ESC 脉宽上限（μs）

	// ===== 电机（推力映射）=====
	// 将归一化油门（0~1）线性映射到电机推力输出范围，保留余量供姿态修正使用。
	{"MOT_THR_MIN", &motThrMin}, // 推力下限（0~1），摇杆最低位时的电机输出，建议≥0.05 防失速
	{"MOT_THR_MAX", &motThrMax}, // 推力上限（0~1），摇杆最高位时的电机输出，建议≤0.9 保留姿态修正余量

	// ===== 遥控（通道校准）=====
	// 原始 RC 信号经校准转换为归一化值（-1~1 或 0~1）。
	// channelZero[n]：摇杆中立点原始值；channelMax[n]：摇杆最大行程原始值。
	// 校准公式：normalized = (raw - zero) / max
	{"RC_ZERO_0", &channelZero[0]}, // 通道 0 中立点（校准用）
	{"RC_ZERO_1", &channelZero[1]}, // 通道 1 中立点
	{"RC_ZERO_2", &channelZero[2]}, // 通道 2 中立点
	{"RC_ZERO_3", &channelZero[3]}, // 通道 3 中立点
	{"RC_ZERO_4", &channelZero[4]}, // 通道 4 中立点
	{"RC_ZERO_5", &channelZero[5]}, // 通道 5 中立点
	{"RC_ZERO_6", &channelZero[6]}, // 通道 6 中立点
	{"RC_ZERO_7", &channelZero[7]}, // 通道 7 中立点
	{"RC_MAX_0",  &channelMax[0]},  // 通道 0 行程幅值（校准用）
	{"RC_MAX_1",  &channelMax[1]},  // 通道 1 行程幅值
	{"RC_MAX_2",  &channelMax[2]},  // 通道 2 行程幅值
	{"RC_MAX_3",  &channelMax[3]},  // 通道 3 行程幅值
	{"RC_MAX_4",  &channelMax[4]},  // 通道 4 行程幅值
	{"RC_MAX_5",  &channelMax[5]},  // 通道 5 行程幅值
	{"RC_MAX_6",  &channelMax[6]},  // 通道 6 行程幅值
	{"RC_MAX_7",  &channelMax[7]},  // 通道 7 行程幅值

	// ===== 遥控（通道功能映射）=====
	// 指定各功能轴使用的物理通道编号（0-based 整数）。
	{"RC_ROLL",     &rollChannel},     // 横滚通道编号
	{"RC_PITCH",    &pitchChannel},    // 俯仰通道编号
	{"RC_THROTTLE", &throttleChannel}, // 油门通道编号
	{"RC_YAW",      &yawChannel},      // 偏航通道编号
	{"RC_MODE",     &modeChannel},     // 飞行模式切换通道编号（三挡拨杆）

	// ===== 遥控（串口硬件配置）=====
	// 四个参数均绑定 setupRC 回调：修改后立即重新初始化 RC 串口，无需重启即可生效。
	{"RC_RX_PIN",   &rcRxPin,    setupRC}, // 遥控接收器数据输入 GPIO 引脚（UART RX）
	{"RC_PROTOCOL", &rcProtocol, setupRC}, // 遥控协议：0=SBUS（电平反相），1=CRSF/ELRS（正逻辑 420000 baud）
	{"RC_TX_PIN",   &rcTxPin,    setupRC}, // 遥测回传 GPIO 引脚（UART TX）；-1=不启用回传
	{"RC_BAUD",     &rcBaud,     setupRC}, // 遥控串口波特率（bps）；SBUS=100000，CRSF=420000

#if BOARD_WIFI_ENABLED
	// ===== WiFi =====
	{"WIFI_MODE",     &wifiMode},      // WiFi 工作模式：0=关闭，1=STA（连接已有热点），2=AP（自建配网热点）
	{"WIFI_LOC_PORT", &udpLocalPort},  // 本地 UDP 监听端口（地面站发送到此端口）
	{"WIFI_REM_PORT", &udpRemotePort}, // 远端 UDP 目标端口（飞控主动发送到此端口）

	// ===== MAVLink 遥测 =====
	{"MAV_SYS_ID",    &mavlinkSysId},       // MAVLink 系统 ID（1~254），区分多机时需唯一
	{"MAV_RATE_SLOW", &telemetrySlow.rate}, // 慢速遥测发送频率（Hz），用于心跳、电池等低频数据
	{"MAV_RATE_FAST", &telemetryFast.rate}, // 快速遥测发送频率（Hz），用于姿态、角速率等高频数据
#endif

	// ===== 故障保护 =====
	{"SF_RC_LOSS_TIME",  &rcLossTimeout}, // RC 信号丢失超时阈值（秒），超时后进入自动下降模式
	{"SF_DESCEND_TIME",  &descendTime},   // 自动下降时过渡到目标推力的时间（秒）
	{"SF_DESCEND_THRUST", &descendThrust}, // 自动下降目标推力（归一化指令，需按机体验证）
};

static bool startsWith(const char *value, const char *prefix) {
	return strncmp(value, prefix, strlen(prefix)) == 0;
}

static bool within(float value, float low, float high) {
	return isfinite(value) && value >= low && value <= high;
}

static bool validParameterValue(const char *name, bool integer, float value) {
	if (!isfinite(value)) {
		// Channel indices start as NaN until RC calibration. This is an intentional
		// "unassigned" sentinel, so it must not be reported as corrupted config.
		if (isnan(value) && (!strcmp(name, "RC_ROLL") || !strcmp(name, "RC_PITCH") ||
			!strcmp(name, "RC_THROTTLE") || !strcmp(name, "RC_YAW") || !strcmp(name, "RC_MODE")))
			return true;
		return false;
	}
	if (integer && (value < INT_MIN || value > INT_MAX || floorf(value) != value)) return false;

	if (startsWith(name, "CTL_FLT_MODE_"))
		return within(value, 0, 4) && floorf(value) == value && (int)value != ALTHOLD;
	if (!strcmp(name, "RC_ROLL") || !strcmp(name, "RC_PITCH") || !strcmp(name, "RC_THROTTLE") ||
		!strcmp(name, "RC_YAW") || !strcmp(name, "RC_MODE"))
		return within(value, 0, 15) && floorf(value) == value;
	if (startsWith(name, "RC_ZERO_")) {
		int channel = atoi(name + 8);
		if (!within(value, 0, 2047) || channel < 0 || channel >= 16) return false;
		return channelMax[channel] == 0 || fabsf(value - channelMax[channel]) > 10;
	}
	if (startsWith(name, "RC_MAX_")) {
		int channel = atoi(name + 7);
		if (!within(value, 0, 2047) || channel < 0 || channel >= 16) return false;
		return value == 0 || fabsf(value - channelZero[channel]) > 10;
	}
	if (startsWith(name, "MOT_PIN_")) return within(value, 0, 48) && floorf(value) == value;
	if (!strcmp(name, "MOT_PWM_FREQ")) return within(value, 100, 80000) && floorf(value) == value;
	if (!strcmp(name, "MOT_PWM_RES")) return within(value, 1, 20) && floorf(value) == value;
	if (!strcmp(name, "MOT_PWM_STOP"))
		return within(value, 0, 10000) && (pwmMax < 0 || value <= pwmMax);
	if (!strcmp(name, "MOT_PWM_MIN"))
		return within(value, 0, 10000) && (pwmMax < 0 || value < pwmMax);
	if (!strcmp(name, "MOT_PWM_MAX"))
		return value == -1 || (within(value, 1, 10000) && value > pwmMin && value >= pwmStop);
	if (!strcmp(name, "MOT_THR_MIN")) return within(value, 0, 1) && value <= motThrMax;
	if (!strcmp(name, "MOT_THR_MAX")) return within(value, 0, 1) && value >= motThrMin;
	if (strstr(name, "_RATE_MAX")) return within(value, 0.1f, 20);
	if (startsWith(name, "CTL_TRIM_")) return within(value, -0.5f, 0.5f);
	if (startsWith(name, "CTL_") && (strstr(name, "_RATE_") || strstr(name, "_WU")))
		return within(value, 0, strstr(name, "_WU") ? 20 : 100);
	if (startsWith(name, "CTL_") && (strstr(name, "_P") || strstr(name, "_I") || strstr(name, "_D")))
		return within(value, 0, 100);
	if (!strcmp(name, "CTL_TILT_MAX")) return within(value, 0.05f, 1.3963f);
	if (startsWith(name, "IMU_ROT_")) return within(value, -PI, PI);
	if (!strcmp(name, "IMU_GYRO_BIAS_A") || !strcmp(name, "EST_RATES_LPF_A")) return within(value, 0, 1);
	if (startsWith(name, "IMU_ACC_SCALE_")) return within(value, 0.8f, 1.2f);
	if (startsWith(name, "IMU_ACC_BIAS_")) return within(value, -2, 2);
	if (!strcmp(name, "EST_ACC_WEIGHT") || !strcmp(name, "EST_LVL_GATE_THR")) return within(value, 0, 1);
	if (!strcmp(name, "EST_LVL_BIAS_GAIN")) return within(value, 0, 0.01f);
	if (!strcmp(name, "RC_RX_PIN") || !strcmp(name, "RC_TX_PIN"))
		return within(value, -1, 48) && floorf(value) == value;
	if (!strcmp(name, "RC_PROTOCOL")) return within(value, 0, 1) && floorf(value) == value;
	if (!strcmp(name, "RC_BAUD")) return within(value, 9600, 1000000) && floorf(value) == value;
	if (!strcmp(name, "WIFI_MODE")) return within(value, 0, 2) && floorf(value) == value;
	if (!strcmp(name, "WIFI_LOC_PORT") || !strcmp(name, "WIFI_REM_PORT"))
		return within(value, 1, 65535) && floorf(value) == value;
	if (!strcmp(name, "MAV_SYS_ID")) return within(value, 1, 254) && floorf(value) == value;
	if (!strcmp(name, "MAV_RATE_SLOW") || !strcmp(name, "MAV_RATE_FAST")) return within(value, 0.1f, 100);
	if (!strcmp(name, "SF_RC_LOSS_TIME")) return within(value, 0.1f, 10);
	if (!strcmp(name, "SF_DESCEND_TIME")) return within(value, 1, 120);
	if (!strcmp(name, "SF_DESCEND_THRUST")) return within(value, 0.05f, 0.5f);
	return true;
}

static bool allParametersValid() {
	for (auto &parameter : parameters)
		if (!validParameterValue(parameter.name, parameter.integer, parameter.getValue())) return false;
	return true;
}

static void reportInvalidParameters(const char *phase) {
	for (auto &parameter : parameters) {
		const float value = parameter.getValue();
		if (validParameterValue(parameter.name, parameter.integer, value)) continue;
		print("PARAM_INVALID phase=%s name=%s value=%.7g integer=%u\n",
			phase, parameter.name, value, parameter.integer ? 1 : 0);
		char message[48];
		snprintf(message, sizeof(message), "%s %s=%.6g", phase, parameter.name, value);
		recordSystemLogEvent("PARAM_BAD", message);
	}
}

void printInvalidParameterValues() {
	bool found = false;
	for (auto &parameter : parameters) {
		const float value = parameter.getValue();
		if (validParameterValue(parameter.name, parameter.integer, value)) continue;
		print("PARAM_INVALID name=%s value=%.7g integer=%u\n",
			parameter.name, value, parameter.integer ? 1 : 0);
		found = true;
	}
	if (!found) print("PARAMETER active but all current parameter values pass validation.\n");
}

void setupParameters() {
	print("Setup parameters\n");
	dirtyParameterCount = 0;
	parameterStorageReady = storage.begin("flix", false);
	if (!parameterStorageReady) {
		// Never erase the full NVS partition here: it also contains Wi-Fi credentials
		// and persistent system logs. Keep the existing data and run with defaults.
		print("[NVS] Preferences.begin 失败；为保护 Wi-Fi 凭据和日志，本次启动不擦除 NVS。参数无法持久化。\n");
	}
	recordSystemLogEvent("NVS", parameterStorageReady ? "preferences=ready" : "preferences=failed");
	// Earlier firmware used the inverse WIFI_MODE numbering (1=AP, 2=STA).
	// Convert existing values once while retaining the user's effective mode.
#if BOARD_WIFI_ENABLED
	// NVS key names allow at most 15 characters; WIFI_MODE_SCHEMA was 16,
	// so it was never stored and the legacy conversion ran after every reboot.
	static const char *wifiModeVersionKey = "WIFI_MODE_VER";
	static const uint8_t wifiModeVersion = 1;
	if (storage.getUChar(wifiModeVersionKey, 0) != wifiModeVersion) {
		int legacyMode = -1;
		int migratedMode = -1;
		if (storage.isKey("WIFI_MODE")) {
			legacyMode = (int)storage.getFloat("WIFI_MODE", 0);
			migratedMode = legacyMode == 1 ? 2 : legacyMode == 2 ? 1 : legacyMode;
			if (migratedMode != legacyMode &&
				storage.putFloat("WIFI_MODE", (float)migratedMode) != sizeof(float)) {
				migratedMode = -1;
			}
		}
		const bool modeReady = legacyMode < 0 || migratedMode >= 0;
		const bool versionWritten = modeReady && storage.putUChar(wifiModeVersionKey, wifiModeVersion) > 0;
		const bool versionVerified = versionWritten && storage.getUChar(wifiModeVersionKey, 0) == wifiModeVersion;
		if (!versionVerified && legacyMode >= 0 && migratedMode != legacyMode) {
			// Leave the old value intact if the version marker cannot be committed,
			// so a later boot can retry the same one-time conversion.
			storage.putFloat("WIFI_MODE", (float)legacyMode);
		}
		print("WIFI_MODE_MIGRATION old=%d new=%d version=%u result=%s\n", legacyMode, migratedMode,
			(unsigned)storage.getUChar(wifiModeVersionKey, 0), versionVerified ? "OK" : "FAIL");
		char migrationEvent[48];
		snprintf(migrationEvent, sizeof(migrationEvent), "old=%d new=%d ver=%u result=%s", legacyMode,
			migratedMode, (unsigned)storage.getUChar(wifiModeVersionKey, 0), versionVerified ? "OK" : "FAIL");
		recordSystemLogEvent("WIFI_MIG", migrationEvent);
	}
#endif
	// Read parameters from storage
	for (auto &parameter : parameters) {
		if (!storage.isKey(parameter.name)) {
			storage.putFloat(parameter.name, parameter.getValue()); // store default value
		}
		float stored = storage.getFloat(parameter.name, parameter.getValue());
		if (!validParameterValue(parameter.name, parameter.integer, stored)) {
			stored = parameter.getValue();
			storage.putFloat(parameter.name, stored);
			setDiagnosticFault(DIAG_PARAMETER, true);
			print("[参数诊断] %s 存储值无效或越界，已回退默认值。\n", parameter.name);
		}
		parameter.setValue(stored);
		parameter.cache = parameter.getValue();
		parameter.dirty = false;
	}
	// 存储异常值已回退默认值后，当前参数集若全部有效则清除活动故障；历史次数仍保留。
	const bool parametersValid = allParametersValid();
	if (!parametersValid) reportInvalidParameters("load");
	setDiagnosticFault(DIAG_PARAMETER, !parametersValid);
}

int parametersCount() {
	return sizeof(parameters) / sizeof(parameters[0]);
}

const char *getParameterName(int index) {
	if (index < 0 || index >= parametersCount()) return "";
	return parameters[index].name;
}

float getParameter(int index) {
	if (index < 0 || index >= parametersCount()) return NAN;
	return parameters[index].getValue();
}

float getParameter(const char *name) {
	for (auto &parameter : parameters) {
		if (strcasecmp(parameter.name, name) == 0) {
			return parameter.getValue();
		}
	}
	return NAN;
}

bool setParameter(const char *name, const float value) {
	for (auto &parameter : parameters) {
		if (strcasecmp(parameter.name, name) == 0) {
			if (!validParameterValue(parameter.name, parameter.integer, value)) return false;
			parameter.setValue(value);
			const float current = parameter.getValue();
			const bool dirty = current != parameter.cache &&
				!(isnan(current) && isnan(parameter.cache));
			if (dirty != parameter.dirty) {
				if (dirty) ++dirtyParameterCount;
				else if (dirtyParameterCount) --dirtyParameterCount;
				parameter.dirty = dirty;
			}
			if (parameter.callback) parameter.callback();
			setDiagnosticFault(DIAG_PARAMETER, !allParametersValid());
			return true;
		}
	}
	return false;
}

void syncParameters() {
	static Rate rate(1);
	if (!rate) return; // sync once per second
	if (armed || motorsActive() || !parameterStorageReady || !dirtyParameterCount) return;

	bool hasInvalidParameter = false;
	for (auto &parameter : parameters) {
		if (!parameter.dirty) continue;
		if (!validParameterValue(parameter.name, parameter.integer, parameter.getValue())) {
			hasInvalidParameter = true;
			continue;
		}
		const float value = parameter.getValue();
		if (value == parameter.cache || (isnan(value) && isnan(parameter.cache))) {
			parameter.dirty = false;
			if (dirtyParameterCount) --dirtyParameterCount;
			continue;
		}
		size_t written = storage.putFloat(parameter.name, value);
		if (written != sizeof(float)) continue; // 写入失败时不更新cache，保留旧值，下一轮 1Hz 周期自动重试
		parameter.cache = value;
		parameter.dirty = false;
		if (dirtyParameterCount) --dirtyParameterCount;
	}
	if (hasInvalidParameter) {
		if (!(getActiveDiagnosticFaults() & DIAG_PARAMETER)) reportInvalidParameters("runtime");
		setDiagnosticFault(DIAG_PARAMETER, true);
	} else {
		setDiagnosticFault(DIAG_PARAMETER, !allParametersValid());
	}
}

bool saveParameterNow(const char *name) {
	if (!name || armed || motorsActive() || !parameterStorageReady) return false;
	for (auto &parameter : parameters) {
		if (strcasecmp(parameter.name, name) != 0) continue;
		const float value = parameter.getValue();
		if (!validParameterValue(parameter.name, parameter.integer, value)) return false;
		const size_t written = storage.putFloat(parameter.name, value);
		if (written != sizeof(float)) return false;
		const float stored = storage.getFloat(parameter.name, NAN);
		if (!isfinite(stored) || stored != value) return false;
		parameter.cache = value;
		if (parameter.dirty && dirtyParameterCount) --dirtyParameterCount;
		parameter.dirty = false;
		return true;
	}
	return false;
}

void printParameters() {
	for (auto &parameter : parameters) {
		print("%s = %g\n", parameter.name, parameter.getValue());
	}
}

void resetParameters() {
	// Reset only registered flight parameters. Wi-Fi credentials, migration
	// metadata, and persistent system logs share this namespace and must survive.
	for (auto &parameter : parameters) storage.remove(parameter.name);
	ESP.restart();
}
