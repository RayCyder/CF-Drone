#!/usr/bin/env python3
"""Static contracts for the lightweight legacy HTTP redirect path."""

from pathlib import Path


source = (Path(__file__).resolve().parent.parent / "web_rc.ino").read_text(encoding="utf-8")
start = source.index("static void handleRedirect8080()")
end = source.index("// ==================== 主设置函数", start)
handler = source[start:end]

assert "redirectClient8080.setTimeout(20);" in handler
assert "while (!client.available()" not in handler
assert "redirectClientAcceptedMs = millis();" in handler
assert "millis() - redirectClientAcceptedMs" in handler
assert "vTaskDelay" not in handler

print("web HTTP responsiveness contracts passed")
