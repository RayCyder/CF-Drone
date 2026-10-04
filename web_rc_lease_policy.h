#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define WEB_RC_LEASE_TOKEN_CHARS 16

struct WebRCLeasePolicy {
    char token[WEB_RC_LEASE_TOKEN_CHARS + 1] = {};
    uint32_t lastSeenMs = 0;
    uint32_t generation = 0;

    bool active(uint32_t nowMs, uint32_t timeoutMs) const {
        return token[0] != '\0' && (uint32_t)(nowMs - lastSeenMs) < timeoutMs;
    }

    bool acquire(uint32_t nowMs, uint32_t timeoutMs, const char *newToken,
                 bool *ownerChanged, bool replaceActive = false) {
        if (!newToken || !*newToken || strlen(newToken) != WEB_RC_LEASE_TOKEN_CHARS) return false;
        if (!replaceActive && active(nowMs, timeoutMs)) return false;
        const bool changed = strcmp(token, newToken) != 0;
        strncpy(token, newToken, WEB_RC_LEASE_TOKEN_CHARS);
        token[WEB_RC_LEASE_TOKEN_CHARS] = '\0';
        lastSeenMs = nowMs;
        generation++;
        if (ownerChanged) *ownerChanged = changed;
        return true;
    }

    bool validateAndTouch(const char *candidate, uint32_t nowMs, uint32_t timeoutMs) {
        if (!candidate || !*candidate || !active(nowMs, timeoutMs)) return false;
        if (strncmp(token, candidate, WEB_RC_LEASE_TOKEN_CHARS + 1) != 0) return false;
        lastSeenMs = nowMs;
        return true;
    }
};

static inline bool webRCLeaseAllowsEmergencyButtonOverride(int type, int buttonIndex) {
    return type == 2 && (buttonIndex == 1 || buttonIndex == 2 || buttonIndex == 3);
}
