#include <assert.h>
#include <stdint.h>

#include "../wifi_recovery_policy.h"

int main() {
    using namespace WifiRecoveryPolicy;

    assert(!apReady(false, true, true));
    assert(!apReady(true, false, true));
    assert(!apReady(true, true, false));
    assert(apReady(true, true, true));

    assert(!apStartTimedOut(true, 3999, 1000));
    assert(apStartTimedOut(true, 4000, 1000));
    assert(!apStartTimedOut(false, 5000, 1000));
    assert(apStartTimedOut(true, 0x00000100U, 0xfffff000U));

    assert(!apRefreshDue(true, false, 60999, 1000));
    assert(apRefreshDue(true, false, 61000, 1000));
    assert(!apRefreshDue(true, true, 61000, 1000));
    assert(!apRefreshDue(false, false, 61000, 1000));

    assert(staRetryDue(true, false, true, 10000, 10000));
    assert(!staRetryDue(true, true, true, 10000, 10000));
    assert(!staRetryDue(true, false, false, 10000, 10000));
    assert(!staRetryDue(false, false, true, 10000, 10000));
    assert(staRetryDue(true, false, true, 0x00000010U, 0xfffffff0U));
    assert(maintenanceAllowed(false, false));
    assert(!maintenanceAllowed(true, false));
    assert(!maintenanceAllowed(false, true));
    assert(flightApiAllowed(false));
    assert(!flightApiAllowed(true));
    assert(restartReady(true, true, false, false));
    assert(!restartReady(true, false, false, false));
    assert(!restartReady(true, true, true, false));
    assert(!restartReady(true, true, false, true));
    return 0;
}
