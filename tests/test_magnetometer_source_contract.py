from pathlib import Path

root = Path(__file__).resolve().parents[1]
external = (root / "drivers/external_sensors.cpp").read_text()
control = (root / "control.ino").read_text()
heading = (root / "navigation_heading.h").read_text()

save = external[external.index("bool saveMagCalibration()") : external.index("bool resetMagCalibration()")]
persistence = save[save.index("beginPersistentWriteBatch") : save.index("finishPersistentWriteBatch")]
assert "storage.putBytes" in persistence
assert "portENTER_CRITICAL" not in persistence
assert "portEXIT_CRITICAL" not in persistence

navigation_api = external[
    external.index("float navigationHeadingRadians") : external.index("bool barometerAvailable")
]
assert "heading.initialized" in navigation_api
assert "estimate.trusted" not in navigation_api

assert "rejectionLatched_" in heading
assert "!rejectionLatched_" in heading
assert "MagHeadingLossGuard" in heading
assert "magneticHeadingLossExceeded" in control
assert "descend();" in control[control.index("magneticHeadingLossExceeded") :]

print("magnetometer source contracts passed")
