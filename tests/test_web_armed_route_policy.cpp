#include <cassert>
#include "../web_armed_route_policy.h"

int main() {
    const char *getAllowed[] = {
        "/", "/web_rc/status", "/route/status", "/descent-calibration/status",
        "/vibration-calibration/status"
    };
    for (const char *path : getAllowed) {
        assert(webArmedRouteAllowed(path, true, false));
        assert(!webArmedRouteAllowed(path, false, true));
    }
    const char *postAllowed[] = {
        "/web_rc", "/web_rc/heartbeat", "/web_rc/lease",
        "/route/takeover", "/descent-calibration/start",
        "/descent-calibration/stop", "/console/disable"
    };
    for (const char *path : postAllowed) {
        assert(webArmedRouteAllowed(path, false, true));
        assert(!webArmedRouteAllowed(path, true, false));
    }
    assert(!webArmedRouteAllowed("/console", true, false));
    assert(!webArmedRouteAllowed("/console/cmd", false, true));
    assert(!webArmedRouteAllowed("/logs/status", true, false));
    assert(!webArmedRouteAllowed("/logs.csv", true, false));
    assert(!webArmedRouteAllowed("/diag/trace.csv", true, false));
    assert(!webArmedRouteAllowed("/route/upload", false, true));
    assert(!webArmedRouteAllowed("/route/stop", false, true));
    assert(!webArmedRouteAllowed("/route/start", false, true));
    assert(!webArmedRouteAllowed("/level-calibration/apply", false, true));
    assert(!webArmedRouteAllowed(nullptr, true, false));
}
