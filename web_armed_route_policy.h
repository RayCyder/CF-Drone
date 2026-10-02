#pragma once

#include <string.h>

// Keep only flight control and brief status requests available while outputs run.
inline bool webArmedRouteAllowed(const char *path, bool get, bool post) {
    if (!path) return false;
    if (get) {
        return strcmp(path, "/") == 0 ||
            strcmp(path, "/web_rc/status") == 0 ||
            strcmp(path, "/route/status") == 0 ||
            strcmp(path, "/descent-calibration/status") == 0 ||
            strcmp(path, "/vibration-calibration/status") == 0;
    }
    if (post) {
        return strcmp(path, "/web_rc") == 0 ||
            strcmp(path, "/web_rc/heartbeat") == 0 ||
            strcmp(path, "/web_rc/lease") == 0 ||
            strcmp(path, "/route/takeover") == 0 ||
            strcmp(path, "/descent-calibration/start") == 0 ||
            strcmp(path, "/descent-calibration/stop") == 0 ||
            strcmp(path, "/console/disable") == 0;
    }
    return false;
}
