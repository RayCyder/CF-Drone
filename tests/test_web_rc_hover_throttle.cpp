#include <cassert>
#include <cmath>

#include "../web_rc_hover_throttle.h"

int main() {
    const auto fullScale = computeWebRCHoverThrottleReturn(0.525f, 1.0f);
    assert(std::fabs(fullScale.percent - 52.5f) < 1e-5f);
    assert(fullScale.reachable);

    const auto reducedScale = computeWebRCHoverThrottleReturn(0.525f, 0.7f);
    assert(std::fabs(reducedScale.percent - 75.0f) < 1e-5f);
    assert(reducedScale.reachable);

    const auto insufficientScale = computeWebRCHoverThrottleReturn(0.525f, 0.5f);
    assert(std::fabs(insufficientScale.percent - 100.0f) < 1e-5f);
    assert(!insufficientScale.reachable);

    const auto invalidScale = computeWebRCHoverThrottleReturn(0.525f, 0.0f);
    assert(std::fabs(invalidScale.percent - 100.0f) < 1e-5f);
    assert(!invalidScale.reachable);
}
