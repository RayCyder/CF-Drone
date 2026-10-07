#pragma once

#include <stdint.h>
#include <string.h>

namespace WifiRecoveryPolicy {

static constexpr uint32_t AP_START_TIMEOUT_MS = 3000;
static constexpr uint32_t AP_NO_CLIENT_REFRESH_MS = 60000;
static constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 8000;
static constexpr uint32_t STA_RETRY_RESET_DELAY_MS = 250;

enum StaRetryAction : uint8_t {
    STA_RETRY_WAIT = 0,
    STA_RETRY_RESET,
    STA_RETRY_BEGIN,
};

enum StaRetryPhase : uint8_t {
    STA_RETRY_IDLE = 0,
    STA_RETRY_CONNECTING,
    STA_RETRY_RESET_WAIT,
};

struct StaRetryState {
    uint32_t deadline = 0;
    uint8_t profile = 0;
    StaRetryPhase phase = STA_RETRY_IDLE;
    bool advanceProfileOnBegin = false;
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

inline void staRetryBeginAttempt(StaRetryState &state, uint8_t profile, uint32_t now) {
    state.profile = profile;
    state.phase = STA_RETRY_CONNECTING;
    state.advanceProfileOnBegin = false;
    state.deadline = now + STA_CONNECT_TIMEOUT_MS;
}

inline void staRetryResetComplete(StaRetryState &state, uint32_t now) {
    state.deadline = now + STA_RETRY_RESET_DELAY_MS;
}

inline void staRetryConnected(StaRetryState &state) {
    state.deadline = 0;
    state.profile = 0;
    state.phase = STA_RETRY_IDLE;
    state.advanceProfileOnBegin = false;
}

inline StaRetryAction staRetryStep(StaRetryState &state, bool configured,
    bool connected, uint8_t profileCount, uint32_t now) {
    if (!configured || !profileCount || connected) {
        staRetryConnected(state);
        return STA_RETRY_WAIT;
    }
    if (state.phase == STA_RETRY_IDLE) {
        state.phase = STA_RETRY_RESET_WAIT;
        state.advanceProfileOnBegin = false;
        state.deadline = now + STA_RETRY_RESET_DELAY_MS;
        return STA_RETRY_RESET;
    }
    if (state.phase == STA_RETRY_CONNECTING) {
        if (!deadlineReached(now, state.deadline)) return STA_RETRY_WAIT;
        state.phase = STA_RETRY_RESET_WAIT;
        state.advanceProfileOnBegin = true;
        state.deadline = now + STA_RETRY_RESET_DELAY_MS;
        return STA_RETRY_RESET;
    }
    if (!deadlineReached(now, state.deadline)) return STA_RETRY_WAIT;
    if (state.advanceProfileOnBegin)
        state.profile = (uint8_t)((state.profile + 1) % profileCount);
    state.phase = STA_RETRY_CONNECTING;
    state.advanceProfileOnBegin = false;
    state.deadline = now + STA_CONNECT_TIMEOUT_MS;
    return STA_RETRY_BEGIN;
}

inline bool maintenanceAllowed(bool armed, bool motorsActive) {
    return !armed && !motorsActive;
}

inline bool portalStartAllowed(bool armed, bool motorsActive) {
    return !armed && !motorsActive;
}

inline bool restartBlocksArming(bool scheduled) {
    return scheduled;
}

inline bool restartReady(bool scheduled, bool deadlineReached, bool armed, bool motorsActive) {
    return scheduled && deadlineReached && !armed && !motorsActive;
}

}  // namespace WifiRecoveryPolicy
