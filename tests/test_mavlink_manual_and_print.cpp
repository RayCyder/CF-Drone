#include "Arduino.h"
#include <cassert>
#include <cmath>
#include <cstring>

#define WIFI_ENABLED 1
#define WEB_RC_ENABLED 1

#include "../vector.h"
#include "../quaternion.h"
#include "../control.h"
#include "../flight_log.h"
#include "../parameter_storage_key.h"
#include "MAVLink.h"

int testCriticalDepth = 0;
int testCriticalEnterCount = 0;
int testCriticalExitCount = 0;

double t = 2.0;
double controlTime = 0.0;
float controlRoll = 0.0f, controlPitch = 0.0f, controlYaw = 0.0f, controlThrottle = 0.0f, controlMode = NAN;
float motors[4] = {};
bool armed = false;
bool landed = true;
const int RAW = 0, STAB = 2, AUTO = 4;
int mode = STAB;
uint16_t channels[8] = {};
Vector rates, gyro, acc;
Quaternion attitude;
int wifiMode = 1;
bool webActive = false;
ControlSource currentSource = CONTROL_SOURCE_NONE;
mavlink_message_t testLastPackedMessage;
bool acceptMavlinkManual = true;
ControlSource markedManualSource = CONTROL_SOURCE_NONE;

uint32_t millis() { return 1000; }
uint32_t micros() { return 1000000; }
int receiveWiFi(uint8_t*, size_t) { return 0; }
void sendWiFi(uint8_t*, int) {}
void print(const char*, ...) {}
bool motorsActive() { for (float motor : motors) if (motor != 0.0f) return true; return false; }
bool requestArm() { armed = true; return true; }
void disarm(DisarmReason) { armed = false; }
bool setFlightMode(int requestedMode) { mode = requestedMode; return true; }
bool isUsingWebRC() { return webActive; }
ControlSource getCurrentControlSource() { return currentSource; }
void setCurrentControlSource(ControlSource source) { currentSource = source; }
bool canAcceptMavlinkManualControl() { return acceptMavlinkManual; }
void markManualControlInput(ControlSource source) { markedManualSource = source; currentSource = source; }
bool submitAutoAttitudeTarget(const AutoAttitudeCommand&) { return true; }
bool submitAutoActuatorTarget(const AutoActuatorCommand&) { return true; }
const char* testParameterNames[] = {"EST_LVL_BIAS_GAIN", "SF_DESCEND_THRUST"};
float testParameterValues[] = {0.1f, 0.2f};
int parametersCount() { return 2; }
const char* getParameterName(int index) { return testParameterNames[index]; }
int parameterIndex(const char* name) {
    for (int i = 0; i < parametersCount(); ++i)
        if (parameterNameMatches(testParameterNames[i], name)) return i;
    return -1;
}
float getParameter(int index) { return testParameterValues[index]; }
float getParameter(const char* name) { const int i = parameterIndex(name); return i < 0 ? NAN : getParameter(i); }
bool setParameter(const char* name, float value) {
    const int i = parameterIndex(name);
    if (i < 0) return false;
    testParameterValues[i] = value;
    return true;
}
void setMavlinkConsoleCommandOutput(bool) {}
void doCommand(const char*, bool) {}
FlightLogStatus getFlightLogStatus() { return {FROZEN, 0, 0, 0, 0, 0}; }
bool freezeFlightLog() { return true; }
size_t readFrozenLogBytes(uint32_t, uint32_t, uint8_t*, size_t) { return 0; }

void sendMavlink();
void receiveMavlink();
void sendMessage(const void *msg);
void handleMavlink(const void *_msg);
void serviceMavlinkLogTransfer();
void serviceMavlinkParameterList();
void mavlinkPrint(const char* str);
void sendMavlinkPrint();

#include "../mavlink.ino"

static mavlink_message_t manualControl(int16_t x, int16_t y, int16_t z, int16_t r) {
    mavlink_message_t msg;
    msg.msgid = MAVLINK_MSG_ID_MANUAL_CONTROL;
    msg.manual.target = 1;
    msg.manual.x = x;
    msg.manual.y = y;
    msg.manual.z = z;
    msg.manual.r = r;
    return msg;
}

int main() {
    auto msg = manualControl(250, -500, 750, 125);
    acceptMavlinkManual = false;
    currentSource = CONTROL_SOURCE_PHYSICAL_RC;
    controlRoll = 0.11f;
    controlPitch = 0.22f;
    controlYaw = 0.33f;
    controlThrottle = 0.44f;
    controlTime = 1.0;
    handleMavlink(&msg);
    assert(controlRoll == 0.11f);
    assert(controlPitch == 0.22f);
    assert(controlYaw == 0.33f);
    assert(controlThrottle == 0.44f);
    assert(controlTime == 1.0);

    acceptMavlinkManual = true;
    currentSource = CONTROL_SOURCE_PHYSICAL_RC;
    handleMavlink(&msg);
    assert(std::fabs(controlPitch - 0.25f) < 1e-6f);
    assert(std::fabs(controlRoll + 0.5f) < 1e-6f);
    assert(std::fabs(controlThrottle - 0.75f) < 1e-6f);
    assert(std::fabs(controlYaw - 0.125f) < 1e-6f);
    assert(controlMode != controlMode);
    assert(controlTime == t);
    assert(markedManualSource == CONTROL_SOURCE_MAVLINK_MANUAL);

    mavlinkConnected = true;
    const int beforePrintEnter = testCriticalEnterCount;
    mavlinkPrint("abc");
    assert(testCriticalDepth == 0);
    assert(testCriticalEnterCount == beforePrintEnter + 1);
    assert(testCriticalExitCount == testCriticalEnterCount);

    const int beforeSendEnter = testCriticalEnterCount;
    sendMavlinkPrint();
    assert(testCriticalDepth == 0);
    assert(testCriticalEnterCount == beforeSendEnter + 1);
    assert(testCriticalExitCount == testCriticalEnterCount);
    assert(testLastPackedMessage.msgid == MAVLINK_MSG_ID_SERIAL_CONTROL);
    assert(testLastPackedMessage.serialControl.count == 3);
    assert(std::memcmp(testLastPackedMessage.serialControl.data, "abc", 3) == 0);

    mavlinkPrint("clear-me");
    armed = true;
    const int beforeClearEnter = testCriticalEnterCount;
    sendMavlinkPrint();
    assert(testCriticalDepth == 0);
    assert(testCriticalEnterCount == beforeClearEnter + 1);
    assert(testCriticalExitCount == testCriticalEnterCount);
    armed = false;
    sendMavlinkPrint();
    assert(testLastPackedMessage.serialControl.count == 3);

    for (int i = 0; i < parametersCount(); ++i) {
        mavlink_message_t request;
        request.msgid = MAVLINK_MSG_ID_PARAM_REQUEST_LIST;
        handleMavlink(&request);
        for (int n = 0; n <= i; ++n) serviceMavlinkParameterList();
        const mavlink_param_value_t listed = testLastPackedMessage.paramValue;
        assert(listed.param_index == i);
        assert(std::strlen(listed.param_id) <= 15);

        mavlink_message_t change;
        change.msgid = MAVLINK_MSG_ID_PARAM_SET;
        std::memcpy(change.paramSet.param_id, listed.param_id, sizeof(change.paramSet.param_id));
        change.paramSet.param_value = 0.3f + i;
        handleMavlink(&change);
        assert(std::fabs(testParameterValues[i] - change.paramSet.param_value) < 1e-6f);
        assert(testLastPackedMessage.paramValue.param_index == i);
        assert(std::memcmp(testLastPackedMessage.paramValue.param_id,
                           listed.param_id, sizeof(listed.param_id)) == 0);

        mavlink_message_t read;
        read.msgid = MAVLINK_MSG_ID_PARAM_REQUEST_READ;
        std::memcpy(read.paramRequestRead.param_id, listed.param_id, sizeof(read.paramRequestRead.param_id));
        handleMavlink(&read);
        assert(testLastPackedMessage.paramValue.param_index == i);
        assert(std::fabs(testLastPackedMessage.paramValue.param_value - testParameterValues[i]) < 1e-6f);
    }
    return 0;
}
