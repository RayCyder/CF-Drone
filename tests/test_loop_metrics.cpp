#include "../loop_metrics.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main() {
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
