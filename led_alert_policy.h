#pragma once

// Keep the Web-reported LED state and the physical LED policy in sync.
inline bool ledFastBlinkRequested(bool armed, bool armedAlert, bool disarmedAlert) {
    return armed ? armedAlert : disarmedAlert;
}
