#include "Arduino.h"
#include <cassert>
#include <cstdio>
#define WEB_RC_ENABLED 1
#define WIFI_ENABLED 0
#include "../vector.h"
#include "../quaternion.h"
#include "../diagnostics.h"

double t=1,controlTime=0;
float dt=.001f,loopRate=1000;
float controlRoll=0,controlPitch=0,controlYaw=0,controlThrottle=0,controlMode=NAN;
float batteryVoltage=4.1f;
Vector rates,gyro,acc(0,0,9.80665f);
Quaternion attitude;
float motors[4]={};
bool motorTestActive=false,motorTestArmInhibit=false,imuOK=true;
const int MOTOR_REAR_LEFT=0,MOTOR_REAR_RIGHT=1,MOTOR_FRONT_RIGHT=2,MOTOR_FRONT_LEFT=3;
uint32_t nowMs=1000;
uint32_t millis(){return nowMs;}
uint32_t micros(){return nowMs*1000;}
bool webRCEnabled=false,useWebRC=false;
unsigned long webRCLastUpdate=0;
bool isUsingWebRC(){return webRCEnabled && useWebRC;}
uint16_t webEdges=0;
uint16_t takeWebRCButtonPressEdges(uint16_t* buttons){*buttons=webEdges; const uint16_t result=webEdges; webEdges=0;return result;}
void setWebRCWarn(const char*){} void clearWebRCWarn(){}
void webRCLossFailsafe();
bool motorOutputsOK=true;
struct TestESP {unsigned getFreeHeap(){return 100000;}} ESP;
bool batteryAlertActiveForFlight(bool){return false;}
void printInvalidParameterValues(){}
void print(const char*,...) {}
bool batteryBlocksArming(){return false;}
bool isAccelCalibrationActive(){return false;}
void sendMotors() {}
bool motorsActive(){for(float m:motors)if(m!=0)return true;return false;}
void recordSystemLogEvent(const char*,const char*) {}
bool tryArmWithSystemLog();
void failsafe(); void interpretControls(); void controlAttitude();void controlRates();void controlTorque();
void desaturate(float&,float&,float&,float&);
void descend();void clearControlledLanding();bool isControlledLandingActive();
void rcLossFailsafe();void autoFailsafe();void invertedFailsafe();void batteryFailsafe();
#define VBAT_ABSENT_THRESHOLD .5f
#define VBAT_WARN_THRESHOLD 3.5f
#define VBAT_CRITICAL_THRESHOLD 3.f
#define BATTERY_FLYING_THRUST_MIN .15f
#define BATTERY_ACTION_DEBOUNCE_TIME .9f
#include "../control.ino"
#include "../safety.ino"
#include "../diagnostics.ino"
bool tryArmWithSystemLog(){armed=true;return true;}
int main(){
    armed=true; mode=STAB; controlMode=0; controlThrottle=.7625f;
    control(); // establish the existing RC selector before entering LAND
    thrustTarget=.7f;
    descend(); const float first=thrustTarget;
    descend(); assert(thrustTarget==first); // idempotent within one frame
    controlMode=0; controlThrottle=.7625f; // stale RC selector must not overwrite LAND
    for(int i=0;i<3000;++i){ t+=dt; ++nowMs; control(); }
    assert(isControlledLandingActive() && armed);
    assert(fabsf(thrustTarget-descendThrust)<.001f);
    assert(!(getActiveDiagnosticFaults() & DIAG_AUTO_TARGET_TIMEOUT)); // manual descent isn't an external timeout
    disarm(); assert(!isControlledLandingActive() && !armed);
    for(float m:motors) assert(m==0);
    controlMode=NAN; controlThrottle=0;
    assert(!setFlightMode(ALTHOLD));
    assert(!setFlightMode(AUTO)); // no preflight target stream
    assert(setFlightMode(STAB));
    AutoAttitudeCommand cmd{Quaternion(),Vector(),.5f,true,false};
    AutoAttitudeCommand invalid=cmd; invalid.attitude=Quaternion(0,0,0,0);
    assert(!submitAutoAttitudeTarget(invalid));
    invalid=cmd; invalid.thrust=2; assert(!submitAutoAttitudeTarget(invalid));
    invalid=cmd; invalid.useAttitude=false; invalid.useRates=true; invalid.rates=Vector(INFINITY,0,0);
    assert(!submitAutoAttitudeTarget(invalid));
    assert(submitAutoAttitudeTarget(cmd));
    assert(!autoTargetReady() && !armed);
    nowMs+=50; assert(submitAutoAttitudeTarget(cmd));
    assert(!autoTargetReady());
    nowMs+=50; assert(submitAutoAttitudeTarget(cmd));
    assert(autoTargetReady() && !armed);
    assert(setFlightMode(AUTO));
    assert(requestArm());
    controlTime=t-100; // a stale manual RC stream must not interrupt external AUTO
    failsafe(); assert(!isControlledLandingActive());
    webRCEnabled=useWebRC=true; webRCLastUpdate=nowMs-9000;
    failsafe(); assert(!isControlledLandingActive()); // stale Web session cannot interrupt external targets
    webRCEnabled=useWebRC=false;
    AutoAttitudeCommand bad=cmd; bad.thrust=NAN;
    nowMs+=501; assert(!submitAutoAttitudeTarget(bad));
    failsafe(); assert(isControlledLandingActive());
    assert(getActiveDiagnosticFaults() & DIAG_AUTO_TARGET_TIMEOUT);
    assert(!setFlightMode(RAW)); // landing requires an explicit stabilized/rate takeover
    webRCEnabled=useWebRC=true; webEdges=0x0100; interpretWebRC();
    assert(mode==AUTO && isControlledLandingActive()); // unsupported ALTHOLD cannot take over
    webRCEnabled=useWebRC=false;
    const float landingThrust=thrustTarget;
    for(int i=0;i<3;i++){nowMs+=50;submitAutoAttitudeTarget(cmd);}
    assert(isControlledLandingActive() && thrustTarget==landingThrust);
    updateDiagnostics(); assert(getActiveDiagnosticFaults() & DIAG_AUTO_TARGET_TIMEOUT);
    assert(setFlightMode(STAB)); assert(!isControlledLandingActive());
    disarm();
    // Fresh candidate packets must not keep an old applied command alive forever.
    setFlightMode(STAB); controlTime=0;
    for(int i=0;i<3;i++){nowMs+=50;submitAutoAttitudeTarget(cmd);}
    assert(setFlightMode(AUTO) && requestArm());
    AutoActuatorCommand alternatingMotors{{.2f,.3f,.4f,.5f}};
    for(int i=0;i<12 && !isControlledLandingActive();i++){
        nowMs+=50; t+=.05;
        if(i%2==0) submitAutoActuatorTarget(alternatingMotors);
        else submitAutoAttitudeTarget(cmd);
        failsafe();
    }
    assert(isControlledLandingActive());
    disarm();
    // Valid samples spanning millis wrap, including one at exactly zero.
    nowMs=UINT32_MAX-99; submitAutoAttitudeTarget(cmd);
    nowMs+=50; submitAutoAttitudeTarget(cmd);
    nowMs+=50; submitAutoAttitudeTarget(cmd);
    assert(nowMs==0 && autoTargetReady());
    nowMs+=501; assert(autoTargetTimedOut());
    disarm(); setFlightMode(STAB); controlTime=0;
    AutoActuatorCommand motorsCmd{{.2f,.3f,.4f,.5f}};
    for(int i=0;i<3;i++){nowMs+=50;assert(submitAutoActuatorTarget(motorsCmd));}
    assert(!armed); for(float m:motors)assert(m==0);
    assert(setFlightMode(AUTO) && requestArm());
    submitAutoActuatorTarget(motorsCmd);
    assert(fabsf(thrustTarget-.35f)<1e-6f && motors[3]==.5f);
    rollRatePID.integral=.2f;
    nowMs+=501; t+=.501; failsafe();
    assert(isControlledLandingActive() && rollRatePID.integral==0);
    disarm();
    puts("landing/control state regression: PASS");
}
