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
    assert(!secondChanged);
    assert(policy.validateAndTouch("AAAABBBBCCCCDDDD", 400, timeoutMs));
    assert(!policy.validateAndTouch("1111222233334444", 500, timeoutMs));

    // A second page cannot replace the active controller before expiry.
    assert(!policy.acquire(600, timeoutMs, "EEEEFFFFGGGGHHHH", &secondChanged));
    assert(!secondChanged);
    assert(policy.validateAndTouch("AAAABBBBCCCCDDDD", 601, timeoutMs));
    assert(!policy.validateAndTouch("EEEEFFFFGGGGHHHH", 601, timeoutMs));

    assert(!policy.validateAndTouch("AAAABBBBCCCCDDDD", 10601, timeoutMs));
    assert(policy.acquire(10601, timeoutMs, "1111222233334444", &secondChanged));
    assert(secondChanged);
    assert(!policy.validateAndTouch("AAAABBBBCCCCDDDD", 10602, timeoutMs));
    assert(policy.validateAndTouch("1111222233334444", 10602, timeoutMs));

    assert(!policy.acquire(10603, timeoutMs, "", nullptr));
    assert(!policy.acquire(10603, timeoutMs, "short", nullptr));

    assert(!webRCLeaseAllowsEmergencyButtonOverride(1, 1));
    assert(!webRCLeaseAllowsEmergencyButtonOverride(2, 0));
    assert(webRCLeaseAllowsEmergencyButtonOverride(2, 1));
    assert(webRCLeaseAllowsEmergencyButtonOverride(2, 2));
    assert(webRCLeaseAllowsEmergencyButtonOverride(2, 3));
    assert(!webRCLeaseAllowsEmergencyButtonOverride(2, 4));
}
