// 主程序
// 使用Arduino IDE 2.3.x以上版本编译，不支持老版本的1.8.19！
// 必须安装ESP32开发板核心库（即esp32 by Espressif Systems）和依赖（MAVLINK等）

#include "vector.h"
#include "quaternion.h"
#include "util.h"
#include "board_config.h"
#include "diagnostics.h"
#include "control.h"
#include "flight_log.h"
#include "log_transfer.h"
#include "system_log.h"
#include "web_rc_input.h"
#include <esp_system.h>

// WiFi 和 Web 遥控器开关由 board_config.h 按芯片自动设置：
// 如需手动覆盖，在此处 #undef 后重新 #define
#define WIFI_ENABLED    BOARD_WIFI_ENABLED
#define WEB_RC_ENABLED  BOARD_WEB_RC_ENABLED

double t = NAN; // 当前步进时间，单位：秒
float dt; // 与上一步进的时间差，单位：秒
float controlRoll, controlPitch, controlYaw, controlThrottle; // 飞手输入指令，范围 [-1, 1]
float controlMode = NAN;
Vector gyro; // 陀螺仪数据
Vector acc; // 加速度计数据，单位：m/s/s
Vector rates; // 滤波后的角速度，单位：rad/s
Quaternion attitude; // 估计出的姿态（四元数）
bool landed; // are we landed and stationary

void sendMotors();
void descend();
void updateAccelCalibration();
#if WIFI_ENABLED
void setupWiFi();
void serviceWiFi();
bool configWiFi(bool ap, const char *ssid, const char *password);
void printWiFiInfo();
#endif

static const char *resetReasonName(esp_reset_reason_t reason) {
	switch (reason) {
		case ESP_RST_POWERON: return "POWERON";
		case ESP_RST_EXT: return "EXTERNAL";
		case ESP_RST_SW: return "SOFTWARE";
		case ESP_RST_PANIC: return "PANIC";
		case ESP_RST_INT_WDT: return "INT_WDT";
		case ESP_RST_TASK_WDT: return "TASK_WDT";
		case ESP_RST_WDT: return "WDT";
		case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
		case ESP_RST_BROWNOUT: return "BROWNOUT";
		case ESP_RST_SDIO: return "SDIO";
		default: return "UNKNOWN";
	}
}

void setup() {
	Serial.begin(115200); // 初始化串口，波特率115200
	disableBrownOut(); // 禁用ESP32低压复位检测，防止电机启动瞬间电压跌落导致误复位
	char bootEvent[112];
	snprintf(bootEvent, sizeof(bootEvent), "reset=%s(%d) chip=%s revision=%d", resetReasonName(esp_reset_reason()),
		(int)esp_reset_reason(), ESP.getChipModel(), ESP.getChipRevision());
	print("琛光科技 CF-Drone Flight Controller\n"); // 品牌标识（LICENSE 附加条款第二条要求固件启动输出保留品牌名称）
	print("BOOT build=%s %s chip=%s revision=%d cpu_mhz=%lu flash_bytes=%lu reset=%s(%d) free_heap=%lu\n",
		__DATE__, __TIME__, ESP.getChipModel(), ESP.getChipRevision(),
		(unsigned long)ESP.getCpuFreqMHz(), (unsigned long)ESP.getFlashChipSize(),
		resetReasonName(esp_reset_reason()), (int)esp_reset_reason(), (unsigned long)ESP.getFreeHeap());
	print("程序开始初始化！\n");
	setupParameters(); // 从Flash加载参数（未存储时使用默认值）
	initializeSystemLog(); // 恢复上次启动的故障/系统事件历史
	recordSystemLogEvent("BOOT", bootEvent);
	setupLED(); // 初始化状态指示灯
	setupMotors(); // 初始化电机输出（PWM/DShot）
	setLED(true); // 点亮LED，提示正在初始化
#if WIFI_ENABLED
	setupWiFi(); // 初始化WiFi（用于Web遥控/MAVLink等）
#endif
#if WEB_RC_ENABLED
	setupWebRC();  // 初始化Web遥控器
#endif
	setupIMU(); // 初始化IMU（陀螺仪/加速度计）
	setupRC(); // 初始化遥控接收机（SBUS/ELRS等协议）
	initializeDiagnostics();
	setLED(false); // 熄灭LED，提示初始化完成
	print("程序初始化完成！\n");
	print("================================\n");
}

void loop() {
	uint32_t stageStarted = micros();
	readIMU(); // 读取IMU原始数据（陀螺仪/加速度计），并完成校准与坐标旋转
	recordLoopStage("imu", micros() - stageStarted);
	step(); // 计算主循环步进时间 t 与时间差 dt，并统计循环频率
	recordLoopTiming(dt);
	stageStarted = micros();
	readRC(); // 读取遥控接收机输入
#if WEB_RC_ENABLED
	readWebRC();  // 读取Web遥控器输入
	processConsoleCommandQueue(); // 将网页命令放到主循环执行，避免阻塞HTTP回调
#endif
	recordLoopStage("rc_web", micros() - stageStarted);
	stageStarted = micros();
	uint32_t groupStarted = stageStarted;
	estimate(); // 姿态与状态估计（互补滤波融合IMU数据）
	recordLoopStage("estimate", micros() - stageStarted);
	stageStarted = micros();
	updateBatteryVoltage(); // 更新电池电压采样与低电压保护判断
	recordLoopStage("battery_adc", micros() - stageStarted);
	stageStarted = micros();
	control(); // 飞控核心：姿态环PID解算，输出电机控制量
	recordLoopStage("control_law", micros() - stageStarted);
	stageStarted = micros();
	sendMotors(); // 将电机控制量输出到电机（PWM/DShot）
	recordLoopStage("motor_out", micros() - stageStarted);
	recordLoopStage("control", micros() - groupStarted);
	stageStarted = micros();
	groupStarted = stageStarted;
	handleInput(); // 处理串口/Web控制台输入命令
	serviceFlightLogExport(); // 限额发送，解锁时取消
	updateAccelCalibration(); // 六面校准逐帧推进，不阻塞飞控主循环
	recordLoopStage("serial_input", micros() - stageStarted);
	stageStarted = micros();
#if WIFI_ENABLED
	processMavlink(); // 处理MAVLink通信
#endif
	recordLoopStage("mavlink", micros() - stageStarted);
	stageStarted = micros();
	groupStarted = stageStarted;
	logData(); // 记录飞行日志数据
	stageStarted = micros();
	syncParameters(); // 参数变更后延迟写入Flash，避免频繁擦写
	recordLoopStage("param_sync", micros() - stageStarted);
	updateLED(); // 根据当前飞行状态刷新LED指示效果
	updateDiagnostics();
#if WIFI_ENABLED
	serviceWiFi(); // 更新连接状态、配网DNS与超时回退
#endif
	recordLoopStage("maintenance", micros() - groupStarted);
}
