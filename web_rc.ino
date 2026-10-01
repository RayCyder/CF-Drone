// 网页遥控服务端

#if WEB_RC_ENABLED

#include <WebServer.h>
#include <WiFi.h>
#include <ctype.h>
#include <stdlib.h>
#include "web_rc_html.h"
#include "board_config.h"
#include "diagnostics.h"
#include "task_switch_trace_runtime.h"
#include "system_log.h"
#include "web_rc_input.h"
#include "open_loop_sequence.h"
#include "descent_calibration.h"
#include "imu_capture.h"
#include "control.h"
#include "flight_log.h"

// 飞控统一控制变量（供协议适配层写入，与 SBUS/MAVLink 共用）
extern double t;
extern double controlTime;
extern float controlRoll, controlPitch, controlYaw, controlThrottle, controlMode;
extern float batteryVoltage;
extern bool ledFastBlinkActive();
extern const char* motd;

// ==================== 配置常量 ====================
#define WEB_RC_TIMEOUT_MS    10000          // 连接超时：最后一次收包超过此时间(ms)视为断连；需大于心跳间隔
// VBAT_ADC_PIN / VBAT_ADC_SAMPLES / VBAT_DIVIDER 已迁移至 battery.ino

// ==================== 连接状态标志 ====================
bool webRCEnabled    = false;  // Web RC 当前有有效连接（由 readWebRC() 每帧更新）
bool useWebRC        = false;  // 当前正在使用 Web RC 控制（与 webRCEnabled 保持同步）
bool webRCUpdated    = false;  // 收到过至少一次网页控制包（首次连接前为 false）
static bool webRCStickUpdated = false; // 心跳不能单独接管实体遥控
bool webConsoleEnabled = false; // Web 调试控制台开关：POST /console/enable 开启，开启后 print() 写入缓冲区
char webRCWarnMsg[64] = ""; // 待发送给前端的警告消息，发送一次后自动清空

// ==================== 最后处理的请求上下文（供响应构造使用）====================
static int lastProcType        = 0;   // 1=摇杆 2=按钮 4=心跳
static int lastProcButtonIdx   = -1;  // 按钮事件：按钮序号（0~15），其他请求为 -1
static int lastProcButtonState = -1;  // 按钮事件：按下=1/松开=0，其他请求为 -1

// ==================== 摇杆暂存值（供状态端点读取）====================
// 单位：油门 0~100（%），姿态轴 ±30（°），按下=1/松开=0
float webRCRoll     = 0.0f;
float webRCPitch    = 0.0f;
float webRCYaw      = 0.0f;
float webRCThrottle = 0.0f;
uint16_t webRCButtons    = 0;       // 16位按钮位掩码，bit0=解锁 bit1=上锁 bit2=急停 bit6=STAB bit7=ACRO bit8=ALTHOLD
static uint16_t webRCButtonPressEdges = 0;
unsigned long webRCLastUpdate = 0;  // 最后一次收包的 millis() 时间戳
unsigned long webRCLastStickUpdate = 0; // 最后一次摇杆包的时间戳

// Browser-uploaded open-loop sequence. Fixed double buffers avoid heap churn and
// keep route execution local to the flight loop after upload.
static OpenLoopPackedStep openLoopBuffers[2][OPEN_LOOP_MAX_STEPS];
static portMUX_TYPE openLoopMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t openLoopActiveBuffer = 0;
static uint16_t openLoopCount = 0;
static uint16_t openLoopIndex = 0;
static uint32_t openLoopTotalMs = 0;
static uint32_t openLoopRevision = 0;
static uint32_t openLoopStartMs = 0;
static uint32_t openLoopCurrentDeadlineMs = 0;
static uint32_t openLoopLastSchedulerMs = 0;
static OpenLoopControls openLoopAppliedControls = {0, 0, 0, 0};
static bool openLoopLandingStarted = false;
static bool openLoopStartRequested = false;
static bool openLoopStopRequested = false;
static bool openLoopTakeoverRequested = false;
static uint32_t openLoopRequestedRevision = 0;
static bool openLoopUploadInProgress = false;
static uint8_t openLoopState = OPEN_LOOP_STATE_EMPTY;
static const char *openLoopReason = "empty";

enum VibrationCalibrationState : uint8_t { VIBRATION_EMPTY, VIBRATION_QUEUED, VIBRATION_RUNNING, VIBRATION_COMPLETE, VIBRATION_ABORTED };
struct VibrationMotorResult { float gyroRms; float accelRms; uint16_t samples; };
static portMUX_TYPE vibrationCalibrationMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t vibrationCalibrationState = VIBRATION_EMPTY;
static volatile uint8_t vibrationCalibrationIndex = 0;
static volatile bool vibrationCalibrationStartRequested = false;
static volatile uint32_t vibrationCalibrationMotorStartedMs = 0;
static volatile const char *vibrationCalibrationReason = "empty";
static VibrationMotorResult vibrationCalibrationResults[4] = {};
static const int vibrationMotorIds[4] = {MOTOR_FRONT_RIGHT, MOTOR_FRONT_LEFT, MOTOR_REAR_RIGHT, MOTOR_REAR_LEFT};
static const char *vibrationMotorNames[4] = {"FR", "FL", "RR", "RL"};

extern int mode;
extern const int STAB, AUTO;
extern bool armed;
extern bool motorsActive();
extern bool motorTestActive;
extern bool motorOutputsOK;
extern bool batteryBlocksArming();
extern bool hasBlockingDiagnosticFault();
extern bool isAccelCalibrationActive();
extern void testMotor(int n);
extern void cancelMotorTest();
extern ImuCaptureBuffer imuCapture;
extern const int MOTOR_REAR_LEFT, MOTOR_REAR_RIGHT, MOTOR_FRONT_RIGHT, MOTOR_FRONT_LEFT;
extern void descend();
extern bool isControlledLandingActive();
extern bool startDescentCalibration();
extern bool stopDescentCalibration();
extern void clearDescentCalibration();
extern DescentCalibrationSummary getDescentCalibrationSummary();
extern bool copyDescentCalibrationSample(uint16_t index, DescentCalibrationSample &sample);
extern bool setParameter(const char *name, float value);
extern float getParameter(const char *name);
extern bool saveParameterNow(const char *name);
extern bool isParameterDirty(const char *name);
extern bool parameterPersistenceReady();
extern float webRCThrottleScale, webRCStickScale, webRCYawScale;
extern float stickDeadzone, throttleDeadzone;
extern WebServer &webRCServer;

#define WEB_LOG_CSV_COLUMNS_CAPACITY 41
#define WEB_LOG_CSV_ROW_CAPACITY 1024
static_assert(WEB_LOG_CSV_COLUMNS_CAPACITY >= FLIGHT_LOG_COLUMNS, "HTTP CSV export capacity must cover all flight log columns");
static_assert(WEB_LOG_CSV_ROW_CAPACITY >= 1024, "HTTP CSV rows require at least 1024 bytes");

static const char *flightLogStateName(FlightLogState state) {
    switch (state) {
        case ROLLING: return "ROLLING";
        case POST_TRIGGER: return "POST_TRIGGER";
        case FROZEN: return "FROZEN";
        default: return "UNKNOWN";
    }
}

static const char *descentCalibrationStateName(DescentCalibrationState state) {
    switch (state) {
        case DESCENT_CALIBRATION_RECORDING: return "recording";
        case DESCENT_CALIBRATION_COMPLETE: return "complete";
        case DESCENT_CALIBRATION_ABORTED: return "aborted";
        default: return "empty";
    }
}

static bool parseCalibrationValueArg(float &value) {
    const String text = webRCServer.arg("value");
    if (text.isEmpty()) return false;
    char *end = nullptr;
    value = strtof(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && isfinite(value);
}

static bool appendCsvFloat(char *line, size_t capacity, int &used, float value, bool first) {
    const int written = snprintf(line + used, capacity - used, "%s%.7g", first ? "" : ",", value);
    if (written <= 0 || written >= (int)(capacity - used)) return false;
    used += written;
    return true;
}

static void copyOpenLoopStep(uint8_t activeBuffer, uint16_t index, OpenLoopPackedStep &step) {
    step = openLoopBuffers[activeBuffer][index];
}

bool isLocalSequenceRunning() {
    portENTER_CRITICAL(&openLoopMux);
    const bool running = openLoopState == OPEN_LOOP_STATE_RUNNING || openLoopState == OPEN_LOOP_STATE_START_PENDING;
    portEXIT_CRITICAL(&openLoopMux);
    return running;
}

static void setOpenLoopReason(const char *reason) {
    openLoopReason = reason ? reason : "unknown";
}

void cancelLocalSequenceForManualMode() {
    portENTER_CRITICAL(&openLoopMux);
    if (openLoopState == OPEN_LOOP_STATE_RUNNING || openLoopState == OPEN_LOOP_STATE_START_PENDING ||
        openLoopState == OPEN_LOOP_STATE_LANDING) {
        openLoopState = OPEN_LOOP_STATE_ABORTED;
        openLoopStartRequested = false;
        openLoopStopRequested = false;
        openLoopTakeoverRequested = false;
        openLoopLandingStarted = false;
        setOpenLoopReason("manual_takeover");
    }
    portEXIT_CRITICAL(&openLoopMux);
}

static void applyOpenLoopControls(const OpenLoopControls &controls) {
    controlRoll = openLoopClampFloat(controls.roll, -1.0f, 1.0f);
    controlPitch = openLoopClampFloat(controls.pitch, -1.0f, 1.0f);
    controlYaw = openLoopClampFloat(controls.yaw, -1.0f, 1.0f);
    controlThrottle = openLoopClampFloat(controls.throttle, 0.0f, 1.0f);
    controlMode = NAN;
    setCurrentControlSource(CONTROL_SOURCE_LOCAL_SEQUENCE);
}

static void startOpenLoopNow(uint32_t now) {
    openLoopIndex = 0;
    openLoopStartMs = now;
    openLoopLastSchedulerMs = now;
    openLoopLandingStarted = false;
    openLoopAppliedControls = {controlRoll, controlPitch, controlYaw, controlThrottle};
    openLoopCurrentDeadlineMs = now + openLoopBuffers[openLoopActiveBuffer][0].durationMs;
    openLoopState = OPEN_LOOP_STATE_RUNNING;
    setOpenLoopReason("running");
}

static bool vibrationRouteBusy() {
    bool busy;
    portENTER_CRITICAL(&openLoopMux);
    busy = openLoopState == OPEN_LOOP_STATE_RUNNING || openLoopState == OPEN_LOOP_STATE_START_PENDING ||
        openLoopState == OPEN_LOOP_STATE_LANDING;
    portEXIT_CRITICAL(&openLoopMux);
    return busy;
}

static void setVibrationCalibrationState(uint8_t state, const char *reason) {
    portENTER_CRITICAL(&vibrationCalibrationMux);
    vibrationCalibrationState = state;
    vibrationCalibrationReason = reason;
    portEXIT_CRITICAL(&vibrationCalibrationMux);
}

static void serviceVibrationCalibration() {
    if (vibrationCalibrationState == VIBRATION_QUEUED && vibrationCalibrationStartRequested) {
        vibrationCalibrationStartRequested = false;
        if (armed || motorsActive() || !motorOutputsOK || isAccelCalibrationActive() ||
            batteryBlocksArming() || hasBlockingDiagnosticFault() || vibrationRouteBusy() ||
            !imuCapture.start(armed, motorsActive(), ESP.getFreeHeap())) {
            setVibrationCalibrationState(VIBRATION_ABORTED, "preflight_failed");
            return;
        }
        testMotor(vibrationMotorIds[vibrationCalibrationIndex]);
        if (!motorTestActive) {
            imuCapture.stop();
            setVibrationCalibrationState(VIBRATION_ABORTED, "motor_test_rejected");
            return;
        }
        vibrationCalibrationMotorStartedMs = millis();
        setVibrationCalibrationState(VIBRATION_RUNNING, "running");
        return;
    }
    if (vibrationCalibrationState != VIBRATION_RUNNING) return;
    if (armed || batteryBlocksArming() || hasBlockingDiagnosticFault()) {
        imuCapture.stop();
        cancelMotorTest();
        setVibrationCalibrationState(VIBRATION_ABORTED, "safety_state_changed");
        return;
    }
    if (motorTestActive) {
        if ((uint32_t)(millis() - vibrationCalibrationMotorStartedMs) > 4000U) {
            imuCapture.stop();
            cancelMotorTest();
            setVibrationCalibrationState(VIBRATION_ABORTED, "motor_test_timeout");
        }
        return;
    }
    if (imuCapture.state() == IMU_CAPTURE_RUNNING) imuCapture.stop();
    const uint16_t count = imuCapture.size();
    if (count < 100) {
        imuCapture.release();
        setVibrationCalibrationState(VIBRATION_ABORTED, "insufficient_imu_samples");
        return;
    }
    double sums[6] = {}, squares[6] = {};
    for (uint16_t i = 0; i < count; ++i) {
        ImuCaptureSample sample;
        if (!imuCapture.copy(i, sample)) continue;
        for (int axis = 0; axis < 3; ++axis) {
            const double gyro = sample.gyroMicroRadPerSec[axis] / 1000000.0;
            const double accel = sample.accCentiMetersPerSec2[axis] / 100.0;
            sums[axis] += gyro; squares[axis] += gyro * gyro;
            sums[axis + 3] += accel; squares[axis + 3] += accel * accel;
        }
    }
    double gyroVariance = 0, accelVariance = 0;
    for (int axis = 0; axis < 3; ++axis) {
        gyroVariance += squares[axis] / count - (sums[axis] / count) * (sums[axis] / count);
        accelVariance += squares[axis + 3] / count - (sums[axis + 3] / count) * (sums[axis + 3] / count);
    }
    VibrationMotorResult result = {
        (float)sqrt(gyroVariance > 0 ? gyroVariance : 0),
        (float)sqrt(accelVariance > 0 ? accelVariance : 0), count
    };
    imuCapture.release();
    portENTER_CRITICAL(&vibrationCalibrationMux);
    vibrationCalibrationResults[vibrationCalibrationIndex] = result;
    ++vibrationCalibrationIndex;
    const bool complete = vibrationCalibrationIndex >= 4;
    vibrationCalibrationState = complete ? VIBRATION_COMPLETE : VIBRATION_QUEUED;
    vibrationCalibrationStartRequested = !complete;
    vibrationCalibrationReason = complete ? "complete" : "next_motor";
    portEXIT_CRITICAL(&vibrationCalibrationMux);
}

static void enterOpenLoopLandingLocked(const char *reason) {
    openLoopState = OPEN_LOOP_STATE_LANDING;
    openLoopStartRequested = false;
    openLoopStopRequested = false;
    openLoopTakeoverRequested = false;
    openLoopLandingStarted = false;
    setOpenLoopReason(reason);
}

static void stepOpenLoopSequence() {
    bool beginLanding = false;
    bool requestManualStab = false;
    bool applyStep = false;
    OpenLoopPackedStep step;
    uint32_t elapsedForSlew = 0;
    const uint32_t now = millis();

    portENTER_CRITICAL(&openLoopMux);
    if (openLoopTakeoverRequested) {
        openLoopTakeoverRequested = false;
        openLoopStartRequested = false;
        openLoopStopRequested = false;
        openLoopState = OPEN_LOOP_STATE_ABORTED;
        openLoopLandingStarted = false;
        setOpenLoopReason("takeover_requested");
        requestManualStab = true;
    } else if (openLoopStopRequested) {
        openLoopStopRequested = false;
        if (armed) enterOpenLoopLandingLocked("stop_requested");
        else {
            openLoopState = OPEN_LOOP_STATE_COMPLETE;
            setOpenLoopReason("stop_disarmed");
        }
    }

    if (openLoopStartRequested && openLoopState == OPEN_LOOP_STATE_START_PENDING) {
        if (openLoopRequestedRevision != openLoopRevision) {
            openLoopState = OPEN_LOOP_STATE_READY;
            openLoopStartRequested = false;
            setOpenLoopReason("revision_mismatch");
        } else if (openLoopCount == 0) {
            openLoopState = OPEN_LOOP_STATE_EMPTY;
            openLoopStartRequested = false;
            setOpenLoopReason("no_uploaded_sequence");
        } else if (!isWebRCEnabled()) {
            openLoopState = OPEN_LOOP_STATE_READY;
            openLoopStartRequested = false;
            setOpenLoopReason("web_rc_link_required");
        } else if (!armed || mode != STAB) {
            openLoopState = OPEN_LOOP_STATE_READY;
            openLoopStartRequested = false;
            setOpenLoopReason("requires_armed_stab");
        } else {
            openLoopStartRequested = false;
            startOpenLoopNow(now);
        }
    }

    if (openLoopState == OPEN_LOOP_STATE_LANDING) {
        if (!openLoopLandingStarted) {
            openLoopLandingStarted = true;
            beginLanding = true;
        } else if (!armed) {
            openLoopState = OPEN_LOOP_STATE_COMPLETE;
            setOpenLoopReason("disarmed");
        } else if (!isControlledLandingActive()) {
            openLoopState = OPEN_LOOP_STATE_ABORTED;
            setOpenLoopReason("landing_interrupted");
        }
    } else if (openLoopState == OPEN_LOOP_STATE_RUNNING) {
        const uint32_t dtMs = now - openLoopLastSchedulerMs;
        if (!armed) {
            openLoopState = OPEN_LOOP_STATE_COMPLETE;
            setOpenLoopReason("disarmed");
        } else if (mode != STAB) {
            openLoopState = OPEN_LOOP_STATE_ABORTED;
            setOpenLoopReason("mode_changed");
        } else if (dtMs > OPEN_LOOP_SCHEDULER_GAP_MS) {
            enterOpenLoopLandingLocked("scheduler_gap");
            beginLanding = true;
        } else {
            if (openLoopElapsed(now, openLoopCurrentDeadlineMs)) {
                openLoopIndex++;
                if (openLoopIndex >= openLoopCount) {
                    enterOpenLoopLandingLocked("sequence_complete");
                    beginLanding = true;
                } else {
                    openLoopCurrentDeadlineMs += openLoopBuffers[openLoopActiveBuffer][openLoopIndex].durationMs;
                    if (openLoopElapsed(now, openLoopCurrentDeadlineMs)) {
                        enterOpenLoopLandingLocked("multiple_expired_segments");
                        beginLanding = true;
                    }
                }
            }
            if (openLoopState == OPEN_LOOP_STATE_RUNNING) {
                copyOpenLoopStep(openLoopActiveBuffer, openLoopIndex, step);
                elapsedForSlew = dtMs;
                openLoopLastSchedulerMs = now;
                applyStep = true;
            }
        }
    }
    portEXIT_CRITICAL(&openLoopMux);

    if (requestManualStab) setFlightMode(STAB);
    if (beginLanding) descend();
    if (applyStep) {
        OpenLoopControls target;
        openLoopMapStepToControls(step, stickDeadzone, throttleDeadzone,
                                  webRCStickScale, webRCYawScale, webRCThrottleScale, target);
        portENTER_CRITICAL(&openLoopMux);
        openLoopSlewControls(openLoopAppliedControls, target, elapsedForSlew);
        OpenLoopControls controls = openLoopAppliedControls;
        portEXIT_CRITICAL(&openLoopMux);
        applyOpenLoopControls(controls);
    }
}

static bool parseRevisionArg(uint32_t &revision) {
    if (!webRCServer.hasArg("revision")) return false;
    const String text = webRCServer.arg("revision");
    if (text.isEmpty() || text.length() > 10) return false;
    char *end = nullptr;
    const unsigned long value = strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || value == 0 || value > UINT32_MAX) return false;
    revision = (uint32_t)value;
    return true;
}
// ==================== 灵敏度缩放 ====================
// 最终输出 = 处理后角度 × scale / STICK_MAX，结果写入 control* ([-1,1])
// 减小 scale → 飞机响应更柔和；增大 → 更灵敏
float webRCThrottleScale = 1.0f;    // 油门最大功率限制，1.0=100%，0.8=最多只能推到80%油门
float webRCStickScale    = 0.85f;   // 横滚/俯仰灵敏度，建议范围 0.5~1.0
float webRCYawScale      = 0.68f;   // 偏航灵敏度，偏航惯性小故单独偏低，建议范围 0.3~0.8

// ==================== 死区 ====================
// 归一化空间（0~1 比例），摇杆归中误差在死区内输出恒为 0
// 增大 → 中立区更宽、误触更少；减小 → 响应更灵敏但易漂移
float stickDeadzone    = 0.06f;     // 横滚/俯仰/偏航共用，6% 归一化死区
float throttleDeadzone = 0.06f;     // 油门低端死区，推杆低于此比例时输出 0%（防抖）

// ==================== 输入值域（勿随意修改，需与前端保持一致）====================
static const float THROTTLE_MIN = 0.0f;    // 油门输出下限（%）
static const float THROTTLE_MAX = 100.0f;  // 油门输出上限（%）
static const float STICK_MAX    = 30.0f;   // 姿态轴最大角度（°），对应前端满偏；修改需同步调整 PID TILT_MAX
static const float RAW_MAX      = 100.0f;  // 前端摇杆归一化满偏值，前端 JS 固定输出 ±100

// ==================== 内部状态（运行时，勿手动修改）====================
static float lastValidThrottle = 0.0f;     // 上次通过验证的油门值，异常时回退使用
static float lastValidRoll     = 0.0f;     // 上次通过验证的横滚值
static float lastValidPitch    = 0.0f;     // 上次通过验证的俯仰值
static float lastValidYaw      = 0.0f;     // 上次通过验证的偏航值
static unsigned long lastDataErrorTime = 0; // 上次数据异常时间，用于限速错误日志输出

// ==================== Web 服务器 ====================
class ResponsiveWebServer : public WebServer {
public:
    explicit ResponsiveWebServer(int port) : WebServer(port) {}

    void handleClient() override {
        // WebServer serves one client at a time and otherwise waits 5 s for an
        // accepted socket to send its first byte. Browser preconnects can hold
        // up every stick packet even though the control loop remains healthy.
        if (_currentStatus == HC_WAIT_READ && !_currentClient.available() &&
            (uint32_t)(millis() - _statusChange) > 150) {
            _currentClient.stop();
            _currentClient = NetworkClient();
            _currentStatus = HC_NONE;
        }
        WebServer::handleClient();
    }
};

static ResponsiveWebServer responsiveWebRCServer(80);
WebServer &webRCServer = responsiveWebRCServer; // 主服务器：80端口

#if WIFI_ENABLED
extern bool isWiFiConfigPortalActive();
extern bool configWiFi(bool ap, const char *ssid, const char *password);
extern void scheduleWiFiRestart();
extern const char *wifiConfigLastError();
extern int getWiFiProfileCount();
extern bool getWiFiProfileSsid(int index, char *destination, size_t capacity);
extern bool removeWiFiProfile(const char *ssid);
extern size_t getWiFiProfileStorageUsedBytes();
extern size_t getWiFiProfileStorageTotalBytes();

static String wifiJsonQuote(const char *value) {
    String result = "\"";
    if (value) {
        for (const uint8_t *p = (const uint8_t *)value; *p; ++p) {
            const uint8_t c = *p;
            if (c == '"' || c == '\\') { result += '\\'; result += (char)c; }
            else if (c < 0x20) {
                char escaped[7];
                snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                result += escaped;
            } else result += (char)c;
        }
    }
    result += '"';
    return result;
}
#endif

// ------旧PCB印刷地址访问 :8080 → 301跳转到80端口；旧地址全部淘汰后可删除------------
// 8080端口兼容重定向：堆指针，setupWebRC()中动态构造，BSS仅占4字节
// ESP32-C3无旧用户，条件编译去除以避免单核上200ms自旋阻塞和额外socket占用
#ifndef CONFIG_IDF_TARGET_ESP32C3
static WiFiServer* redirectServer8080 = nullptr;
#endif
// ------旧PCB印刷地址访问 :8080 → 301跳转到80端口；旧地址全部淘汰后可删除------------

// ==================== 控制台日志缓冲区 ====================
#define CONSOLE_LINES    BOARD_CONSOLE_LINES    // 控制台行数：C3=20（节省~7KB RAM）/ ESP32&S3=50
#define CONSOLE_LINE_LEN BOARD_CONSOLE_LINE_LEN  // 每行字符数：C3=160 / ESP32&S3=240
static char consoleBuf[CONSOLE_LINES][CONSOLE_LINE_LEN];
static int  consoleTail   = 0;
static int  consoleFilled = 0;
static int  consoleTotal  = 0;   // 单调递增总行数，用于增量拉取
static portMUX_TYPE consoleMux = portMUX_INITIALIZER_UNLOCKED;

#define CONSOLE_CMD_QUEUE_SIZE 4
#define CONSOLE_CMD_LEN 64
static char consoleCmdQueue[CONSOLE_CMD_QUEUE_SIZE][CONSOLE_CMD_LEN];
static int  consoleCmdHead  = 0;
static int  consoleCmdTail  = 0;
static int  consoleCmdCount = 0;
static portMUX_TYPE consoleCmdMux = portMUX_INITIALIZER_UNLOCKED;

#define WEB_RC_INPUT_QUEUE_SIZE 3
static WebRCInputEvent webRCInputQueue[WEB_RC_INPUT_QUEUE_SIZE];
static int webRCInputHead = 0;
static int webRCInputTail = 0;
static int webRCInputCount = 0;
static portMUX_TYPE webRCInputMux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE webRCStateMux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE webRCWarnMux = portMUX_INITIALIZER_UNLOCKED;
extern void setWebConsoleCommandOutput(bool enabled);

uint16_t getWebRCButtons() {
    portENTER_CRITICAL(&webRCStateMux);
    const uint16_t buttons = webRCButtons;
    portEXIT_CRITICAL(&webRCStateMux);
    return buttons;
}

uint16_t takeWebRCButtonPressEdges(uint16_t *buttons) {
    portENTER_CRITICAL(&webRCStateMux);
    if (buttons) *buttons = webRCButtons;
    const uint16_t edges = webRCButtonPressEdges;
    webRCButtonPressEdges = 0;
    portEXIT_CRITICAL(&webRCStateMux);
    return edges;
}

static bool enqueueWebRCInput(const WebRCInputEvent &event) {
    portENTER_CRITICAL(&webRCInputMux);
    // Keep the newest control input if HTTP requests briefly outpace the loop.
    if (webRCInputCount == WEB_RC_INPUT_QUEUE_SIZE) {
        webRCInputHead = (webRCInputHead + 1) % WEB_RC_INPUT_QUEUE_SIZE;
        webRCInputCount--;
    }
    webRCInputQueue[webRCInputTail] = event;
    webRCInputTail = (webRCInputTail + 1) % WEB_RC_INPUT_QUEUE_SIZE;
    webRCInputCount++;
    portEXIT_CRITICAL(&webRCInputMux);
    return true;
}

static bool dequeueWebRCInput(WebRCInputEvent &event) {
    bool dequeued = false;
    portENTER_CRITICAL(&webRCInputMux);
    if (webRCInputCount > 0) {
        event = webRCInputQueue[webRCInputHead];
        webRCInputHead = (webRCInputHead + 1) % WEB_RC_INPUT_QUEUE_SIZE;
        webRCInputCount--;
        dequeued = true;
    }
    portEXIT_CRITICAL(&webRCInputMux);
    return dequeued;
}

bool enqueueConsoleCmd(const char* cmd) {
    if (!cmd || !*cmd) return false;
    bool enqueued = false;
    portENTER_CRITICAL(&consoleCmdMux);
    if (consoleCmdCount < CONSOLE_CMD_QUEUE_SIZE) {
        strncpy(consoleCmdQueue[consoleCmdTail], cmd, CONSOLE_CMD_LEN - 1);
        consoleCmdQueue[consoleCmdTail][CONSOLE_CMD_LEN - 1] = '\0';
        consoleCmdTail = (consoleCmdTail + 1) % CONSOLE_CMD_QUEUE_SIZE;
        consoleCmdCount++;
        enqueued = true;
    }
    portEXIT_CRITICAL(&consoleCmdMux);
    return enqueued;
}

void processConsoleCommandQueue() {
    char command[CONSOLE_CMD_LEN];
    portENTER_CRITICAL(&consoleCmdMux);
    if (consoleCmdCount <= 0) {
        portEXIT_CRITICAL(&consoleCmdMux);
        return;
    }
    strncpy(command, consoleCmdQueue[consoleCmdHead], sizeof(command) - 1);
    command[sizeof(command) - 1] = '\0';
    consoleCmdHead = (consoleCmdHead + 1) % CONSOLE_CMD_QUEUE_SIZE;
    consoleCmdCount--;
    portEXIT_CRITICAL(&consoleCmdMux);
    String cmd = command;
    setWebConsoleCommandOutput(true);
    doCommand(cmd, false);
    setWebConsoleCommandOutput(false);
}

void webLog(const char* msg) {
    // 按 \n 拆分写入，避免换行符污染 JSON
    const char* start = msg;
    while (*start) {
        const char* end = strchr(start, '\n');
        int len = end ? (int)(end - start) : (int)strlen(start);
        if (len > 0) {
            int copy = (len < CONSOLE_LINE_LEN - 1) ? len : (CONSOLE_LINE_LEN - 1);
            portENTER_CRITICAL(&consoleMux);
            strncpy(consoleBuf[consoleTail], start, copy);
            consoleBuf[consoleTail][copy] = '\0';
            consoleTail = (consoleTail + 1) % CONSOLE_LINES;
            if (consoleFilled < CONSOLE_LINES) consoleFilled++;
            consoleTotal++;
            portEXIT_CRITICAL(&consoleMux);
        }
        if (!end) break;
        start = end + 1;
    }
}

// ==================== 摇杆处理 ====================

// 归一化空间死区（输入/输出均为 [-1, 1]）
// 超出死区的部分线性重映射到满幅，满推摇杆=满输出
float applyDeadzone(float norm, float deadzone) {
    if (fabsf(norm) < deadzone) return 0.0f;
    float sign = (norm > 0.0f) ? 1.0f : -1.0f;
    return sign * (fabsf(norm) - deadzone) / (1.0f - deadzone);
}

// 处理姿态轴（横滚/俯仰/偏航）
// 输入 raw ∈ [-100, +100]，输出 ∈ [-STICK_MAX, +STICK_MAX] (°)
float processAxis(float raw, float& lastValid) {
    if (isnan(raw) || isinf(raw) || fabsf(raw) > 1000.0f) {
        if (millis() - lastDataErrorTime > 2000) lastDataErrorTime = millis();
        return lastValid;
    }
    float norm = constrain(raw, -RAW_MAX, RAW_MAX) / RAW_MAX;
    norm = applyDeadzone(norm, stickDeadzone);
    lastValid = norm * STICK_MAX;
    return lastValid;
}

// 处理油门轴
// 前端：左摇杆Y轴，底部=-100，顶部=+100
// 输出：pct ∈ [0, 100]
float processThrottle(float raw) {
    if (isnan(raw) || isinf(raw) || fabsf(raw) > 1000.0f) return lastValidThrottle;
    raw = constrain(raw, -RAW_MAX, RAW_MAX);
    // 线性映射：-100→0%，0→50%，+100→100%
    float pct = (raw + RAW_MAX) / (2.0f * RAW_MAX) * THROTTLE_MAX;
    if (pct < throttleDeadzone * THROTTLE_MAX) pct = 0.0f;
    pct = constrain(pct * webRCThrottleScale, THROTTLE_MIN, THROTTLE_MAX);
    lastValidThrottle = pct;
    return pct;
}

// ==================== 核心数据处理 ====================

void setWebRCInput(float roll, float pitch, float yaw, float throttle) {
    float pThrottle = processThrottle(throttle);
    float pYaw      = processAxis(yaw,   lastValidYaw);
    float pPitch    = processAxis(pitch, lastValidPitch);
    float pRoll     = processAxis(roll,  lastValidRoll);

    // 暂存处理后的值（供状态端点读取）
    portENTER_CRITICAL(&webRCStateMux);
    webRCThrottle = pThrottle;
    webRCYaw      = pYaw;
    webRCPitch    = pPitch;
    webRCRoll     = pRoll;
    webRCLastUpdate = millis();
    webRCUpdated  = true;
    webRCLastStickUpdate = webRCLastUpdate;
    webRCStickUpdated = true;
    portEXIT_CRITICAL(&webRCStateMux);

    // 写入统一控制变量（与 SBUS/MAVLink 同路径）
    if (!isLocalSequenceRunning()) {
        controlRoll     = constrain(pRoll  * webRCStickScale / STICK_MAX, -1.0f, 1.0f);
        controlPitch    = constrain(pPitch * webRCStickScale / STICK_MAX, -1.0f, 1.0f);
        controlYaw      = constrain(pYaw   * webRCYawScale   / STICK_MAX, -1.0f, 1.0f);
        controlThrottle = pThrottle / THROTTLE_MAX;
        controlMode     = NAN;
        controlTime     = t;
    }

    static float lastLoggedThrottle = -1.0f;
    if (fabsf(pThrottle - lastLoggedThrottle) > 5.0f) {
        char event[96];
        snprintf(event, sizeof(event), "T=%.0f%% R=%.1f P=%.1f Y=%.1f Btn=0x%04X",
                 pThrottle, pRoll, pPitch, pYaw, getWebRCButtons());
        recordSystemLogEvent("WEB_RC_INPUT", event);
        lastLoggedThrottle = pThrottle;
    }
}

// ==================== JSON 协议处理器 ====================

const char* findJsonValue(const char* json, const char* key) {
    const char* p = strstr(json, key);
    if (!p) return nullptr;
    p = strchr(p, ':');
    if (!p) return nullptr;
    p++;
    while (*p == ' ' || *p == '"') p++;
    return p;
}

bool handleJSONProtocol(String& body) {
    if (body.length() == 0 || body.indexOf('{') == -1) return false;
    const char* json = body.c_str();
    const char* typePos = strstr(json, "\"t\":");
    if (!typePos) return false;
    int type = atoi(typePos + 4);
    const char* v;

    lastProcType        = type;
    lastProcButtonIdx   = -1;
    lastProcButtonState = -1;

    switch (type) {
        case 1: { // 摇杆数据
            float th = 0, r = 0, p = 0, y = 0;
            uint32_t ts = 0;
            if ((v = findJsonValue(json, "\"th\""))) th = atof(v);
            if ((v = findJsonValue(json, "\"r\"")))  r  = atof(v);
            if ((v = findJsonValue(json, "\"p\"")))  p  = atof(v);
            if ((v = findJsonValue(json, "\"y\"")))  y  = atof(v);
            if ((v = findJsonValue(json, "\"ts\""))) ts = atol(v);
            WebRCInputEvent event = {type, r, p, y, th, -1, 0, millis()};
            enqueueWebRCInput(event);
            break;
        }
        case 2: { // 按钮事件
            int idx = 0, state = 0;
            uint32_t ts = 0;
            if ((v = findJsonValue(json, "\"b\"")))  idx   = atoi(v);
            if ((v = findJsonValue(json, "\"s\"")))  state = atoi(v);
            if ((v = findJsonValue(json, "\"ts\""))) ts    = atol(v);
            if (idx >= 0 && idx < 16) {
                const uint16_t buttonMask = (uint16_t)(1U << idx);
                portENTER_CRITICAL(&webRCStateMux);
                if (state) {
                    webRCButtons |= buttonMask;
                    webRCButtonPressEdges |= buttonMask;
                } else {
                    webRCButtons &= (uint16_t)~buttonMask;
                }
                webRCLastUpdate = millis();
                webRCUpdated = true;
                portEXIT_CRITICAL(&webRCStateMux);
                lastProcButtonIdx   = idx;
                lastProcButtonState = state;
            }
            break;
        }
        case 4: // 心跳
            enqueueWebRCInput({type, 0, 0, 0, 0, -1, 0, millis()});
            break;
    }
    return true;
}

static void processWebRCInputQueue() {
    WebRCInputEvent event;
    while (dequeueWebRCInput(event)) {
        if (event.type == 1) {
            setWebRCInput(event.roll, event.pitch, event.yaw, event.throttle);
        } else {
            portENTER_CRITICAL(&webRCStateMux);
            webRCLastUpdate = event.receivedAt;
            webRCUpdated = true;
            portEXIT_CRITICAL(&webRCStateMux);
        }
    }
}

void setWebRCWarn(const char* msg) {
    portENTER_CRITICAL(&webRCWarnMux);
    strncpy(webRCWarnMsg, msg, sizeof(webRCWarnMsg) - 1);
    webRCWarnMsg[sizeof(webRCWarnMsg) - 1] = '\0';
    portEXIT_CRITICAL(&webRCWarnMux);
}

void clearWebRCWarn() {
    portENTER_CRITICAL(&webRCWarnMux);
    webRCWarnMsg[0] = '\0';
    portEXIT_CRITICAL(&webRCWarnMux);
}

static bool consumeWebRCWarn(char *destination, size_t capacity) {
    if (!destination || capacity == 0) return false;
    portENTER_CRITICAL(&webRCWarnMux);
    const bool hasWarning = webRCWarnMsg[0] != '\0';
    if (hasWarning) {
        strncpy(destination, webRCWarnMsg, capacity - 1);
        destination[capacity - 1] = '\0';
        webRCWarnMsg[0] = '\0';
    }
    portEXIT_CRITICAL(&webRCWarnMux);
    return hasWarning;
}

// ==================== 最后处理的请求上下文（供响应构造使用）====================
// ==================== HTTP 请求处理 ====================

void handleWebRCRequest() {
    if (!webRCServer.hasArg("plain")) {
        webRCServer.send(400, "application/json", "{\"e\":\"no data\"}");
        return;
    }
    String body = webRCServer.arg("plain");
    if (handleJSONProtocol(body)) {
        char resp[320];
        // warn 在按钮事件（rt=2）和心跳包（rt=4）响应中携带并清除：
        //   rt=2：用户主动操作（解锁被拒等），需要即时反馈
        //   rt=4：系统自动触发（低电自动上锁等），心跳周期≈2s，延迟可接受
        // 摇杆包（rt=1）不消费 warn，避免摇杆包抢在 state=0 响应之前把 warn 清掉
        char warning[sizeof(webRCWarnMsg)];
        bool deliverWarn = (lastProcType == 2 || lastProcType == 4) && consumeWebRCWarn(warning, sizeof(warning));
        if (deliverWarn) {
            if (lastProcButtonIdx >= 0) {
                snprintf(resp, sizeof(resp),
                    "{\"s\":\"ok\",\"m\":%d,\"arm\":%d,\"rt\":%d,\"bi\":%d,\"bs\":%d,\"warn\":\"%s\"}",
                    mode, (int)armed, lastProcType, lastProcButtonIdx, lastProcButtonState, warning);
            } else {
                snprintf(resp, sizeof(resp),
                    "{\"s\":\"ok\",\"m\":%d,\"arm\":%d,\"rt\":%d,\"warn\":\"%s\"}",
                    mode, (int)armed, lastProcType, warning);
            }
        } else {
            if (lastProcButtonIdx >= 0) {
                snprintf(resp, sizeof(resp),
                    "{\"s\":\"ok\",\"m\":%d,\"arm\":%d,\"rt\":%d,\"bi\":%d,\"bs\":%d}",
                    mode, (int)armed, lastProcType, lastProcButtonIdx, lastProcButtonState);
            } else {
                snprintf(resp, sizeof(resp),
                    "{\"s\":\"ok\",\"m\":%d,\"arm\":%d,\"rt\":%d}",
                    mode, (int)armed, lastProcType);
            }
        }
        webRCServer.send(200, "application/json", resp);
    } else {
        webRCServer.send(400, "application/json", "{\"e\":\"parse failed\"}");
    }
}

// ==================== 连接状态 ====================

bool isWebRCEnabled() {
    bool updated;
    bool stickUpdated;
    unsigned long lastUpdate, lastStickUpdate;
    portENTER_CRITICAL(&webRCStateMux);
    updated = webRCUpdated;
    stickUpdated = webRCStickUpdated;
    lastUpdate = webRCLastUpdate;
    lastStickUpdate = webRCLastStickUpdate;
    portEXIT_CRITICAL(&webRCStateMux);
    const unsigned long now = millis();
    return updated && stickUpdated && (now - lastUpdate < WEB_RC_TIMEOUT_MS) &&
        (now - lastStickUpdate < WEB_RC_TIMEOUT_MS);
}

bool isUsingWebRC() {
    return useWebRC && isWebRCEnabled();
}

// ------旧PCB印刷地址访问 :8080 → 301跳转到80端口；旧地址全部淘汰后可删除------------
// 兼容旧遥控端口，处理8080端口的301重定向请求（WiFiServer原始TCP，手动解析HTTP）
// ESP32-C3无旧用户，编译时完全去除，避免200ms自旋阻塞在单核上饿死WiFi任务
#ifndef CONFIG_IDF_TARGET_ESP32C3
static void handleRedirect8080() {
    if (!redirectServer8080) return;
    WiFiClient client = redirectServer8080->accept();
    if (!client) return;

    String path = "/";
    String host = "";
    unsigned long t0 = millis();
    while (!client.available() && millis() - t0 < 200) {}

    if (client.available()) {
        // 解析请求行：GET /path HTTP/1.1
        String reqLine = client.readStringUntil('\n');
        int s1 = reqLine.indexOf(' ');
        int s2 = reqLine.indexOf(' ', s1 + 1);
        if (s1 >= 0 && s2 > s1) path = reqLine.substring(s1 + 1, s2);

        // 读取请求头，提取Host（去掉 :8080 端口部分）
        while (client.available()) {
            String line = client.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) break;
            if (line.startsWith("Host:") || line.startsWith("host:")) {
                host = line.substring(5);
                host.trim();
                int colon = host.indexOf(':');
                if (colon >= 0) host = host.substring(0, colon);
            }
        }
    }

    client.print("HTTP/1.1 301 Moved Permanently\r\nLocation: http://");
    client.print(host);
    client.print(path);
    client.print("\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    client.stop();
}
#endif
// ------旧PCB印刷地址访问 :8080 → 301跳转到80端口；旧地址全部淘汰后可删除------------

// ==================== 主设置函数 ====================

void setupWebRC() {
    print("Setup WEB RC\n");
    lastValidThrottle = THROTTLE_MIN;
    lastValidRoll = lastValidPitch = lastValidYaw = 0.0f;

    webRCServer.on("/", HTTP_GET, []() {
#if WIFI_ENABLED
        if (isWiFiConfigPortalActive()) {
            webRCServer.send_P(200, "text/html; charset=utf-8", wifiConfigHtml);
            return;
        }
#endif
        webRCServer.send_P(200, "text/html; charset=utf-8", webRCIndexHtml);
    });
#if WIFI_ENABLED
    webRCServer.on("/wifi", HTTP_GET, []() {
        webRCServer.send_P(200, "text/html; charset=utf-8", wifiConfigHtml);
    });
    webRCServer.on("/wifi/profiles", HTTP_GET, []() {
        String json = "{\"profiles\":[";
        const int count = getWiFiProfileCount();
        for (int i = 0; i < count; ++i) {
            char ssid[33] = {};
            if (!getWiFiProfileSsid(i, ssid, sizeof(ssid))) continue;
            if (json[json.length() - 1] != '[') json += ',';
            json += "{\"ssid\":" + wifiJsonQuote(ssid) + ",\"priority\":" + String(i + 1) + "}";
        }
        json += "],\"limit\":4,\"storage_used\":" + String((unsigned)getWiFiProfileStorageUsedBytes()) +
            ",\"storage_total\":" + String((unsigned)getWiFiProfileStorageTotalBytes()) + "}";
        webRCServer.send(200, "application/json", json);
    });
    webRCServer.on("/telemetry", HTTP_GET, []() {
        webRCServer.send_P(200, "text/html; charset=utf-8", telemetryHtml);
    });
    webRCServer.on("/wifi/scan", HTTP_GET, []() {
        int16_t scanState = WiFi.scanComplete();
        if (webRCServer.hasArg("refresh") || scanState == WIFI_SCAN_FAILED) {
            if (scanState == WIFI_SCAN_RUNNING) {
                webRCServer.send(200, "application/json", "{\"state\":\"scanning\"}");
                return;
            }
            WiFi.scanDelete();
            WiFi.scanNetworks(true, true, false, 250); // 异步扫描，不阻塞飞控主循环
            scanState = WiFi.scanComplete();
        }
        if (scanState == WIFI_SCAN_RUNNING) {
            webRCServer.send(200, "application/json", "{\"state\":\"scanning\"}");
            return;
        }
        if (scanState < 0) {
            webRCServer.send(503, "application/json", "{\"state\":\"error\",\"message\":\"无法启动 Wi-Fi 扫描\"}");
            return;
        }

        String json;
        json.reserve(1800);
        json = "{\"state\":\"done\",\"networks\":[";
        bool first = true;
        const int count = scanState < 20 ? scanState : 20;
        for (int i = 0; i < count; ++i) {
            const String ssid = WiFi.SSID(i);
            if (ssid.isEmpty()) continue; // 隐藏网络由手工输入
            if (!first) json += ',';
            first = false;
            json += "{\"ssid\":\"";
            for (size_t j = 0; j < ssid.length(); ++j) {
                const char c = ssid[j];
                if (c == '\"' || c == '\\\\') json += '\\\\';
                if ((uint8_t)c >= 0x20) json += c;
            }
            json += "\",\"rssi\":";
            json += WiFi.RSSI(i);
            json += ",\"open\":";
            json += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false";
            json += '}';
        }
        json += "]}";
        webRCServer.send(200, "application/json", json);
    });
    webRCServer.on("/wifi/save", HTTP_POST, []() {
        const String ssid = webRCServer.arg("ssid");
        const String password = webRCServer.arg("password");
        if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63 ||
            (!password.isEmpty() && password.length() < 8)) {
            webRCServer.send(400, "application/json", "{\"ok\":0,\"message\":\"SSID无效或密码长度不符合要求\"}");
            return;
        }
        if (!configWiFi(false, ssid.c_str(), password.c_str())) {
            const char *reason = wifiConfigLastError();
            String message = "保存失败（" + String(reason) + "）；飞控未重启。";
            String json = "{\"ok\":0,\"reason\":" + wifiJsonQuote(reason) + ",\"message\":" + wifiJsonQuote(message.c_str()) + "}";
            webRCServer.send(500, "application/json", json);
            return;
        }
        webRCServer.send(200, "application/json", "{\"ok\":1,\"message\":\"网络已加入优先列表（最多4个），新添加的网络优先尝试。飞控即将重启。\"}");
        scheduleWiFiRestart();
    });
    webRCServer.on("/wifi/remove", HTTP_POST, []() {
        const String ssid = webRCServer.arg("ssid");
        if (!removeWiFiProfile(ssid.c_str())) {
            const char *reason = wifiConfigLastError();
            String message = "删除失败（" + String(reason) + "）。";
            String json = "{\"ok\":0,\"reason\":" + wifiJsonQuote(reason) + ",\"message\":" + wifiJsonQuote(message.c_str()) + "}";
            webRCServer.send(500, "application/json", json);
            return;
        }
        webRCServer.send(200, "application/json", "{\"ok\":1,\"message\":\"已删除网络配置。\"}");
    });
#endif
    webRCServer.on("/web_rc",           HTTP_POST, handleWebRCRequest);
    webRCServer.on("/web_rc/heartbeat", HTTP_POST, handleWebRCRequest);

    webRCServer.on("/route/upload", HTTP_POST, []() {
        if (!webRCServer.hasArg("plain")) {
            webRCServer.send(400, "application/json", "{\"ok\":0,\"error\":\"missing sequence\"}");
            return;
        }
        if (webRCServer.arg("plain").length() > OPEN_LOOP_MAX_BODY) {
            webRCServer.send(413, "application/json", "{\"ok\":0,\"error\":\"body_too_large\"}");
            return;
        }
        uint8_t stagingIndex;
        portENTER_CRITICAL(&openLoopMux);
        const bool busy = openLoopUploadInProgress || openLoopState == OPEN_LOOP_STATE_RUNNING ||
            openLoopState == OPEN_LOOP_STATE_START_PENDING || openLoopState == OPEN_LOOP_STATE_LANDING;
        if (!busy) openLoopUploadInProgress = true;
        stagingIndex = openLoopActiveBuffer ^ 1U;
        portEXIT_CRITICAL(&openLoopMux);
        if (busy) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"route busy\"}");
            return;
        }
        const String body = webRCServer.arg("plain");
        const OpenLoopParseResult parsed = parseOpenLoopSequenceText(
            body.c_str(), body.length(), openLoopBuffers[stagingIndex], OPEN_LOOP_MAX_STEPS);
        if (!parsed.ok || armed || motorsActive()) {
            portENTER_CRITICAL(&openLoopMux);
            openLoopUploadInProgress = false;
            portEXIT_CRITICAL(&openLoopMux);
            const int status = (!parsed.ok) ? 400 : 409;
            char response[128];
            snprintf(response, sizeof(response), "{\"ok\":0,\"error\":\"%s\"}",
                !parsed.ok ? parsed.reason : "upload_requires_disarmed_motors_stopped");
            webRCServer.send(status, "application/json", response);
            return;
        }
        uint32_t revision;
        portENTER_CRITICAL(&openLoopMux);
        openLoopActiveBuffer = stagingIndex;
        openLoopCount = parsed.count;
        openLoopTotalMs = parsed.totalMs;
        openLoopIndex = 0;
        openLoopRevision++;
        if (openLoopRevision == 0) openLoopRevision = 1;
        revision = openLoopRevision;
        openLoopState = OPEN_LOOP_STATE_READY;
        openLoopStartRequested = false;
        openLoopStopRequested = false;
        openLoopTakeoverRequested = false;
        openLoopUploadInProgress = false;
        setOpenLoopReason("uploaded");
        portEXIT_CRITICAL(&openLoopMux);
        char response[96];
        snprintf(response, sizeof(response), "{\"ok\":1,\"state\":\"ready\",\"plan_revision\":%lu}", (unsigned long)revision);
        webRCServer.send(200, "application/json", response);
    });
    webRCServer.on("/route/start", HTTP_POST, []() {
        uint32_t revision = 0;
        if (!parseRevisionArg(revision)) {
            webRCServer.send(400, "application/json", "{\"ok\":0,\"error\":\"missing_revision\"}");
            return;
        }
        portENTER_CRITICAL(&openLoopMux);
        const bool accepted = !openLoopUploadInProgress && openLoopState == OPEN_LOOP_STATE_READY &&
            openLoopCount > 0 && revision == openLoopRevision && isWebRCEnabled() && armed && mode == STAB;
        if (accepted) {
            openLoopRequestedRevision = revision;
            openLoopStartRequested = true;
            openLoopState = OPEN_LOOP_STATE_START_PENDING;
            setOpenLoopReason("start_pending");
        }
        portEXIT_CRITICAL(&openLoopMux);
        if (!accepted) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"requires_current_plan_connected_armed_stab\"}");
            return;
        }
        webRCServer.send(202, "application/json", "{\"ok\":1,\"state\":\"start_pending\",\"pending\":true}");
    });
    webRCServer.on("/route/stop", HTTP_POST, []() {
        portENTER_CRITICAL(&openLoopMux);
        const bool active = openLoopState == OPEN_LOOP_STATE_RUNNING || openLoopState == OPEN_LOOP_STATE_START_PENDING;
        if (active) openLoopStopRequested = true;
        portEXIT_CRITICAL(&openLoopMux);
        if (!active) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"sequence_not_running\"}");
            return;
        }
        webRCServer.send(202, "application/json", "{\"ok\":1,\"pending\":true}");
    });
    webRCServer.on("/route/takeover", HTTP_POST, []() {
        portENTER_CRITICAL(&openLoopMux);
        const bool active = openLoopState == OPEN_LOOP_STATE_RUNNING || openLoopState == OPEN_LOOP_STATE_START_PENDING ||
            openLoopState == OPEN_LOOP_STATE_LANDING;
        if (active) openLoopTakeoverRequested = true;
        portEXIT_CRITICAL(&openLoopMux);
        if (!active) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"sequence_not_active\"}");
            return;
        }
        webRCServer.send(202, "application/json", "{\"ok\":1,\"pending\":true}");
    });
    webRCServer.on("/route/status", HTTP_GET, []() {
        uint8_t state;
        uint16_t count, index;
        uint32_t totalMs, revision;
        const char *reason;
        bool pending;
        portENTER_CRITICAL(&openLoopMux);
        state = openLoopState;
        count = openLoopCount;
        index = openLoopIndex;
        totalMs = openLoopTotalMs;
        revision = openLoopRevision;
        reason = openLoopReason;
        pending = openLoopStartRequested || openLoopStopRequested || openLoopTakeoverRequested;
        portEXIT_CRITICAL(&openLoopMux);
        char response[256];
        snprintf(response, sizeof(response),
            "{\"state\":\"%s\",\"count\":%u,\"step\":%u,\"duration_s\":%.1f,\"plan_revision\":%lu,\"pending\":%s,\"reason\":\"%s\",\"arm\":%d,\"mode\":%d}",
            openLoopStateName(state), (unsigned)count,
            (unsigned)(index < count ? index + 1 : count), totalMs / 1000.0,
            (unsigned long)revision, pending ? "true" : "false", reason, (int)armed, mode);
        webRCServer.send(200, "application/json", response);
    });

    webRCServer.on("/vibration-calibration/start", HTTP_POST, []() {
        const bool confirmed = webRCServer.arg("confirm") == "1";
        portENTER_CRITICAL(&vibrationCalibrationMux);
        const bool active = vibrationCalibrationState == VIBRATION_QUEUED || vibrationCalibrationState == VIBRATION_RUNNING;
        portEXIT_CRITICAL(&vibrationCalibrationMux);
        if (!confirmed || active || armed || motorsActive() || !motorOutputsOK || isAccelCalibrationActive() ||
            batteryBlocksArming() || hasBlockingDiagnosticFault() || vibrationRouteBusy() ||
            imuCapture.state() != IMU_CAPTURE_IDLE) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"requires_confirmation_disarmed_ready_motors_idle_imu_and_no_faults\"}");
            return;
        }
        portENTER_CRITICAL(&vibrationCalibrationMux);
        vibrationCalibrationIndex = 0;
        memset(vibrationCalibrationResults, 0, sizeof(vibrationCalibrationResults));
        vibrationCalibrationState = VIBRATION_QUEUED;
        vibrationCalibrationReason = "queued";
        vibrationCalibrationStartRequested = true;
        portEXIT_CRITICAL(&vibrationCalibrationMux);
        webRCServer.send(202, "application/json", "{\"ok\":1,\"state\":\"queued\"}");
    });
    webRCServer.on("/vibration-calibration/status", HTTP_GET, []() {
        uint8_t state, index;
        const char *reason;
        VibrationMotorResult results[4];
        portENTER_CRITICAL(&vibrationCalibrationMux);
        state = vibrationCalibrationState;
        index = vibrationCalibrationIndex;
        reason = (const char *)vibrationCalibrationReason;
        memcpy(results, vibrationCalibrationResults, sizeof(results));
        portEXIT_CRITICAL(&vibrationCalibrationMux);
        const char *stateName[] = {"empty", "queued", "running", "complete", "aborted"};
        char json[480];
        snprintf(json, sizeof(json),
            "{\"state\":\"%s\",\"step\":%u,\"reason\":\"%s\",\"motors\":["
            "{\"name\":\"FR\",\"gyro_rms\":%.6f,\"accel_rms\":%.5f,\"samples\":%u},"
            "{\"name\":\"FL\",\"gyro_rms\":%.6f,\"accel_rms\":%.5f,\"samples\":%u},"
            "{\"name\":\"RR\",\"gyro_rms\":%.6f,\"accel_rms\":%.5f,\"samples\":%u},"
            "{\"name\":\"RL\",\"gyro_rms\":%.6f,\"accel_rms\":%.5f,\"samples\":%u}]}",
            state < 5 ? stateName[state] : "unknown", (unsigned)index, reason,
            results[0].gyroRms, results[0].accelRms, results[0].samples,
            results[1].gyroRms, results[1].accelRms, results[1].samples,
            results[2].gyroRms, results[2].accelRms, results[2].samples,
            results[3].gyroRms, results[3].accelRms, results[3].samples);
        webRCServer.send(200, "application/json", json);
    });
    webRCServer.on("/vibration-calibration.csv", HTTP_GET, []() {
        if (armed || motorsActive() || vibrationCalibrationState != VIBRATION_COMPLETE) {
            webRCServer.send(409, "text/plain", "complete calibration and stop motors before downloading\n");
            return;
        }
        WiFiClient client = webRCServer.client();
        client.setNoDelay(true);
        client.print("HTTP/1.1 200 OK\r\nContent-Type: text/csv; charset=utf-8\r\n");
        client.print("Cache-Control: no-store\r\nConnection: close\r\nX-Calibration-Rows: 4\r\n");
        client.print("Content-Disposition: attachment; filename=\"cf-drone-vibration-calibration.csv\"\r\n\r\n");
        client.print("motor,gyro_rms_rad_s,accel_rms_m_s2,samples\n");
        for (uint8_t i = 0; i < 4 && client.connected(); ++i) {
            VibrationMotorResult result;
            portENTER_CRITICAL(&vibrationCalibrationMux);
            result = vibrationCalibrationResults[i];
            portEXIT_CRITICAL(&vibrationCalibrationMux);
            client.printf("%s,%.6f,%.5f,%u\n", vibrationMotorNames[i], result.gyroRms, result.accelRms, result.samples);
        }
        client.stop();
    });

    webRCServer.on("/descent-calibration/start", HTTP_POST, []() {
        portENTER_CRITICAL(&openLoopMux);
        const bool routeBusy = openLoopState == OPEN_LOOP_STATE_RUNNING ||
            openLoopState == OPEN_LOOP_STATE_START_PENDING || openLoopState == OPEN_LOOP_STATE_LANDING;
        portEXIT_CRITICAL(&openLoopMux);
        if (routeBusy || !armed || mode != STAB || isControlledLandingActive()) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"requires_armed_manual_stab_flight\"}");
            return;
        }
        if (!startDescentCalibration()) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"capture_already_active_or_unavailable\"}");
            return;
        }
        webRCServer.send(202, "application/json", "{\"ok\":1,\"state\":\"recording\"}");
    });
    webRCServer.on("/descent-calibration/stop", HTTP_POST, []() {
        if (!stopDescentCalibration()) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"capture_not_recording\"}");
            return;
        }
        const DescentCalibrationSummary summary = getDescentCalibrationSummary();
        char response[192];
        snprintf(response, sizeof(response),
            "{\"ok\":1,\"state\":\"%s\",\"samples\":%u,\"duration_ms\":%lu}",
            descentCalibrationStateName(summary.state), (unsigned)summary.sampleCount,
            (unsigned long)summary.durationMs);
        webRCServer.send(200, "application/json", response);
    });
    webRCServer.on("/descent-calibration/clear", HTTP_POST, []() {
        if (armed || motorsActive()) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"requires_disarmed_motors_stopped\"}");
            return;
        }
        clearDescentCalibration();
        webRCServer.send(200, "application/json", "{\"ok\":1,\"state\":\"empty\"}");
    });
    webRCServer.on("/descent-calibration/status", HTTP_GET, []() {
        const DescentCalibrationSummary summary = getDescentCalibrationSummary();
        char response[320];
        snprintf(response, sizeof(response),
            "{\"state\":\"%s\",\"reason\":\"%s\",\"samples\":%u,\"duration_ms\":%lu,"
            "\"usable\":%s,\"median_thrust\":%.3f,\"mean_battery_v\":%.3f,\"max_tilt_deg\":%.2f,"
            "\"thrust_spread\":%.3f,\"faults\":%u,\"armed\":%s,\"mode\":%d}",
            descentCalibrationStateName(summary.state), summary.reason ? summary.reason : "unknown",
            (unsigned)summary.sampleCount, (unsigned long)summary.durationMs,
            summary.usable ? "true" : "false", summary.medianThrust, summary.meanBatteryV,
            summary.maxTiltDeg, summary.thrustP90MinusP10, (unsigned)summary.faults,
            armed ? "true" : "false", mode);
        webRCServer.send(200, "application/json", response);
    });
    webRCServer.on("/descent-calibration/save", HTTP_POST, []() {
        float value;
        if (armed || motorsActive() || !parameterPersistenceReady() ||
            !parseCalibrationValueArg(value) || !isfinite(value) || value < 0.05f || value > 0.5f) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"requires_valid_calibration_and_disarmed_persistent_storage\"}");
            return;
        }
        if (!setParameter("SF_DESCEND_THRUST", value) || !saveParameterNow("SF_DESCEND_THRUST")) {
            webRCServer.send(500, "application/json", "{\"ok\":0,\"error\":\"parameter_save_not_queued\"}");
            return;
        }
        char response[96];
        snprintf(response, sizeof(response), "{\"ok\":1,\"pending\":%s,\"value\":%.3f}",
            isParameterDirty("SF_DESCEND_THRUST") ? "true" : "false", getParameter("SF_DESCEND_THRUST"));
        webRCServer.send(202, "application/json", response);
    });
    webRCServer.on("/descent-calibration/save-status", HTTP_GET, []() {
        float requested;
        if (!parseCalibrationValueArg(requested) || requested < 0.05f || requested > 0.5f) {
            webRCServer.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_value\"}");
            return;
        }
        const float current = getParameter("SF_DESCEND_THRUST");
        const bool dirty = isParameterDirty("SF_DESCEND_THRUST");
        char response[128];
        snprintf(response, sizeof(response), "{\"ok\":1,\"saved\":%s,\"dirty\":%s,\"value\":%.3f}",
            (!dirty && fabsf(current - requested) < 0.0005f) ? "true" : "false",
            dirty ? "true" : "false", current);
        webRCServer.send(200, "application/json", response);
    });
    webRCServer.on("/descent-calibration.csv", HTTP_GET, []() {
        if (armed || motorsActive()) {
            webRCServer.send(409, "text/plain", "motors active; disarm before downloading calibration data\n");
            return;
        }
        const DescentCalibrationSummary summary = getDescentCalibrationSummary();
        if (summary.state != DESCENT_CALIBRATION_COMPLETE && summary.state != DESCENT_CALIBRATION_ABORTED) {
            webRCServer.send(409, "text/plain", "no completed calibration capture\n");
            return;
        }
        WiFiClient client = webRCServer.client();
        client.setNoDelay(true);
        client.setTimeout(100);
        client.print("HTTP/1.1 200 OK\r\nContent-Type: text/csv; charset=utf-8\r\n");
        client.print("Cache-Control: no-store\r\nConnection: close\r\n");
        client.printf("X-Calibration-Rows: %u\r\n", (unsigned)summary.sampleCount);
        client.print("Content-Disposition: attachment; filename=\"cf-drone-descent-calibration.csv\"\r\n\r\n");
        client.print("elapsed_ms,thrust_target,battery_v,roll_deg,pitch_deg,rc_throttle,faults,flight_mode,control_source\n");
        char line[160];
        for (uint16_t i = 0; i < summary.sampleCount && client.connected(); ++i) {
            DescentCalibrationSample sample;
            if (!copyDescentCalibrationSample(i, sample)) break;
            const int length = snprintf(line, sizeof(line), "%lu,%.2f,%.3f,%.2f,%.2f,%.4f,%u,%u,%u\n",
                (unsigned long)sample.elapsedMs, sample.thrustCenti / 100.0f, sample.batteryMv / 1000.0f,
                sample.rollCentiDeg / 100.0f, sample.pitchCentiDeg / 100.0f,
                sample.rcThrottleCenti / 10000.0f, (unsigned)sample.faults,
                (unsigned)sample.mode, (unsigned)sample.controlSource);
            if (length <= 0 || length >= (int)sizeof(line) || client.write((const uint8_t *)line, length) != (size_t)length) break;
            if ((i & 0x0f) == 0x0f) vTaskDelay(pdMS_TO_TICKS(1));
        }
        client.stop();
    });

    webRCServer.on("/console", HTTP_GET, []() {
        int since = -1;
        int limit = 20;
        if (webRCServer.hasArg("since")) since = webRCServer.arg("since").toInt();
        if (webRCServer.hasArg("limit")) limit = webRCServer.arg("limit").toInt();
        if (limit <= 0) limit = 20;
        if (limit > CONSOLE_LINES) limit = CONSOLE_LINES;
        char (*responseLines)[CONSOLE_LINE_LEN] = (char (*)[CONSOLE_LINE_LEN])malloc((size_t)limit * CONSOLE_LINE_LEN);
        if (!responseLines) {
            webRCServer.send(503, "application/json", "{\"e\":\"out of memory\"}");
            return;
        }

        int availableFrom, total, filled;
        portENTER_CRITICAL(&consoleMux);
        total = consoleTotal;
        filled = consoleFilled;
        int availableFromSnapshot = max(0, total - filled);
        int sendFrom = (since >= 0) ? since : availableFromSnapshot;
        if (sendFrom < availableFromSnapshot) sendFrom = availableFromSnapshot;
        if (sendFrom > total) sendFrom = total;
        int sendTo = min(total, sendFrom + limit);
        availableFrom = availableFromSnapshot;
        for (int i = sendFrom; i < sendTo; ++i) {
            int idx = i % CONSOLE_LINES;
            strncpy(responseLines[i - sendFrom], consoleBuf[idx], CONSOLE_LINE_LEN - 1);
            responseLines[i - sendFrom][CONSOLE_LINE_LEN - 1] = '\0';
        }
        portEXIT_CRITICAL(&consoleMux);

        String json = "{\"total\":";
        json += total;
        json += ",\"next\":";
        json += sendTo;
        json += ",\"has_more\":";
        json += (sendTo < consoleTotal) ? "true" : "false";
        json += ",\"lines\":[";

        bool first = true;
        for (int i = sendFrom; i < sendTo; i++) {
            if (!first) json += ",";
            first = false;
            json += "\"";
            String line = responseLines[i - sendFrom];
            line.replace("\\", "\\\\");  // \ → \\
            line.replace("\"", "\\\"");  // " → \"
            line.replace("\n", "\\n");    // 兜底：换行 → \n
            line.replace("\r", "\\r");    // 兜底：CR   → \r
            json += line + "\"";
        }
        json += "]}";
        free(responseLines);
        webRCServer.send(200, "application/json", json);
    });

    webRCServer.on("/console/cmd", HTTP_POST, []() {
        String cmd = webRCServer.arg("plain");
        cmd.trim();
        if (cmd.length() == 0) {
            webRCServer.send(400, "application/json", "{\"ok\":0,\"e\":\"empty command\"}");
            return;
        }

        char buf[CONSOLE_LINE_LEN];
        snprintf(buf, sizeof(buf), "> %s", cmd.c_str());
        webLog(buf);

        if (!enqueueConsoleCmd(cmd.c_str())) {
            webLog("! command queue is full");
            webRCServer.send(503, "application/json", "{\"ok\":0,\"e\":\"queue full\"}");
            return;
        }

        webRCServer.send(200, "application/json", "{\"ok\":1,\"queued\":1}");
    });

    webRCServer.on("/console/enable", HTTP_POST, []() {
        webConsoleEnabled = true;
        webLog(motd);
        webRCServer.send(200, "application/json", "{\"ok\":1}");
    });

    webRCServer.on("/console/disable", HTTP_POST, []() {
        webConsoleEnabled = false;
        webRCServer.send(200, "application/json", "{\"ok\":1}");
    });

    webRCServer.on("/web_rc/status", HTTP_GET, []() {
        bool updated;
        bool stickUpdated;
        unsigned long lastUpdate, lastStickUpdate;
        float throttle, roll, pitch, yaw;
        portENTER_CRITICAL(&webRCStateMux);
        updated = webRCUpdated;
        stickUpdated = webRCStickUpdated;
        lastUpdate = webRCLastUpdate;
        lastStickUpdate = webRCLastStickUpdate;
        throttle = webRCThrottle; roll = webRCRoll; pitch = webRCPitch; yaw = webRCYaw;
        portEXIT_CRITICAL(&webRCStateMux);
        const unsigned long now = millis();
        const bool enabled = updated && stickUpdated &&
            (now - lastUpdate < WEB_RC_TIMEOUT_MS) &&
            (now - lastStickUpdate < WEB_RC_TIMEOUT_MS);
        float vbat = batteryVoltage;
        if (isnan(vbat) || vbat < 0.0f) vbat = 0.0f;
        char json[448];
        snprintf(json, sizeof(json),
            "{\"armed\":%s,\"led_fast_blink\":%s,\"enabled\":%s,\"active\":%s,"
            "\"voltage\":%.2f,"
            "\"throttle\":%.1f,\"roll\":%.1f,\"pitch\":%.1f,\"yaw\":%.1f,\"faults\":%lu}",
            armed ? "true" : "false",
            ledFastBlinkActive() ? "true" : "false",
            enabled ? "true" : "false",
            (useWebRC && enabled) ? "true" : "false",
            vbat,
            throttle, roll, pitch, yaw,
            (unsigned long)getActiveDiagnosticFaults());
        webRCServer.send(200, "application/json", json);
    });

    webRCServer.on("/logs/status", HTTP_GET, []() {
        const FlightLogStatus status = getFlightLogStatus();
        char json[224];
        snprintf(json, sizeof(json),
            "{\"state\":\"%s\",\"generation\":%lu,\"rowCount\":%lu,"
            "\"reasonMask\":%lu,\"missedSamples\":%lu,\"triggerUs\":%llu}",
            flightLogStateName(status.state),
            (unsigned long)status.generation,
            (unsigned long)status.rowCount,
            (unsigned long)status.reasonMask,
            (unsigned long)status.missedSamples,
            (unsigned long long)status.triggerUs);
        webRCServer.send(200, "application/json", json);
    });

    webRCServer.on("/diag/trace.csv", HTTP_GET, []() {
        if (armed || motorsActive()) {
            webRCServer.send(409, "text/plain", "motors active; disarm before downloading loop traces\n");
            return;
        }
        uint32_t sequence, endSequence, overwritten;
        getLoopTraceRange(sequence, endSequence, overwritten);
        WiFiClient client = webRCServer.client();
        client.setNoDelay(true);
        client.setTimeout(100);
        client.print("HTTP/1.1 200 OK\r\nContent-Type: text/csv; charset=utf-8\r\n");
        client.print("Cache-Control: no-store\r\nConnection: close\r\n");
        client.printf("X-Loop-Trace-Rows: %lu\r\nX-Loop-Trace-Overwritten: %lu\r\n",
            (unsigned long)(endSequence - sequence), (unsigned long)overwritten);
        client.print("Content-Disposition: attachment; filename=\"cf-drone-loop-trace.csv\"\r\n\r\n");
        client.print("sequence,uptime_ms,dt_us,loop_sequence");
        for (uint8_t i = 0; i < LOOP_TRACE_STAGE_COUNT; ++i) {
            client.print(',');
            client.print(getLoopTraceStageName(i));
            client.print("_us");
        }
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
        client.print(",imu_wait_start_us,imu_wait_end_us,imu_irq_count_delta,imu_last_irq_us");
        client.print(",imu_sem_takes,imu_sem_timeouts,imu_read_attempts,imu_read_ready");
        client.print(",imu_read_total_us,imu_read_max_us,imu_interrupt_source,imu_wait_result");
        client.print(",imu_sem_wait_max_us");
#endif
        client.print("\n");

        for (uint32_t seq = sequence; seq < endSequence && client.connected(); ++seq) {
            if (armed || motorsActive()) break;
            LoopOverrunTrace trace;
            if (!copyLoopTrace(seq, trace)) {
                client.print("# trace changed during export; retry while disarmed\n");
                break;
            }
            char line[384];
            int used = snprintf(line, sizeof(line), "%lu,%lu,%lu,%lu",
                (unsigned long)trace.sequence, (unsigned long)trace.uptimeMs,
                (unsigned long)trace.dtUs, (unsigned long)trace.loopSequence);
            for (uint8_t i = 0; i < LOOP_TRACE_STAGE_COUNT && used > 0 && used < (int)sizeof(line); ++i) {
                const int added = snprintf(line + used, sizeof(line) - (size_t)used,
                    ",%lu", (unsigned long)trace.stageUs[i]);
                if (added < 0 || added >= (int)(sizeof(line) - (size_t)used)) { used = -1; break; }
                used += added;
            }
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
            if (used > 0 && used < (int)sizeof(line)) {
                const ImuWaitTrace &imuWait = trace.imuWait;
                const int added = snprintf(line + used, sizeof(line) - (size_t)used,
                    ",%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%u,%u,%u",
                    (unsigned long)imuWait.waitStartedUs,
                    (unsigned long)imuWait.waitEndedUs,
                    (unsigned long)imuWait.interruptCount,
                    (unsigned long)imuWait.lastInterruptUs,
                    (unsigned long)imuWait.semaphoreTakes,
                    (unsigned long)imuWait.semaphoreTimeouts,
                    (unsigned long)imuWait.readAttempts,
                    (unsigned long)imuWait.readyReads,
                    (unsigned long)imuWait.readTotalUs,
                    (unsigned long)imuWait.readMaxUs,
                    (unsigned)imuWait.interruptSource,
                    (unsigned)imuWait.result,
                    (unsigned)imuWait.semaphoreWaitMaxUs);
                if (added < 0 || added >= (int)(sizeof(line) - (size_t)used)) used = -1;
                else used += added;
            }
#endif
            if (used <= 0 || used + 1 >= (int)sizeof(line)) break;
            line[used++] = '\n';
            if (client.write((const uint8_t *)line, used) != (size_t)used) break;
            if ((seq & 0x03) == 0x03) vTaskDelay(pdMS_TO_TICKS(1));
        }
        client.stop();
    });

    webRCServer.on("/diag/trace/worst", HTTP_GET, []() {
        if (armed || motorsActive()) {
            webRCServer.send(409, "text/plain", "motors active; disarm before reading loop traces\n");
            return;
        }
        LoopOverrunTrace trace;
        if (!copyWorstLoopTrace(trace)) {
            webRCServer.send(200, "application/json", "{\"available\":false}");
            return;
        }
        char json[512];
        int used = snprintf(json, sizeof(json),
            "{\"available\":true,\"sequence\":%lu,\"uptime_ms\":%lu,"
            "\"dt_us\":%lu,\"loop_sequence\":%lu,\"stage_us\":[",
            (unsigned long)trace.sequence, (unsigned long)trace.uptimeMs,
            (unsigned long)trace.dtUs, (unsigned long)trace.loopSequence);
        for (uint8_t i = 0; i < LOOP_TRACE_STAGE_COUNT && used > 0 && used < (int)sizeof(json); ++i) {
            const int added = snprintf(json + used, sizeof(json) - (size_t)used,
                "%s%lu", i ? "," : "", (unsigned long)trace.stageUs[i]);
            if (added < 0 || added >= (int)(sizeof(json) - (size_t)used)) {
                used = -1;
                break;
            }
            used += added;
        }
        if (used <= 0 || used + 3 >= (int)sizeof(json)) {
            webRCServer.send(500, "application/json", "{\"error\":\"trace serialization failed\"}");
            return;
        }
        json[used++] = ']';
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
        const ImuWaitTrace &imuWait = trace.imuWait;
        const int imuAdded = snprintf(json + used, sizeof(json) - (size_t)used,
            ",\"imu_wait\":[%lu,%lu,%u,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u]",
            (unsigned long)imuWait.waitStartedUs, (unsigned long)imuWait.waitEndedUs,
            (unsigned)imuWait.interruptCount, (unsigned long)imuWait.lastInterruptUs,
            (unsigned)imuWait.semaphoreTakes, (unsigned)imuWait.semaphoreTimeouts,
            (unsigned)imuWait.readAttempts, (unsigned)imuWait.readyReads,
            (unsigned)imuWait.readTotalUs, (unsigned)imuWait.readMaxUs,
            (unsigned)imuWait.interruptSource, (unsigned)imuWait.result,
            (unsigned)imuWait.semaphoreWaitMaxUs);
        if (imuAdded < 0 || imuAdded >= (int)(sizeof(json) - (size_t)used)) {
            webRCServer.send(500, "application/json", "{\"error\":\"trace serialization failed\"}");
            return;
        }
        used += imuAdded;
#endif
        if (used + 2 >= (int)sizeof(json)) {
            webRCServer.send(500, "application/json", "{\"error\":\"trace serialization failed\"}");
            return;
        }
        json[used++] = '}';
        json[used] = '\0';
        webRCServer.send(200, "application/json", json);
    });

    webRCServer.on("/diag/scheduler.csv", HTTP_GET, []() {
        if (armed || motorsActive()) {
            webRCServer.send(409, "text/plain", "motors active; disarm before downloading scheduler trace\n");
            return;
        }
        const uint8_t coreCount = taskSwitchTraceCoreCount();
        if (!coreCount) {
            webRCServer.send(404, "text/plain", "scheduler trace is available only in the diagnostic build\n");
            return;
        }
        if (!freezeTaskSwitchTrace()) {
            webRCServer.send(503, "text/plain", "scheduler trace snapshot busy; retry while disarmed\n");
            return;
        }
        struct TraceResumeGuard {
            ~TraceResumeGuard() { unfreezeTaskSwitchTrace(); }
        } resumeGuard;

        uint32_t oldest[2] = {}, next[2] = {}, overwritten[2] = {};
        for (uint8_t core = 0; core < coreCount; ++core)
            taskSwitchTraceRange(core, oldest[core], next[core], overwritten[core]);

        WiFiClient client = webRCServer.client();
        client.setNoDelay(true);
        client.setTimeout(100);
        client.print("HTTP/1.1 200 OK\r\nContent-Type: text/csv; charset=utf-8\r\n");
        client.print("Cache-Control: no-store\r\nConnection: close\r\n");
        client.printf("X-Task-Trace-Core0-Overwritten: %lu\r\nX-Task-Trace-Core1-Overwritten: %lu\r\n\r\n",
            (unsigned long)overwritten[0], (unsigned long)overwritten[1]);
        client.print("sequence,timestamp_us,loop_sequence,capture_id,task_handle,core,event,dropped_events\n");
        for (uint8_t core = 0; core < coreCount && client.connected(); ++core) {
            for (uint32_t sequence = oldest[core]; sequence < next[core] && client.connected(); ++sequence) {
                TaskSwitchTraceEvent event;
                if (!copyTaskSwitchTrace(core, sequence, event)) continue;
                char line[128];
                const int length = snprintf(line, sizeof(line), "%lu,%lu,%lu,%u,0x%08lx,%u,%u,%u\n",
                    (unsigned long)event.sequence, (unsigned long)event.timestampUs,
                    (unsigned long)event.loopSequence, (unsigned)event.captureId,
                    (unsigned long)event.taskHandle, (unsigned)event.coreId,
                    (unsigned)event.kind, (unsigned)event.droppedEvents);
                if (length <= 0 || length >= (int)sizeof(line) ||
                    client.write((const uint8_t *)line, (size_t)length) != (size_t)length) break;
                if ((sequence & 0x0f) == 0x0f) vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
        client.stop();
    });

    webRCServer.on("/diag/ipc.csv", HTTP_GET, []() {
        if (armed || motorsActive()) {
            webRCServer.send(409, "text/plain", "motors active; disarm before downloading IPC trace\n");
            return;
        }
        if (!taskSwitchTraceCoreCount()) {
            webRCServer.send(404, "text/plain", "IPC trace is available only in the diagnostic build\n");
            return;
        }
        if (!freezeTaskSwitchTrace()) {
            webRCServer.send(503, "text/plain", "IPC trace snapshot busy; retry while disarmed\n");
            return;
        }
        struct TraceResumeGuard {
            ~TraceResumeGuard() { unfreezeTaskSwitchTrace(); }
        } resumeGuard;

        TaskIpcTraceEvent events[1] = {};
        uint32_t overwritten = 0;
        const uint8_t count = copyTaskIpcTrace(events, 1, overwritten);
        WiFiClient client = webRCServer.client();
        client.setNoDelay(true);
        client.setTimeout(100);
        client.print("HTTP/1.1 200 OK\r\nContent-Type: text/csv; charset=utf-8\r\n");
        client.print("Cache-Control: no-store\r\nConnection: close\r\n\r\n");
        client.print("sequence,request_us,callback_start_us,callback_us,caller_task,caller_pc,caller_core,target_core,overwritten\n");
        for (uint8_t i = 0; i < count && client.connected(); ++i) {
            char line[144];
            const int length = snprintf(line, sizeof(line), "%lu,%lu,%lu,%lu,0x%08lx,0x%08lx,%u,%u,%lu\n",
                (unsigned long)events[i].sequence, (unsigned long)events[i].requestUs,
                (unsigned long)events[i].callbackStartedUs, (unsigned long)events[i].callbackUs,
                (unsigned long)events[i].callerTask,
                (unsigned long)events[i].callerPc,
                (unsigned)events[i].callerCore, (unsigned)events[i].targetCore,
                (unsigned long)overwritten);
            if (length <= 0 || length >= (int)sizeof(line) ||
                client.write((const uint8_t *)line, (size_t)length) != (size_t)length) break;
        }
        client.stop();
    });

    webRCServer.on("/logs/resume", HTTP_POST, []() {
        if (armed || motorsActive()) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"motors active\"}");
            return;
        }
        if (!resumeFlightLog()) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"post trigger capture still active\"}");
            return;
        }
        const FlightLogStatus status = getFlightLogStatus();
        char json[192];
        snprintf(json, sizeof(json),
            "{\"ok\":1,\"state\":\"%s\",\"generation\":%lu,\"rowCount\":%lu}",
            flightLogStateName(status.state), (unsigned long)status.generation,
            (unsigned long)status.rowCount);
        webRCServer.send(200, "application/json", json);
    });

    webRCServer.on("/logs.csv", HTTP_GET, []() {
        if (armed || motorsActive()) {
            webRCServer.send(409, "text/plain", "motors active; disarm before downloading logs\n");
            return;
        }
        FlightLogStatus status = getFlightLogStatus();
        if (status.state != FROZEN) {
            if (!freezeFlightLog()) {
                webRCServer.send(409, "text/plain", "log is busy or in post-trigger capture\n");
                return;
            }
            status = getFlightLogStatus();
        }
        if (status.state != FROZEN) {
            webRCServer.send(409, "text/plain", "log is not frozen\n");
            return;
        }
        const int columns = getLogColumnCount();
        if (columns <= 0 || columns > WEB_LOG_CSV_COLUMNS_CAPACITY) {
            webRCServer.send(500, "text/plain", "log column count exceeds HTTP export capacity\n");
            return;
        }
        const uint32_t generation = status.generation;
        const uint32_t rowCount = status.rowCount;
        WiFiClient client = webRCServer.client();
        client.setNoDelay(true);
        client.setTimeout(100);
        client.print("HTTP/1.1 200 OK\r\nContent-Type: text/csv; charset=utf-8\r\n");
        client.print("Cache-Control: no-store\r\nConnection: close\r\n");
        client.printf("X-Flight-Log-Rows: %lu\r\n", (unsigned long)rowCount);
        client.print("Content-Disposition: attachment; filename=\"cf-drone-flight-log.csv\"\r\n\r\n");
        for (int i = 0; i < columns; ++i) {
            if (i) client.print(',');
            client.print(getLogColumnName(i));
        }
        client.print("\n");

        float row[WEB_LOG_CSV_COLUMNS_CAPACITY];
        char line[WEB_LOG_CSV_ROW_CAPACITY];
        for (uint32_t i = 0; i < rowCount && client.connected(); ++i) {
            if (armed || motorsActive()) break;
            const FlightLogStatus current = getFlightLogStatus();
            if (current.generation != generation || current.state != FROZEN) break;
            if (!copyFrozenLogRow(generation, i, row, WEB_LOG_CSV_COLUMNS_CAPACITY)) break;
            int used = 0;
            bool ok = true;
            for (int column = 0; column < columns; ++column) {
                ok = appendCsvFloat(line, sizeof(line), used, row[column], column == 0);
                if (!ok) break;
            }
            if (!ok || used + 1 >= (int)sizeof(line)) break;
            line[used++] = '\n';
            if (client.write((const uint8_t *)line, used) != (size_t)used) break;
            if ((i & 0x03) == 0x03) vTaskDelay(pdMS_TO_TICKS(1));
        }
        client.stop();
    });

    webRCServer.onNotFound([]() {
#if WIFI_ENABLED
        if (isWiFiConfigPortalActive()) {
            webRCServer.sendHeader("Location", "/wifi", true);
            webRCServer.send(302, "text/plain", "Wi-Fi configuration");
            return;
        }
#endif
        webRCServer.send(404, "text/plain", "Not found");
    });

    webRCServer.begin();

    if (xTaskCreatePinnedToCore([](void*) {
            for (;;) {
                webRCServer.handleClient();
#ifndef CONFIG_IDF_TARGET_ESP32C3
                handleRedirect8080();
#endif
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }, "web_rc_http", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
        print("WEB_RC_HTTP state=DISABLED reason=task_create_failed\n");
        recordSystemLogEvent("WEB_RC", "http_task_create_failed");
    } else {
        print("WEB_RC_HTTP state=READY core=0 priority=1\n");
        recordSystemLogEvent("WEB_RC", "http_task_ready core=0 priority=1");
    }

    // ------旧PCB印刷地址访问 :8080 → 301跳转到80端口；旧地址全部淘汰后可删除------------
#ifndef CONFIG_IDF_TARGET_ESP32C3
    redirectServer8080 = new WiFiServer(8080); // 堆构造，不占BSS
    redirectServer8080->begin(); // 8080端口轻量重定向（WiFiServer），兼容旧PCB印刷地址
#endif
    // ------旧PCB印刷地址访问 :8080 → 301跳转到80端口；旧地址全部淘汰后可删除------------

#if WIFI_ENABLED
    if (isWiFiConfigPortalActive()) {
        print("✓ Web RC / Wi-Fi 配置: http://%s/wifi\n", WiFi.softAPIP().toString().c_str());
    } else {
        print("✓ Web RC 已启动；STA连接后访问 http://<飞控IP>/\n");
    }
#else
    print("✓ Web RC 已启动\n");
#endif
    print("  死区 摇杆=%.0f%% 油门=%.0f%% | 缩放 摇杆=%.2f 偏航=%.2f\n",
          stickDeadzone * 100.0f, throttleDeadzone * 100.0f,
          webRCStickScale, webRCYawScale);
}

// ==================== 主循环函数 ====================

void readWebRC() {
    processWebRCInputQueue();
    stepOpenLoopSequence();
    serviceVibrationCalibration();
    
    if (isWebRCEnabled()) {
        webRCEnabled = useWebRC = true;
    } else {
        webRCEnabled = useWebRC = false;
    }
	if (isUsingWebRC()) setDiagnosticFault(DIAG_WEB_RC_LOSS, false);
}

#else
void setupWebRC() { print("Web RC已禁用\n"); }
void readWebRC()  {}
void processConsoleCommandQueue() {}
#endif
