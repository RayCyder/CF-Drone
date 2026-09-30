#!/usr/bin/env python3
"""Collect repeated, disarmed raw-gyro/temperature snapshots over serial."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import importlib.util
import re
import statistics
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CAPTURE_SCRIPT = ROOT / "tools/capture_motor_imu.py"
_spec = importlib.util.spec_from_file_location("capture_motor_imu", CAPTURE_SCRIPT)
if _spec is None or _spec.loader is None:
    raise RuntimeError(f"Could not load serial helper from {CAPTURE_SCRIPT}")
_capture = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_capture)
SerialConsole = _capture.SerialConsole
preflight = _capture.preflight

RAW_HEADER = (
    "time_us,gyro_x_rad_s,gyro_y_rad_s,gyro_z_rad_s,acc_x_m_s2,acc_y_m_s2,"
    "acc_z_m_s2,temperature_c,gyro_sensor_uncorrected_x_rad_s,"
    "gyro_sensor_uncorrected_y_rad_s,gyro_sensor_uncorrected_z_rad_s"
)


def capture_snapshot(console, output: Path, index: int) -> Path:
    console.send("imucap raw-start")
    started = console.wait_for(r"IMU_CAPTURE state=running .*raw_gyro=1", 5)
    print(f"snapshot {index}: {started}")

    # Capture fills in about 1.03 s at the measured 990 Hz loop rate.
    time.sleep(1.2)
    console.send("imucap status")
    status = console.wait_for(r"IMU_CAPTURE state=2 rows=1024", 5)
    print(status)

    console.send("imucap dump-raw-temp")
    header = console.wait_for(r"^" + re.escape(RAW_HEADER) + r"$", 5)
    lines: list[str] = []
    done = console.wait_for(r"IMU_CAPTURE_DONE rows=1024", 40, lines, echo=False)
    rows = [line for line in lines if line.count(",") == 10 and line.split(",", 1)[0].isdigit()]
    if len(rows) != 1024:
        raise RuntimeError(f"snapshot {index}: expected 1,024 CSV rows, got {len(rows)}; terminator={done}")

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(header.split(","))
        writer.writerows(line.split(",") for line in rows)

    with output.open(newline="", encoding="utf-8") as stream:
        captured = list(csv.DictReader(stream))
    temperatures = [float(row["temperature_c"]) for row in captured]
    raw_means = [statistics.mean(float(row[f"gyro_sensor_uncorrected_{axis}_rad_s"]) for row in captured)
                 for axis in "xyz"]
    raw_sds = [statistics.pstdev(float(row[f"gyro_sensor_uncorrected_{axis}_rad_s"]) for row in captured)
               for axis in "xyz"]
    print(f"saved {output}: rows={len(captured)} temp_C={statistics.mean(temperatures):.2f} "
          f"range={min(temperatures):.2f}..{max(temperatures):.2f} "
          f"raw_gyro_mean=" + ",".join(f"{value:.6f}" for value in raw_means) +
          " raw_gyro_sd=" + ",".join(f"{value:.6f}" for value in raw_sds))
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/cu.usbserial-10")
    parser.add_argument("--samples", type=int, default=8, help="number of one-second snapshots (default: 8)")
    parser.add_argument("--interval-s", type=float, default=60.0,
                        help="start-to-start interval in seconds (default: 60)")
    parser.add_argument("--out", type=Path, default=ROOT / "data/attitude")
    args = parser.parse_args()
    if not 2 <= args.samples <= 100:
        parser.error("--samples must be between 2 and 100")
    if not 10.0 <= args.interval_s <= 3600.0:
        parser.error("--interval-s must be between 10 and 3600 seconds")

    console = SerialConsole(args.port)
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    try:
        console.send("")
        time.sleep(0.2)
        preflight(console)
        start = time.monotonic()
        for index in range(args.samples):
            due = start + index * args.interval_s
            delay = due - time.monotonic()
            if delay > 0:
                print(f"waiting {delay:.1f}s before snapshot {index + 1}/{args.samples}")
                time.sleep(delay)
            path = args.out / f"raw-temperature-{stamp}-{index + 1:02d}.csv"
            capture_snapshot(console, path, index + 1)
        preflight(console)
        print("capture sequence complete; aircraft remains disarmed and motors were not started")
        return 0
    finally:
        console.close()


if __name__ == "__main__":
    raise SystemExit(main())
