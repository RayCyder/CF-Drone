#include "../loop_metrics.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main() {
    LoopStageEma ema;
    assert(ema.roundedUs()==0 && ema.samples==0);
    ema.update(100); assert(ema.roundedUs()==100 && ema.samples==1);
    for(int i=0;i<16;++i) ema.update(200);
    assert(ema.roundedUs()>160 && ema.roundedUs()<190);

    LoopOverrunTraceRing traceRing;
    LoopOverrunTrace trace;
    for(uint32_t i=0;i<LOOP_TRACE_CAPACITY+3;++i) {
        trace.uptimeMs=1000+i; trace.dtUs=1501+i; trace.loopSequence=2000+i; trace.stageUs[2]=i*10;
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
        trace.imuWait.waitStartedUs=3000+i; trace.imuWait.interruptCount=i;
        trace.imuWait.semaphoreTimeouts=i+1;
#endif
        traceRing.push(trace);
    }
    assert(traceRing.count==LOOP_TRACE_CAPACITY && traceRing.overwritten==3);
    assert(traceRing.oldestSequence()==3 && traceRing.nextSequence==LOOP_TRACE_CAPACITY+3);
    assert(!traceRing.copy(2,trace));
    assert(traceRing.copy(3,trace) && trace.sequence==3 && trace.uptimeMs==1003 && trace.dtUs==1504 && trace.loopSequence==2003);
    LoopOverrunTrace worst;
    assert(traceRing.copyWorst(worst) && worst.dtUs==1501+LOOP_TRACE_CAPACITY+2 &&
           worst.sequence==LOOP_TRACE_CAPACITY+2);
    for(uint32_t i=0;i<LOOP_TRACE_CAPACITY+5;++i) {
        trace.dtUs=1501+(i%3);
        trace.loopSequence=3000+i;
        traceRing.push(trace);
    }
    assert(traceRing.copyWorst(worst) && worst.dtUs==1501+LOOP_TRACE_CAPACITY+2 &&
           worst.sequence==LOOP_TRACE_CAPACITY+2);
    trace.dtUs=50000;
    trace.loopSequence=4000;
    traceRing.push(trace);
    assert(traceRing.copyWorst(worst) && worst.dtUs==50000 && worst.loopSequence==4000 &&
           worst.sequence==2*LOOP_TRACE_CAPACITY+8);

    LoopOverrunTraceRing retained;
    for (uint32_t i=0;i<5;++i) {
        trace.dtUs=1000+i;
        trace.loopSequence=5000+i;
        retained.push(trace);
    }
    retained.freeze();
    trace.dtUs=9000;
    trace.loopSequence=6000;
    retained.push(trace);
    assert(retained.frozen && retained.count==5 && retained.nextSequence==5);
    assert(retained.copy(4,trace) && trace.loopSequence==5004);
    assert(retained.copyWorst(worst) && worst.dtUs==1004 && worst.loopSequence==5004);
    retained.clear();
    assert(!retained.frozen && retained.count==0 && !retained.hasWorstRecord);
#if defined(CF_DRONE_ENABLE_TASK_SWITCH_TRACE)
    assert(trace.imuWait.waitStartedUs==3003 && trace.imuWait.interruptCount==3 && trace.imuWait.semaphoreTimeouts==4);
#endif

    LoopTimingMetrics m;
    assert(m.observe(0)==0 && m.observe(NAN)==0 && m.observe(INFINITY)==0);
    assert(m.invalid==3 && m.samples==0);
    for(int i=0;i<99;++i) assert(m.observe(.001f)==1000);
    assert(m.observe(.010001f)==10001);
    assert(m.p99UpperUs()==1000 && m.missedSlots==9);
    assert(m.observe(.010001f)==10001 && m.p99UpperUs()==0);
    m = {};
    for(uint32_t us : {500u,501u,750u,751u,1000u,1001u,1250u,1251u,1500u,1501u,2000u})
        assert(m.observe(us*.000001f)==us);
    assert(m.samples==11 && m.over1000==6 && m.over1500==2 && m.missedSlots==1);
    assert(m.buckets[0]==1 && m.buckets[1]==2 && m.buckets[2]==2 && m.buckets[3]==2 && m.buckets[4]==2 && m.buckets[5]==2);
    assert(m.p99UpperUs()==2000 && m.maximumUs==2000);
    assert(m.observe(5000)==UINT32_MAX && m.maximumUs==UINT32_MAX);
    puts("1 ms loop metrics regression: PASS");
}
