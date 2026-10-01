// control.h
#ifndef CONTROL_H
#define CONTROL_H

#include <stdint.h>
#include "vector.h"
#include "quaternion.h"

// 飞行模式定义
enum FlightMode {
    MODE_MANUAL = 0,
    MODE_ACRO = 1,
    MODE_STAB = 2,
    MODE_ALTHOLD = 3,  // 气压计定高（油门=升降速率）
    MODE_AUTO = 4
};

enum AutoTargetKind {
    AUTO_TARGET_NONE = 0,
    AUTO_TARGET_ATTITUDE = 1,
    AUTO_TARGET_ACTUATOR = 2
};

enum ControlSource {
    CONTROL_SOURCE_NONE = 0,
    CONTROL_SOURCE_PHYSICAL_RC = 1,
    CONTROL_SOURCE_WEB_RC = 2,
    CONTROL_SOURCE_LOCAL_SEQUENCE = 3,
    CONTROL_SOURCE_EXTERNAL_ATTITUDE = 4,
    CONTROL_SOURCE_EXTERNAL_MOTORS = 5,
    CONTROL_SOURCE_LANDING = 6
};

enum DisarmReason : uint8_t {
    DISARM_REASON_UNKNOWN = 0,
    DISARM_REASON_WEB_LOCK = 1,
    DISARM_REASON_WEB_EMERGENCY = 2,
    DISARM_REASON_RC_GESTURE = 3,
    DISARM_REASON_CLI = 4,
    DISARM_REASON_MAVLINK = 5,
    DISARM_REASON_CRITICAL_FAULT = 6,
    DISARM_REASON_INVERTED = 7,
    DISARM_REASON_BATTERY_IDLE_LOW = 8
};

struct AutoAttitudeCommand {
    Quaternion attitude;
    Vector rates;
    float thrust;
    bool useAttitude;
    bool useRates;
};

struct AutoActuatorCommand {
    float motors[4];
};

// 控制状态结构
struct ControlState {
    int mode;
    bool armed;
    float thrustTarget;
    float rollTarget;
    float pitchTarget;
    float yawTarget;
};

// 函数声明
void control();
void disarm(DisarmReason reason = DISARM_REASON_UNKNOWN);
DisarmReason getLastDisarmReason();
void interpretControls();
void interpretWebRC();
void combineInputs();
void controlAttitude();
void controlRates();
void controlTorque();

// 辅助函数
const char* getModeName();
bool isSupportedFlightMode(int requestedMode);
bool setFlightMode(int requestedMode);
void resetControlTargets();
bool submitAutoAttitudeTarget(const AutoAttitudeCommand& target);
bool submitAutoActuatorTarget(const AutoActuatorCommand& target);
bool autoTargetReady();
bool autoTargetTimedOut();
void resetAutoTargetState();
ControlSource getCurrentControlSource();
void setCurrentControlSource(ControlSource source);
Vector constrainRatesToConfiguredLimits(const Vector& rates);
bool ratesWithinConfiguredLimits(const Vector& rates);

// 外部函数声明
bool isUsingWebRC();
void setWebRCWarn(const char* msg);
void clearWebRCWarn();

#endif // CONTROL_H
