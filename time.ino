// Keep absolute time monotonic across the Arduino 32-bit micros() wrap.
#include <esp_timer.h>

float loopRate;

void step() {
    static int64_t previousUs = 0;
    static bool initialized = false;
    const int64_t nowUs = esp_timer_get_time();
    const int64_t elapsedUs = initialized ? nowUs - previousUs : 0;
    previousUs = nowUs;
    initialized = true;
    dt = elapsedUs > 0 ? (float)elapsedUs * 1e-6f : 0.0f;
    t = (double)nowUs * 1e-6;
    computeLoopRate();
}

void computeLoopRate() {
    static double windowStart = 0;
    static uint32_t count = 0;
    ++count;
    const double elapsed = t - windowStart;
    if (elapsed >= 1.0) {
        loopRate = (float)(count / elapsed);
        windowStart = t;
        count = 0;
    }
}
