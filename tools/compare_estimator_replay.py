#!/usr/bin/env python3
"""Replay identical motor-vibration or synthetic truth traces through estimators."""

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
PRE_REPLACEMENT_CAPTURES = (
    "data/attitude/motor-fr-vqf-repeat-20260930.csv",
    "data/attitude/motor-fr-20260930-220951.csv",
    "data/attitude/motor-fl-20260930-221327.csv",
    "data/attitude/motor-rl-20260930-221336.csv",
    "data/attitude/motor-rr-20260930-221731.csv",
)
POST_REPLACEMENT_CAPTURES = (
    "data/attitude/motor-fr-20261001-001156.csv",
    "data/attitude/motor-fr-20261001-001909.csv",
    "data/attitude/motor-fr-20261001-001918.csv",
    "data/attitude/motor-fl-20261001-001301.csv",
    "data/attitude/motor-rl-20261001-001310.csv",
    "data/attitude/motor-rr-20261001-081759.csv",
)
SHARED_HEADERS = ("quaternion.h", "vector.h", "lpf.h", "util.h")


def compile_driver(compiler: str, include_source: Path, output: Path,
                   raw_tolerance: float | None = None) -> None:
    command = [
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-vla",
        "-I", str(ROOT / "tests/stubs"), "-I", str(ROOT),
        f'-DESTIMATOR_SOURCE="{include_source}"',
        str(ROOT / "tests/estimator_replay_driver.cpp"), "-o", str(output),
    ]
    if raw_tolerance is not None:
        command.insert(-3, f"-DEST_RAW_ACCEL_NORM_TOLERANCE={raw_tolerance:.9g}f")
    subprocess.run(command, check=True)


def read_output(binary: Path, capture: Path,
                initial_attitude: tuple[float, float, float] | None = None,
                acc_weight: float | None = None,
                gyro_bias: tuple[float, float, float] | None = None) -> dict[int, tuple[float, float, float]]:
    command = [str(binary), str(capture)]
    if initial_attitude is not None or acc_weight is not None or gyro_bias is not None:
        command.extend(str(value) for value in (initial_attitude or (0.0, 0.0, 0.0)))
        command.append(str(0.003 if acc_weight is None else acc_weight))
        command.extend(str(value) for value in (gyro_bias or (0.0, 0.0, 0.0)))
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    samples = {}
    for line in result.stdout.splitlines():
        index, roll, pitch, yaw = line.split(",")
        samples[int(index)] = (float(roll), float(pitch), float(yaw))
    if len(samples) < 200:
        raise RuntimeError(f"too few replay samples in {capture}: {len(samples)}")
    return samples


def read_truth(path: Path) -> dict[int, tuple[float, float, float]]:
    with path.open(newline="") as stream:
        rows = csv.DictReader(stream)
        return {int(row["sample"]): (float(row["roll_deg"]), float(row["pitch_deg"]),
                                      float(row["yaw_deg"])) for row in rows}


def metrics(values: list[float]) -> tuple[float, float, float]:
    return statistics.fmean(values), statistics.pstdev(values), max(values) - min(values)


def parse_tolerances(value: str) -> list[float]:
    try:
        tolerances = [float(part) for part in value.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError("use comma-separated fractions such as 0.05,0.1,0.2") from error
    if not tolerances or any(not 0.0 < item <= 1.0 for item in tolerances):
        raise argparse.ArgumentTypeError("raw-norm tolerance values must be in (0, 1]")
    return tolerances


def parse_acc_weights(value: str) -> list[float]:
    try:
        weights = [float(part) for part in value.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError("use comma-separated values such as 0,0.001,0.003") from error
    if not weights or any(not 0.0 <= item <= 1.0 for item in weights):
        raise argparse.ArgumentTypeError("acceleration weights must be in [0, 1]")
    return weights


def parse_gyro_bias(value: str) -> tuple[float, float, float]:
    try:
        bias = tuple(float(part) for part in value.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError("use three comma-separated rad/s values") from error
    if len(bias) != 3 or any(not math.isfinite(item) or abs(item) > 1.0 for item in bias):
        raise argparse.ArgumentTypeError("gyro bias must contain three finite rad/s values in [-1, 1]")
    return bias


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="*", help="explicit motor IMU CSV files (overrides --dataset)")
    parser.add_argument("--dataset", choices=("post-replacement", "pre-replacement"),
                        default="post-replacement",
                        help="default capture set (default: post-replacement, all four motors)")
    parser.add_argument("--baseline-ref", default="65e9e5a", help="git revision before the raw-norm confidence gate")
    parser.add_argument("--skip-start-ms", type=float, default=100.0,
                        help="motor-start transient to exclude (default: 100 ms)")
    parser.add_argument("--raw-tolerances", type=parse_tolerances, default=[0.1],
                        help="comma-separated raw-norm tolerances to replay (default: 0.1)")
    parser.add_argument("--acc-weights", type=parse_acc_weights, default=[0.003],
                        help="comma-separated EST_ACC_WEIGHT values (default: 0.003)")
    parser.add_argument("--gyro-bias", type=parse_gyro_bias, default=(0.0, 0.0, 0.0),
                        help="constant gyro bias in rad/s, e.g. 0.001,0,0")
    parser.add_argument("--truth-csv", type=Path,
                        help="optional sample,roll_deg,pitch_deg,yaw_deg truth CSV for a synthetic trace")
    args = parser.parse_args()
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("A C++17 compiler is required")
    if args.skip_start_ms < 0:
        raise SystemExit("--skip-start-ms must be nonnegative")

    baseline_result = subprocess.run(
        ["git", "-C", str(ROOT), "show", f"{args.baseline_ref}:estimate.ino"],
        check=True, capture_output=True, text=True,
    )
    baseline_source_text = baseline_result.stdout
    for header in SHARED_HEADERS:
        baseline_source_text = baseline_source_text.replace(
            f'#include "{header}"', f'#include "{ROOT / header}"')

    default_captures = (POST_REPLACEMENT_CAPTURES if args.dataset == "post-replacement"
                        else PRE_REPLACEMENT_CAPTURES)
    captures = [Path(path) if Path(path).is_absolute() else ROOT / path
                for path in (args.captures or default_captures)]
    for capture in captures:
        if not capture.is_file():
            raise SystemExit(f"capture not found: {capture}")
    truth = args.truth_csv.resolve() if args.truth_csv else None
    if truth and not truth.is_file():
        raise SystemExit(f"truth file not found: {truth}")

    with tempfile.TemporaryDirectory(prefix="cf-drone-estimator-replay-") as temp:
        temp_path = Path(temp)
        baseline_source = temp_path / "estimate_baseline.ino"
        baseline_source.write_text(baseline_source_text)
        baseline_bin = temp_path / "baseline"
        compile_driver(compiler, baseline_source, baseline_bin)
        reference = read_truth(truth) if truth else None
        initial_attitude = reference[min(reference)] if reference else None
        baseline_outputs = {
            weight: {capture: read_output(baseline_bin, capture, initial_attitude, weight, args.gyro_bias)
                     for capture in captures}
            for weight in args.acc_weights
        }
        candidates: dict[tuple[float, float], dict[Path, dict[int, tuple[float, float, float]]]] = {}
        for tolerance in args.raw_tolerances:
            candidate_binary = temp_path / f"candidate_{tolerance:.4f}"
            compile_driver(compiler, ROOT / "estimate.ino", candidate_binary, tolerance)
            for weight in args.acc_weights:
                candidates[(tolerance, weight)] = {
                    capture: read_output(candidate_binary, capture, initial_attitude, weight, args.gyro_bias)
                    for capture in captures
                }

        if truth:
            print(f"Trace | raw tolerance | accWeight | gyro bias {args.gyro_bias} rad/s | "
                  "roll std/pp deg (base→candidate) | "
                  "pitch std/pp deg (base→candidate) | RMSE R/P/Y deg (base→candidate)")
        else:
            print(f"Capture | raw tolerance | accWeight | gyro bias {args.gyro_bias} rad/s | "
                  "roll std/pp deg (base→candidate) | "
                  "pitch std/pp deg (base→candidate) | static-to-motor mean shift R/P deg (base→candidate)")
        for capture in captures:
            for tolerance in args.raw_tolerances:
                for weight in args.acc_weights:
                    baseline = baseline_outputs[weight][capture]
                    candidate = candidates[(tolerance, weight)][capture]
                    static_indices = [index for index in baseline if 350 <= index < 450]
                    active_indices = [index for index in baseline if index >= 450 + round(args.skip_start_ms)]
                    if len(static_indices) < 50 or len(active_indices) < 100:
                        raise RuntimeError(f"insufficient pre-roll or motor samples in {capture}")
                    base_static = [statistics.fmean(baseline[index][axis] for index in static_indices)
                                   for axis in (0, 1)]
                    base_active = [[baseline[index][axis] for index in active_indices] for axis in (0, 1)]
                    base_stats = [metrics(values) for values in base_active]
                    base_shift = [base_stats[axis][0] - base_static[axis] for axis in (0, 1)]
                    candidate_static = [statistics.fmean(candidate[index][axis] for index in static_indices)
                                        for axis in (0, 1)]
                    candidate_active = [[candidate[index][axis] for index in active_indices] for axis in (0, 1)]
                    candidate_stats = [metrics(values) for values in candidate_active]
                    candidate_shift = [candidate_stats[axis][0] - candidate_static[axis] for axis in (0, 1)]
                    prefix = (f"{capture.name} | {tolerance:.1%} | {weight:.4g} | "
                              f"{base_stats[0][1]:.4f}/{base_stats[0][2]:.4f}→{candidate_stats[0][1]:.4f}/{candidate_stats[0][2]:.4f} | "
                              f"{base_stats[1][1]:.4f}/{base_stats[1][2]:.4f}→{candidate_stats[1][1]:.4f}/{candidate_stats[1][2]:.4f} | ")
                    if truth:
                        indices = [index for index in active_indices if index >= 1000 and
                                   index in reference and index in candidate]
                        if not indices:
                            raise RuntimeError(f"no overlapping truth samples in {capture}")
                        errors = []
                        for axis in range(3):
                            base_sq = [(baseline[index][axis] - reference[index][axis]) ** 2 for index in indices]
                            cand_sq = [(candidate[index][axis] - reference[index][axis]) ** 2 for index in indices]
                            errors.append((math.sqrt(statistics.fmean(base_sq)),
                                           math.sqrt(statistics.fmean(cand_sq))))
                        print(prefix + " / ".join(f"{base:.4f}→{cand:.4f}" for base, cand in errors))
                    else:
                        print(prefix + f"{base_shift[0]:+.3f}/{base_shift[1]:+.3f}→"
                              f"{candidate_shift[0]:+.3f}/{candidate_shift[1]:+.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
