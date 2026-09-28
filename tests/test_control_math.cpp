#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#define PI 3.14159265358979323846
#define constrain(x,a,b) ((x)<(a)?(a):((x)>(b)?(b):(x)))
double t = NAN;
float dt;
int64_t simulatedUs;
int64_t esp_timer_get_time() { return simulatedUs; }
void computeLoopRate();
#include "../time.ino"
#include "../pid.h"
int main() {
    for (int64_t start : {600000000LL, 3600000000LL, 4294966796LL, 259200000000LL}) {
        simulatedUs = start; step();
        const double before = t;
        for (int i=0; i<100; ++i) {
            simulatedUs += 1000; step();
            assert(fabsf(dt - .001f) < 1e-8f);
        }
        assert(t > before && fabs(t - before - .1) < 1e-8);
    }
    t=0;
    PID p(0,1,0,.2f);
    dt=.001f;
    for (int i=0;i<10000;++i) { t+=dt; p.update(1); }
    assert(p.integral <= .200001f);
    for (int i=0;i<1000;++i) { t+=dt; p.update(-1); }
    assert(p.integral < 0);
    const float held = p.integral;
    for (int i=0;i<1000;++i) p.update(1, .001f, false);
    assert(p.integral == held);
    p.update(-1, .001f, true);
    assert(p.integral <= held);
    p.update(NAN, .001f);
    assert(p.integral==0 && p.derivative==0);
    p.i=0; p.update(1, .001f); assert(p.integral==0);
    PID d(0,0,1);
    assert(d.update(1,.001f)==0); // first sample has no derivative kick
    d.update(2,.2f); assert(d.derivative==0);
    p.reset();
    assert(p.integral==0 && p.derivative==0);
    puts("clock and PID regression: PASS");
}
