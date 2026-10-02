#include "Arduino.h"
#include <cassert>
#include <cstdio>
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define WEB_RC_ENABLED 1
#define WIFI_ENABLED 0
#include "../vector.h"
#include "../quaternion.h"
#include "../diagnostics.h"
#include "../task_switch_trace_runtime.h"
#include "../level_calibration_state.h"

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
bool localSequenceActive=false;
bool isLocalSequenceRunning(){return localSequenceActive;}
bool localSequenceAutoReady=false;
bool isLocalSequenceReadyForAuto(){return localSequenceAutoReady && webRCEnabled && useWebRC;}
unsigned localSequenceCancelCount=0;
void cancelLocalSequenceForManualMode(){++localSequenceCancelCount;localSequenceActive=false;}
unsigned long webRCLastUpdate=0;
unsigned long webRCLastStickUpdate=0;
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
LevelCalibrationState levelCalibrationState=LEVEL_EMPTY;
bool parameterWritePending=false;
bool rotationRestartPending=false;
bool isLevelCalibrationActive(){return levelCalibrationBlocksArming(levelCalibrationState);}
bool parameterPersistencePending(){return parameterWritePending;}
bool imuRotationRestartPending(){return rotationRestartPending;}
void sendMotors() {}
bool motorsActive(){for(float m:motors)if(m!=0)return true;return false;}
unsigned motorTestCancelCount=0,vibrationAbortCount=0;
void cancelMotorTest() {++motorTestCancelCount;motorTestActive=false;}
void abortVibrationCalibrationForDisarm() {++vibrationAbortCount;}
unsigned systemEventCount=0;
void recordSystemLogEvent(const char*,const char*) {++systemEventCount;}
unsigned flightLogTriggerCount=0;
void triggerFlightLog(uint32_t) {++flightLogTriggerCount;}
bool tryArmWithSystemLog();
bool systemLogArmingBlocked(){return false;}
uint8_t taskSwitchTraceCoreCount(){return 0;}
void taskSwitchTraceRange(uint8_t, uint32_t &oldest, uint32_t &next, uint32_t &overwritten){
    oldest=next=overwritten=0;
}
bool copyTaskSwitchTrace(uint8_t, uint32_t, TaskSwitchTraceEvent &){return false;}
uint8_t copyTaskIpcTrace(TaskIpcTraceEvent *, uint8_t, uint32_t &overwritten){
    overwritten=0;return 0;
}
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
#include "../slow_loop_retention.cpp"
bool tryArmWithSystemLog(){armed=true;return true;}
int main(){
    armed=false; mode=STAB; controlThrottle=0;
    motorTestActive=true; motors[MOTOR_FRONT_RIGHT]=.05f;
    const unsigned cancelsBefore=motorTestCancelCount, abortsBefore=vibrationAbortCount;
    disarm(DISARM_REASON_CLI);
    assert(motorTestCancelCount==cancelsBefore+1);
    assert(vibrationAbortCount==abortsBefore+1);
    assert(!motorTestActive); for(float motor:motors) assert(motor==0);
    levelCalibrationState=LEVEL_COLLECTING;
    assert(!requestArm());
    assert(armBlockReason() != nullptr);
    levelCalibrationState=LEVEL_APPLIED;
    assert(!requestArm()); // Writing the mounting angles requires a reboot before arming.
    levelCalibrationState=LEVEL_EMPTY;
    rotationRestartPending=true;
    assert(!requestArm()); // Direct parameter writes also require a reboot after persistence.
    rotationRestartPending=false;
    parameterWritePending=true;
    assert(!requestArm());
    parameterWritePending=false;
    armed=true; mode=STAB; controlMode=0; controlThrottle=.7625f;
    control(); // establish the existing RC selector before entering LAND
    const Quaternion savedAttitudeTarget = attitudeTarget;
    attitude = Quaternion();
    attitudeTarget = Quaternion::fromEuler(Vector(0, 0, 0.6f));
    ratesExtra = Vector(0, 0, 0);
    thrustTarget = .7f;
    controlAttitude();
    assert(fabsf(ratesTarget.z) < 1e-6f); // gyro-only yaw cannot hold an absolute heading
    ratesExtra.z = .25f;
    controlAttitude();
    assert(fabsf(ratesTarget.z - .25f) < 1e-6f); // pilot yaw-rate input remains active
    mode = AUTO;
    ratesExtra.z = 0;
    controlAttitude();
    assert(fabsf(ratesTarget.z - .6f * YAW_P) < 1e-5f); // external AUTO heading targets retain their yaw loop
    mode = STAB;
    attitudeTarget = savedAttitudeTarget;
    ratesExtra = Vector(0, 0, 0);
    thrustTarget=.7f;
    float satA=1.15f, satB=.75f, satC=.65f, satD=1.05f;
    desaturate(satA,satB,satC,satD);
    assert(fabsf(motorMixScale-1.0f)<1e-6f); // high collective is reduced to preserve full torque
    assert(fabsf(max(max(satA,satB),max(satC,satD))-1.0f)<1e-6f);
    assert((satA+satB+satC+satD)*.25f < .9f);
    satA=.4f; satB=.1f; satC=-.2f; satD=.1f;
    desaturate(satA,satB,satC,satD);
    assert(motorMixScale<1.0f); // low collective is preserved; torque is scaled to avoid raising thrust
    assert(fabsf((satA+satB+satC+satD)*.25f-.1f)<1e-6f);
    assert(min(min(satA,satB),min(satC,satD))>=0.0f);
    // A manual throttle release immediately followed by LAND must hand off
    // from the last powered command rather than ramping up from zero.
    thrustTarget=.7f;
    failsafe(); // cache the powered command from the preceding control frame
    thrustTarget=0.0f;
    nowMs+=100;
    descend();
    assert(thrustTarget>.49f && thrustTarget<=ALTHOLD_HOVER_THRUST);
    const float first=thrustTarget;
    descend(); assert(thrustTarget==first); // idempotent within one frame
    controlMode=0; controlThrottle=.7625f; // stale RC selector must not overwrite LAND
    for(int i=0;i<3000;++i){ t+=dt; ++nowMs; control(); }
    assert(isControlledLandingActive() && armed);
    assert(fabsf(thrustTarget-descendThrust)<.001f);
    assert(!(getActiveDiagnosticFaults() & DIAG_AUTO_TARGET_TIMEOUT)); // manual descent isn't an external timeout
    disarm(); assert(!isControlledLandingActive() && !armed);
    for(float m:motors) assert(m==0);
    // An expired handoff must not resurrect old thrust after a long idle gap.
    armed=true; mode=STAB; thrustTarget=.7f;
    failsafe();
    thrustTarget=0.0f;
    nowMs+=LANDING_THRUST_HANDOFF_MAX_AGE_MS+1;
    descend();
    assert(thrustTarget<.01f);
    disarm();
    // A stale browser session present at arm time must not turn a serial/RC
    // zero-throttle arm into an immediate Web-RC-loss landing.
    armed=true; mode=STAB; thrustTarget=0.0f;
    webRCEnabled=useWebRC=true;
    webRCLastUpdate=nowMs-100;
    webRCLastStickUpdate=nowMs-9000;
    webRCLossFailsafe();
    assert(!isControlledLandingActive());
    assert(!(getActiveDiagnosticFaults() & DIAG_WEB_RC_LOSS));
    assert(!webRCEnabled && !useWebRC);
    disarm();
    // A stale pre-arm RC timestamp at zero throttle must not enter the landing
    // ramp after arming; that would raise thrust without a live pilot input.
    setCurrentControlSource(CONTROL_SOURCE_PHYSICAL_RC);
    armed=true; mode=STAB; thrustTarget=0.0f; controlThrottle=0.0f;
    controlTime=t-100.0;
    failsafe();
    assert(!isControlledLandingActive());
    assert(getCurrentControlSource()==CONTROL_SOURCE_PHYSICAL_RC);
    assert(thrustTarget==0.0f);
    assert(controlTime==0.0);
    disarm();
    setCurrentControlSource(CONTROL_SOURCE_PHYSICAL_RC);
    armed=true; mode=STAB; thrustTarget=0.03f; controlThrottle=0.03f;
    controlTime=t-100.0;
    failsafe();
    assert(!armed);
    assert(!isControlledLandingActive());
    assert(thrustTarget==0.0f);
    for(float m:motors) assert(m==0);
    setCurrentControlSource(CONTROL_SOURCE_PHYSICAL_RC);
    armed=true; mode=STAB; thrustTarget=0.10f; controlThrottle=0.10f;
    controlTime=t;
    failsafe();
    assert(!isControlledLandingActive());
    controlTime=t-100.0;
    failsafe();
    assert(isControlledLandingActive());
    assert(getCurrentControlSource()==CONTROL_SOURCE_LANDING);
    disarm();
    // Heartbeats cannot keep a stale Web stick command alive while armed.
    armed=true; mode=STAB; thrustTarget=.7f;
    webRCEnabled=useWebRC=true;
    webRCLastUpdate=webRCLastStickUpdate=nowMs;
    webRCLossFailsafe();
    webRCLastUpdate=nowMs-100;
    webRCLastStickUpdate=nowMs-9000;
    webRCLossFailsafe();
    assert(isControlledLandingActive());
    assert(getActiveDiagnosticFaults() & DIAG_WEB_RC_LOSS);
    webRCLastUpdate=webRCLastStickUpdate=nowMs;
    webRCLossFailsafe(); // reset the timeout latch for later cases
    webRCEnabled=useWebRC=false;
    disarm();
    setDiagnosticFault(DIAG_WEB_RC_LOSS, false);
    armed=true; mode=STAB; thrustTarget=0.2f;
    webRCEnabled=useWebRC=true;
    webRCLastUpdate=webRCLastStickUpdate=nowMs;
    webRCLossFailsafe();
    webRCEnabled=useWebRC=false;
    armed=false;
    webRCLossFailsafe(); // disarmed disabled WebRC must clear the fresh-stick latch
    armed=true; mode=STAB; thrustTarget=0.0f;
    webRCEnabled=useWebRC=true;
    webRCLastUpdate=nowMs-100;
    webRCLastStickUpdate=nowMs-9000;
    webRCLossFailsafe();
    assert(!isControlledLandingActive());
    assert(!(getActiveDiagnosticFaults() & DIAG_WEB_RC_LOSS));
    assert(!webRCEnabled && !useWebRC);
    disarm();
    controlMode=NAN; controlThrottle=0;
    assert(!setFlightMode(ALTHOLD));
    assert(!setFlightMode(AUTO)); // no preflight target stream
    assert(setFlightMode(STAB));
    // A connected, uploaded local plan admits AUTO without an external target.
    localSequenceAutoReady=true; webRCEnabled=useWebRC=true;
    webRCLastUpdate=webRCLastStickUpdate=nowMs;
    assert(setFlightMode(AUTO) && requestArm());
    webRCLossFailsafe(); // reset the prior simulated link-loss latch
    localSequenceActive=true;
    controlRoll=controlPitch=controlYaw=0; controlThrottle=.3f;
    interpretControls(); controlAttitude();
    assert(thrustTarget>motThrMin && fabsf(ratesTarget.z)<1e-6f);
    attitudeTarget=Quaternion::fromEuler(Vector(0,0,.6f)); ratesExtra.z=.2f;
    controlAttitude(); assert(fabsf(ratesTarget.z-.2f)<1e-6f);
    autoFailsafe(); assert(!isControlledLandingActive());
    updateDiagnostics();
    assert(!(getActiveDiagnosticFaults() & DIAG_AUTO_TARGET_TIMEOUT));
    nowMs+=9001;
    webRCLastUpdate=webRCLastStickUpdate=nowMs-9000;
    webRCLossFailsafe(); assert(isControlledLandingActive());
    localSequenceActive=false; localSequenceAutoReady=false;
    disarm(); assert(setFlightMode(STAB));
    webRCEnabled=useWebRC=false;
    setDiagnosticFault(DIAG_WEB_RC_LOSS, false);
    localSequenceActive=true; armed=false; mode=STAB; controlThrottle=0; controlYaw=1;
    interpretControls(); assert(!armed); // generated sequence values are not RC arm gestures
    armed=true; controlYaw=0; controlThrottle=.2f;
    setCurrentControlSource(CONTROL_SOURCE_LOCAL_SEQUENCE);
    control();
    assert(getCurrentControlSource()==CONTROL_SOURCE_LOCAL_SEQUENCE);
    assert(thrustTarget>motThrMin);
    for(float motor:motors) assert(motor>0); // sequence values reach the motor mixer
    disarm();
    assert(setFlightMode(STAB) && !localSequenceActive && localSequenceCancelCount==1);
    controlRoll=controlPitch=controlYaw=0; controlThrottle=.3f; controlMode=NAN;
    webRCEnabled=useWebRC=false; localSequenceActive=false; setFlightMode(STAB);
    nowMs=20000;
    assert(canAcceptMavlinkManualControl());
    markManualControlInput(CONTROL_SOURCE_MAVLINK_MANUAL);
    interpretControls();
    assert(getCurrentControlSource()==CONTROL_SOURCE_MAVLINK_MANUAL);
    markManualControlInput(CONTROL_SOURCE_PHYSICAL_RC);
    assert(!canAcceptMavlinkManualControl()); // fresh physical RC owns manual input between frames
    nowMs+=251;
    assert(canAcceptMavlinkManualControl());
    webRCEnabled=useWebRC=true;
    assert(!canAcceptMavlinkManualControl());
    webRCEnabled=useWebRC=false;
    localSequenceActive=true;
    assert(!canAcceptMavlinkManualControl());
    localSequenceActive=false;
    controlThrottle=0;
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
    assert(!canAcceptMavlinkManualControl());
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
    assert(!canAcceptMavlinkManualControl());
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
    clearDiagnosticHistory();
    recordLoopTiming(0); recordLoopTiming(NAN); recordLoopTiming(.001f); recordLoopTiming(.0015f);
    assert(!(getActiveDiagnosticFaults() & DIAG_LOOP_OVERRUN));
    const unsigned triggerCountBeforeJitter=flightLogTriggerCount;
    armed=true;
    dt=.001501f; recordLoopTiming(dt); assert(!(getActiveDiagnosticFaults() & DIAG_LOOP_OVERRUN));
    assert(flightLogTriggerCount==triggerCountBeforeJitter); // near-budget jitter is reported but keeps the armed ring rolling
    dt=.005001f; recordLoopTiming(dt);
    assert(getActiveDiagnosticFaults() & DIAG_LOOP_OVERRUN);
    assert(flightLogTriggerCount==triggerCountBeforeJitter+1); // a sustained-scale stall preserves the armed snapshot
    armed=false;
    assert(loopTiming.samples==2 && loopTiming.invalid==0 && loopTiming.maximumUs==5001);
    const unsigned eventCount=systemEventCount;
    for(int i=0;i<1000;i++) recordLoopStage(LOOP_STAGE_CONTROL_LAW,201);
    assert(systemEventCount==eventCount); // hot-path counters do not format/emit events
    assert(loopStages[LOOP_STAGE_CONTROL_LAW].overBudget==0);
    nowMs=0; recordLoopTiming(.005001f); nowMs=10001; updateDiagnostics();
    assert(!(getActiveDiagnosticFaults() & DIAG_LOOP_OVERRUN));

    clearDiagnosticHistory();
    armed=true;
#if CF_DRONE_ENABLE_LOOP_STAGE_MONITOR
    beginLoopTraceCycle();
    recordLoopStage(LOOP_STAGE_RC_WEB,120);
    recordLoopStage(LOOP_STAGE_ESTIMATE,240);
    finishLoopTraceCycle();
    beginLoopTraceCycle();
    recordLoopStage(LOOP_STAGE_LOOP_GAP,100);
    recordLoopStage(LOOP_STAGE_IMU_WAIT,800);
    recordLoopStage(LOOP_STAGE_IMU_PROCESS,100);
#endif
    recordLoopTiming(.002f);
    assert(getLoopTraceCount()==1);
    LoopOverrunTrace trace;
    assert(copyLoopTrace(getLoopTraceOldestSequence(),trace));
    assert(trace.dtUs==2000);
#if CF_DRONE_ENABLE_LOOP_STAGE_MONITOR
    assert(trace.stageUs[LOOP_TRACE_RC_WEB]==120);
    assert(trace.stageUs[LOOP_TRACE_ESTIMATE]==240);
    assert(trace.stageUs[LOOP_TRACE_LOOP_GAP]==100);
    assert(trace.stageUs[LOOP_TRACE_IMU_WAIT]==800);
    assert(trace.stageUs[LOOP_TRACE_IMU_PROCESS]==100);
    assert(trace.stageUs[LOOP_TRACE_UNACCOUNTED]==640);
    // Preserve the newest sub-5 ms peak instead of spending both regular
    // retention slots on the first two minor overruns of an armed session.
    slowLoopRetentionArmedSession=false;
    LoopOverrunTrace firstJitter{}; firstJitter.dtUs=1581;
    firstJitter.stageUs[LOOP_TRACE_MAVLINK]=744;
    LoopOverrunTrace secondJitter{}; secondJitter.dtUs=1574;
    secondJitter.stageUs[LOOP_TRACE_RC_WEB]=779;
    LoopOverrunTrace laterPeak{}; laterPeak.dtUs=3471;
    laterPeak.stageUs[LOOP_TRACE_IMU_PROCESS]=2540;
    assert(slowLoopCaptureAction(firstJitter)==1);
    assert(slowLoopCaptureAction(secondJitter)==1);
    assert(slowLoopCaptureAction(laterPeak)==2);
    LoopOverrunTrace stall{}; stall.dtUs=5431;
    assert(slowLoopCaptureAction(stall)==1); // two stall slots remain reserved
#else
    assert(trace.stageUs[LOOP_TRACE_UNACCOUNTED]==2000);
#endif
    puts("landing/control state regression: PASS");
}
