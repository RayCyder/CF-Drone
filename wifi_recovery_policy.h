#pragma once

#include <stdint.h>
#include <string.h>

namespace WifiRecoveryPolicy {

static constexpr uint32_t AP_START_TIMEOUT_MS = 3000;
static constexpr uint32_t AP_NO_CLIENT_REFRESH_MS = 60000;
static constexpr uint32_t STA_RETRY_INTERVAL_MS = 3000;
static constexpr uint32_t STA_RETRY_RESET_DELAY_MS = 250;

enum StaRetryAction : uint8_t {
    STA_RETRY_WAIT = 0,
    STA_RETRY_RESET,
    STA_RETRY_BEGIN,
};

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

inline bool staRetryDue(bool staConfigured, bool connected, bool /*portalOpen*/,
    uint32_t now, uint32_t retryAt) {
    // Retry the station link directly even before a configuration portal is
    // allowed. During flight the portal is intentionally disabled, so tying
    // retries to it can leave Web RC offline indefinitely after one bad join.
    return staConfigured && !connected && deadlineReached(now, retryAt);
}

inline StaRetryAction staRetryAction(bool staConfigured, bool connected, bool portalOpen,
    bool resetPending, uint32_t now, uint32_t retryAt) {
    if (!staRetryDue(staConfigured, connected, portalOpen, now, retryAt)) return STA_RETRY_WAIT;
    return resetPending ? STA_RETRY_BEGIN : STA_RETRY_RESET;
}

inline bool maintenanceAllowed(bool armed, bool motorsActive) {
    return !armed && !motorsActive;
}

inline bool flightApiAllowed(bool configPortalActive) {
    return !configPortalActive;
}

inline bool portalHttpAllowed(const char *path, bool getRequest, bool postRequest) {
    if (!path) return false;
    if (getRequest) {
        return strcmp(path, "/") == 0 || strcmp(path, "/wifi") == 0 ||
            strcmp(path, "/wifi/profiles") == 0 || strcmp(path, "/wifi/scan") == 0;
    }
    if (postRequest) {
        return strcmp(path, "/wifi/save") == 0 || strcmp(path, "/wifi/remove") == 0;
    }
    return false;
}

inline bool portalStartAllowed(bool armed, bool motorsActive) {
    return !armed && !motorsActive;
}

inline bool restartReady(bool scheduled, bool deadlineReached, bool armed, bool motorsActive) {
    return scheduled && deadlineReached && !armed && !motorsActive;
}

}  // namespace WifiRecoveryPolicy
