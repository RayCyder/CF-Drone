#!/usr/bin/env python3
"""Contracts for the unified preflight decision exposed by diag brief."""

from pathlib import Path


root = Path(__file__).resolve().parent.parent
diagnostics = (root / "diagnostics.ino").read_text(encoding="utf-8")
control = (root / "control.ino").read_text(encoding="utf-8")

assert "extern const char* armBlockReason();" in diagnostics
assert "arm_ready=%u" in diagnostics
assert "PREFLIGHT_REASON %s" in diagnostics
assert "Wi-Fi 配置已更新，等待飞控重启" in control

print("preflight contract checks passed")
