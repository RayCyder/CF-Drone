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
DEFAULT_CAPTURES = (
    "data/attitude/motor-fr-vqf-repeat-20260930.csv",
    "data/attitude/motor-fr-20260930-220951.csv",
    "data/attitude/motor-fl-20260930-221327.csv",
    "data/attitude/motor-rl-20260930-221336.csv",
    "data/attitude/motor-rr-20260930-221731.csv",
)
SHARED_HEADERS = ("quaternion.h", "vector.h", "lpf.h", "util.h")
RAW_TOLERANCE_DECL = "const float rawNormTolerance = ONE_G * 0.1f;"


def compile_driver(compiler: str, include_source: Path, output: Path) -> None:
    command = [
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-vla",
        "-I", str(ROOT / "tests/stubs"), "-I", str(ROOT),
        f'-DESTIMATOR_SOURCE="{include_source}"',
        str(ROOT / "tests/estimator_replay_driver.cpp"), "-o", str(output),
    ]
    subprocess.run(command, check=True)


def read_output(binary: Path, capture: Path,
                initial_attitude: tuple[float, float, float] | None = None) -> dict[int, tuple[float, float, float]]:
    command = [str(binary), str(capture)]
    if initial_attitude is not None:
        command.extend(str(value) for value in initial_attitude)
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="*", help="motor IMU CSV files (defaults to the five archived captures)")
    parser.add_argument("--baseline-ref", default="65e9e5a", help="git revision before the raw-norm confidence gate")
    parser.add_argument("--skip-start-ms", type=float, default=100.0,
                        help="motor-start transient to exclude (default: 100 ms)")
    parser.add_argument("--raw-tolerances", type=parse_tolerances, default=[0.1],
                        help="comma-separated raw-norm tolerances to replay (default: 0.1)")
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

    captures = [Path(path) if Path(path).is_absolute() else ROOT / path
                for path in (args.captures or DEFAULT_CAPTURES)]
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
        baseline_outputs = {capture: read_output(baseline_bin, capture, initial_attitude) for capture in captures}
        candidates: dict[float, dict[Path, dict[int, tuple[float, float, float]]]] = {}
        current_source_text = (ROOT / "estimate.ino").read_text()
        if current_source_text.count(RAW_TOLERANCE_DECL) != 1:
            raise RuntimeError("could not uniquely locate the raw accelerometer norm tolerance")
        for tolerance in args.raw_tolerances:
            candidate_source = temp_path / f"estimate_rawtol_{tolerance:.4f}.ino"
            replacement = f"const float rawNormTolerance = ONE_G * {tolerance:.6g}f;"
            candidate_source.write_text(current_source_text.replace(RAW_TOLERANCE_DECL, replacement))
            candidate_binary = temp_path / f"candidate_{tolerance:.4f}"
            compile_driver(compiler, candidate_source, candidate_binary)
            candidates[tolerance] = {
                capture: read_output(candidate_binary, capture, initial_attitude) for capture in captures
            }

        if truth:
            print("Trace | raw tolerance | roll std/pp deg (base→candidate) | "
                  "pitch std/pp deg (base→candidate) | RMSE R/P/Y deg (base→candidate)")
        else:
            print("Capture | raw tolerance | roll std/pp deg (base→candidate) | "
                  "pitch std/pp deg (base→candidate) | static-to-motor mean shift R/P deg (base→candidate)")
        for capture in captures:
            baseline = baseline_outputs[capture]
            static_indices = [index for index in baseline if 350 <= index < 450]
            active_indices = [index for index in baseline if index >= 450 + round(args.skip_start_ms)]
            if len(static_indices) < 50 or len(active_indices) < 100:
                raise RuntimeError(f"insufficient pre-roll or motor samples in {capture}")
            base_static = [statistics.fmean(baseline[index][axis] for index in static_indices)
                           for axis in (0, 1)]
            base_active = [[baseline[index][axis] for index in active_indices] for axis in (0, 1)]
            base_stats = [metrics(values) for values in base_active]
            base_shift = [base_stats[axis][0] - base_static[axis] for axis in (0, 1)]
            for tolerance in args.raw_tolerances:
                candidate = candidates[tolerance][capture]
                candidate_static = [statistics.fmean(candidate[index][axis] for index in static_indices)
                                    for axis in (0, 1)]
                candidate_active = [[candidate[index][axis] for index in active_indices] for axis in (0, 1)]
                candidate_stats = [metrics(values) for values in candidate_active]
                candidate_shift = [candidate_stats[axis][0] - candidate_static[axis] for axis in (0, 1)]
                prefix = (f"{capture.name} | {tolerance:.1%} | "
                          f"{base_stats[0][1]:.4f}/{base_stats[0][2]:.4f}→{candidate_stats[0][1]:.4f}/{candidate_stats[0][2]:.4f} | "
                          f"{base_stats[1][1]:.4f}/{base_stats[1][2]:.4f}→{candidate_stats[1][1]:.4f}/{candidate_stats[1][2]:.4f} | ")
                if truth:
                    indices = [index for index in active_indices if index >= 1000 and
                               index in reference and index in candidate]
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
