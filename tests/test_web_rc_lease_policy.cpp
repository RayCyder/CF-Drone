#include <cassert>
#include "../web_rc_lease_policy.h"

int main() {
    WebRCLeasePolicy policy;
    bool changed = false;
    constexpr uint32_t timeoutMs = 10000;

    assert(policy.acquire(100, timeoutMs, "AAAABBBBCCCCDDDD", &changed));
    assert(changed);
    assert(policy.validateAndTouch("AAAABBBBCCCCDDDD", 200, timeoutMs));

    bool secondChanged = false;
    assert(!policy.acquire(300, timeoutMs, "1111222233334444", &secondChanged));
    assert(policy.validateAndTouch("AAAABBBBCCCCDDDD", 400, timeoutMs));
    assert(!policy.validateAndTouch("1111222233334444", 500, timeoutMs));

    assert(!policy.validateAndTouch("AAAABBBBCCCCDDDD", 10400, timeoutMs));
    assert(policy.acquire(10400, timeoutMs, "1111222233334444", &secondChanged));
    assert(secondChanged);
    assert(!policy.validateAndTouch("AAAABBBBCCCCDDDD", 10401, timeoutMs));
    assert(policy.validateAndTouch("1111222233334444", 10401, timeoutMs));

    assert(!policy.acquire(10402, timeoutMs, "", nullptr));
    assert(!policy.acquire(10402, timeoutMs, "short", nullptr));

    assert(!webRCLeaseAllowsEmergencyButtonOverride(1, 1));
    assert(!webRCLeaseAllowsEmergencyButtonOverride(2, 0));
    assert(webRCLeaseAllowsEmergencyButtonOverride(2, 1));
    assert(webRCLeaseAllowsEmergencyButtonOverride(2, 2));
    assert(!webRCLeaseAllowsEmergencyButtonOverride(2, 3));
}
