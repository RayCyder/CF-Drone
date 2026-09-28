// 网页遥控服务端

#if WEB_RC_ENABLED

#include <WebServer.h>
#include <WiFi.h>
#include <ctype.h>
#include <stdlib.h>
#include "web_rc_html.h"
#include "board_config.h"
#include "diagnostics.h"
#include "system_log.h"
#include "web_rc_input.h"
#include "flight_log.h"

// 飞控统一控制变量（供协议适配层写入，与 SBUS/MAVLink 共用）
extern double t;
extern double controlTime;
extern float controlRoll, controlPitch, controlYaw, controlThrottle, controlMode;
extern float batteryVoltage;
extern const char* motd;

// ==================== 配置常量 ====================
#define WEB_RC_TIMEOUT_MS    10000          // 连接超时：最后一次收包超过此时间(ms)视为断连；需大于心跳间隔
// VBAT_ADC_PIN / VBAT_ADC_SAMPLES / VBAT_DIVIDER 已迁移至 battery.ino

// ==================== 连接状态标志 ====================
bool webRCEnabled    = false;  // Web RC 当前有有效连接（由 readWebRC() 每帧更新）
bool useWebRC        = false;  // 当前正在使用 Web RC 控制（与 webRCEnabled 保持同步）
bool webRCUpdated    = false;  // 收到过至少一次摇杆数据（首次连接前为 false）
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

// Browser-uploaded open-loop sequence. The bounded RAM queue lets the flight
// controller execute it locally without a continuous browser connection.
#define OPEN_LOOP_MAX_STEPS 128
#define OPEN_LOOP_MAX_BODY  4096
static OpenLoopStep *openLoopSteps = nullptr;
static portMUX_TYPE openLoopMux = portMUX_INITIALIZER_UNLOCKED;
static uint16_t openLoopCount = 0;
static uint16_t openLoopIndex = 0;
static unsigned long openLoopStepStarted = 0;
static bool openLoopRunning = false;
static bool openLoopLandingStarted = false;
static uint8_t openLoopState = 0; // 0=empty, 1=ready, 2=running, 3=landing, 4=complete, 5=aborted
static float openLoopTotalSeconds = 0.0f;

extern int mode;
extern const int STAB, AUTO;
extern bool armed;
extern bool motorsActive();
extern void descend();

#define WEB_LOG_CSV_COLUMNS_CAPACITY 40
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

static bool appendCsvFloat(char *line, size_t capacity, int &used, float value, bool first) {
    const int written = snprintf(line + used, capacity - used, "%s%.7g", first ? "" : ",", value);
    if (written <= 0 || written >= (int)(capacity - used)) return false;
    used += written;
    return true;
}

static void skipRouteSeparators(char *&cursor) {
    while (*cursor && (isspace((unsigned char)*cursor) || *cursor == ',')) cursor++;
}

static bool parseOpenLoopLine(char *line, OpenLoopStep &step) {
    char *cursor = line;
    skipRouteSeparators(cursor);
    if (!*cursor || *cursor == '#') return false;

    float values[5];
    for (int i = 0; i < 5; i++) {
        skipRouteSeparators(cursor);
        if (!*cursor) return false;
        char *end = nullptr;
        values[i] = strtof(cursor, &end);
        if (end == cursor || isnan(values[i]) || isinf(values[i])) return false;
        cursor = end;
        if (*cursor && !isspace((unsigned char)*cursor) && *cursor != ',') return false;
    }
    skipRouteSeparators(cursor);
    if (*cursor) return false;
    if (values[0] < 0.1f || values[0] > 600.0f || values[1] < 0.0f || values[1] > 100.0f ||
        fabsf(values[2]) > 100.0f || fabsf(values[3]) > 100.0f || fabsf(values[4]) > 100.0f) return false;
    step = {values[0], values[1], values[2], values[3], values[4]};
    return true;
}

static bool parseOpenLoopSequence(const String &body, OpenLoopStep *staging, uint16_t &count, float &duration) {
    if (body.isEmpty() || body.length() > OPEN_LOOP_MAX_BODY) return false;
    count = 0;
    duration = 0.0f;
    char line[128];
    size_t lineLength = 0;
    const char *text = body.c_str();
    for (size_t i = 0; i <= body.length(); i++) {
        const char c = i == body.length() ? '\n' : text[i];
        if (c == '\n') {
            line[lineLength] = '\0';
            char *trim = line;
            while (isspace((unsigned char)*trim)) trim++;
            if (*trim && *trim != '#') {
                if (count >= OPEN_LOOP_MAX_STEPS || !parseOpenLoopLine(trim, staging[count])) return false;
                duration += staging[count].duration;
                if (duration > 1800.0f) return false;
                count++;
            }
            lineLength = 0;
        } else {
            if (lineLength >= sizeof(line) - 1) return false;
            line[lineLength++] = c;
        }
    }
    return count > 0;
}

static const char *openLoopStateName(uint8_t state) {
    switch (state) {
        case 1: return "ready";
        case 2: return "running";
        case 3: return "landing";
        case 4: return "complete";
        case 5: return "aborted";
        default: return "empty";
    }
}

static void stepOpenLoopSequence() {
    bool applyStep = false;
    bool beginLanding = false;
    OpenLoopStep step;
    const unsigned long now = millis();

    portENTER_CRITICAL(&openLoopMux);
    if (openLoopState == 3) {
        if (!openLoopLandingStarted) {
            openLoopLandingStarted = true;
            beginLanding = true;
        } else if (!armed) {
            openLoopState = 4;
        } else if (mode != AUTO) {
            openLoopState = 5;
        }
    } else if (openLoopRunning) {
        if (!armed || mode != STAB) {
            openLoopRunning = false;
            openLoopState = armed ? 5 : 4;
        } else {
            if (now - openLoopStepStarted >= (unsigned long)(openLoopSteps[openLoopIndex].duration * 1000.0f)) {
                openLoopIndex++;
                if (openLoopIndex >= openLoopCount) {
                    openLoopRunning = false;
                    openLoopState = 3;
                    openLoopLandingStarted = true;
                    beginLanding = true;
                } else {
                    openLoopStepStarted = now;
                }
            }
            if (openLoopRunning) {
                step = openLoopSteps[openLoopIndex];
                applyStep = true;
            }
        }
    }
    portEXIT_CRITICAL(&openLoopMux);

    if (beginLanding) descend();
    if (applyStep) setWebRCInput(step.roll, step.pitch, step.yaw, step.throttle * 2.0f - 100.0f);
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
WebServer webRCServer(80);          // 主服务器：80端口（标准HTTP，无需在URL中写端口）

#if WIFI_ENABLED
extern bool isWiFiConfigPortalActive();
extern bool configWiFi(bool ap, const char *ssid, const char *password);
extern void scheduleWiFiRestart();
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
    portEXIT_CRITICAL(&webRCStateMux);

    // 写入统一控制变量（与 SBUS/MAVLink 同路径）
    controlRoll     = constrain(pRoll  * webRCStickScale / STICK_MAX, -1.0f, 1.0f);
    controlPitch    = constrain(pPitch * webRCStickScale / STICK_MAX, -1.0f, 1.0f);
    controlYaw      = constrain(pYaw   * webRCYawScale   / STICK_MAX, -1.0f, 1.0f);
    controlThrottle = pThrottle / THROTTLE_MAX;
    controlMode     = NAN;
    controlTime     = t;

    static float lastPrintedThrottle = -1.0f;
    if (fabsf(pThrottle - lastPrintedThrottle) > 5.0f) {
        print("WebRC T=%.0f%% R=%.1f P=%.1f Y=%.1f Btn=0x%04X\n",
              pThrottle, pRoll, pPitch, pYaw, getWebRCButtons());
        lastPrintedThrottle = pThrottle;
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
    unsigned long lastUpdate;
    portENTER_CRITICAL(&webRCStateMux);
    updated = webRCUpdated;
    lastUpdate = webRCLastUpdate;
    portEXIT_CRITICAL(&webRCStateMux);
    return updated && (millis() - lastUpdate < WEB_RC_TIMEOUT_MS);
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
        webRCServer.send(200, "text/html", webRCIndexHtml);
    });
#if WIFI_ENABLED
    webRCServer.on("/wifi", HTTP_GET, []() {
        webRCServer.send_P(200, "text/html; charset=utf-8", wifiConfigHtml);
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
            webRCServer.send(500, "application/json", "{\"ok\":0,\"message\":\"保存失败：飞控未能验证配置写入，请重试；本次不会重启。\"}");
            return;
        }
        webRCServer.send(200, "application/json", "{\"ok\":1,\"message\":\"配置已保存，飞控即将重启。请稍后将手机连接到同一路由器。\"}");
        scheduleWiFiRestart();
    });
#endif
    webRCServer.on("/web_rc",           HTTP_POST, handleWebRCRequest);
    webRCServer.on("/web_rc/heartbeat", HTTP_POST, handleWebRCRequest);

    webRCServer.on("/route/upload", HTTP_POST, []() {
        uint16_t count = 0;
        float duration = 0.0f;
        if (!webRCServer.hasArg("plain")) {
            webRCServer.send(400, "application/json", "{\"ok\":0,\"error\":\"missing sequence\"}");
            return;
        }
        const String body = webRCServer.arg("plain");
        OpenLoopStep *staging = (OpenLoopStep *)malloc(sizeof(OpenLoopStep) * OPEN_LOOP_MAX_STEPS);
        if (!staging) {
            webRCServer.send(503, "application/json", "{\"ok\":0,\"error\":\"out of memory\"}");
            return;
        }
        if (!parseOpenLoopSequence(body, staging, count, duration)) {
            free(staging);
            webRCServer.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid sequence: 5 numeric fields per row; max 128 rows and 30 minutes\"}");
            return;
        }
        portENTER_CRITICAL(&openLoopMux);
        if (openLoopState == 2 || openLoopState == 3) {
            portEXIT_CRITICAL(&openLoopMux);
            free(staging);
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"route busy\"}");
            return;
        }
        OpenLoopStep *previousSteps = openLoopSteps;
        openLoopSteps = staging;
        staging = nullptr;
        openLoopCount = count;
        openLoopIndex = 0;
        openLoopTotalSeconds = duration;
        openLoopRunning = false;
        openLoopLandingStarted = false;
        openLoopState = 1;
        portEXIT_CRITICAL(&openLoopMux);
        free(previousSteps);
        webRCServer.send(200, "application/json", "{\"ok\":1,\"state\":\"ready\"}");
    });
    webRCServer.on("/route/start", HTTP_POST, []() {
        portENTER_CRITICAL(&openLoopMux);
        const bool hasSequence = openLoopState == 1 && openLoopCount > 0;
        portEXIT_CRITICAL(&openLoopMux);
        if (!hasSequence) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"no uploaded sequence\"}");
            return;
        }
        if (!isWebRCEnabled() || !armed || mode != STAB) {
            webRCServer.send(409, "application/json", "{\"ok\":0,\"error\":\"requires connected Web RC, armed, STAB mode\"}");
            return;
        }
        portENTER_CRITICAL(&openLoopMux);
        openLoopIndex = 0;
        openLoopStepStarted = millis();
        openLoopRunning = true;
        openLoopState = 2;
        portEXIT_CRITICAL(&openLoopMux);
        webRCServer.send(200, "application/json", "{\"ok\":1,\"state\":\"running\"}");
    });
    webRCServer.on("/route/stop", HTTP_POST, []() {
        portENTER_CRITICAL(&openLoopMux);
        if (openLoopRunning) {
            openLoopRunning = false;
            openLoopLandingStarted = false;
            openLoopState = armed ? 3 : 4;
        }
        portEXIT_CRITICAL(&openLoopMux);
        webRCServer.send(200, "application/json", "{\"ok\":1}");
    });
    webRCServer.on("/route/status", HTTP_GET, []() {
        uint8_t state;
        uint16_t count, index;
        float duration;
        portENTER_CRITICAL(&openLoopMux);
        state = openLoopState;
        count = openLoopCount;
        index = openLoopIndex;
        duration = openLoopTotalSeconds;
        portEXIT_CRITICAL(&openLoopMux);
        char response[192];
        snprintf(response, sizeof(response),
            "{\"state\":\"%s\",\"count\":%u,\"step\":%u,\"duration_s\":%.1f,\"arm\":%d,\"mode\":%d}",
            openLoopStateName(state), (unsigned)count,
            (unsigned)(index < count ? index + 1 : count), duration, (int)armed, mode);
        webRCServer.send(200, "application/json", response);
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
        unsigned long lastUpdate;
        float throttle, roll, pitch, yaw;
        portENTER_CRITICAL(&webRCStateMux);
        updated = webRCUpdated;
        lastUpdate = webRCLastUpdate;
        throttle = webRCThrottle; roll = webRCRoll; pitch = webRCPitch; yaw = webRCYaw;
        portEXIT_CRITICAL(&webRCStateMux);
        const bool enabled = updated && (millis() - lastUpdate < WEB_RC_TIMEOUT_MS);
        float vbat = batteryVoltage;
        if (isnan(vbat) || vbat < 0.0f) vbat = 0.0f;
        char json[448];
        snprintf(json, sizeof(json),
            "{\"enabled\":%s,\"active\":%s,"
            "\"voltage\":%.2f,"
            "\"throttle\":%.1f,\"roll\":%.1f,\"pitch\":%.1f,\"yaw\":%.1f,\"faults\":%lu}",
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
