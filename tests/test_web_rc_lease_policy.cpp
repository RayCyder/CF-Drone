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

    // A caller that has separately authenticated continuity may replace the
    // active lease, as used for an armed page reload with the prior stop token.
    assert(policy.acquire(700, timeoutMs, "EEEEFFFFGGGGHHHH", &secondChanged, true));
    assert(secondChanged);
    assert(!policy.validateAndTouch("AAAABBBBCCCCDDDD", 701, timeoutMs));
    assert(policy.validateAndTouch("EEEEFFFFGGGGHHHH", 701, timeoutMs));

    assert(!policy.validateAndTouch("EEEEFFFFGGGGHHHH", 10701, timeoutMs));
    assert(policy.acquire(10701, timeoutMs, "1111222233334444", &secondChanged));
    assert(secondChanged);
    assert(!policy.validateAndTouch("EEEEFFFFGGGGHHHH", 10702, timeoutMs));
    assert(policy.validateAndTouch("1111222233334444", 10702, timeoutMs));

    assert(!policy.acquire(10703, timeoutMs, "", nullptr));
    assert(!policy.acquire(10703, timeoutMs, "short", nullptr));

    assert(!webRCLeaseAllowsEmergencyButtonOverride(1, 1));
    assert(!webRCLeaseAllowsEmergencyButtonOverride(2, 0));
    assert(webRCLeaseAllowsEmergencyButtonOverride(2, 1));
    assert(webRCLeaseAllowsEmergencyButtonOverride(2, 2));
    assert(webRCLeaseAllowsEmergencyButtonOverride(2, 3));
    assert(!webRCLeaseAllowsEmergencyButtonOverride(2, 4));
}
