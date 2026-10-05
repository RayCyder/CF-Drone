#!/usr/bin/env python3
"""Static contracts for the lightweight legacy HTTP redirect path."""

from pathlib import Path


source = (Path(__file__).resolve().parent.parent / "web_rc.ino").read_text(encoding="utf-8")
start = source.index("static void handleRedirect8080()")
end = source.index("// ==================== 主设置函数", start)
handler = source[start:end]

assert "readStringUntil" not in handler
assert "while (!client.available()" not in handler
assert "redirectClientAcceptedMs = millis();" in handler
assert "millis() - redirectClientAcceptedMs" in handler
assert "LEGACY_HTTP_REDIRECT_REQUEST_TIMEOUT_MS" in handler
assert "LEGACY_HTTP_REDIRECT_READ_BUDGET_BYTES" in handler
assert "redirectRequestParser8080->append" in handler
assert "redirectRequestParser8080->complete()" in handler
assert "vTaskDelay" not in handler

portal_policy_start = source.index("static bool rejectFlightApiInConfigPortal()")
portal_policy_end = source.index("// ------旧PCB印刷地址访问", portal_policy_start)
portal_policy = source[portal_policy_start:portal_policy_end]
assert "return false;" in portal_policy
assert "WifiRecoveryPolicy::maintenanceAllowed(armed, motorsActive())" in portal_policy

assert 'webRCServer.on("/", HTTP_GET' in source
assert 'webRCServer.on("/web_rc",' in source
assert 'webRCServer.on("/web_rc/heartbeat",' in source
for field in ["barometer_available", "barometer_usable", "barometer_guard_ready",
              "barometer_reason", "barometer_age_ms", "relative_altitude_m",
              "vertical_speed_mps"]:
    assert f'\\"{field}\\"' in source

print("web HTTP responsiveness contracts passed")
