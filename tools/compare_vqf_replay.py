#!/usr/bin/env python3
"""Compare current and VQF 6D attitude output on identical IMU captures."""

from __future__ import annotations

import argparse
import csv
import math
import shutil
import statistics
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CAPTURES = (
    "data/attitude/motor-fr-20261001-001156.csv",
    "data/attitude/motor-fr-20261001-001909.csv",
    "data/attitude/motor-fr-20261001-001918.csv",
    "data/attitude/motor-fl-20261001-001301.csv",
    "data/attitude/motor-rl-20261001-001310.csv",
    "data/attitude/motor-rr-20261001-081759.csv",
)


def compile_driver(compiler: str, output: Path, vqf: bool, tau_acc: float = 3.0,
                  estimator_source: Path | None = None) -> None:
    command = [
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-vla",
        "-I", str(ROOT / "tests/stubs"), "-I", str(ROOT),
        f'-DESTIMATOR_SOURCE="{estimator_source or ROOT / "estimate.ino"}"',
    ]
    if vqf:
        command.extend(("-DATTITUDE_ESTIMATOR_VQF=1", "-DVQF_SINGLE_PRECISION",
                        f"-DVQF_TAU_ACC={tau_acc:.9g}"))
    command.append(str(ROOT / "tests/estimator_replay_driver.cpp"))
    if vqf:
        command.append(str(ROOT / "basicvqf.cpp"))
    command.extend(("-o", str(output)))
    subprocess.run(command, check=True)


def read_capture_timing(path: Path) -> tuple[list[int], int]:
    timestamps: list[int] = []
    with path.open(encoding="utf-8") as stream:
        next(stream, None)
        for line in stream:
            try:
                timestamps.append(int(line.split(",", 1)[0]))
            except ValueError:
                continue
    if len(timestamps) < 500:
        raise RuntimeError(f"too few capture samples in {path}: {len(timestamps)}")
    intervals = [((b - a) & 0xFFFFFFFF) for a, b in zip(timestamps, timestamps[1:])]
    median_dt_us = int(statistics.median(intervals))
    return timestamps, median_dt_us


def run_driver(binary: Path, capture: Path, acc_weight: float,
               initial_attitude: tuple[float, float, float] = (0.0, 0.0, 0.0)) -> dict[int, tuple[float, float, float]]:
    result = subprocess.run(
        [str(binary), str(capture), *(str(value) for value in initial_attitude),
         str(acc_weight), "0", "0", "0"],
        check=True, capture_output=True, text=True,
    )
    samples: dict[int, tuple[float, float, float]] = {}
    for line in result.stdout.splitlines():
        index, roll, pitch, yaw = line.split(",")
        samples[int(index)] = (float(roll), float(pitch), float(yaw))
    return samples


def read_truth(path: Path) -> dict[int, tuple[float, float, float]]:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = csv.DictReader(stream)
        truth = {int(row["sample"]): (float(row["roll_deg"]), float(row["pitch_deg"]),
                                       float(row["yaw_deg"])) for row in rows}
    if not truth or any(not math.isfinite(value) for sample in truth.values() for value in sample):
        raise ValueError(f"truth CSV contains no finite samples: {path}")
    return truth


def truth_rmse(samples: dict[int, tuple[float, float, float]],
               truth: dict[int, tuple[float, float, float]], first_index: int) -> tuple[float, float, float]:
    indices = [index for index in samples if index >= max(1000, first_index) and index in truth]
    if len(indices) < 100:
        raise RuntimeError("insufficient overlapping samples for truth comparison")
    return tuple(math.sqrt(statistics.fmean(
        (((samples[i][axis] - truth[i][axis] + 180.0) % 360.0) - 180.0) ** 2
        for i in indices)) for axis in range(3))


def axis_summary(samples: dict[int, tuple[float, float, float]], axis: int,
                 first_index: int) -> tuple[float, float, float]:
    values = [sample[axis] for index, sample in samples.items() if index >= first_index]
    if len(values) < 100:
        raise RuntimeError(f"insufficient replay samples after index {first_index}: {len(values)}")
    return statistics.fmean(values), statistics.pstdev(values), max(values) - min(values)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="*",
                        help="IMU CSV captures (defaults to post-replacement FR, FL, RL, and RR)")
    parser.add_argument("--acc-weight", type=float, default=0.0005,
                        help="current estimator EST_ACC_WEIGHT (default: 0.0005)")
    parser.add_argument("--skip-start-ms", type=float, default=100.0,
                        help="motor-start transient to exclude (default: 100 ms)")
    parser.add_argument("--tau-acc", type=float, action="append", dest="tau_acc_values",
                        help="VQF acceleration correction time constant in seconds; repeat to compare values (default: 3.0)")
    parser.add_argument("--min-confidence", type=float, action="append", dest="confidence_thresholds",
                        help="minimum accelerometer confidence required for a VQF update; repeat to compare values (default: 0.0)")
    parser.add_argument("--truth-csv", type=Path,
                        help="optional sample,roll_deg,pitch_deg,yaw_deg reference for synthetic traces")
    args = parser.parse_args()
    if not math.isfinite(args.acc_weight) or not 0.0 <= args.acc_weight <= 1.0:
        parser.error("--acc-weight must be finite and in [0, 1]")
    if not math.isfinite(args.skip_start_ms) or args.skip_start_ms < 0:
        parser.error("--skip-start-ms must be finite and nonnegative")

    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("A C++17 compiler is required")
    captures = [Path(path) if Path(path).is_absolute() else ROOT / path
                for path in (args.captures or DEFAULT_CAPTURES)]
    for capture in captures:
        if not capture.is_file():
            raise SystemExit(f"capture not found: {capture}")
    truth_path = args.truth_csv.resolve() if args.truth_csv else None
    if truth_path and not truth_path.is_file():
        parser.error(f"truth CSV not found: {truth_path}")
    truth = read_truth(truth_path) if truth_path else None
    tau_acc_values = args.tau_acc_values or [3.0]
    if any(not math.isfinite(value) or value <= 0.0 for value in tau_acc_values):
        parser.error("--tau-acc values must be finite and greater than zero")
    confidence_thresholds = args.confidence_thresholds or [0.0]
    if any(not math.isfinite(value) or not 0.0 <= value < 1.0 for value in confidence_thresholds):
        parser.error("--min-confidence values must be finite and in [0, 1)")

    with tempfile.TemporaryDirectory(prefix="cf-drone-vqf-replay-") as directory:
        temp_path = Path(directory)
        current_bin = temp_path / "current-estimator"
        compile_driver(compiler, current_bin, vqf=False)
        vqf_bins = []
        base_estimator = (ROOT / "estimate.ino").read_text(encoding="utf-8")
        gate_expression = "landed || correctionConfidence > 0.0f"
        if base_estimator.count(gate_expression) != 1:
            raise RuntimeError("could not uniquely locate the VQF confidence gate")
        for gate_index, threshold in enumerate(confidence_thresholds):
            estimator_source = ROOT / "estimate.ino"
            if threshold > 0.0:
                estimator_source = temp_path / f"estimate-confidence-{gate_index}.ino"
                estimator_source.write_text(base_estimator.replace(
                    gate_expression, f"landed || correctionConfidence > {threshold:.9g}f"), encoding="utf-8")
            for tau_index, tau_acc in enumerate(tau_acc_values):
                vqf_bin = temp_path / f"vqf-single-precision-{gate_index}-{tau_index}"
                compile_driver(compiler, vqf_bin, vqf=True, tau_acc=tau_acc,
                               estimator_source=estimator_source)
                vqf_bins.append((tau_acc, threshold, vqf_bin))
        print("Capture | estimator | roll mean/std/pp (deg) | pitch mean/std/pp (deg)" +
              (" | RMSE roll/pitch/yaw (deg)" if truth else ""))
        for capture in captures:
            _, median_dt_us = read_capture_timing(capture)
            first_index = 450 + round(args.skip_start_ms * 1000.0 / median_dt_us)
            initial_attitude = truth[min(truth)] if truth else (0.0, 0.0, 0.0)
            current = run_driver(current_bin, capture, args.acc_weight, initial_attitude)
            outputs = [("current", current)]
            outputs.extend((f"VQF 6D tauAcc={tau_acc:g}s minConf>{threshold:g}",
                            run_driver(vqf_bin, capture, args.acc_weight, initial_attitude))
                           for tau_acc, threshold, vqf_bin in vqf_bins)
            for name, samples in outputs:
                roll = axis_summary(samples, 0, first_index)
                pitch = axis_summary(samples, 1, first_index)
                row = (f"{capture.name} | {name} | "
                       f"{roll[0]:+.4f}/{roll[1]:.4f}/{roll[2]:.4f} | "
                       f"{pitch[0]:+.4f}/{pitch[1]:.4f}/{pitch[2]:.4f}")
                if truth:
                    row += " | " + "/".join(f"{value:.4f}" for value in
                                              truth_rmse(samples, truth, first_index))
                print(row)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
