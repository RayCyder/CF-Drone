#include <assert.h>
#include "../led_alert_policy.h"

int main() {
    assert(!ledFastBlinkRequested(false, false, false));
    assert(ledFastBlinkRequested(false, false, true));
    assert(!ledFastBlinkRequested(false, true, false));
    assert(!ledFastBlinkRequested(true, false, false));
    assert(ledFastBlinkRequested(true, true, false));
    assert(!ledFastBlinkRequested(true, false, true));
}
