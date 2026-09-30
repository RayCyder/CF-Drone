// Implementation of command line interface 

#include "pid.h"
#include "vector.h"
#include "util.h"
#include "diagnostics.h"
#include "system_log.h"
#include "lpf.h"
#include "flight_log.h"
#include "log_transfer.h"
#include "imu_capture.h"

extern LowPassFilter<Vector> gyroBiasFilter;
static LogOutputChunk serialLogChunk;
static bool serialLogActive = false;
static uint32_t serialLogGeneration = 0, serialLogRows = 0, serialLogRow = 0;
static bool showMotd = true;
static bool serialImuCaptureActive = false;
static uint16_t serialImuCaptureRow = 0;
extern ImuCaptureBuffer imuCapture;

#if WEB_RC_ENABLED
extern bool webConsoleEnabled;
extern void webLog(const char* msg);
static TaskHandle_t webConsoleCommandTask = nullptr;

void setWebConsoleCommandOutput(bool enabled) {
	webConsoleCommandTask = enabled ? xTaskGetCurrentTaskHandle() : nullptr;
}
#endif

extern const int MOTOR_REAR_LEFT, MOTOR_REAR_RIGHT, MOTOR_FRONT_RIGHT, MOTOR_FRONT_LEFT;
extern const int RAW, ACRO, STAB, AUTO;
extern float dt, loopRate;
extern double t;
extern double controlTime;
extern uint16_t channels[16];
extern float controlRoll, controlPitch, controlThrottle, controlYaw, controlMode;
extern int rcRxPin, rcProtocol, rcBaud;
extern uint32_t rcSerialBytesRead, rcProtocolFramesValid, rcChannelFramesAccepted;
extern uint32_t rcCrcRejects, rcResyncBytesDropped;
extern float motors[4];
extern int mode;
extern bool armed;
bool motorsActive();
static bool mavlinkConsoleCommand = false;
void setMavlinkConsoleCommandOutput(bool enabled) { mavlinkConsoleCommand = enabled; }
extern bool requestArm();
extern void disarm();
extern bool setFlightMode(int requestedMode);
#if WEB_RC_ENABLED
extern bool isUsingWebRC();
#endif

const char* motd =
"CLI命令菜单，输入相应命令，回车后执行:\n"
"help - 帮助\n"
"p - 显示所有参数\n"
"p <name> - 显示指定参数\n"
"p <name> <value> - 设置参数\n"
"p MOT_PIN_FL 14 - 参数设置示例，前左电机引脚为14\n"
"preset - 重置飞控参数（保留Wi-Fi凭据和系统日志）\n"
"mfr, mfl, mrr, mrl - 测试马达 (马达不受算法影响运转，为了安全不要装桨叶！！！)\n"
"ca - 六面校准加速度计\n"
"imucap [start|stop|status|dump] - 采集/导出约1秒、1kHz机体坐标系IMU数据（仅上锁）\n"
"ps - 显示pitch/roll/yaw姿态\n"
"cr - 校准RC遥控器\n"
"rc - 显示RC遥控数据\n"
"wifi - 显示WiFi信息\n"
"ap <ssid> <password> - 配置AP模式SSID和密码\n"
"sta <ssid> <password> - 配置STA客户端模式\n"
"raw/stab/acro/auto - 飞行模式设定\n"
"arm - 解锁无人机\n"
"disarm - 锁定无人机\n"
"psq - 显示姿态四元数\n"
"imu - 显示IMU数据\n"
"time - 显示时间信息\n"
"mot - 显示motor输出\n"
"sys - 显示系统info信息\n"
"diag - 显示故障诊断\n"
"diag clear - 清理诊断历史计数\n"
"log [dump|status|resume|clear] - 导出快照、恢复采样或清理事件历史\n"
"reboot - 重启无人机\n"
"reset - 重置无人机\n";

void print(const char* format, ...) {
    if (armed || motorsActive() || serialLogActive) {
        char event[96]; va_list args;
        va_start(args, format); vsnprintf(event, sizeof(event), format, args); va_end(args);
        recordSystemLogEvent("FLIGHT_MSG", event);
        return; // no UART drain or heap allocation while outputs are active
    }
	// 固定 1000 字节缓冲区 + vsnprintf 会静默截断超长内容（例如开机菜单 motd），且截断后连换行符都可能丢失，
	// 导致后续打印内容拼接到同一行。这里先用栈上小缓冲区尝试格式化，若实际所需长度超过缓冲区，
	// 再按精确所需大小临时用堆内存重新格式化，避免任何长度的内容被静默截断。
	char stackBuf[512];
	va_list args, argsCopy;
	va_start(args, format);
	va_copy(argsCopy, args);
	int needed = vsnprintf(stackBuf, sizeof(stackBuf), format, argsCopy);
	va_end(argsCopy);
	if (needed < 0) needed = 0; // 编码错误兵底，避免负数导致后续内存分配异常

	char *buf = stackBuf;
	bool heapAllocated = false;
	if (needed >= (int)sizeof(stackBuf)) {
		buf = (char*)malloc(needed + 1);
		if (buf) {
			vsnprintf(buf, needed + 1, format, args);
			heapAllocated = true;
		} else {
			buf = stackBuf; // 内存不足兵底：退回已截断的栈缓冲区内容
		}
	}
	va_end(args);

#if WEB_RC_ENABLED
	// Web console commands execute on the flight loop. Keep their output in the
	// web ring buffer instead of blocking the control loop on the UART TX buffer.
	if (webConsoleCommandTask == xTaskGetCurrentTaskHandle()) {
		webLog(buf);
	} else {
		Serial.print(buf);
#if WIFI_ENABLED
		mavlinkPrint(buf);
#endif
		if (webConsoleEnabled) webLog(buf);
	}
#else
	Serial.print(buf);
#if WIFI_ENABLED
	mavlinkPrint(buf);
#endif
#endif
	if (heapAllocated) free(buf);
}

void pause(float duration) {
	double start = t;
	while (t - start < duration) {
		readIMU(); // 长时间阻塞命令（ca/cr）期间也需要持续刷新IMU/姿态，否则打印信息会定格在进入pause前的旧值
		step();
		estimate();
		handleInput();
#if WIFI_ENABLED
		processMavlink();
#endif
#if WEB_RC_ENABLED
		readWebRC(); // 保持 HTTP 服务器在长时间命令（ca/cr）期间持续响应
#endif
		delay(50);
	}
}

void doCommand(String str, bool echo = false) {
	// parse command
	String command, arg0, arg1;
	splitString(str, command, arg0, arg1);
	if (command.isEmpty()) return;
    serialLogActive = false; serialLogChunk.clear(); // a new command cancels the previous serial export

	command.toLowerCase();
	if ((armed || motorsActive()) && command != "disarm" && command != "stab" &&
        command != "acro" && command != "auto" && command != "raw") {
        static uint32_t lastDeniedMs = 0;
        if ((uint32_t)(millis() - lastDeniedMs) >= 1000) {
            recordSystemLogEvent("CLI_DENIED", "outputs_active; stop motors first");
            lastDeniedMs = millis();
        }
        return;
    }
	// echo command
	if (echo) {
		print("> %s\n", str.c_str());
	}

	command.toLowerCase();
	serialImuCaptureActive = false;

	// execute command
	if (command == "help" || command == "motd") {
		print("%s\n", motd);
	} else if (command == "p" && arg0 == "") {
		printParameters();
	} else if (command == "p" && arg0 != "" && arg1 == "") {
		print("%s = %g\n", arg0.c_str(), getParameter(arg0.c_str()));
	} else if (command == "p") {
		bool success = setParameter(arg0.c_str(), arg1.toFloat());
		if (success) {
			print("%s = %g\n", arg0.c_str(), getParameter(arg0.c_str()));
		} else {
			print("参数不存在或数值无效/越界: %s\n", arg0.c_str());
		}
	} else if (command == "preset") {
		resetParameters();
	} else if (command == "time") {
		print("Time: %f\n", t);
		print("Loop rate: %.0f\n", loopRate);
		print("dt: %f\n", dt);
	} else if (command == "ps") {
		Vector a = attitude.toEuler();
		print("roll: %f pitch: %f yaw: %f\n", degrees(a.x), degrees(a.y), degrees(a.z));
	} else if (command == "psq") {
		print("qw: %f qx: %f qy: %f qz: %f\n", attitude.w, attitude.x, attitude.y, attitude.z);
	} else if (command == "imu") {
		printIMUInfo();
		printIMUCalibration();
		print("landed: %d\n", landed);
	} else if (command == "imucap") {
		if (arg0 == "start") {
			if (imuCapture.start(armed, motorsActive(), ESP.getFreeHeap())) print("IMU_CAPTURE state=running capacity=%u sample_period_us~1000\n", IMU_CAPTURE_CAPACITY);
			else print("IMU_CAPTURE start rejected: require disarmed/stopped motors, >=48 KiB heap, and available memory\n");
		} else if (arg0 == "stop") {
			imuCapture.stop();
			print("IMU_CAPTURE state=%u rows=%u\n", (unsigned)imuCapture.state(), (unsigned)imuCapture.size());
		} else if (arg0 == "status") {
			print("IMU_CAPTURE state=%u rows=%u capacity=%u\n", (unsigned)imuCapture.state(),
				(unsigned)imuCapture.size(), IMU_CAPTURE_CAPACITY);
		} else if (arg0 == "dump") {
			if (armed || motorsActive() || imuCapture.state() == IMU_CAPTURE_RUNNING) {
				print("IMU_CAPTURE dump rejected: stop capture and motors first\n");
			} else if (!imuCapture.size()) {
				print("IMU_CAPTURE empty\n");
			} else {
				serialImuCaptureRow = 0;
				serialImuCaptureActive = true;
				print("time_us,gyro_x_rad_s,gyro_y_rad_s,gyro_z_rad_s,acc_x_m_s2,acc_y_m_s2,acc_z_m_s2\n");
			}
		} else {
			print("usage: imucap start|stop|status|dump\n");
		}
	} else if (command == "arm") {
		if (!requestArm()) print("系统未满足解锁条件，请检查油门、电池、IMU、故障和电机测试状态。\n");
	} else if (command == "disarm") {
		disarm();
	} else if (command == "raw") {
		if (!setFlightMode(RAW)) print("模式切换被拒绝\n");
	} else if (command == "stab") {
		if (!setFlightMode(STAB)) print("模式切换被拒绝\n");
	} else if (command == "acro") {
		if (!setFlightMode(ACRO)) print("模式切换被拒绝\n");
	} else if (command == "auto") {
		if (!setFlightMode(AUTO)) print("AUTO未就绪：需外部目标流至少3包且持续100ms\n");
	} else if (command == "rc") {
		print("channels: ");
		for (int i = 0; i < 16; i++) {
			print("%u ", channels[i]);
		}
		print("\nroll: %g pitch: %g yaw: %g throttle: %g mode: %g\n",
			controlRoll, controlPitch, controlYaw, controlThrottle, controlMode);
		print("time: %.1f\n", controlTime);
		print("mode: %s\n", getModeName());
		print("armed: %d\n", armed);
		print("RC_LINK protocol=%s rx_pin=%d baud=%d rx_bytes=%lu valid_frames=%lu channel_frames=%lu crc_rejects=%lu resync_bytes=%lu control_age_s=%.3f",
			rcProtocol == 1 ? "CRSF" : "SBUS", rcRxPin, rcBaud,
			(unsigned long)rcSerialBytesRead, (unsigned long)rcProtocolFramesValid,
			(unsigned long)rcChannelFramesAccepted, (unsigned long)rcCrcRejects,
			(unsigned long)rcResyncBytesDropped,
			controlTime > 0 ? (float)(t - controlTime) : -1.0f);
#if WEB_RC_ENABLED
		print(" web_rc_active=%u\n", isUsingWebRC() ? 1 : 0);
#else
		print(" web_rc_active=0\n");
#endif
	} else if (command == "wifi") {
#if WIFI_ENABLED
		printWiFiInfo();
#endif
	} else if (command == "ap") {
#if WIFI_ENABLED
		configWiFi(true, arg0.c_str(), arg1.c_str());
#endif
	} else if (command == "sta") {
#if WIFI_ENABLED
		configWiFi(false, arg0.c_str(), arg1.c_str());
#endif
	} else if (command == "mot") {
		print("front-right %g front-left %g rear-right %g rear-left %g\n",
			motors[MOTOR_FRONT_RIGHT], motors[MOTOR_FRONT_LEFT], motors[MOTOR_REAR_RIGHT], motors[MOTOR_REAR_LEFT]);
	} else if (command == "log") {
		if (arg0 == "clear") {
			print(clearSystemLogHistory() ? "系统日志历史已清理，将在锁定状态写入。\n" :
				"清理失败：需保持锁定、停止电机测试且存储可用。\n");
        } else if (arg0 == "resume") {
            print(resumeFlightLog() ? "飞行日志已恢复滚动采样。\n" : "恢复失败：请停止电机。\n");
        } else if (arg0 == "status") {
            FlightLogStatus status = getFlightLogStatus();
            print("FLIGHT_LOG version=1 state=%u generation=%lu rows=%lu reasons=0x%08lx missed=%lu\n",
                (unsigned)status.state, (unsigned long)status.generation, (unsigned long)status.rowCount,
                (unsigned long)status.reasonMask, (unsigned long)status.missedSamples);
        } else if (arg0 == "dump") {
            printLogData();
        } else {
            printLogHeader();
        }
	} else if (command == "cr") {
		calibrateRC();
	} else if (command == "ca") {
		calibrateAccel();
	} else if (command == "mfr") {
		testMotor(MOTOR_FRONT_RIGHT);
	} else if (command == "mfl") {
		testMotor(MOTOR_FRONT_LEFT);
	} else if (command == "mrr") {
		testMotor(MOTOR_REAR_RIGHT);
	} else if (command == "mrl") {
		testMotor(MOTOR_REAR_LEFT);
	} else if (command == "sys") {
#ifdef ESP32
		print("Chip: %s\n", ESP.getChipModel());
		print("Temperature: %.1f °C\n", temperatureRead());
		print("Free heap: %d\n", ESP.getFreeHeap());
		// Print tasks table
		print("Num  Task                Handle      Stack  Prio  Core  CPU%%\n");
		int taskCount = uxTaskGetNumberOfTasks();
		TaskStatus_t *systemState = new TaskStatus_t[taskCount];
		uint32_t totalRunTime;
		uxTaskGetSystemState(systemState, taskCount, &totalRunTime);
		for (int i = 0; i < taskCount; i++) {
			String core = systemState[i].xCoreID == tskNO_AFFINITY ? "*" : String(systemState[i].xCoreID);
			int cpuPercentage = systemState[i].ulRunTimeCounter / (totalRunTime / 100);
			print("%-5d%-20s0x%08lx %-7d%-6d%-6s%d\n",systemState[i].xTaskNumber, systemState[i].pcTaskName,
				(unsigned long)(uintptr_t)systemState[i].xHandle, systemState[i].usStackHighWaterMark,
				systemState[i].uxCurrentPriority, core.c_str(), cpuPercentage);
		}
		delete[] systemState;
#endif
	} else if (command == "diag" && arg0 == "clear") {
		clearDiagnosticHistory();
		printDiagnostics();
	} else if (command == "diag") {
		printDiagnostics();
	} else if (command == "reset") {
		attitude = Quaternion();
		gyroBiasFilter.reset();
	} else if (command == "reboot") {
		ESP.restart();
	} else {
		print("Invalid command: %s\n", command.c_str());
	}
}

void handleInput() {
	static size_t motdOffset = 0;
	static String input;
	static bool overflow = false;

	if (showMotd && !armed && !motorsActive()) {
		const size_t motdLength = strlen(motd);
		const size_t writable = Serial.availableForWrite();
		const size_t chunk = writable < 32 ? writable : 32;
		if (chunk > 0) {
			motdOffset += Serial.write((const uint8_t *)motd + motdOffset,
				(chunk < motdLength - motdOffset) ? chunk : motdLength - motdOffset);
		}
		if (motdOffset >= motdLength) {
			Serial.write('\n');
			showMotd = false;
		}
	}

	for (int budget = 32; budget > 0 && Serial.available(); --budget) {
		char c = Serial.read();
		if (c == '\n') {
            if (!overflow) doCommand(input);
            else recordSystemLogEvent("CLI_DENIED", "command exceeds 192 bytes");
            input.clear(); overflow = false;
        } else if (c != '\r') {
            if (input.length() < 192 && !overflow) input += c;
            else { overflow = true; input.clear(); }
		}
	}
}


static bool formatLogHeader(LogOutputChunk &chunk) {
    chunk.clear();
    for (int i = 0; i < getLogColumnCount(); ++i) {
        const int n = snprintf(chunk.data + chunk.size, sizeof(chunk.data) - chunk.size,
            "%s%s", getLogColumnName(i), i + 1 == getLogColumnCount() ? "\n" : ",");
        if (n < 0 || (size_t)n >= sizeof(chunk.data) - chunk.size) { chunk.clear(); return false; }
        chunk.size += n;
    }
    return true;
}

void printLogHeader() {
    LogOutputChunk header;
    if (formatLogHeader(header)) print("%s", header.data);
}

void printLogData() {
#if WEB_RC_ENABLED
    if (webConsoleCommandTask == xTaskGetCurrentTaskHandle()) {
        print("请在实时日志页下载故障快照，或访问 /logs.csv；仅电机停止时可导出。\n");
        return;
    }
#endif
    if (mavlinkConsoleCommand) {
        print("Use MAVLink LOG_REQUEST_DATA id=0, or /logs.csv\n");
        return;
    }
    if (!freezeFlightLog()) { print("快照尚未就绪：请停止电机并等待故障后1秒采样完成。\n"); return; }
    const FlightLogStatus status = getFlightLogStatus();
    serialLogGeneration = status.generation;
    serialLogRows = status.rowCount;
    serialLogRow = 0;
    showMotd = false;
    serialLogActive = formatLogHeader(serialLogChunk);
}

void serviceFlightLogExport() {
    if (!serialLogActive) return;
    const FlightLogStatus status = getFlightLogStatus();
    if (armed || motorsActive() || status.generation != serialLogGeneration || status.state != FROZEN) {
        serialLogActive = false; serialLogChunk.clear(); return;
    }
    if (serialLogChunk.empty()) {
        if (serialLogRow >= serialLogRows) { serialLogActive = false; return; }
        float row[FLIGHT_LOG_COLUMNS];
        if (!copyFrozenLogRow(serialLogGeneration, serialLogRow++, row, FLIGHT_LOG_COLUMNS)) {
            serialLogActive = false; return;
        }
        serialLogChunk.clear();
        for (int i = 0; i < FLIGHT_LOG_COLUMNS; ++i) {
            const int n = snprintf(serialLogChunk.data + serialLogChunk.size,
                sizeof(serialLogChunk.data) - serialLogChunk.size, "%.7g%s", row[i],
                i + 1 == FLIGHT_LOG_COLUMNS ? "\n" : ",");
            if (n < 0 || (size_t)n >= sizeof(serialLogChunk.data) - serialLogChunk.size) {
                serialLogActive = false; serialLogChunk.clear(); return;
            }
            serialLogChunk.size += n;
        }
    }
    const int available = Serial.availableForWrite();
    serialLogChunk.send(available > 0 ? (size_t)available : 0, 64,
        [](const uint8_t *data, size_t length) { return Serial.write(data, length); });
}

void serviceImuCaptureExport() {
	if (!serialImuCaptureActive) return;
	if (armed || motorsActive()) {
		serialImuCaptureActive = false;
		return;
	}
	if (serialImuCaptureRow >= imuCapture.size()) {
		serialImuCaptureActive = false;
		const uint16_t exportedRows = imuCapture.size();
		imuCapture.release();
		Serial.printf("IMU_CAPTURE_DONE rows=%u\n", exportedRows);
		return;
	}
	const int available = Serial.availableForWrite();
	if (available <= 0) return;
	ImuCaptureSample sample;
	if (!imuCapture.copy(serialImuCaptureRow, sample)) {
		serialImuCaptureActive = false;
		return;
	}
	char line[128];
	const int length = snprintf(line, sizeof(line), "%lu,%.6f,%.6f,%.6f,%.2f,%.2f,%.2f\n",
		(unsigned long)sample.timeUs,
		sample.gyroMicroRadPerSec[0] * 1.0e-6f,
		sample.gyroMicroRadPerSec[1] * 1.0e-6f,
		sample.gyroMicroRadPerSec[2] * 1.0e-6f,
		sample.accCentiMetersPerSec2[0] * 0.01f,
		sample.accCentiMetersPerSec2[1] * 0.01f,
		sample.accCentiMetersPerSec2[2] * 0.01f);
	if (length > 0 && length < (int)sizeof(line) && available >= length &&
		Serial.write((const uint8_t *)line, (size_t)length) == (size_t)length) {
		++serialImuCaptureRow;
	}
}
