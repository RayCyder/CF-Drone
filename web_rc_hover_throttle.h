#pragma once

#include <math.h>

struct WebRCHoverThrottleReturn {
    float percent;
    bool reachable;
};

inline WebRCHoverThrottleReturn computeWebRCHoverThrottleReturn(float requiredControlInput,
                                                                 float throttleScale) {
    if (!isfinite(requiredControlInput) || !isfinite(throttleScale) || throttleScale <= 0.0f) {
        return {100.0f, false};
    }
    const float requestedInput = requiredControlInput / throttleScale;
    const float boundedInput = fminf(1.0f, fmaxf(0.0f, requestedInput));
    return {boundedInput * 100.0f, requestedInput <= 1.0f};
}
