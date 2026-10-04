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
    assert(staRetryDue(true, false, false, 10000, 10000));
    assert(!staRetryDue(false, false, true, 10000, 10000));
    assert(staRetryDue(true, false, true, 0x00000010U, 0xfffffff0U));
    assert(staRetryAction(true, false, true, false, 10000, 10000) == STA_RETRY_RESET);
    assert(staRetryAction(true, false, true, true, 10249, 10250) == STA_RETRY_WAIT);
    assert(staRetryAction(true, false, true, true, 10250, 10250) == STA_RETRY_BEGIN);
    assert(staRetryAction(true, true, true, true, 10250, 10250) == STA_RETRY_WAIT);
    assert(staRetryAction(true, false, false, true, 10250, 10250) == STA_RETRY_BEGIN);
    assert(maintenanceAllowed(false, false));
    assert(!maintenanceAllowed(true, false));
    assert(!maintenanceAllowed(false, true));
    assert(flightApiAllowed(false));
    assert(!flightApiAllowed(true));
    assert(portalHttpAllowed("/", true, false));
    assert(portalHttpAllowed("/wifi", true, false));
    assert(portalHttpAllowed("/wifi/profiles", true, false));
    assert(portalHttpAllowed("/wifi/scan", true, false));
    assert(portalHttpAllowed("/wifi/save", false, true));
    assert(portalHttpAllowed("/wifi/remove", false, true));
    assert(!portalHttpAllowed("/telemetry", true, false));
    assert(!portalHttpAllowed("/logs.csv", true, false));
    assert(!portalHttpAllowed("/diag/ipc.csv", true, false));
    assert(!portalHttpAllowed("/web_rc", false, true));
    assert(!portalHttpAllowed("/wifi/save", true, false));
    assert(!portalHttpAllowed("/wifi/scan", false, true));
    assert(!portalHttpAllowed("/wifi/profiles/extra", true, false));
    assert(!portalHttpAllowed(nullptr, true, false));
    assert(portalStartAllowed(false, false));
    assert(!portalStartAllowed(true, false));
    assert(!portalStartAllowed(false, true));
    assert(restartReady(true, true, false, false));
    assert(!restartReady(true, false, false, false));
    assert(!restartReady(true, true, true, false));
    assert(!restartReady(true, true, false, true));
    return 0;
}
