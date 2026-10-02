#pragma once

#include <stdint.h>
#include <string.h>
#include "web_rc_lease_policy.h"

enum WebRCFastStopAction : uint8_t {
    WEB_RC_FAST_STOP_NONE,
    WEB_RC_FAST_STOP_LAND,
    WEB_RC_FAST_STOP_LOCK,
    WEB_RC_FAST_STOP_KILL,
};

static inline WebRCFastStopAction parseWebRCFastStopRequest(
    const char *requestLine, const char *issuedToken) {
    if (!requestLine || !issuedToken ||
        strlen(issuedToken) != WEB_RC_LEASE_TOKEN_CHARS) return WEB_RC_FAST_STOP_NONE;
    const char *candidate = nullptr;
    WebRCFastStopAction action = WEB_RC_FAST_STOP_NONE;
    constexpr char killPrefix[] = "POST /kill?s=";
    constexpr char lockPrefix[] = "POST /lock?s=";
    constexpr char landPrefix[] = "POST /land?s=";
    if (strncmp(requestLine, killPrefix, sizeof(killPrefix) - 1) == 0) {
        candidate = requestLine + sizeof(killPrefix) - 1;
        action = WEB_RC_FAST_STOP_KILL;
    } else if (strncmp(requestLine, lockPrefix, sizeof(lockPrefix) - 1) == 0) {
        candidate = requestLine + sizeof(lockPrefix) - 1;
        action = WEB_RC_FAST_STOP_LOCK;
    } else if (strncmp(requestLine, landPrefix, sizeof(landPrefix) - 1) == 0) {
        candidate = requestLine + sizeof(landPrefix) - 1;
        action = WEB_RC_FAST_STOP_LAND;
    }
    if (!candidate || strlen(candidate) < WEB_RC_LEASE_TOKEN_CHARS + 1 ||
        candidate[WEB_RC_LEASE_TOKEN_CHARS] != ' ' ||
        memcmp(candidate, issuedToken, WEB_RC_LEASE_TOKEN_CHARS) != 0)
        return WEB_RC_FAST_STOP_NONE;
    return action;
}
