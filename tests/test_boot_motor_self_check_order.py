#!/usr/bin/env python3
"""Regression checks for the boot motor self-check/Wi-Fi ordering contract."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
main = (ROOT / "CF-Drone.ino").read_text(encoding="utf-8")
web = (ROOT / "web_rc.ino").read_text(encoding="utf-8")

setup = main[main.index("void setup()") : main.index("void loop()")]
expected_order = (
    "setupMotors();",
    "setupIMU();",
    "runBootMotorSelfCheckBeforeWiFi();",
    "setupWiFi();",
    "setupWebRC();",
)
positions = [setup.index(call) for call in expected_order]
assert positions == sorted(positions), f"unexpected boot order: {dict(zip(expected_order, positions))}"

runner_start = web.index("void runBootMotorSelfCheckBeforeWiFi()")
runner_end = web.index("static void enterOpenLoopLandingLocked", runner_start)
runner = web[runner_start:runner_end]
for required in (
    "vibrationCalibrationState = VIBRATION_BOOT_WAIT",
    "readIMU();",
    "serviceMotorTest();",
    "serviceVibrationCalibration();",
    "serviceSerialConsoleOutput();",
    "vibrationCalibrationState == VIBRATION_COMPLETE",
    "phase=pre_wifi",
):
    assert required in runner, f"boot self-check runner lost required behavior: {required}"

baseline_save = web.index("const VibrationMotorResult baseline = saveVibrationBaselineCapture()")
baseline_reject = web.index("baseline.samples < VIBRATION_RESPONSE_MIN_BASELINE_SAMPLES", baseline_save)
assert baseline_save < baseline_reject, "captured baseline must be saved before an unstable-baseline abort"
assert "saveCurrentVibrationMotorCapture();" in web, (
    "partial motor capture must remain available to Web after an aborted test"
)

timeout_start = runner.index("const uint8_t timedOutState = vibrationCalibrationState")
timeout_release = runner.index("imuCapture.release();", timeout_start)
for saved_call in ("saveVibrationBaselineCapture();", "saveCurrentVibrationMotorCapture();"):
    assert timeout_start < runner.index(saved_call, timeout_start) < timeout_release, (
        f"boot timeout must preserve partial capture before release: {saved_call}"
    )

web_setup_start = web.index("void setupWebRC()")
web_loop_start = web.index("void readWebRC()", web_setup_start)
web_setup = web[web_setup_start:web_loop_start]
assert "vibrationCalibrationState = VIBRATION_BOOT_WAIT" not in web_setup, (
    "Web setup must not start or overwrite the already completed boot motor self-check"
)

status_start = web.index('webRCServer.on("/vibration-calibration/status"')
status_end = web.index('webRCServer.on("/vibration-calibration.csv"', status_start)
status_route = web[status_start:status_end]
for saved_value in ("vibrationCalibrationResults", "vibrationBaseline", "vibrationCalibrationState"):
    assert saved_value in status_route, f"Web status no longer reads saved {saved_value}"

print("boot motor self-check ordering contract passed")
