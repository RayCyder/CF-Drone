#include <cassert>
#include "../web_rc_fast_stop_policy.h"

int main() {
    const char *token = "AAAABBBBCCCCDDDD";
    assert(parseWebRCFastStopRequest(
        "POST /kill?s=AAAABBBBCCCCDDDD HTTP/1.1\r", token) == WEB_RC_FAST_STOP_KILL);
    assert(parseWebRCFastStopRequest(
        "POST /lock?s=AAAABBBBCCCCDDDD HTTP/1.1\r", token) == WEB_RC_FAST_STOP_LOCK);
    assert(parseWebRCFastStopRequest(
        "POST /land?s=AAAABBBBCCCCDDDD HTTP/1.1\r", token) == WEB_RC_FAST_STOP_LAND);
    assert(WEB_RC_FAST_STOP_KILL > WEB_RC_FAST_STOP_LOCK &&
        WEB_RC_FAST_STOP_LOCK > WEB_RC_FAST_STOP_LAND);
    assert(parseWebRCFastStopRequest("POST /kill HTTP/1.1\r", token) == WEB_RC_FAST_STOP_NONE);
    assert(parseWebRCFastStopRequest(
        "POST /kill?s=AAAABBBBCCCCDDDC HTTP/1.1\r", token) == WEB_RC_FAST_STOP_NONE);
    assert(parseWebRCFastStopRequest(
        "POST /kill?s=AAAABBBBCCCCDDD HTTP/1.1\r", token) == WEB_RC_FAST_STOP_NONE);
    assert(parseWebRCFastStopRequest(
        "GET /kill?s=AAAABBBBCCCCDDDD HTTP/1.1\r", token) == WEB_RC_FAST_STOP_NONE);
    assert(parseWebRCFastStopRequest(
        "POST /kill?s=AAAABBBBCCCCDDDD HTTP/1.1\r", "") == WEB_RC_FAST_STOP_NONE);
}
