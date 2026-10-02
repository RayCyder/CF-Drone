// 网页遥控服务端

#if WEB_RC_ENABLED

#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>
#include <ctype.h>
#include <stdlib.h>
#include "web_rc_html.h"
#include "board_config.h"
#include "diagnostics.h"
#include "slow_loop_retention.h"
#include "task_switch_trace_runtime.h"
#include "system_log.h"
#include "web_rc_input.h"
#include "open_loop_sequence.h"
#include "descent_calibration.h"
#include "imu_capture.h"
#include "level_calibration_state.h"
#include "quaternion.h"
#include "control.h"
#include "flight_log.h"
#include "wifi_recovery_policy.h"
#include "web_rc_lease_policy.h"
#include "web_rc_fast_stop_policy.h"
#include "web_armed_route_policy.h"
#include "vibration_motor_result.h"

// 飞控统一控制变量（供协议适配层写入，与 SBUS/MAVLink 共用）
extern double t;
extern double controlTime;
extern float controlRoll, controlPitch, controlYaw, controlThrottle, controlMode;
extern float batteryVoltage;
extern const char* armBlockReason();
bool isWebRCEnabled();
extern float thrustTarget;
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
static char *openLoopUploadedText = nullptr;
static bool openLoopRecordedInput = false;
static uint16_t openLoopIndex = 0;
static uint32_t openLoopTotalMs = 0;
static uint32_t openLoopRevision = 0;
static uint32_t openLoopStartMs = 0;
static uint32_t openLoopCurrentDeadlineMs = 0;
static uint32_t openLoopLastSchedulerMs = 0;
static OpenLoopControls openLoopAppliedControls = {0, 0, 0, 0};
static bool openLoopLandingStarted = false;
static bool openLoopTakeoverRequested = false;
static bool openLoopUploadInProgress = false;
static uint8_t openLoopState = OPEN_LOOP_STATE_EMPTY;
static const char *openLoopReason = "empty";

enum VibrationCalibrationState : uint8_t {
    VIBRATION_EMPTY, VIBRATION_BOOT_WAIT, VIBRATION_QUEUED, VIBRATION_BASELINE,
    VIBRATION_RUNNING, VIBRATION_SETTLING, VIBRATION_COMPLETE, VIBRATION_ABORTED
};
static constexpr float VIBRATION_TEST_OUTPUT = 0.05f;
static constexpr uint32_t VIBRATION_TEST_MS = 500;
static constexpr uint32_t VIBRATION_BASELINE_MS = 200;
static constexpr uint32_t VIBRATION_SETTLE_MS = 1000;
static constexpr uint32_t VIBRATION_BOOT_DELAY_MS = 2000;
static portMUX_TYPE vibrationCalibrationMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t vibrationCalibrationState = VIBRATION_EMPTY;
static volatile uint8_t vibrationCalibrationIndex = 0;
static volatile bool vibrationCalibrationStartRequested = false;
static volatile uint32_t vibrationPhaseStartedMs = 0;
static bool vibrationBaselineComplete = false;
static VibrationMotorResult vibrationBaseline = {};
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
extern bool startMotorTest(int n, float output, uint32_t durationMs);
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
extern bool persistLevelRotationPairNow(float oldRoll, float oldPitch, float newRoll, float newPitch);
extern Vector imuRotation;
extern bool imuOK;
extern Quaternion attitude;
extern bool isParameterDirty(const char *name);
extern bool parameterPersistenceReady();
extern float webRCThrottleScale, webRCStickScale, webRCYawScale;
extern float stickDeadzone, throttleDeadzone;
extern WebServer &webRCServer;
#if WIFI_ENABLED
extern uint32_t getWiFiDisconnectCount();
extern uint32_t getWiFiLastDisconnectMs();
#endif

#define WEB_LOG_CSV_COLUMNS_CAPACITY 41
#define WEB_LOG_CSV_ROW_CAPACITY 1024
static_assert(WEB_LOG_CSV_COLUMNS_CAPACITY >= FLIGHT_LOG_COLUMNS, "HTTP CSV export capacity must cover all flight log columns");
static_assert(WEB_LOG_CSV_ROW_CAPACITY >= 1024, "HTTP CSV rows require at least 1024 bytes");

static portMUX_TYPE levelCalibrationMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t levelCalibrationState = LEVEL_EMPTY;
static const char *levelCalibrationReason = "empty";
static Vector levelCalibrationBaseRotation;
static Vector levelCalibrationProposedRotation;
static float levelCalibrationBeforeRollDeg = 0.0f;
static float levelCalibrationBeforePitchDeg = 0.0f;
static float levelCalibrationAccelNorm = 0.0f;
static float levelCalibrationAccelSd = 0.0f;
static float levelCalibrationGyroSd = 0.0f;
static uint32_t levelCalibrationStartedMs = 0;
static bool levelCalibrationStartRequested = false;
static bool levelCalibrationCancelRequested = false;
static bool levelCalibrationCaptureOwned = false;
static uint16_t levelCalibrationProcessCount = 0;
static uint16_t levelCalibrationProcessIndex = 0;
static float levelCalibrationSum[6] = {};
static float levelCalibrationSquared[6] = {};
static bool levelCalibrationProcessComplete = false;
static const uint16_t LEVEL_CALIBRATION_PROCESS_CHUNK = 64;

bool isLevelCalibrationActive() {
    const uint8_t state = __atomic_load_n(&levelCalibrationState, __ATOMIC_ACQUIRE);
    return levelCalibrationBlocksArming((LevelCalibrationState)state);
}

static void setLevelCalibrationState(uint8_t state, const char *reason) {
    portENTER_CRITICAL(&levelCalibrationMux);
    levelCalibrationReason = reason ? reason : "unknown";
    __atomic_store_n(&levelCalibrationState, (uint8_t)state, __ATOMIC_RELEASE);
    portEXIT_CRITICAL(&levelCalibrationMux);
}

static bool publishLevelCalibrationReady() {
    portENTER_CRITICAL(&levelCalibrationMux);
    const bool ready = levelCalibrationState == LEVEL_PROCESSING &&
        !__atomic_load_n(&levelCalibrationCancelRequested, __ATOMIC_ACQUIRE);
    if (ready) {
        levelCalibrationReason = "ready_for_confirmation";
        __atomic_store_n(&levelCalibrationState, (uint8_t)LEVEL_READY, __ATOMIC_RELEASE);
    }
    portEXIT_CRITICAL(&levelCalibrationMux);
    return ready;
}

static uint8_t getLevelCalibrationState() {
    return __atomic_load_n(&levelCalibrationState, __ATOMIC_ACQUIRE);
}

static bool getLevelCalibrationStartRequested() {
    return __atomic_load_n(&levelCalibrationStartRequested, __ATOMIC_ACQUIRE);
}

static bool getLevelCalibrationCancelRequested() {
    return __atomic_load_n(&levelCalibrationCancelRequested, __ATOMIC_ACQUIRE);
}

static void setLevelCalibrationStartRequested(bool requested) {
    __atomic_store_n(&levelCalibrationStartRequested, requested, __ATOMIC_RELEASE);
}

static void setLevelCalibrationCancelRequested(bool requested) {
    __atomic_store_n(&levelCalibrationCancelRequested, requested, __ATOMIC_RELEASE);
}

static bool takeLevelCalibrationStartRequest() {
    return __atomic_exchange_n(&levelCalibrationStartRequested, false, __ATOMIC_ACQ_REL);
}

static bool takeLevelCalibrationCancelRequest() {
    return __atomic_exchange_n(&levelCalibrationCancelRequested, false, __ATOMIC_ACQ_REL);
}

static void getLevelCalibrationSnapshot(uint8_t &state, const char *&reason) {
    portENTER_CRITICAL(&levelCalibrationMux);
    state = levelCalibrationState;
    reason = levelCalibrationReason;
    portEXIT_CRITICAL(&levelCalibrationMux);
}

static void resetLevelCalibrationProcessing() {
    levelCalibrationProcessCount = 0;
    levelCalibrationProcessIndex = 0;
    levelCalibrationProcessComplete = false;
    for (uint8_t axis = 0; axis < 6; ++axis) {
        levelCalibrationSum[axis] = 0.0f;
        levelCalibrationSquared[axis] = 0.0f;
    }
}

static void startLevelCalibrationProcessing() {
    resetLevelCalibrationProcessing();
    levelCalibrationProcessCount = imuCapture.size();
    levelCalibrationProcessComplete = levelCalibrationProcessCount == IMU_CAPTURE_CAPACITY;
    setLevelCalibrationState(LEVEL_PROCESSING, "processing");
}

static void finishLevelCalibrationProcessing() {
    const uint16_t count = levelCalibrationProcessCount;
    const bool complete = levelCalibrationProcessComplete && count == IMU_CAPTURE_CAPACITY;
    imuCapture.release();
    levelCalibrationCaptureOwned = false;
    if (!complete) {
        setLevelCalibrationState(LEVEL_REJECTED, "incomplete_capture");
        return;
    }
    float mean[6], sd[6];
    for (uint8_t axis = 0; axis < 6; ++axis) {
        mean[axis] = levelCalibrationSum[axis] / count;
        sd[axis] = sqrtf(max(0.0f, levelCalibrationSquared[axis] / count - mean[axis] * mean[axis]));
    }
    const Vector gravity(mean[0], mean[1], mean[2]);
    levelCalibrationAccelNorm = gravity.norm();
    levelCalibrationAccelSd = max(sd[0], max(sd[1], sd[2]));
    levelCalibrationGyroSd = max(sd[3], max(sd[4], sd[5]));
    if (!gravity.valid() || !isfinite(levelCalibrationAccelNorm) ||
        fabsf(levelCalibrationAccelNorm - ONE_G) > ONE_G * 0.05f ||
        levelCalibrationAccelSd > 0.10f || levelCalibrationGyroSd > 0.01f ||
        Vector(mean[3], mean[4], mean[5]).norm() > 0.03f) {
        setLevelCalibrationState(LEVEL_REJECTED, "not_stationary_or_gravity_invalid");
        return;
    }
    // rotateVector(v, q) applies the inverse of q. Undo the current sensor-to-body
    // rotation, then find X/Y mounting angles that map gravity to body +Z.
    const Vector sensor = Quaternion::rotateVector(
        gravity, Quaternion::fromEuler(levelCalibrationBaseRotation));
    const Vector candidate(
        atan2f(sensor.y, sensor.z),
        -atan2f(sensor.x, sqrtf(sensor.y * sensor.y + sensor.z * sensor.z)),
        levelCalibrationBaseRotation.z);
    const Vector corrected = Quaternion::rotateVector(
        sensor, Quaternion::fromEuler(candidate).inversed());
    if (!candidate.valid() || corrected.z <= 0.0f ||
        fabsf(candidate.x - levelCalibrationBaseRotation.x) > radians(15.0f) ||
        fabsf(candidate.y - levelCalibrationBaseRotation.y) > radians(15.0f) ||
        fabsf(corrected.x) > 0.02f || fabsf(corrected.y) > 0.02f) {
        setLevelCalibrationState(LEVEL_REJECTED, "mounting_offset_or_geometry_invalid");
        return;
    }
    levelCalibrationBeforeRollDeg = degrees(atan2f(gravity.y, gravity.z));
    levelCalibrationBeforePitchDeg = degrees(atan2f(-gravity.x, sqrtf(gravity.y * gravity.y + gravity.z * gravity.z)));
    levelCalibrationProposedRotation = candidate;
    publishLevelCalibrationReady();
}

static void processLevelCalibrationCaptureChunk() {
    if (getLevelCalibrationState() != LEVEL_PROCESSING) return;
    uint16_t processed = 0;
    while (levelCalibrationProcessComplete &&
        levelCalibrationProcessIndex < levelCalibrationProcessCount &&
        processed < LEVEL_CALIBRATION_PROCESS_CHUNK) {
        ImuCaptureSample sample;
        if (!imuCapture.copy(levelCalibrationProcessIndex, sample)) {
            levelCalibrationProcessComplete = false;
            break;
        }
        const float values[6] = {
            sample.accCentiMetersPerSec2[0] * 0.01f,
            sample.accCentiMetersPerSec2[1] * 0.01f,
            sample.accCentiMetersPerSec2[2] * 0.01f,
            sample.gyroMicroRadPerSec[0] * 1e-6f,
            sample.gyroMicroRadPerSec[1] * 1e-6f,
            sample.gyroMicroRadPerSec[2] * 1e-6f
        };
        for (uint8_t axis = 0; axis < 6; ++axis) {
            levelCalibrationSum[axis] += values[axis];
            levelCalibrationSquared[axis] += values[axis] * values[axis];
        }
        ++levelCalibrationProcessIndex;
        ++processed;
    }
    if (!levelCalibrationProcessComplete || levelCalibrationProcessIndex >= levelCalibrationProcessCount) {
        finishLevelCalibrationProcessing();
    }
}

static void serviceLevelCalibration() {
    const uint8_t state = getLevelCalibrationState();
    if (takeLevelCalibrationCancelRequest()) {
        setLevelCalibrationStartRequested(false);
        if (levelCalibrationCaptureOwned) {
            if (imuCapture.state() == IMU_CAPTURE_RUNNING) imuCapture.stop();
            imuCapture.release();
            levelCalibrationCaptureOwned = false;
        }
        resetLevelCalibrationProcessing();
        setLevelCalibrationState(LEVEL_EMPTY, "cancelled");
        return;
    }
    if (state == LEVEL_QUEUED && takeLevelCalibrationStartRequest()) {
        resetLevelCalibrationProcessing();
        if (armed || motorsActive() || !imuOK || isAccelCalibrationActive() || motorTestActive ||
            vibrationRouteBusy() || vibrationCalibrationState == VIBRATION_BOOT_WAIT ||
            vibrationCalibrationState == VIBRATION_QUEUED ||
            vibrationCalibrationState == VIBRATION_BASELINE ||
            vibrationCalibrationState == VIBRATION_RUNNING ||
            vibrationCalibrationState == VIBRATION_SETTLING || isLocalSequenceRunning() ||
            !parameterPersistenceReady() || imuCapture.state() != IMU_CAPTURE_IDLE) {
            setLevelCalibrationState(LEVEL_REJECTED, "preflight_failed");
            return;
        }
        levelCalibrationBaseRotation = imuRotation;
        if (!imuCapture.start(armed, motorsActive(), ESP.getFreeHeap())) {
            setLevelCalibrationState(LEVEL_REJECTED, "capture_start_failed");
            return;
        }
        levelCalibrationCaptureOwned = true;
        levelCalibrationStartedMs = millis();
        setLevelCalibrationState(LEVEL_COLLECTING, "collecting");
        return;
    }
    if (state == LEVEL_COLLECTING) {
        const ImuCaptureState captureState = imuCapture.state();
        if (captureState == IMU_CAPTURE_READY) {
            startLevelCalibrationProcessing();
        } else if ((uint32_t)(millis() - levelCalibrationStartedMs) > 5000U) {
            if (captureState == IMU_CAPTURE_RUNNING) imuCapture.stop();
            if (levelCalibrationCaptureOwned) {
                imuCapture.release();
                levelCalibrationCaptureOwned = false;
            }
            setLevelCalibrationState(LEVEL_REJECTED, "capture_timeout");
        } else if (captureState == IMU_CAPTURE_IDLE && levelCalibrationCaptureOwned) {
            imuCapture.release();
            levelCalibrationCaptureOwned = false;
            setLevelCalibrationState(LEVEL_REJECTED, "capture_stopped");
        }
        return;
    }
    if (state == LEVEL_PROCESSING) processLevelCalibrationCaptureChunk();
}

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

bool isLocalSequenceReadyForAuto() {
    portENTER_CRITICAL(&openLoopMux);
    const bool ready = openLoopState == OPEN_LOOP_STATE_READY && openLoopCount > 0 &&
        !openLoopUploadInProgress;
    portEXIT_CRITICAL(&openLoopMux);
    return ready && !isControlledLandingActive() && isWebRCEnabled();
}

static void setOpenLoopReason(const char *reason) {
    openLoopReason = reason ? reason : "unknown";
}

void cancelLocalSequenceForManualMode() {
    portENTER_CRITICAL(&openLoopMux);
    if (openLoopState == OPEN_LOOP_STATE_RUNNING || openLoopState == OPEN_LOOP_STATE_START_PENDING ||
        openLoopState == OPEN_LOOP_STATE_LANDING) {
        openLoopState = OPEN_LOOP_STATE_ABORTED;
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

void abortVibrationCalibrationForDisarm() {
    const uint8_t state = vibrationCalibrationState;
    if (state != VIBRATION_BOOT_WAIT && state != VIBRATION_QUEUED && state != VIBRATION_BASELINE &&
        state != VIBRATION_RUNNING && state != VIBRATION_SETTLING) return;
    if (state == VIBRATION_BASELINE || state == VIBRATION_RUNNING) {
        if (imuCapture.state() == IMU_CAPTURE_RUNNING) imuCapture.stop();
        imuCapture.release();
    }
    vibrationCalibrationStartRequested = false;
    setVibrationCalibrationState(VIBRATION_ABORTED, "operator_stop");
}

static VibrationMotorResult measureVibrationCapture() {
    const uint16_t count = imuCapture.size();
    double sums[6] = {}, squares[6] = {};
    uint16_t valid = 0;
    for (uint16_t i = 0; i < count; ++i) {
        ImuCaptureSample sample;
        if (!imuCapture.copy(i, sample)) continue;
        for (int axis = 0; axis < 3; ++axis) {
            const double gyro = sample.gyroMicroRadPerSec[axis] / 1000000.0;
            const double accel = sample.accCentiMetersPerSec2[axis] / 100.0;
            sums[axis] += gyro; squares[axis] += gyro * gyro;
            sums[axis + 3] += accel; squares[axis + 3] += accel * accel;
        }
        ++valid;
    }
    if (!valid) return {0, 0, 0};
    double gyroVariance = 0, accelVariance = 0;
    for (int axis = 0; axis < 3; ++axis) {
        gyroVariance += squares[axis] / valid - (sums[axis] / valid) * (sums[axis] / valid);
        accelVariance += squares[axis + 3] / valid - (sums[axis + 3] / valid) * (sums[axis + 3] / valid);
    }
    return {(float)sqrt(gyroVariance > 0 ? gyroVariance : 0),
        (float)sqrt(accelVariance > 0 ? accelVariance : 0), valid};
}

static VibrationMotorResult saveCurrentVibrationMotorCapture() {
    const VibrationMotorResult result = measureVibrationCapture();
    portENTER_CRITICAL(&vibrationCalibrationMux);
    if (vibrationCalibrationIndex < 4)
        vibrationCalibrationResults[vibrationCalibrationIndex] = result;
    portEXIT_CRITICAL(&vibrationCalibrationMux);
    return result;
}

static VibrationMotorResult saveVibrationBaselineCapture() {
    const VibrationMotorResult baseline = measureVibrationCapture();
    portENTER_CRITICAL(&vibrationCalibrationMux);
    vibrationBaseline = baseline;
    portEXIT_CRITICAL(&vibrationCalibrationMux);
    return baseline;
}

static const char *motorResponseName(const VibrationMotorResult &result,
                                     const VibrationMotorResult &baseline) {
    return vibrationMotorResponseDetected(result, baseline) ? "detected" : "inconclusive";
}

static void serviceVibrationCalibration() {
#if CF_DRONE_ENABLE_BOOT_MOTOR_SELF_CHECK
    if (vibrationCalibrationState == VIBRATION_BOOT_WAIT) {
        if ((uint32_t)(millis() - vibrationPhaseStartedMs) < VIBRATION_BOOT_DELAY_MS) return;
        if (armed || motorsActive() || !imuOK || !motorOutputsOK || controlThrottle > 0.01f ||
            isAccelCalibrationActive() || isLevelCalibrationActive() || motorTestActive ||
            batteryBlocksArming() || hasBlockingDiagnosticFault() || vibrationRouteBusy() ||
            !parameterPersistenceReady() || imuCapture.state() != IMU_CAPTURE_IDLE) {
            setVibrationCalibrationState(VIBRATION_ABORTED, "boot_preflight_failed");
            recordSystemLogEvent("MOTOR_SELF_CHECK", "boot_preflight_failed");
            return;
        }
        vibrationCalibrationStartRequested = true;
        setVibrationCalibrationState(VIBRATION_QUEUED, "boot_queued");
        recordSystemLogEvent("MOTOR_SELF_CHECK", "boot_started output=5% pulse_ms=500");
    }
#endif
    if (vibrationCalibrationState == VIBRATION_SETTLING) {
        if (motorsActive() || motorTestActive) {
            cancelMotorTest();
            setVibrationCalibrationState(VIBRATION_ABORTED, "final_stop_failed");
            return;
        }
        if ((uint32_t)(millis() - vibrationPhaseStartedMs) < VIBRATION_SETTLE_MS) return;
        setVibrationCalibrationState(VIBRATION_COMPLETE, "complete");
        return;
    }
    if (vibrationCalibrationState == VIBRATION_QUEUED && vibrationCalibrationStartRequested) {
        // Let the previous motor's frame vibration decay before sampling the next one.
        if (vibrationCalibrationIndex > 0 &&
            (uint32_t)(millis() - vibrationPhaseStartedMs) < VIBRATION_SETTLE_MS) return;
        vibrationCalibrationStartRequested = false;
        if (armed || motorsActive() || !motorOutputsOK || isAccelCalibrationActive() ||
            isLevelCalibrationActive() || motorTestActive ||
            batteryBlocksArming() || hasBlockingDiagnosticFault() || vibrationRouteBusy() ||
            imuCapture.state() != IMU_CAPTURE_IDLE) {
            setVibrationCalibrationState(VIBRATION_ABORTED, "preflight_failed");
            return;
        }
        if (!vibrationBaselineComplete) {
            if (!imuCapture.start(armed, motorsActive(), ESP.getFreeHeap())) {
                setVibrationCalibrationState(VIBRATION_ABORTED, "baseline_capture_failed");
                return;
            }
            vibrationPhaseStartedMs = millis();
            setVibrationCalibrationState(VIBRATION_BASELINE, "baseline_collecting");
            return;
        }
        if (!imuCapture.start(armed, motorsActive(), ESP.getFreeHeap())) {
            setVibrationCalibrationState(VIBRATION_ABORTED, "motor_capture_failed");
            return;
        }
        if (!startMotorTest(vibrationMotorIds[vibrationCalibrationIndex],
                VIBRATION_TEST_OUTPUT, VIBRATION_TEST_MS)) {
            imuCapture.stop();
            imuCapture.release();
            setVibrationCalibrationState(VIBRATION_ABORTED, "motor_test_rejected");
            return;
        }
        vibrationPhaseStartedMs = millis();
        setVibrationCalibrationState(VIBRATION_RUNNING, "running");
        return;
    }
    if (vibrationCalibrationState == VIBRATION_BASELINE) {
        if (armed || motorsActive() || batteryBlocksArming() || hasBlockingDiagnosticFault()) {
            imuCapture.stop();
            imuCapture.release();
            setVibrationCalibrationState(VIBRATION_ABORTED, "baseline_safety_state_changed");
            return;
        }
        if (imuCapture.state() == IMU_CAPTURE_RUNNING &&
            (uint32_t)(millis() - vibrationPhaseStartedMs) < VIBRATION_BASELINE_MS) return;
        if (imuCapture.state() == IMU_CAPTURE_RUNNING) imuCapture.stop();
        const VibrationMotorResult baseline = saveVibrationBaselineCapture();
        imuCapture.release();
        if (baseline.samples < VIBRATION_RESPONSE_MIN_BASELINE_SAMPLES ||
            baseline.accelRms > 0.25f || baseline.gyroRms > 0.05f) {
            setVibrationCalibrationState(VIBRATION_ABORTED, "baseline_unstable");
            return;
        }
        portENTER_CRITICAL(&vibrationCalibrationMux);
        vibrationBaselineComplete = true;
        vibrationCalibrationStartRequested = true;
        vibrationCalibrationState = VIBRATION_QUEUED;
        vibrationCalibrationReason = "baseline_complete";
        portEXIT_CRITICAL(&vibrationCalibrationMux);
        return;
    }
    if (vibrationCalibrationState != VIBRATION_RUNNING) return;
    if (armed || batteryBlocksArming() || hasBlockingDiagnosticFault()) {
        imuCapture.stop();
        saveCurrentVibrationMotorCapture();
        imuCapture.release();
        cancelMotorTest();
        setVibrationCalibrationState(VIBRATION_ABORTED, "safety_state_changed");
        return;
    }
    if (motorTestActive) {
        if ((uint32_t)(millis() - vibrationPhaseStartedMs) > VIBRATION_TEST_MS + 150U) {
            imuCapture.stop();
            saveCurrentVibrationMotorCapture();
            imuCapture.release();
            cancelMotorTest();
            setVibrationCalibrationState(VIBRATION_ABORTED, "motor_test_timeout");
        }
        return;
    }
    if (imuCapture.state() == IMU_CAPTURE_RUNNING) imuCapture.stop();
    const VibrationMotorResult result = saveCurrentVibrationMotorCapture();
    if (imuCapture.size() < VIBRATION_RESPONSE_MIN_MOTOR_SAMPLES) {
        imuCapture.release();
        setVibrationCalibrationState(VIBRATION_ABORTED, "insufficient_imu_samples");
        return;
    }
    imuCapture.release();
    vibrationPhaseStartedMs = millis();
    portENTER_CRITICAL(&vibrationCalibrationMux);
    vibrationCalibrationResults[vibrationCalibrationIndex] = result;
    ++vibrationCalibrationIndex;
    const bool complete = vibrationCalibrationIndex >= 4;
    vibrationCalibrationState = complete ? VIBRATION_SETTLING : VIBRATION_QUEUED;
    vibrationCalibrationStartRequested = !complete;
    vibrationCalibrationReason = complete ? "final_stop_wait" : "next_motor";
    portEXIT_CRITICAL(&vibrationCalibrationMux);
}

void runBootMotorSelfCheckBeforeWiFi() {
#if CF_DRONE_ENABLE_BOOT_MOTOR_SELF_CHECK
    portENTER_CRITICAL(&vibrationCalibrationMux);
    vibrationCalibrationIndex = 0;
    vibrationBaselineComplete = false;
    vibrationBaseline = {};
    memset(vibrationCalibrationResults, 0, sizeof(vibrationCalibrationResults));
    vibrationCalibrationStartRequested = false;
    vibrationPhaseStartedMs = millis();
    vibrationCalibrationState = VIBRATION_BOOT_WAIT;
    vibrationCalibrationReason = "boot_wait";
    portEXIT_CRITICAL(&vibrationCalibrationMux);

    print("MOTOR_SELF_CHECK state=BOOT_WAIT delay_ms=%lu output=5%% pulse_ms=500 auto_boot=1 phase=pre_wifi\n",
        (unsigned long)VIBRATION_BOOT_DELAY_MS);
    const uint32_t startedMs = millis();
    const uint32_t timeoutMs = VIBRATION_BOOT_DELAY_MS + VIBRATION_BASELINE_MS +
        4U * VIBRATION_TEST_MS + 4U * VIBRATION_SETTLE_MS + 2500U;
    while (vibrationCalibrationState == VIBRATION_BOOT_WAIT ||
           vibrationCalibrationState == VIBRATION_QUEUED ||
           vibrationCalibrationState == VIBRATION_BASELINE ||
           vibrationCalibrationState == VIBRATION_RUNNING ||
           vibrationCalibrationState == VIBRATION_SETTLING) {
        readIMU();
        updateBatteryVoltage();
        serviceMotorTest();
        serviceVibrationCalibration();
        // The regular loop is not running yet, so drain the already bounded
        // UART queue here to preserve boot/self-check logs without overflow.
        serviceSerialConsoleOutput();
        if ((uint32_t)(millis() - startedMs) > timeoutMs) {
            const uint8_t timedOutState = vibrationCalibrationState;
            if (imuCapture.state() == IMU_CAPTURE_RUNNING) imuCapture.stop();
            if (timedOutState == VIBRATION_BASELINE) saveVibrationBaselineCapture();
            else if (timedOutState == VIBRATION_RUNNING) saveCurrentVibrationMotorCapture();
            imuCapture.release();
            cancelMotorTest();
            setVibrationCalibrationState(VIBRATION_ABORTED, "boot_timeout");
            recordSystemLogEvent("MOTOR_SELF_CHECK", "boot_timeout");
            break;
        }
        delay(0);
    }
    if (motorTestActive) cancelMotorTest();
    const bool complete = vibrationCalibrationState == VIBRATION_COMPLETE;
    print("MOTOR_SELF_CHECK state=%s phase=pre_wifi step=%u reason=%s\n",
        complete ? "COMPLETE" : "ABORTED", (unsigned)vibrationCalibrationIndex,
        (const char *)vibrationCalibrationReason);
    recordSystemLogEvent("MOTOR_SELF_CHECK", complete ? "boot_complete_pre_wifi" :
        (const char *)vibrationCalibrationReason);
#endif
}

static void enterOpenLoopLandingLocked(const char *reason) {
    openLoopState = OPEN_LOOP_STATE_LANDING;
    openLoopTakeoverRequested = false;
    openLoopLandingStarted = false;
    setOpenLoopReason(reason);
}

static void stepOpenLoopSequence() {
    bool beginLanding = false;
    bool requestManualStab = false;
    bool applyStep = false;
    bool recordedInput = false;
    OpenLoopPackedStep step;
    uint32_t elapsedForSlew = 0;
    const uint32_t now = millis();

    portENTER_CRITICAL(&openLoopMux);
    if (openLoopTakeoverRequested) {
        openLoopTakeoverRequested = false;
        openLoopState = OPEN_LOOP_STATE_ABORTED;
        openLoopLandingStarted = false;
        setOpenLoopReason("takeover_requested");
        requestManualStab = true;
    }

    if (isControlledLandingActive() &&
        (openLoopState == OPEN_LOOP_STATE_RUNNING ||
         openLoopState == OPEN_LOOP_STATE_START_PENDING))
        enterOpenLoopLandingLocked("operator_landing");

    // An uploaded plan is the single local AUTO program. Start on the next
    // armed AUTO flight-loop tick; the browser does not stream replay inputs.
    if (openLoopState == OPEN_LOOP_STATE_READY && openLoopCount > 0 &&
        !openLoopUploadInProgress && armed && mode == AUTO &&
        !isControlledLandingActive() && isWebRCEnabled()) {
        startOpenLoopNow(now);
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
        } else if (mode != STAB && mode != AUTO) {
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
                recordedInput = openLoopRecordedInput;
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
        // Browser recordings already contain the pilot's time-varying stick
        // values. A second slew changes their amplitude and timing. Keep the
        // existing slew for authored plans that lack the recording marker.
        if (recordedInput) openLoopAppliedControls = target;
        else openLoopSlewControls(openLoopAppliedControls, target, elapsedForSlew);
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

    uint32_t idleDropCount() const { return idleDrops; }
    uint32_t maxHandleTimeUs() const { return maxHandleUs; }
    uint32_t slowHandleCount() const { return slowHandles; }

    void handleClient() override {
        // WebServer serves one client at a time and otherwise waits 5 s for an
        // accepted socket to send its first byte. Browser preconnects can hold
        // up every stick packet even though the control loop remains healthy.
        if (_currentStatus == HC_WAIT_READ && !_currentClient.available() &&
            (uint32_t)(millis() - _statusChange) > 150) {
            _currentClient.stop();
            _currentClient = NetworkClient();
            _currentStatus = HC_NONE;
            if (idleDrops < UINT32_MAX) ++idleDrops;
        }
        const uint32_t startedUs = micros();
        WebServer::handleClient();
        const uint32_t elapsedUs = (uint32_t)(micros() - startedUs);
        if (elapsedUs > maxHandleUs) maxHandleUs = elapsedUs;
        if (elapsedUs >= 100000UL && slowHandles < UINT32_MAX) ++slowHandles;
    }

private:
    uint32_t idleDrops = 0;
    uint32_t maxHandleUs = 0;
    uint32_t slowHandles = 0;
};

static ResponsiveWebServer responsiveWebRCServer(80);
WebServer &webRCServer = responsiveWebRCServer; // 主服务器：80端口
static uint32_t webRCMaxRequestUs = 0;
static uint32_t webRCSlowRequests = 0;

#if CF_DRONE_ENABLE_FAST_STOP_SERVER
// A separate tiny listener prevents a slow page/download request on port 80
// from queueing an emergency stop behind WebServer::handleClient().
static WiFiServer fastStopServer(82);
static uint8_t pendingFastStop = WEB_RC_FAST_STOP_NONE;
static void copyWebRCStopToken(char *destination);

WebRCFastStopAction consumeWebRCFastStop() {
    return (WebRCFastStopAction)__atomic_exchange_n(&pendingFastStop,
        (uint8_t)WEB_RC_FAST_STOP_NONE, __ATOMIC_ACQ_REL);
}

static void serviceFastStopClient() {
    static WiFiClient client;
    static uint32_t acceptedAtMs = 0;
    static char requestLine[48];
    static size_t lineLength = 0;
    if (!client) {
        client = fastStopServer.available();
        if (!client) return;
        acceptedAtMs = millis();
        lineLength = 0;
    }
    bool complete = false;
    while (client.available() > 0 && lineLength < sizeof(requestLine) - 1) {
        const int next = client.read();
        if (next < 0) break;
        if (next == '\n') { complete = true; break; }
        requestLine[lineLength++] = (char)next;
    }
    if (!complete) {
        // Never let a partial or slow client delay a later stop request.
        if (lineLength == sizeof(requestLine) - 1 ||
            (uint32_t)(millis() - acceptedAtMs) >= 4 || !client.connected())
            client.stop();
        return;
    }
    requestLine[lineLength] = '\0';
    char stopToken[WEB_RC_LEASE_TOKEN_CHARS + 1];
    copyWebRCStopToken(stopToken);
    const WebRCFastStopAction action = parseWebRCFastStopRequest(requestLine, stopToken);
    if (action != WEB_RC_FAST_STOP_NONE) {
        // Kill takes priority over lock, and both take priority over landing.
        uint8_t old = __atomic_load_n(&pendingFastStop, __ATOMIC_RELAXED);
        if ((uint8_t)action > old)
            __atomic_store_n(&pendingFastStop, (uint8_t)action, __ATOMIC_RELEASE);
    }
    client.print(action != WEB_RC_FAST_STOP_NONE
        ? "HTTP/1.1 204 No Content\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"
        : "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    client.stop();
}
#else
WebRCFastStopAction consumeWebRCFastStop() { return WEB_RC_FAST_STOP_NONE; }
#endif

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

static bool rejectFlightApiInConfigPortal() {
#if WIFI_ENABLED
    if (!WifiRecoveryPolicy::flightApiAllowed(isWiFiConfigPortalActive())) {
        webRCServer.send(403, "application/json", "{\"ok\":0,\"error\":\"flight_api_disabled_in_wifi_config_portal\"}");
        return true;
    }
#endif
    return false;
}

static bool rejectWiFiMaintenanceWhileActive() {
    if (!WifiRecoveryPolicy::maintenanceAllowed(armed, motorsActive())) {
        webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"requires_disarmed_motors_stopped\"}");
        return true;
    }
    return false;
}

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
static WebRCLeasePolicy webRCLease;
static char webRCStopToken[WEB_RC_LEASE_TOKEN_CHARS + 1] = {};
static portMUX_TYPE webRCStopTokenMux = portMUX_INITIALIZER_UNLOCKED;
static bool webRCStopTokenMatches(const char *candidate) {
    portENTER_CRITICAL(&webRCStopTokenMux);
    const bool matches = candidate && webRCStopToken[0] &&
        strncmp(webRCStopToken, candidate, WEB_RC_LEASE_TOKEN_CHARS + 1) == 0;
    portEXIT_CRITICAL(&webRCStopTokenMux);
    return matches;
}
static void publishWebRCStopToken(const char *token) {
    portENTER_CRITICAL(&webRCStopTokenMux);
    memcpy(webRCStopToken, token, WEB_RC_LEASE_TOKEN_CHARS + 1);
    portEXIT_CRITICAL(&webRCStopTokenMux);
}
#if CF_DRONE_ENABLE_FAST_STOP_SERVER
static void copyWebRCStopToken(char *destination) {
    portENTER_CRITICAL(&webRCStopTokenMux);
    memcpy(destination, webRCStopToken, WEB_RC_LEASE_TOKEN_CHARS + 1);
    portEXIT_CRITICAL(&webRCStopTokenMux);
}
#endif
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

static void clearWebRCQueuedInputAndButtons() {
    portENTER_CRITICAL(&webRCInputMux);
    webRCInputHead = 0;
    webRCInputTail = 0;
    webRCInputCount = 0;
    portEXIT_CRITICAL(&webRCInputMux);

    portENTER_CRITICAL(&webRCStateMux);
    webRCButtons = 0;
    webRCButtonPressEdges = 0;
    webRCRoll = 0.0f;
    webRCPitch = 0.0f;
    webRCYaw = 0.0f;
    webRCThrottle = 0.0f;
    webRCUpdated = false;
    webRCStickUpdated = false;
    webRCLastUpdate = 0;
    webRCLastStickUpdate = 0;
    portEXIT_CRITICAL(&webRCStateMux);
    webRCEnabled = false;
    useWebRC = false;
}

static void makeWebRCLeaseToken(char *token, size_t capacity) {
    if (!token || capacity < WEB_RC_LEASE_TOKEN_CHARS + 1) return;
    const uint32_t a = (uint32_t)esp_random();
    const uint32_t b = (uint32_t)esp_random();
    snprintf(token, capacity, "%08lX%08lX", (unsigned long)a, (unsigned long)b);
}

static bool readWebRCLeaseToken(const String *body, char *token, size_t capacity) {
    if (!token || capacity == 0) return false;
    token[0] = '\0';
    if (webRCServer.hasArg("lease")) {
        const String value = webRCServer.arg("lease");
        if (value.length() == WEB_RC_LEASE_TOKEN_CHARS) {
            strncpy(token, value.c_str(), capacity - 1);
            token[capacity - 1] = '\0';
            return true;
        }
    }
    if (!body) return false;
    const char *lease = strstr(body->c_str(), "\"lease\"");
    if (!lease) return false;
    const char *colon = strchr(lease, ':');
    if (!colon) return false;
    const char *firstQuote = strchr(colon, '"');
    if (!firstQuote) return false;
    const char *start = firstQuote + 1;
    const char *end = strchr(start, '"');
    if (!end || end - start != WEB_RC_LEASE_TOKEN_CHARS) return false;
    const size_t copy = (size_t)(end - start);
    if (copy >= capacity) return false;
    memcpy(token, start, copy);
    token[copy] = '\0';
    return true;
}

static bool readWebRCStopToken(const String &body, char *token, size_t capacity) {
    if (!token || capacity < WEB_RC_LEASE_TOKEN_CHARS + 1) return false;
    const char *field = strstr(body.c_str(), "\"stop\"");
    const char *colon = field ? strchr(field, ':') : nullptr;
    const char *quote = colon ? strchr(colon, '"') : nullptr;
    if (!quote) return false;
    const char *value = quote + 1;
    const char *end = strchr(value, '"');
    if (!end || end - value != WEB_RC_LEASE_TOKEN_CHARS) return false;
    memcpy(token, value, WEB_RC_LEASE_TOKEN_CHARS);
    token[WEB_RC_LEASE_TOKEN_CHARS] = '\0';
    return true;
}

static bool isWebRCEmergencyButtonOverride(const String &body) {
    const char *json = body.c_str();
    const char *typePos = strstr(json, "\"t\":");
    const char *buttonPos = strstr(json, "\"b\":");
    if (!typePos || !buttonPos) return false;
    return webRCLeaseAllowsEmergencyButtonOverride(atoi(typePos + 4), atoi(buttonPos + 4));
}

static bool requireWebRCLease(const String *body = nullptr) {
    char token[WEB_RC_LEASE_TOKEN_CHARS + 1];
    const uint32_t now = millis();
    if (!readWebRCLeaseToken(body, token, sizeof(token)) ||
        !webRCLease.validateAndTouch(token, now, WEB_RC_TIMEOUT_MS)) {
        webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"web_rc_lease_required\"}");
        return false;
    }
    return true;
}

static bool requireWebRCStopToken(const String &body) {
    char token[WEB_RC_LEASE_TOKEN_CHARS + 1];
    if (readWebRCStopToken(body, token, sizeof(token)) &&
        webRCStopTokenMatches(token)) return true;
    webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"web_rc_stop_token_required\"}");
    return false;
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

#if CF_DRONE_ENABLE_WEB_INPUT_EVENT_LOG
    static float lastLoggedThrottle = -1.0f;
    if (fabsf(pThrottle - lastLoggedThrottle) > 5.0f) {
        char event[96];
        snprintf(event, sizeof(event), "T=%.0f%% R=%.1f P=%.1f Y=%.1f Btn=0x%04X",
                 pThrottle, pRoll, pPitch, pYaw, getWebRCButtons());
        recordSystemLogEvent("WEB_RC_INPUT", event);
        lastLoggedThrottle = pThrottle;
    }
#endif
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

static void handleWebRCRequestBody() {
    if (rejectFlightApiInConfigPortal()) return;
    if (!webRCServer.hasArg("plain")) {
        webRCServer.send(400, "application/json", "{\"e\":\"no data\"}");
        return;
    }
    String body = webRCServer.arg("plain");
    if (isWebRCEmergencyButtonOverride(body)) {
        if (!requireWebRCStopToken(body)) return;
    } else if (!requireWebRCLease(&body)) return;
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

void handleWebRCRequest() {
    const uint32_t startedUs = micros();
    handleWebRCRequestBody();
    const uint32_t elapsedUs = (uint32_t)(micros() - startedUs);
    if (elapsedUs > webRCMaxRequestUs) webRCMaxRequestUs = elapsedUs;
    if (elapsedUs >= 100000UL && webRCSlowRequests < UINT32_MAX) ++webRCSlowRequests;
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

#if WIFI_ENABLED
    // Apply the portal policy before route dispatch, including read/export
    // endpoints and any future routes added to this server.
    webRCServer.addMiddleware([](WebServer &server, Middleware::Callback next) {
        const HTTPMethod method = server.method();
        if (isWiFiConfigPortalActive() &&
            !WifiRecoveryPolicy::portalHttpAllowed(server.uri().c_str(),
                method == HTTP_GET, method == HTTP_POST)) {
            server.send(403, "application/json", "{\"ok\":0,\"error\":\"endpoint_disabled_in_wifi_config_portal\"}");
            return true;
        }
        return next();
    });
#endif

    webRCServer.addMiddleware([](WebServer &server, Middleware::Callback next) {
        const HTTPMethod method = server.method();
        if ((armed || motorsActive()) && !webArmedRouteAllowed(server.uri().c_str(),
            method == HTTP_GET, method == HTTP_POST)) {
            server.send(423, "application/json", "{\"ok\":0,\"error\":\"non_flight_request_paused_while_armed\"}");
            return true;
        }
        return next();
    });

    webRCServer.on("/", HTTP_GET, []() {
#if WIFI_ENABLED
        if (isWiFiConfigPortalActive()) {
            webRCServer.send_P(200, "text/html; charset=utf-8", wifiConfigHtml);
            return;
        }
#endif
        // send_P performs one large write for this 100+ KiB page and ignores
        // a short write. A truncated response leaves later JS functions
        // undefined in the browser. Send bounded pieces and account for every
        // byte before closing the connection.
        WiFiClient client = webRCServer.client();
        client.setNoDelay(true);
        const size_t length = sizeof(webRCIndexHtml) - 1;
        char header[160];
        const int headerLength = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
            "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: %u\r\n\r\n",
            (unsigned)length);
        if (headerLength <= 0 || headerLength >= (int)sizeof(header) ||
            client.write((const uint8_t *)header, (size_t)headerLength) != (size_t)headerLength) {
            client.stop();
            return;
        }
        size_t sent = 0;
        uint32_t lastProgressMs = millis();
        while (sent < length && client.connected()) {
            const size_t remaining = length - sent;
            const size_t chunk = remaining < 1024 ? remaining : 1024;
            const size_t written = client.write((const uint8_t *)webRCIndexHtml + sent, chunk);
            if (written > 0) {
                sent += written;
                lastProgressMs = millis();
            } else if ((uint32_t)(millis() - lastProgressMs) > 5000) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        client.stop();
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
        if (rejectWiFiMaintenanceWhileActive()) return;
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
        if (rejectWiFiMaintenanceWhileActive()) return;
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
        if (rejectWiFiMaintenanceWhileActive()) return;
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
    webRCServer.on("/web_rc/lease", HTTP_POST, []() {
        if (rejectFlightApiInConfigPortal()) return;
        // A fresh page may take control while disarmed. In flight, only the
        // current page's stop token can renew an expired lease.
        if (armed && !webRCStopTokenMatches(webRCServer.arg("stop").c_str())) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"web_rc_stop_token_required\"}");
            return;
        }
        char token[WEB_RC_LEASE_TOKEN_CHARS + 1];
        makeWebRCLeaseToken(token, sizeof(token));
        bool ownerChanged = false;
        const uint32_t now = millis();
        if (!webRCLease.acquire(now, WEB_RC_TIMEOUT_MS, token, &ownerChanged)) {
            char response[128];
            const uint32_t age = (uint32_t)(now - webRCLease.lastSeenMs);
            snprintf(response, sizeof(response),
                "{\"ok\":0,\"error\":\"web_rc_lease_in_use\",\"retry_ms\":%lu}",
                (unsigned long)(age < WEB_RC_TIMEOUT_MS ? WEB_RC_TIMEOUT_MS - age : 0));
            webRCServer.send(409, "application/json", response);
            return;
        }
        if (ownerChanged) clearWebRCQueuedInputAndButtons();
        char stopToken[WEB_RC_LEASE_TOKEN_CHARS + 1];
        makeWebRCLeaseToken(stopToken, sizeof(stopToken));
        publishWebRCStopToken(stopToken);
        char response[192];
        snprintf(response, sizeof(response),
            "{\"ok\":1,\"lease\":\"%s\",\"stop\":\"%s\",\"timeout_ms\":%lu}",
            token, stopToken, (unsigned long)WEB_RC_TIMEOUT_MS);
        webRCServer.send(200, "application/json", response);
    });
    webRCServer.on("/web_rc",           HTTP_POST, handleWebRCRequest);
    webRCServer.on("/web_rc/heartbeat", HTTP_POST, handleWebRCRequest);

    webRCServer.on("/route/upload", HTTP_POST, []() {
        if (rejectFlightApiInConfigPortal()) return;
        if (!requireWebRCLease()) return;
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
        const bool recordedInput = body.startsWith("# WEB_RC_RECORDED_V1\n");
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
        char *uploadedText = (char *)malloc(body.length() + 1);
        if (!uploadedText) {
            portENTER_CRITICAL(&openLoopMux);
            openLoopUploadInProgress = false;
            portEXIT_CRITICAL(&openLoopMux);
            webRCServer.send(503, "application/json", "{\"ok\":0,\"error\":\"insufficient_memory\"}");
            return;
        }
        memcpy(uploadedText, body.c_str(), body.length() + 1);
        uint32_t revision;
        char *previousText;
        portENTER_CRITICAL(&openLoopMux);
        openLoopActiveBuffer = stagingIndex;
        openLoopCount = parsed.count;
        previousText = openLoopUploadedText;
        openLoopUploadedText = uploadedText;
        openLoopRecordedInput = recordedInput;
        openLoopTotalMs = parsed.totalMs;
        openLoopIndex = 0;
        openLoopRevision++;
        if (openLoopRevision == 0) openLoopRevision = 1;
        revision = openLoopRevision;
        openLoopState = OPEN_LOOP_STATE_READY;
        openLoopTakeoverRequested = false;
        openLoopUploadInProgress = false;
        setOpenLoopReason("uploaded");
        portEXIT_CRITICAL(&openLoopMux);
        free(previousText);
        char response[96];
        snprintf(response, sizeof(response), "{\"ok\":1,\"state\":\"ready\",\"plan_revision\":%lu}", (unsigned long)revision);
        webRCServer.send(200, "application/json", response);
    });
    webRCServer.on("/route/plan", HTTP_GET, []() {
        if (rejectFlightApiInConfigPortal()) return;
        if (armed) {
            webRCServer.send(409, "text/plain", "disarm_required");
            return;
        }
        uint32_t revision;
        bool available, busy;
        portENTER_CRITICAL(&openLoopMux);
        revision = openLoopRevision;
        busy = openLoopUploadInProgress;
        available = !busy && openLoopCount > 0 && openLoopUploadedText;
        portEXIT_CRITICAL(&openLoopMux);
        if (!available) {
            webRCServer.send(busy ? 409 : 204, "text/plain", "");
            return;
        }
        // Uploads and reads share the WebServer task; the flight loop never edits this text.
        const String text = String(openLoopUploadedText);
        webRCServer.sendHeader("Cache-Control", "no-store");
        webRCServer.sendHeader("X-Plan-Revision", String(revision));
        webRCServer.send(200, "text/plain; charset=utf-8", text);
    });
    webRCServer.on("/route/takeover", HTTP_POST, []() {
        if (rejectFlightApiInConfigPortal()) return;
        if (!requireWebRCLease()) return;
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
        bool pending, recordedInput;
        portENTER_CRITICAL(&openLoopMux);
        state = openLoopState;
        count = openLoopCount;
        index = openLoopIndex;
        totalMs = openLoopTotalMs;
        revision = openLoopRevision;
        reason = openLoopReason;
        pending = openLoopTakeoverRequested;
        recordedInput = openLoopRecordedInput;
        portEXIT_CRITICAL(&openLoopMux);
        char response[288];
        snprintf(response, sizeof(response),
            "{\"state\":\"%s\",\"count\":%u,\"step\":%u,\"duration_s\":%.1f,\"plan_revision\":%lu,\"pending\":%s,\"reason\":\"%s\",\"arm\":%d,\"mode\":%d,\"recorded\":%s}",
            openLoopStateName(state), (unsigned)count,
            (unsigned)(index < count ? index + 1 : count), totalMs / 1000.0,
            (unsigned long)revision, pending ? "true" : "false", reason, (int)armed, mode,
            recordedInput ? "true" : "false");
        webRCServer.send(200, "application/json", response);
    });

    webRCServer.on("/level-calibration/start", HTTP_POST, []() {
        if (rejectFlightApiInConfigPortal()) return;
        if (!requireWebRCLease()) return;
        if (webRCServer.arg("confirm") != "1" || armed || motorsActive() || !imuOK ||
            isAccelCalibrationActive() || motorTestActive || vibrationRouteBusy() ||
            vibrationCalibrationState == VIBRATION_BOOT_WAIT ||
            vibrationCalibrationState == VIBRATION_QUEUED || vibrationCalibrationState == VIBRATION_BASELINE ||
            vibrationCalibrationState == VIBRATION_RUNNING || vibrationCalibrationState == VIBRATION_SETTLING ||
            isLocalSequenceRunning() || !parameterPersistenceReady() ||
            imuCapture.state() != IMU_CAPTURE_IDLE || isLevelCalibrationActive()) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"requires_level_confirm_disarmed_stationary_imu_and_free_capture\"}");
            return;
        }
        setLevelCalibrationStartRequested(true);
        setLevelCalibrationCancelRequested(false);
        setLevelCalibrationState(LEVEL_QUEUED, "queued");
        webRCServer.send(202, "application/json", "{\"ok\":1,\"state\":\"queued\",\"pending\":true}");
    });
    webRCServer.on("/level-calibration/status", HTTP_GET, []() {
        const char *names[] = {"empty", "queued", "collecting", "processing", "ready", "applying", "applied", "rejected", "cancelling"};
        const Vector angles = attitude.toEuler();
        uint8_t state;
        const char *reason;
        getLevelCalibrationSnapshot(state, reason);
        const bool pending = getLevelCalibrationStartRequested() || getLevelCalibrationCancelRequested();
        char json[512];
        snprintf(json, sizeof(json),
            "{\"state\":\"%s\",\"reason\":\"%s\",\"armed\":%s,\"roll_deg\":%.3f,\"pitch_deg\":%.3f,"
            "\"before_roll_deg\":%.3f,\"before_pitch_deg\":%.3f,\"acc_norm\":%.3f,"
            "\"acc_sd\":%.4f,\"gyro_sd\":%.5f,\"old_rot_roll_rad\":%.6f,"
            "\"old_rot_pitch_rad\":%.6f,\"new_rot_roll_rad\":%.6f,\"new_rot_pitch_rad\":%.6f,"
            "\"persist_pending\":%s,\"pending\":%s}",
            names[state], reason, armed ? "true" : "false",
            degrees(angles.x), degrees(angles.y),
            levelCalibrationBeforeRollDeg, levelCalibrationBeforePitchDeg,
            levelCalibrationAccelNorm, levelCalibrationAccelSd, levelCalibrationGyroSd,
            levelCalibrationBaseRotation.x, levelCalibrationBaseRotation.y,
            levelCalibrationProposedRotation.x, levelCalibrationProposedRotation.y,
            (isParameterDirty("IMU_ROT_ROLL") || isParameterDirty("IMU_ROT_PITCH")) ? "true" : "false",
            pending ? "true" : "false");
        webRCServer.send(200, "application/json", json);
    });
    webRCServer.on("/level-calibration/apply", HTTP_POST, []() {
        if (rejectFlightApiInConfigPortal()) return;
        if (!requireWebRCLease()) return;
        if (webRCServer.arg("confirm") != "1" || getLevelCalibrationState() != LEVEL_READY ||
            getLevelCalibrationCancelRequested() ||
            armed || motorsActive() || !imuOK || motorTestActive || vibrationRouteBusy() ||
            isAccelCalibrationActive() || !parameterPersistenceReady() ||
            isParameterDirty("IMU_ROT_ROLL") || isParameterDirty("IMU_ROT_PITCH") ||
            imuRotation.x != levelCalibrationBaseRotation.x ||
            imuRotation.y != levelCalibrationBaseRotation.y ||
            imuRotation.z != levelCalibrationBaseRotation.z) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"stale_or_unsafe_level_calibration\"}");
            return;
        }
        if (!beginPersistentWriteBatch()) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"parameter_write_busy\"}");
            return;
        }
        setLevelCalibrationState(LEVEL_APPLYING, "applying");
        const bool saved = setParameter("IMU_ROT_ROLL", levelCalibrationProposedRotation.x) &&
            setParameter("IMU_ROT_PITCH", levelCalibrationProposedRotation.y) &&
            persistLevelRotationPairNow(levelCalibrationBaseRotation.x, levelCalibrationBaseRotation.y,
                levelCalibrationProposedRotation.x, levelCalibrationProposedRotation.y);
        if (!saved) {
            setParameter("IMU_ROT_ROLL", levelCalibrationBaseRotation.x);
            setParameter("IMU_ROT_PITCH", levelCalibrationBaseRotation.y);
            finishPersistentWriteBatch(true);
            setLevelCalibrationState(LEVEL_REJECTED, "parameter_save_failed");
            webRCServer.send(500, "application/json", "{\"ok\":0,\"error\":\"parameter_save_failed\"}");
            return;
        }
        setLevelCalibrationState(LEVEL_APPLIED, "restart_required_after_parameter_write");
        finishPersistentWriteBatch(true);
        webRCServer.send(202, "application/json", "{\"ok\":1,\"pending\":true}");
    });
    webRCServer.on("/level-calibration/discard", HTTP_POST, []() {
        if (rejectFlightApiInConfigPortal()) return;
        if (!requireWebRCLease()) return;
        const uint8_t state = getLevelCalibrationState();
        if (state == LEVEL_APPLIED) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"restart_required\"}");
            return;
        }
        if (state == LEVEL_APPLYING) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"capture_busy\"}");
            return;
        }
        if (state == LEVEL_QUEUED || state == LEVEL_COLLECTING || state == LEVEL_PROCESSING || state == LEVEL_CANCELLING) {
            setLevelCalibrationCancelRequested(true);
            setLevelCalibrationState(LEVEL_CANCELLING, "cancelling");
            webRCServer.send(202, "application/json", "{\"ok\":1,\"pending\":true}");
            return;
        }
        setLevelCalibrationState(LEVEL_EMPTY, "empty");
        webRCServer.send(200, "application/json", "{\"ok\":1}");
    });

    webRCServer.on("/vibration-calibration/start", HTTP_POST, []() {
        if (rejectFlightApiInConfigPortal()) return;
        if (!requireWebRCLease()) return;
        portENTER_CRITICAL(&vibrationCalibrationMux);
        const bool active = vibrationCalibrationState == VIBRATION_BOOT_WAIT ||
            vibrationCalibrationState == VIBRATION_QUEUED ||
            vibrationCalibrationState == VIBRATION_BASELINE || vibrationCalibrationState == VIBRATION_RUNNING ||
            vibrationCalibrationState == VIBRATION_SETTLING;
        portEXIT_CRITICAL(&vibrationCalibrationMux);
        if (active || armed || motorsActive() || !motorOutputsOK || isAccelCalibrationActive() ||
            isLevelCalibrationActive() ||
            batteryBlocksArming() || hasBlockingDiagnosticFault() || vibrationRouteBusy() ||
            imuCapture.state() != IMU_CAPTURE_IDLE) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"requires_disarmed_ready_motors_idle_imu_and_no_faults\"}");
            return;
        }
        portENTER_CRITICAL(&vibrationCalibrationMux);
        vibrationCalibrationIndex = 0;
        vibrationBaselineComplete = false;
        vibrationBaseline = {};
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
        VibrationMotorResult baseline;
        portENTER_CRITICAL(&vibrationCalibrationMux);
        state = vibrationCalibrationState;
        index = vibrationCalibrationIndex;
        reason = (const char *)vibrationCalibrationReason;
        memcpy(results, vibrationCalibrationResults, sizeof(results));
        baseline = vibrationBaseline;
        portEXIT_CRITICAL(&vibrationCalibrationMux);
        const char *stateName[] = {"empty", "boot_wait", "queued", "baseline", "running", "settling", "complete", "aborted"};
        char json[760];
        snprintf(json, sizeof(json),
            "{\"state\":\"%s\",\"step\":%u,\"reason\":\"%s\","
            "\"baseline\":{\"gyro_rms\":%.6f,\"accel_rms\":%.5f,\"samples\":%u},\"motors\":["
            "{\"name\":\"FR\",\"gyro_rms\":%.6f,\"accel_rms\":%.5f,\"samples\":%u,\"response\":\"%s\"},"
            "{\"name\":\"FL\",\"gyro_rms\":%.6f,\"accel_rms\":%.5f,\"samples\":%u,\"response\":\"%s\"},"
            "{\"name\":\"RR\",\"gyro_rms\":%.6f,\"accel_rms\":%.5f,\"samples\":%u,\"response\":\"%s\"},"
            "{\"name\":\"RL\",\"gyro_rms\":%.6f,\"accel_rms\":%.5f,\"samples\":%u,\"response\":\"%s\"}]}",
            state < 8 ? stateName[state] : "unknown", (unsigned)index, reason,
            baseline.gyroRms, baseline.accelRms, baseline.samples,
            results[0].gyroRms, results[0].accelRms, results[0].samples, motorResponseName(results[0], baseline),
            results[1].gyroRms, results[1].accelRms, results[1].samples, motorResponseName(results[1], baseline),
            results[2].gyroRms, results[2].accelRms, results[2].samples, motorResponseName(results[2], baseline),
            results[3].gyroRms, results[3].accelRms, results[3].samples, motorResponseName(results[3], baseline));
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
        client.print("motor,gyro_rms_rad_s,accel_rms_m_s2,samples,response,baseline_gyro_rms_rad_s,baseline_accel_rms_m_s2\n");
        for (uint8_t i = 0; i < 4 && client.connected(); ++i) {
            VibrationMotorResult result;
            portENTER_CRITICAL(&vibrationCalibrationMux);
            result = vibrationCalibrationResults[i];
            portEXIT_CRITICAL(&vibrationCalibrationMux);
            client.printf("%s,%.6f,%.5f,%u,%s,%.6f,%.5f\n", vibrationMotorNames[i],
                result.gyroRms, result.accelRms, result.samples,
                motorResponseName(result, vibrationBaseline), vibrationBaseline.gyroRms,
                vibrationBaseline.accelRms);
        }
        client.stop();
    });

    webRCServer.on("/descent-calibration/start", HTTP_POST, []() {
        if (rejectFlightApiInConfigPortal()) return;
        if (!requireWebRCLease()) return;
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
        if (rejectFlightApiInConfigPortal()) return;
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
        if (rejectFlightApiInConfigPortal()) return;
        if (!requireWebRCLease()) return;
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
        if (rejectFlightApiInConfigPortal()) return;
        if (!requireWebRCLease()) return;
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
        if (rejectFlightApiInConfigPortal()) return;
        if (!requireWebRCLease()) return;
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
        if (rejectFlightApiInConfigPortal()) return;
        if (!requireWebRCLease()) return;
        webConsoleEnabled = true;
        webLog(motd);
        webRCServer.send(200, "application/json", "{\"ok\":1}");
    });

    webRCServer.on("/console/disable", HTTP_POST, []() {
        if (rejectFlightApiInConfigPortal()) return;
        if (!requireWebRCLease()) return;
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
        bool wifiConnected = false;
        uint32_t wifiDisconnects = 0, wifiLastDisconnectMs = 0;
#if WIFI_ENABLED
        wifiConnected = WiFi.isConnected();
        wifiDisconnects = getWiFiDisconnectCount();
        wifiLastDisconnectMs = getWiFiLastDisconnectMs();
#endif
        char json[704];
        const char *armReason = armBlockReason();
        const bool armReady = armed || !armReason;
        snprintf(json, sizeof(json),
            "{\"armed\":%s,\"led_fast_blink\":%s,\"enabled\":%s,\"active\":%s,"
            "\"voltage\":%.2f,\"throttle\":%.1f,\"roll\":%.1f,\"pitch\":%.1f,\"yaw\":%.1f,"
            "\"faults\":%lu,\"uptime_ms\":%lu,\"wifi_connected\":%s,"
            "\"wifi_disconnects\":%lu,\"wifi_last_disconnect_ms\":%lu,"
            "\"stick_age_ms\":%ld,\"packet_age_ms\":%ld,\"http_idle_drops\":%lu,"
            "\"http_max_handle_us\":%lu,\"http_slow_handles\":%lu,"
            "\"http_rc_max_request_us\":%lu,\"http_rc_slow_requests\":%lu,"
            "\"control_source\":%u,\"thrust_target\":%.3f,"
            "\"arm_ready\":%s,\"arm_reason\":\"%s\"}",
            armed ? "true" : "false",
            ledFastBlinkActive() ? "true" : "false",
            enabled ? "true" : "false",
            (useWebRC && enabled) ? "true" : "false",
            vbat, throttle, roll, pitch, yaw,
            (unsigned long)getActiveDiagnosticFaults(), now,
            wifiConnected ? "true" : "false",
            (unsigned long)wifiDisconnects, (unsigned long)wifiLastDisconnectMs,
            stickUpdated ? (long)(now - lastStickUpdate) : -1L,
            updated ? (long)(now - lastUpdate) : -1L,
            (unsigned long)responsiveWebRCServer.idleDropCount(),
            (unsigned long)responsiveWebRCServer.maxHandleTimeUs(),
            (unsigned long)responsiveWebRCServer.slowHandleCount(),
            (unsigned long)webRCMaxRequestUs,
            (unsigned long)webRCSlowRequests,
            (unsigned)getCurrentControlSource(), thrustTarget,
            armReady ? "true" : "false", armed ? "飞控已解锁" :
                (armReason ? armReason : "当前解锁条件已满足"));
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

    webRCServer.on("/diag/retained.csv", HTTP_GET, []() {
        if (armed || motorsActive()) {
            webRCServer.send(409, "text/plain", "motors active; disarm before downloading retained traces\n");
            return;
        }
        WiFiClient client = webRCServer.client();
        client.setNoDelay(true);
        client.setTimeout(100);
        client.print("HTTP/1.1 200 OK\r\nContent-Type: text/csv; charset=utf-8\r\n");
        client.print("Cache-Control: no-store\r\nConnection: close\r\n");
        client.printf("X-Retained-Integrity: %s\r\nX-Retained-Count: %u\r\nX-Retained-Overwritten: %lu\r\n\r\n",
            retainedSlowLoopIntegrity() ? "ok" : "recovered_or_empty",
            (unsigned)retainedSlowLoopCount(), (unsigned long)retainedSlowLoopOverwritten());
        client.print("type,capture_index,capture_sequence,loop_sequence,uptime_ms,dt_us");
        for (uint8_t stage = 0; stage < LOOP_TRACE_STAGE_COUNT; ++stage)
            client.printf(",stage%u_us", (unsigned)stage);
        client.print(",detail\n");
        for (uint8_t index = 0; index < retainedSlowLoopCount() && client.connected(); ++index) {
            SlowLoopCapture capture{};
            if (!copyRetainedSlowLoop(index, capture)) continue;
            const LoopOverrunTrace &trace = capture.trace;
            client.printf("capture,%u,%lu,%lu,%lu,%lu",
                (unsigned)index, (unsigned long)capture.sequence,
                (unsigned long)trace.loopSequence, (unsigned long)trace.uptimeMs,
                (unsigned long)trace.dtUs);
            for (uint8_t stage = 0; stage < LOOP_TRACE_STAGE_COUNT; ++stage)
                client.printf(",%lu", (unsigned long)trace.stageUs[stage]);
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
            const ImuWaitTrace &imu = trace.imuWait;
            client.printf(",imu=%lu;%lu;%u;%lu;%u;%u;%u;%u;%u;%u;%u;%u;%u",
                (unsigned long)imu.waitStartedUs, (unsigned long)imu.waitEndedUs,
                (unsigned)imu.interruptCount, (unsigned long)imu.lastInterruptUs,
                (unsigned)imu.semaphoreTakes, (unsigned)imu.semaphoreTimeouts,
                (unsigned)imu.readAttempts, (unsigned)imu.readyReads,
                (unsigned)imu.readTotalUs, (unsigned)imu.readMaxUs,
                (unsigned)imu.interruptSource, (unsigned)imu.result,
                (unsigned)imu.semaphoreWaitMaxUs);
#else
            client.print(",imu=unavailable");
#endif
            client.print("\n");
            for (uint8_t i = 0; i < capture.schedulerCount && client.connected(); ++i) {
                const TaskSwitchTraceEvent &event = capture.scheduler[i];
                client.printf("scheduler,%u,%lu,%lu",
                    (unsigned)index, (unsigned long)capture.sequence,
                    (unsigned long)event.loopSequence);
                for (uint8_t column = 0; column < LOOP_TRACE_STAGE_COUNT + 3; ++column)
                    client.print(',');
                client.printf("sequence=%lu;timestamp_us=%lu;capture_id=%u;task_handle=0x%08lx;core=%u;event=%u;dropped=%u\n",
                    (unsigned long)event.sequence, (unsigned long)event.timestampUs,
                    (unsigned)event.captureId,
                    (unsigned long)event.taskHandle, (unsigned)event.coreId,
                    (unsigned)event.kind, (unsigned)event.droppedEvents);
            }
            if (capture.hasIpc && client.connected()) {
                const TaskIpcTraceEvent &ipc = capture.ipc;
                client.printf("ipc,%u,%lu,%lu",
                    (unsigned)index, (unsigned long)capture.sequence,
                    (unsigned long)trace.loopSequence);
                for (uint8_t column = 0; column < LOOP_TRACE_STAGE_COUNT + 3; ++column)
                    client.print(',');
                client.printf("sequence=%lu;request_us=%lu;callback_start_us=%lu;callback_us=%lu;caller_task=0x%08lx;caller_pc=0x%08lx;caller_core=%u;target_core=%u\n",
                    (unsigned long)ipc.sequence, (unsigned long)ipc.requestUs,
                    (unsigned long)ipc.callbackStartedUs, (unsigned long)ipc.callbackUs,
                    (unsigned long)ipc.callerTask, (unsigned long)ipc.callerPc,
                    (unsigned)ipc.callerCore, (unsigned)ipc.targetCore);
            }
        }
        client.stop();
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
        if (rejectFlightApiInConfigPortal()) return;
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

#if CF_DRONE_ENABLE_FAST_STOP_SERVER
    fastStopServer.begin();
    if (xTaskCreatePinnedToCore([](void*) {
            for (;;) {
                serviceFastStopClient();
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }, "web_rc_stop", 3072, nullptr, 2, nullptr, 0) != pdPASS) {
        print("WEB_RC_FAST_STOP state=DISABLED reason=task_create_failed\n");
    } else {
        print("WEB_RC_FAST_STOP state=READY port=82 core=0 priority=2\n");
    }
#endif

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
    serviceLevelCalibration();
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
void runBootMotorSelfCheckBeforeWiFi() {}
void abortVibrationCalibrationForDisarm() {}
WebRCFastStopAction consumeWebRCFastStop() { return WEB_RC_FAST_STOP_NONE; }
void processConsoleCommandQueue() {}
bool isLevelCalibrationActive() { return false; }
#endif
