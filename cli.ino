// Implementation of command line interface 

#include "pid.h"
#include "vector.h"
#include "util.h"
#include "diagnostics.h"
#include "control.h"
#include "system_log.h"
#include "lpf.h"
#include "flight_log.h"
#include "log_transfer.h"
#include "imu_capture.h"
#include "console_output_queue.h"
#include "external_sensors.h"
#include "vertical_flight.h"

extern LowPassFilter<Vector> gyroBiasFilter;
static LogOutputChunk serialLogChunk;
static bool serialLogActive = false;
static uint32_t serialLogGeneration = 0, serialLogRows = 0, serialLogRow = 0;
static bool showMotd = true;
static bool serialImuCaptureActive = false;
static bool serialImuCaptureIncludeTemp = false;
static bool serialImuCaptureIncludeRawGyro = false;
static bool serialImuCaptureHeaderPending = false;
static uint16_t serialImuCaptureRow = 0;
// The optional raw-temperature CSV header is 190 bytes; keep room for it plus
// the longest formatted sample so both use the same bounded chunk exporter.
static char serialImuCaptureLine[256];
static size_t serialImuCaptureLineLength = 0;
static size_t serialImuCaptureLineOffset = 0;
static ConsoleOutputQueue serialConsoleOutputQueue;
static portMUX_TYPE serialConsoleOutputMux = portMUX_INITIALIZER_UNLOCKED;
extern ImuCaptureBuffer imuCapture;

static bool serialConsoleConnected() {
#if defined(CONFIG_IDF_TARGET_ESP32S3) && ARDUINO_USB_CDC_ON_BOOT
	return Serial.isPlugged() && Serial.isConnected();
#else
	return true;
#endif
}

static void queueSerialConsoleOutput(const char *data, size_t length) {
	if (!serialConsoleConnected()) return;
	size_t offset = 0;
	while (offset < length) {
		const size_t chunk = min((size_t)64, length - offset);
		portENTER_CRITICAL(&serialConsoleOutputMux);
		const size_t accepted = serialConsoleOutputQueue.push(data + offset, chunk);
		portEXIT_CRITICAL(&serialConsoleOutputMux);
		offset += accepted;
		if (accepted != chunk) {
			recordSystemLogEvent("CONSOLE", "serial output queue full; command text truncated");
			break;
		}
	}
}

void serviceSerialConsoleOutput() {
	if (!serialConsoleConnected()) {
		// Drop any bytes queued immediately before a disconnect so reconnecting
		// never replays stale calibration or command output into CDC RX.
		char discarded[64];
		portENTER_CRITICAL(&serialConsoleOutputMux);
		serialConsoleOutputQueue.pop(discarded, sizeof(discarded));
		portEXIT_CRITICAL(&serialConsoleOutputMux);
		return;
	}
	// A 64-byte UART write can wait for the TX ring even after
	// availableForWrite() reports space. Keep each write below 1 ms of
	// wire time at 115200 baud, as the IMU capture exporter does.
	char output[8];
	const int uartAvailable = Serial.availableForWrite();
	if (uartAvailable <= 0) return;
	const size_t limit = min(sizeof(output), (size_t)uartAvailable);
	portENTER_CRITICAL(&serialConsoleOutputMux);
	const size_t count = serialConsoleOutputQueue.pop(output, limit);
	portEXIT_CRITICAL(&serialConsoleOutputMux);
	if (count) Serial.write((const uint8_t *)output, count);
}

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
"imucap [start|raw-start|stop|status|dump|dump-temp|dump-raw-temp] - 采集/导出约1秒、1kHz IMU数据（仅上锁）\n"
"ps - 显示pitch/roll和陀螺积分相对yaw\n"
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
"sensors - 读取IMU扩展板的气压计、磁力计、光流与下视测距\n"
"nav - 显示融合高度、垂直速度、定高目标和光流影子位置\n"
"flowcal start|stop|status|reset - 采集PMW3901实物标定统计（上锁、无桨）\n"
"magcal start|status|stop|save|align <deg>|reset - 校准QMC5883P软硬铁偏差和航向安装偏角\n"
"time - 显示时间信息\n"
"mot - 显示motor输出\n"
"sys - 显示系统info信息\n"
"diag [brief] - 显示故障诊断（brief 为轻量预检）\n"
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
		queueSerialConsoleOutput(buf, strlen(buf));
#if WIFI_ENABLED
		mavlinkPrint(buf);
#endif
		if (webConsoleEnabled) webLog(buf);
	}
#else
	queueSerialConsoleOutput(buf, strlen(buf));
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
		serviceSerialConsoleOutput();
#if WIFI_ENABLED && CF_DRONE_ENABLE_MAVLINK
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
		#if !defined(CONFIG_IDF_TARGET_ESP32S3)
		print("> %s\n", str.c_str());
		#endif
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
		print("roll: %f pitch: %f yaw_gyro_relative: %f\n", degrees(a.x), degrees(a.y), degrees(a.z));
	} else if (command == "psq") {
		print("qw: %f qx: %f qy: %f qz: %f\n", attitude.w, attitude.x, attitude.y, attitude.z);
	} else if (command == "imu") {
		printIMUInfo();
		printIMUCalibration();
		print("landed: %d\n", landed);
	} else if (command == "sensors") {
		printExternalSensorReadings(attitude.getRoll(), attitude.getPitch());
	} else if (command == "nav") {
		VerticalFlightState state;
		if (!getVerticalFlightState(state)) {
			print("VERTICAL state=unavailable\n");
		} else {
			print("VERTICAL healthy=%d degraded=%d source=%u altitude_m=%.3f vz_mps=%.3f az_mps2=%.3f target_m=%.3f target_vz_mps=%.3f thrust=%.3f range_raw_valid=%d range_raw_agl_m=%.3f range_ref=%u range_ground_m=%.3f range_relative_m=%.3f range_fusion_valid=%d flow_valid=%d flow_quality=%u flow_x_m=%.3f flow_y_m=%.3f flow_vx_mps=%.3f flow_vy_mps=%.3f\n",
				state.healthy, state.degraded, state.heightSource, state.altitudeMeters,
				state.verticalSpeedMps, state.verticalAccelerationMps2,
				state.altitudeTargetMeters, state.verticalSpeedTargetMps,
				state.thrustCommand, state.rangeValid, state.rangeAglMeters,
				(unsigned)state.rangeReferenceSource, state.rangeGroundBaselineMeters,
				state.rangeRelativeHeightMeters, state.rangeFusionValid,
				state.flowValid, (unsigned)state.flowQuality,
				state.flowPositionXMeters, state.flowPositionYMeters,
				state.flowVelocityXMps, state.flowVelocityYMps);
		}
	} else if (command == "flowcal") {
		if (arg0 == "start") {
			if (startOpticalFlowCalibration())
				print("FLOW_CAL started. Keep motors stopped; run one trial at a time: static, move body +X 1m, move body +Y 1m, or rotate in place. Then use flowcal stop/status.\n");
			else print("FLOW_CAL start rejected: disarm and stop all motor output first.\n");
		} else if (arg0 == "stop") {
			stopOpticalFlowCalibration();
			print("FLOW_CAL stopped; use flowcal status.\n");
		} else if (arg0 == "reset") {
			resetOpticalFlowCalibration();
			print("FLOW_CAL reset.\n");
		} else if (arg0 == "status") {
			OpticalFlowCalibrationStats stats;
			if (!getOpticalFlowCalibrationStats(stats)) {
				print("FLOW_CAL state=empty\n");
			} else {
				const float qualityAverage = stats.validSampleCount ?
					stats.qualitySum / stats.validSampleCount : 0.0f;
				const float rangeAverage = stats.rangeSampleCount ?
					stats.rangeSumMeters / stats.rangeSampleCount : 0.0f;
				const float durationSeconds = ((stats.active ? micros() : stats.endedUs) - stats.startedUs) * 1e-6f;
				print("FLOW_CAL active=%u duration_s=%.2f samples=%lu valid=%lu usable=%lu motion=%lu pixels_x=%lld pixels_y=%lld image_angle_x_rad=%.5f image_angle_y_rad=%.5f gyro_angle_x_rad=%.5f gyro_angle_y_rad=%.5f corrected_angle_x_rad=%.5f corrected_angle_y_rad=%.5f estimated_body_x_m=%.4f estimated_body_y_m=%.4f quality_avg=%.1f quality_min=%u quality_max=%u agl_avg_m=%.3f agl_min_m=%.3f agl_max_m=%.3f\n",
					stats.active, durationSeconds, (unsigned long)stats.sampleCount,
					(unsigned long)stats.validSampleCount, (unsigned long)stats.usableSampleCount,
					(unsigned long)stats.motionSampleCount, (long long)stats.pixelX, (long long)stats.pixelY,
					stats.imageAngleXRad, stats.imageAngleYRad,
					stats.gyroAngleXRad, stats.gyroAngleYRad,
					stats.correctedAngleXRad, stats.correctedAngleYRad,
					stats.estimatedBodyXMeters, stats.estimatedBodyYMeters,
					qualityAverage, stats.validSampleCount ? stats.qualityMinimum : 0,
					stats.qualityMaximum, rangeAverage,
					stats.rangeSampleCount ? stats.rangeMinimumMeters : 0.0f,
					stats.rangeSampleCount ? stats.rangeMaximumMeters : 0.0f);
			}
		} else {
			print("Usage: flowcal start|stop|status|reset\n");
		}
	} else if (command == "magcal") {
		if (arg0 == "start") {
			if (startMagCalibration()) print("MAG_CAL started; keep motors stopped and slowly rotate the aircraft through all axes for 30-60 seconds.\n");
			else print("MAG_CAL start rejected: compass unavailable or outputs active.\n");
		} else if (arg0 == "stop") {
			stopMagCalibration();
			printMagCalibrationStatus();
		} else if (arg0 == "status") {
			printMagCalibrationStatus();
		} else if (arg0 == "save") {
			print(saveMagCalibration() ? "MAG_CAL saved and verified in NVS.\n" : "MAG_CAL save rejected: stop collection and ensure all three axes have adequate coverage.\n");
		} else if (arg0 == "align") {
			char *end = nullptr;
			const float knownHeading = strtof(arg1.c_str(), &end);
			const bool validNumber = arg1.length() > 0 && end != arg1.c_str() && *end == '\0';
			if (validNumber && alignMagHeading(knownHeading, attitude.getRoll(), attitude.getPitch())) print("MAG_CAL heading alignment updated in RAM; use magcal save to persist it.\n");
			else print("MAG_CAL align rejected: first save a valid calibration, keep outputs stopped, and supply reference magnetic heading in degrees [0,360).\n");
		} else if (arg0 == "reset") {
			print(resetMagCalibration() ? "MAG_CAL saved calibration erased; compass heading will remain unavailable until recalibrated.\n" : "MAG_CAL reset rejected: stop outputs and retry when NVS is available.\n");
		} else {
			print("Usage: magcal start|status|stop|save|align <deg>|reset\n");
		}
	} else if (command == "imucap") {
		if (arg0 == "start" || arg0 == "raw-start") {
			const bool captureRawGyro = arg0 == "raw-start";
			if (imuCapture.start(armed, motorsActive(), ESP.getFreeHeap(), captureRawGyro)) {
				print("IMU_CAPTURE state=running capacity=%u sample_period_us~1000 raw_gyro=%u\n",
					IMU_CAPTURE_CAPACITY, captureRawGyro ? 1 : 0);
			} else {
				const unsigned minimumHeapKiB = captureRawGyro
					? (unsigned)(IMU_CAPTURE_RAW_MIN_FREE_HEAP / 1024)
					: (unsigned)(IMU_CAPTURE_MIN_FREE_HEAP / 1024);
				print("IMU_CAPTURE start rejected: require disarmed/stopped motors, >=%u KiB heap, and available memory\n",
					minimumHeapKiB);
			}
		} else if (arg0 == "stop") {
			imuCapture.stop();
			print("IMU_CAPTURE state=%u rows=%u\n", (unsigned)imuCapture.state(), (unsigned)imuCapture.size());
		} else if (arg0 == "status") {
			print("IMU_CAPTURE state=%u rows=%u capacity=%u\n", (unsigned)imuCapture.state(),
				(unsigned)imuCapture.size(), IMU_CAPTURE_CAPACITY);
		} else if (arg0 == "dump" || arg0 == "dump-temp" || arg0 == "dump-raw-temp") {
			if (armed || motorsActive() || imuCapture.state() == IMU_CAPTURE_RUNNING) {
				print("IMU_CAPTURE dump rejected: stop capture and motors first\n");
			} else if (arg0 == "dump-raw-temp" && !imuCapture.capturesRawGyro()) {
				print("IMU_CAPTURE dump rejected: capture with 'imucap raw-start' first\n");
			} else if (!imuCapture.size()) {
				print("IMU_CAPTURE empty\n");
			} else {
				serialImuCaptureRow = 0;
				serialImuCaptureIncludeTemp = arg0 != "dump";
				serialImuCaptureIncludeRawGyro = arg0 == "dump-raw-temp";
				serialImuCaptureHeaderPending = true;
				serialImuCaptureLineLength = 0;
				serialImuCaptureLineOffset = 0;
				serialImuCaptureActive = true;
			}
		} else {
			print("usage: imucap start|raw-start|stop|status|dump|dump-temp|dump-raw-temp\n");
		}
	} else if (command == "arm") {
		if (!requestArm()) print("系统未满足解锁条件，请检查油门、电池、IMU、故障和电机测试状态。\n");
	} else if (command == "disarm") {
		disarm(DISARM_REASON_CLI);
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
	} else if (command == "diag" && arg0 == "brief") {
		printDiagnosticsBrief();
	} else if (command == "diag") {
		printDiagnostics();
	} else if (command == "reset") {
		attitude = Quaternion();
		gyroBiasFilter.reset();
	} else if (command == "reboot") {
		ESP.restart();
	} else {
		#if defined(CONFIG_IDF_TARGET_ESP32S3)
		// On native USB CDC, a disconnected or echoing host can feed diagnostic
		// output back into RX. Do not answer unknown serial text, otherwise the
		// "Invalid command" response can recursively become another command.
		if (echo) {
			static uint32_t lastInvalidSerialEventMs = 0;
			const uint32_t now = millis();
			if ((uint32_t)(now - lastInvalidSerialEventMs) >= 1000U) {
				recordSystemLogEvent("CLI_NOISE", "ignored invalid USB CDC input");
				lastInvalidSerialEventMs = now;
			}
			return;
		}
		#endif
		print("Invalid command: %s\n", command.c_str());
	}
}

void handleInput() {
	static size_t motdOffset = 0;
	static String input;
	static bool overflow = false;

	#if defined(CONFIG_IDF_TARGET_ESP32S3)
	extern bool isAccelCalibrationActive();
	// Calibration is controlled from the Web console on this carrier. Drop USB
	// CDC bytes while sampling so host echo/noise cannot steal IMU loop time.
	// Also ignore buffered CDC input when no USB host is attached.
	if (isAccelCalibrationActive() || !serialConsoleConnected()) {
		for (int budget = 64; budget > 0 && Serial.available(); --budget) Serial.read();
		input.clear();
		overflow = false;
		return;
	}
	#endif

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
			#if defined(CONFIG_IDF_TARGET_ESP32S3)
			if (!overflow) doCommand(input, true);
			#else
			if (!overflow) doCommand(input);
			#endif
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
		serialImuCaptureHeaderPending = false;
		serialImuCaptureLineLength = 0;
		serialImuCaptureLineOffset = 0;
		return;
	}
	if (serialImuCaptureHeaderPending) {
		// Console text is drained before the CSV export. Wait until it has been
		// queued to UART, then send the header through the same ordered stream;
		// print() is asynchronous and could otherwise interleave with data rows.
		portENTER_CRITICAL(&serialConsoleOutputMux);
		const bool consoleOutputPending = serialConsoleOutputQueue.available() > 0;
		portEXIT_CRITICAL(&serialConsoleOutputMux);
		if (consoleOutputPending) return;
		const char *header = serialImuCaptureIncludeRawGyro
			? "time_us,gyro_x_rad_s,gyro_y_rad_s,gyro_z_rad_s,acc_x_m_s2,acc_y_m_s2,acc_z_m_s2,temperature_c,gyro_sensor_uncorrected_x_rad_s,gyro_sensor_uncorrected_y_rad_s,gyro_sensor_uncorrected_z_rad_s\n"
			: serialImuCaptureIncludeTemp
			? "time_us,gyro_x_rad_s,gyro_y_rad_s,gyro_z_rad_s,acc_x_m_s2,acc_y_m_s2,acc_z_m_s2,temperature_c\n"
			: "time_us,gyro_x_rad_s,gyro_y_rad_s,gyro_z_rad_s,acc_x_m_s2,acc_y_m_s2,acc_z_m_s2\n";
		serialImuCaptureLineLength = strlen(header);
		memcpy(serialImuCaptureLine, header, serialImuCaptureLineLength);
	}
	if (!serialImuCaptureHeaderPending && serialImuCaptureLineLength == 0 &&
		serialImuCaptureRow >= imuCapture.size()) {
		serialImuCaptureActive = false;
		const uint16_t exportedRows = imuCapture.size();
		imuCapture.release();
		print("IMU_CAPTURE_DONE rows=%u\n", exportedRows);
		return;
	}
	if (!serialImuCaptureHeaderPending && serialImuCaptureLineLength == 0) {
		ImuCaptureSample sample;
		if (!imuCapture.copy(serialImuCaptureRow, sample)) {
			serialImuCaptureActive = false;
			return;
		}
		int length;
		if (serialImuCaptureIncludeRawGyro) {
			int32_t rawGyro[3];
			if (!imuCapture.copyRawGyro(serialImuCaptureRow, rawGyro)) {
				serialImuCaptureActive = false;
				return;
			}
			length = snprintf(serialImuCaptureLine, sizeof(serialImuCaptureLine),
				"%lu,%.6f,%.6f,%.6f,%.2f,%.2f,%.2f,%.2f,%.6f,%.6f,%.6f\n",
				(unsigned long)sample.timeUs,
				sample.gyroMicroRadPerSec[0] * 1.0e-6f,
				sample.gyroMicroRadPerSec[1] * 1.0e-6f,
				sample.gyroMicroRadPerSec[2] * 1.0e-6f,
				sample.accCentiMetersPerSec2[0] * 0.01f,
				sample.accCentiMetersPerSec2[1] * 0.01f,
				sample.accCentiMetersPerSec2[2] * 0.01f,
				sample.temperatureCentiC * 0.01f,
				rawGyro[0] * 1.0e-6f, rawGyro[1] * 1.0e-6f, rawGyro[2] * 1.0e-6f);
		} else if (serialImuCaptureIncludeTemp) {
			length = snprintf(serialImuCaptureLine, sizeof(serialImuCaptureLine),
				"%lu,%.6f,%.6f,%.6f,%.2f,%.2f,%.2f,%.2f\n",
				(unsigned long)sample.timeUs,
				sample.gyroMicroRadPerSec[0] * 1.0e-6f,
				sample.gyroMicroRadPerSec[1] * 1.0e-6f,
				sample.gyroMicroRadPerSec[2] * 1.0e-6f,
				sample.accCentiMetersPerSec2[0] * 0.01f,
				sample.accCentiMetersPerSec2[1] * 0.01f,
				sample.accCentiMetersPerSec2[2] * 0.01f,
				sample.temperatureCentiC * 0.01f);
		} else {
			length = snprintf(serialImuCaptureLine, sizeof(serialImuCaptureLine),
				"%lu,%.6f,%.6f,%.6f,%.2f,%.2f,%.2f\n",
				(unsigned long)sample.timeUs,
				sample.gyroMicroRadPerSec[0] * 1.0e-6f,
				sample.gyroMicroRadPerSec[1] * 1.0e-6f,
				sample.gyroMicroRadPerSec[2] * 1.0e-6f,
				sample.accCentiMetersPerSec2[0] * 0.01f,
				sample.accCentiMetersPerSec2[1] * 0.01f,
				sample.accCentiMetersPerSec2[2] * 0.01f);
		}
		if (length <= 0 || length >= (int)sizeof(serialImuCaptureLine)) {
			serialImuCaptureActive = false;
			return;
		}
		serialImuCaptureLineLength = (size_t)length;
	}
	const int available = Serial.availableForWrite();
	if (available <= 0) return;
	// Keep each UART write below 1 ms of wire time at 115200 baud. The UART
	// driver can wait for its TX ring when a whole CSV row is written at once.
	const size_t chunk = min((size_t)8, min((size_t)available,
		serialImuCaptureLineLength - serialImuCaptureLineOffset));
	if (!chunk) return;
	if (Serial.write((const uint8_t *)serialImuCaptureLine + serialImuCaptureLineOffset, chunk) != chunk) return;
	serialImuCaptureLineOffset += chunk;
	if (serialImuCaptureLineOffset == serialImuCaptureLineLength) {
		serialImuCaptureLineLength = 0;
		serialImuCaptureLineOffset = 0;
		if (serialImuCaptureHeaderPending) serialImuCaptureHeaderPending = false;
		else ++serialImuCaptureRow;
	}
}
