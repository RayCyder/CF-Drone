#include <assert.h>
#include <stdint.h>

#include "../wifi_recovery_policy.h"

// WIFI-R1: every begin owns a complete 8 s association/DHCP window.
// WIFI-R2: one state machine owns RESET -> 250 ms wait -> BEGIN and profile
// rotation; success clears pending state. WIFI-R3: retry does not depend on AP
// portal availability. WIFI-R4 event diagnostics are exercised in firmware;
// this test locks the transition policy that supplies their action context.
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

    StaRetryState retry;
    staRetryBeginAttempt(retry, 0, 1000);
    assert(retry.phase == STA_RETRY_CONNECTING && retry.deadline == 9000);
    assert(staRetryStep(retry, true, false, 2, 6000) == STA_RETRY_WAIT); // 5 s must not reset.
    assert(retry.phase == STA_RETRY_CONNECTING && retry.profile == 0);
    assert(staRetryStep(retry, true, false, 2, 8999) == STA_RETRY_WAIT);
    assert(staRetryStep(retry, true, false, 2, 9000) == STA_RETRY_RESET);
    staRetryResetComplete(retry, 9000);
    assert(retry.phase == STA_RETRY_RESET_WAIT && retry.deadline == 9250);
    assert(staRetryStep(retry, true, false, 2, 9249) == STA_RETRY_WAIT);
    assert(staRetryStep(retry, true, false, 2, 9250) == STA_RETRY_BEGIN);
    staRetryBeginAttempt(retry, retry.profile, 9250);
    assert(retry.phase == STA_RETRY_CONNECTING && retry.profile == 1 && retry.deadline == 17250);
    // The old profile's 8 s deadline cannot clear a pending/new attempt.
    assert(staRetryStep(retry, true, false, 2, 10000) == STA_RETRY_WAIT);
    assert(retry.profile == 1 && retry.deadline == 17250);
    assert(staRetryStep(retry, true, false, 2, 17250) == STA_RETRY_RESET);
    staRetryResetComplete(retry, 17250);
    assert(staRetryStep(retry, true, false, 2, 17500) == STA_RETRY_BEGIN);
    staRetryBeginAttempt(retry, retry.profile, 17500);
    assert(retry.profile == 0 && retry.deadline == 25500);

    // Association and DHCP completing at 5 s clears all retry state.
    staRetryBeginAttempt(retry, 1, 20000);
    assert(staRetryStep(retry, true, true, 2, 25000) == STA_RETRY_WAIT);
    assert(retry.phase == STA_RETRY_IDLE && retry.deadline == 0 && retry.profile == 0);
    // A later disconnect starts with one reset, waits 249 ms, then retries profile 1.
    assert(staRetryStep(retry, true, false, 2, 26000) == STA_RETRY_RESET);
    staRetryResetComplete(retry, 26000);
    assert(staRetryStep(retry, true, false, 2, 26249) == STA_RETRY_WAIT);
    assert(staRetryStep(retry, true, false, 2, 26250) == STA_RETRY_BEGIN);
    staRetryBeginAttempt(retry, retry.profile, 26250);
    assert(retry.profile == 0 && retry.deadline == 34250);

    // A late GOT_IP while RESET_WAIT is pending cancels the scheduled BEGIN.
    staRetryBeginAttempt(retry, 0, 27000);
    assert(staRetryStep(retry, true, false, 2, 35000) == STA_RETRY_RESET);
    staRetryResetComplete(retry, 35000);
    assert(staRetryStep(retry, true, true, 2, 35100) == STA_RETRY_WAIT);
    assert(retry.phase == STA_RETRY_IDLE && retry.deadline == 0);
    assert(staRetryStep(retry, true, true, 2, 35250) == STA_RETRY_WAIT);
    assert(retry.phase == STA_RETRY_IDLE);

    // Retry operation is independent of portal state, armed state and maintenance policy.
    staRetryBeginAttempt(retry, 0, 30000);
    assert(staRetryStep(retry, true, false, 1, 38000) == STA_RETRY_RESET);
    staRetryResetComplete(retry, 38000);
    assert(staRetryStep(retry, true, false, 1, 38250) == STA_RETRY_BEGIN);
    staRetryBeginAttempt(retry, retry.profile, 38250);

    // Blocking driver calls do not consume either the reset wait or begin window.
    staRetryBeginAttempt(retry, 0, 40000);
    assert(staRetryStep(retry, true, false, 2, 48000) == STA_RETRY_RESET);
    staRetryResetComplete(retry, 48100); // disconnect() took 100 ms.
    assert(staRetryStep(retry, true, false, 2, 48349) == STA_RETRY_WAIT);
    assert(staRetryStep(retry, true, false, 2, 48350) == STA_RETRY_BEGIN);
    staRetryBeginAttempt(retry, retry.profile, 48550); // mode()/begin() took 200 ms.
    assert(retry.deadline == 56550);
    assert(staRetryStep(retry, true, false, 2, 56549) == STA_RETRY_WAIT);

    // Deadlines retain modular uint32 behavior across millis() wrap.
    const uint32_t wrapStart = UINT32_MAX - 1000;
    staRetryBeginAttempt(retry, 0, wrapStart);
    assert(staRetryStep(retry, true, false, 2, wrapStart + 7999) == STA_RETRY_WAIT);
    assert(staRetryStep(retry, true, false, 2, wrapStart + 8000) == STA_RETRY_RESET);
    staRetryResetComplete(retry, wrapStart + 8000);
    assert(staRetryStep(retry, true, false, 2, wrapStart + 8249) == STA_RETRY_WAIT);
    assert(staRetryStep(retry, true, false, 2, wrapStart + 8250) == STA_RETRY_BEGIN);
    staRetryBeginAttempt(retry, retry.profile, wrapStart + 8250);

    staRetryBeginAttempt(retry, 0, 100);
    assert(staRetryStep(retry, false, false, 0, 101) == STA_RETRY_WAIT);
    assert(retry.phase == STA_RETRY_IDLE);
    assert(maintenanceAllowed(false, false));
    assert(!maintenanceAllowed(true, false));
    assert(!maintenanceAllowed(false, true));
    assert(portalStartAllowed(false, false));
    assert(!portalStartAllowed(true, false));
    assert(!portalStartAllowed(false, true));
    assert(!restartBlocksArming(false));
    assert(restartBlocksArming(true));
    assert(restartReady(true, true, false, false));
    assert(!restartReady(true, false, false, false));
    assert(!restartReady(true, true, true, false));
    assert(!restartReady(true, true, false, true));
    return 0;
}
