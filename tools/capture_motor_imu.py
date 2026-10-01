#!/usr/bin/env python3
"""Repeat the disarmed, propeller-off single-motor IMU capture experiment.

No third-party Python packages are required. The firmware must provide the
`imucap` command and the 3-second locked motor test (`mfr`, `mfl`, `mrr`, `mrl`).
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import threading
import math
import os
import re
import select
import statistics
import sys
import termios
import time
from pathlib import Path

CSV_HEADER = (
    "time_us,gyro_x_rad_s,gyro_y_rad_s,gyro_z_rad_s,"
	"acc_x_m_s2,acc_y_m_s2,acc_z_m_s2"
)
CSV_ROW = re.compile(r"^\d+,-?\d+(?:\.\d+)?,-?\d+(?:\.\d+)?,-?\d+(?:\.\d+)?,"
					 r"-?\d+(?:\.\d+)?,-?\d+(?:\.\d+)?,-?\d+(?:\.\d+)?$")
MOTOR_COMMANDS = {"FR": "mfr", "FL": "mfl", "RR": "mrr", "RL": "mrl"}


class SerialConsole:
    def __init__(self, path: str, baud: int = 115200,
                 capture_path: Path | None = None) -> None:
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = 0
        attrs[1] = 0
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[3] = 0
        speed = getattr(termios, f"B{baud}")
        attrs[4] = speed
        attrs[5] = speed
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        self.pending = bytearray()
        self.capture = capture_path.open('wb') if capture_path else None
        self.capture_stop = threading.Event()
        self.capture_ready = threading.Condition()
        self.capture_error: OSError | None = None
        self.capture_thread = None
        if self.capture:
            self.capture_thread = threading.Thread(target=self._record_serial,
                                                    name='serial-capture', daemon=True)
            self.capture_thread.start()

    def _record_serial(self) -> None:
        try:
            while not self.capture_stop.is_set():
                ready, _, _ = select.select([self.fd], [], [], 0.1)
                if not ready:
                    continue
                try:
                    chunk = os.read(self.fd, 8192)
                except BlockingIOError:
                    continue
                if chunk:
                    self.capture.write(chunk)
                    self.capture.flush()
                    with self.capture_ready:
                        self.pending.extend(chunk)
                        self.capture_ready.notify_all()
        except OSError as error:
            with self.capture_ready:
                self.capture_error = error
                self.capture_ready.notify_all()

    def close(self) -> None:
        if self.capture_thread:
            self.capture_stop.set()
            self.capture_thread.join(timeout=1)
            self.capture.close()
        os.close(self.fd)

    def send(self, command: str) -> None:
        os.write(self.fd, (command.strip() + "\n").encode("ascii"))

    def read_line(self, timeout: float) -> str | None:
        deadline = time.monotonic() + timeout
        if self.capture_thread:
            with self.capture_ready:
                while time.monotonic() < deadline:
                    newline = self.pending.find(b"\n")
                    if newline >= 0:
                        line = bytes(self.pending[:newline]).decode("utf-8", "replace").strip("\r\x00 ")
                        del self.pending[:newline + 1]
                        if line:
                            return line
                        continue
                    if self.capture_error:
                        raise self.capture_error
                    self.capture_ready.wait(max(0, deadline - time.monotonic()))
            return None
        while time.monotonic() < deadline:
            newline = self.pending.find(b"\n")
            if newline >= 0:
                line = bytes(self.pending[:newline]).decode("utf-8", "replace").strip("\r\x00 ")
                del self.pending[:newline + 1]
                if line:
                    return line
                continue
            ready, _, _ = select.select([self.fd], [], [], min(0.1, deadline - time.monotonic()))
            if ready:
                try:
                    chunk = os.read(self.fd, 8192)
                except BlockingIOError:
                    continue
                if chunk:
                    self.pending.extend(chunk)
        return None

    def wait_for(self, pattern: str, timeout: float, lines: list[str] | None = None,
			echo: bool = True) -> str:
        compiled = re.compile(pattern)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.read_line(min(0.25, deadline - time.monotonic()))
            if line is None:
                continue
            if echo:
                print(line)
            if lines is not None:
                lines.append(line)
            if compiled.search(line):
                return line
        raise TimeoutError(f"Timed out waiting for serial response matching {pattern!r}")


def preflight(console: SerialConsole) -> None:
    lines: list[str] = []
    console.send("diag brief")
    summary = console.wait_for(r"PREFLIGHT ", 6, lines)
    required = {
        "armed": r"\barmed=0\b",
        "imu_ok": r"\bimu_ok=1\b",
        "motor_ok": r"\bmotor_ok=1\b",
    }
    for label, pattern in required.items():
        if not re.search(pattern, summary):
            raise RuntimeError(f"Preflight failed: {label} is not in the required state: {summary}")
    voltage = re.search(r"\bbattery_mv=(\d+)\b", summary)
    if not voltage or int(voltage.group(1)) < 3500:
        raise RuntimeError(f"Preflight failed: battery voltage is missing/low: {summary}")
    active = re.search(r"\bfaults=0x([0-9a-fA-F]+)", summary)
    # Match firmware hasBlockingDiagnosticFault(): LOOP_OVERRUN and other
    # warning bits are reported, but do not block this disarmed motor test.
    blocking_fault_mask = 0x10F  # IMU init/timeout/invalid, motor init, parameter
    if not active or int(active.group(1), 16) & blocking_fault_mask:
        raise RuntimeError(f"Preflight failed: blocking diagnostic fault present: {summary}")
    if int(active.group(1), 16):
        print(f"Preflight warning (non-blocking while disarmed): {summary}")


def capture_one(console: SerialConsole, motor: str, out_dir: Path, settle: float) -> Path:
    command = MOTOR_COMMANDS[motor]
    console.send("imucap start")
    console.wait_for(r"IMU_CAPTURE state=running", 5)
    time.sleep(settle)

    console.send(command)
    start_line = console.wait_for(r"电机 \d+ 将以 30% 输出运行 3 秒", 5)
    if "拒绝" in start_line:
        raise RuntimeError(f"Firmware rejected {motor} motor test")
    console.wait_for(r"电机测试结束，全部输出已归零", 6)

    console.send("imucap status")
    status = console.wait_for(r"IMU_CAPTURE state=\d+ rows=\d+", 3)
    rows_match = re.search(r"\bstate=(\d+) rows=(\d+)", status)
    if not rows_match or int(rows_match.group(1)) != 2:
        raise RuntimeError(f"Capture is not ready after {motor} test: {status}")

    console.send("imucap dump")
    console.wait_for(r"^" + re.escape(CSV_HEADER) + r"$", 3)
    rows: list[str] = []
    done = console.wait_for(r"IMU_CAPTURE_DONE rows=\d+", 20, rows, echo=False)
    reported = int(re.search(r"rows=(\d+)", done).group(1))
    csv_rows = [line for line in rows if CSV_ROW.fullmatch(line)]
    if reported != 1024 or len(csv_rows) != reported:
        raise RuntimeError(f"Incomplete {motor} capture: firmware reported {reported}, parsed {len(csv_rows)}")

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    path = out_dir / f"motor-{motor.lower()}-{stamp}.csv"
    out_dir.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(CSV_HEADER.split(","))
        writer.writerows(line.split(",") for line in csv_rows)
    print(f"Saved {reported} rows: {path}")
    report_metrics(path, settle)
    return path


def report_metrics(path: Path, pre_roll: float) -> None:
    with path.open(newline="", encoding="utf-8") as stream:
        samples = list(csv.DictReader(stream))
    # Skip the pre-roll and a further 50 ms to exclude motor startup.
    first_motor_sample = min(len(samples), round(pre_roll * 1000) + 50)
    active = samples[first_motor_sample:]
    if len(active) < 20:
        print("Metrics skipped: capture has too few samples after pre-roll/startup.")
        return
    gyro = [[float(row[f"gyro_{axis}_rad_s"]) for axis in "xyz"] for row in active]
    accel = [[float(row[f"acc_{axis}_m_s2"]) for axis in "xyz"] for row in active]
    timestamps = [int(row["time_us"]) for row in samples]
    intervals = [((b - a) & 0xFFFFFFFF) for a, b in zip(timestamps, timestamps[1:])]
    ordered_intervals = sorted(intervals)
    p99 = ordered_intervals[max(0, math.ceil(len(ordered_intervals) * 0.99) - 1)]
    one_g = 9.80665
    in_gravity_band = sum(
        one_g * 0.85 <= math.sqrt(sum(component * component for component in row)) <= one_g * 1.15
        for row in accel
    )
    print(f"Motor-window samples: {first_motor_sample}..{len(samples) - 1} ({len(active)} rows)")
    print("Accel std X/Y/Z (m/s^2): " + "/".join(f"{statistics.pstdev([r[i] for r in accel]):.3f}" for i in range(3)))
    print("Gyro std X/Y/Z (rad/s): " + "/".join(f"{statistics.pstdev([r[i] for r in gyro]):.4f}" for i in range(3)))
    print(f"Raw |acc| in 1g +/-15%: {100.0 * in_gravity_band / len(active):.1f}%")
    print(f"Sample dt median/P99/max: {statistics.median(intervals)} / {p99} / {max(intervals)} us")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/cu.usbserial-10", help="flight-controller serial port")
    parser.add_argument("--out", type=Path, default=Path("data/attitude"), help="directory for CSV captures")
    parser.add_argument("--motors", nargs="+", choices=sorted(MOTOR_COMMANDS), default=["FR", "FL", "RL"])
    parser.add_argument("--pre-roll", type=float, default=0.40,
                        help="seconds to collect stationary data before each motor starts (default: 0.40)")
    parser.add_argument("--confirm-props-removed-and-frame-secured", action="store_true",
                        help="confirm all propellers are removed and the frame is secured")
    args = parser.parse_args()
    if not args.confirm_props_removed_and_frame_secured:
        parser.error("remove all propellers, secure the frame, then pass --confirm-props-removed-and-frame-secured")
    if args.pre_roll < 0 or args.pre_roll > 0.9:
        parser.error("--pre-roll must be between 0 and 0.9 seconds")

    console = SerialConsole(args.port)
    try:
        console.send("")  # synchronize with the line-oriented firmware CLI
        time.sleep(0.2)
        preflight(console)
        for motor in args.motors:
            print(f"\n=== {motor}: locked, 30% for 3 seconds ===")
            preflight(console)
            capture_one(console, motor, args.out, args.pre_roll)
        preflight(console)
        print("All requested captures completed; flight controller remains disarmed.")
        return 0
    except (OSError, RuntimeError, TimeoutError, KeyboardInterrupt) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        print("Motor-test firmware enforces a 3-second auto-stop; verify disarmed state before continuing.",
              file=sys.stderr)
        return 1
    finally:
        console.close()


if __name__ == "__main__":
    raise SystemExit(main())
