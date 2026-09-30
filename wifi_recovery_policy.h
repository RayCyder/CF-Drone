#pragma once

#include <stdint.h>

namespace WifiRecoveryPolicy {

static constexpr uint32_t AP_START_TIMEOUT_MS = 3000;
static constexpr uint32_t AP_NO_CLIENT_REFRESH_MS = 60000;
static constexpr uint32_t STA_RETRY_INTERVAL_MS = 10000;

inline bool elapsed(uint32_t now, uint32_t startedAt, uint32_t interval) {
    return (uint32_t)(now - startedAt) >= interval;
}

inline bool deadlineReached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

inline bool apReady(bool startEventSeen, bool ssidMatches, bool ipValid) {
    return startEventSeen && ssidMatches && ipValid;
}

inline bool apStartTimedOut(bool starting, uint32_t now, uint32_t startedAt) {
    return starting && elapsed(now, startedAt, AP_START_TIMEOUT_MS);
}

inline bool apRefreshDue(bool active, bool hasClients, uint32_t now, uint32_t activeSince) {
    return active && !hasClients && elapsed(now, activeSince, AP_NO_CLIENT_REFRESH_MS);
}

inline bool staRetryDue(bool staConfigured, bool connected, bool portalOpen,
    uint32_t now, uint32_t retryAt) {
    return staConfigured && !connected && portalOpen && deadlineReached(now, retryAt);
}

}  // namespace WifiRecoveryPolicy
