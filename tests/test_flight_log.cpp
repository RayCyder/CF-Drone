#include "Arduino.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include "../vector.h"
#include "../quaternion.h"
#include "../control.h"
#include "../flight_log.h"
float dt=.001f; double t=0, controlTime=0;
bool armed=false, activeMotors=false;
float batteryVoltage=4.1f, controlRoll=0, controlPitch=0, controlYaw=0, controlThrottle=0, controlMode=0;
float motors[4]={}, thrustTarget=0, motorMixScale=1;
Vector gyro,acc,rates,ratesTarget; Quaternion attitude,attitudeTarget;
int mode=2;
#include "../pid.h"
PID rollRatePID(1,1,0),pitchRatePID(1,1,0),yawRatePID(1,1,0);
uint64_t testUs=0;
int64_t esp_timer_get_time(){return testUs;}
bool motorsActive(){return activeMotors;}
void recordDescentCalibrationSample(){}
uint32_t getActiveDiagnosticFaults(){return 0;}
ControlSource getCurrentControlSource(){return CONTROL_SOURCE_NONE;}
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#include "../log.ino"

static void codecTests(){
    float values[40]={}; values[1]=.001234f; values[21]=4.123f; values[28]=-1;
    for(int i=2;i<20;++i)values[i]=.123456f;
    values[20]=.6f; values[22]=-.7f; values[27]=4; values[29]=1; values[30]=1023;
    for(int i=31;i<35;++i)values[i]=.6f;
    values[35]=-20; values[36]=20; values[37]=.1234f;values[38]=.25f;values[39]=3;
    const uint64_t now=(UINT64_C(1)<<32)*1000+123456;
    auto r=FlightLogCodec::encode(values,now); float decoded[40];
    FlightLogCodec::decode(r,now/1000,decoded,40);
    assert(sizeof(r)==80 && sizeof(FlightLogStore)<33000);
    assert(decoded[0]==(float)((double)(now/1000)/1000));
    for(int i=2;i<20;++i)assert(fabsf(decoded[i]-values[i])<=.5001f/FlightLogCodec::vectorScale(i-2));
    assert(decoded[28]==-1 && decoded[30]==1023 && decoded[35]==-20 && decoded[36]==20);
    assert(fabsf(decoded[1]-values[1])<=.00000051f);
    assert(fabsf(decoded[21]-values[21])<=.000501f);
    values[2]=NAN;values[3]=INFINITY;values[4]=-INFINITY;values[20]=-1;values[21]=INFINITY;
    values[28]=NAN; values[35]=33;
    r=FlightLogCodec::encode(values,now);FlightLogCodec::decode(r,now/1000,decoded,40);
    assert(isnan(decoded[2]) && decoded[3]==INFINITY && decoded[4]==-INFINITY);
    assert(decoded[20]==-INFINITY && decoded[21]==INFINITY && isnan(decoded[28]));
    assert(decoded[35]==INFINITY && (r.quality&3)==3);
    uint8_t q=0;assert(FlightLogCodec::signedValue(32766,1,q)==32766);
    assert(FlightLogCodec::signedValue(32766.5f,1,q)==32767);
    assert(FlightLogCodec::unsignedValue(65532,1,q)==65532);
    assert(FlightLogCodec::unsignedValue(65532.5f,1,q)==65534);
}
static void storeTests(){
    FlightLogStore store; float row[40]={}; FlightLogRecord r;uint64_t anchor;uint32_t seq;
    assert(!store.copyLatest(r,anchor,seq));
    assert(store.freeze() && store.freeze() && store.status().rowCount==0);
    assert(!store.copy(store.status().generation,0,r,anchor));store.resume();
    assert(store.sampleDue(0)); assert(!store.sampleDue(9999));assert(store.sampleDue(10000));
    assert(store.sampleDue(55000));assert(store.status().missedSamples==3);
    for(uint64_t i=0;i<500;++i)store.push(FlightLogCodec::encode(row,i*10000),i*10000);
    store.trigger(2,4990000);store.trigger(4,5000000);
    assert(!store.freeze() && store.status().triggerUs==4990000);
    for(uint64_t i=500;i<600;++i)store.push(FlightLogCodec::encode(row,i*10000),i*10000);
    auto status=store.status();assert(status.state==FROZEN && status.reasonMask==6 && status.rowCount==400);
    assert(store.copy(status.generation,0,r,anchor) && r.timeMs==2000);
    assert(store.copy(status.generation,399,r,anchor) && r.timeMs==5990);
    assert(!store.copy(status.generation,400,r,anchor));
    store.push(FlightLogCodec::encode(row,7000000),7000000);
    assert(store.copyLatest(r,anchor,seq) && r.timeMs==7000);
    assert(store.copy(status.generation,399,r,anchor) && r.timeMs==5990);
    store.resume();assert(!store.copy(status.generation,0,r,anchor));
    store.trigger(8,8000000);store.tick(9500000);assert(store.status().state==FROZEN && store.status().rowCount==0);
    store.resume();
    const uint64_t wrap=(UINT64_C(1)<<32)*1000;
    store.push(FlightLogCodec::encode(row,wrap-10000),wrap-10000);
    store.push(FlightLogCodec::encode(row,wrap+10000),wrap+10000);store.freeze();
    assert(store.copy(store.status().generation,0,r,anchor));
    float decoded[40];FlightLogCodec::decode(r,anchor,decoded,40);
    assert(decoded[0]==(float)((double)(wrap-10000)/1000000));
    store.resume();
    store.push(FlightLogCodec::encode(row,10000),10000);
    store.push(FlightLogCodec::encode(row,wrap+20000),wrap+20000);
    assert(store.status().rowCount==1); // unknown epoch history is never misdated
}
static void integrationTests(){
    assert(getLogColumnCount()==40 && !strcmp(getLogColumnName(34),"motor_fl"));
    assert(!strcmp(getLogColumnName(35),"rate_i_x"));
    float row[40];uint32_t seq;
    assert(!copyLatestLogRow(row,40,&seq));
    armed=true;assert(!freezeFlightLog() && !resumeFlightLog());armed=false;
    activeMotors=true;assert(!freezeFlightLog() && !resumeFlightLog());activeMotors=false;
    for(int i=0;i<410;++i){testUs=i*10000;t=testUs/1e6;gyro.x=i*.01f;logData();}
    assert(freezeFlightLog());auto status=getFlightLogStatus();assert(status.rowCount==400);
    std::vector<uint8_t> expected(400*140),actual(expected.size());
    for(uint32_t i=0;i<400;++i){assert(copyFrozenLogRow(status.generation,i,row,35));FlightLogCodec::legacyBytes(row,&expected[i*140]);}
    // Canonical first float32 is 0.1f (first ten records were overwritten), little endian.
    assert(expected[0]==0xcd && expected[1]==0xcc && expected[2]==0xcc && expected[3]==0x3d);
    for(uint32_t ofs: {0u,89u,90u,139u,140u,55999u,56000u,56001u,UINT32_MAX}){
        for(size_t len: {size_t(0),size_t(1),size_t(89),size_t(90),size_t(91),size_t(UINT32_MAX)}){
            size_t n=readFrozenLogBytes(status.generation,ofs,actual.data(),len);
            size_t wanted=ofs>=expected.size()?0:std::min(len,expected.size()-ofs);
            assert(n==wanted);if(n)assert(!memcmp(actual.data(),expected.data()+ofs,n));
        }
    }
    assert(!copyFrozenLogRow(status.generation,0,row,34));
    assert(resumeFlightLog());assert(!readFrozenLogBytes(status.generation,0,actual.data(),90));
    testUs+=10000;armed=true;logData();testUs+=10000;armed=false;logData();
    assert(getFlightLogStatus().state==POST_TRIGGER);
    assert(getFlightLogStatus().reasonMask==FLIGHT_LOG_DISARM_REASON);
    assert(!freezeFlightLog() && !resumeFlightLog());
    testUs+=1500000;logData();assert(getFlightLogStatus().state==FROZEN);
    assert(copyLatestLogRow(row,40,&seq));assert(row[0]==(float)(testUs/1000)/1000);
}
int main(){codecTests();storeTests();integrationTests();puts("flight log regressions passed");}
