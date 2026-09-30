#!/usr/bin/env python3
"""Replay identical recorded motor-vibration samples through two estimators.

This compares output variation only. It does not provide attitude ground truth.
"""

from __future__ import annotations

import argparse
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


def compile_driver(compiler: str, include_source: Path, output: Path) -> None:
    command = [
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-vla",
        "-I", str(ROOT / "tests/stubs"), "-I", str(ROOT),
        f'-DESTIMATOR_SOURCE="{include_source}"',
        str(ROOT / "tests/estimator_replay_driver.cpp"), "-o", str(output),
    ]
    subprocess.run(command, check=True)


def read_output(binary: Path, capture: Path, skip_start_ms: float) -> tuple[list[float], list[float]]:
    result = subprocess.run([str(binary), str(capture)], check=True, capture_output=True, text=True)
    samples = []
    for line in result.stdout.splitlines():
        index, roll, pitch, _yaw = line.split(",")
        if int(index) >= 450 + round(skip_start_ms):
            samples.append((float(roll), float(pitch)))
    if len(samples) < 100:
        raise RuntimeError(f"too few replay samples in {capture}: {len(samples)}")
    return [s[0] for s in samples], [s[1] for s in samples]


def metrics(values: list[float]) -> tuple[float, float, float]:
    return statistics.fmean(values), statistics.pstdev(values), max(values) - min(values)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="*", help="motor IMU CSV files (defaults to the five archived captures)")
    parser.add_argument("--baseline-ref", default="65e9e5a", help="git revision before the raw-norm confidence gate")
    parser.add_argument("--skip-start-ms", type=float, default=100.0,
                        help="motor-start transient to exclude (default: 100 ms)")
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

    with tempfile.TemporaryDirectory(prefix="cf-drone-estimator-replay-") as temp:
        temp_path = Path(temp)
        baseline_source = temp_path / "estimate_baseline.ino"
        baseline_source.write_text(baseline_source_text)
        baseline_bin = temp_path / "baseline"
        candidate_bin = temp_path / "candidate"
        compile_driver(compiler, baseline_source, baseline_bin)
        compile_driver(compiler, ROOT / "estimate.ino", candidate_bin)

        print("Capture | baseline roll std/pp (deg) | candidate roll std/pp (deg) | "
              "baseline pitch std/pp (deg) | candidate pitch std/pp (deg) | std change R/P")
        for capture in captures:
            base_roll, base_pitch = read_output(baseline_bin, capture, args.skip_start_ms)
            cand_roll, cand_pitch = read_output(candidate_bin, capture, args.skip_start_ms)
            br, cr = metrics(base_roll), metrics(cand_roll)
            bp, cp = metrics(base_pitch), metrics(cand_pitch)
            roll_change = 100.0 * (cr[1] / br[1] - 1.0) if br[1] else math.nan
            pitch_change = 100.0 * (cp[1] / bp[1] - 1.0) if bp[1] else math.nan
            print(f"{capture.name} | {br[1]:.4f}/{br[2]:.4f} | {cr[1]:.4f}/{cr[2]:.4f} | "
                  f"{bp[1]:.4f}/{bp[2]:.4f} | {cp[1]:.4f}/{cp[2]:.4f} | "
                  f"{roll_change:+.1f}%/{pitch_change:+.1f}%")
            print(f"  mean shift candidate-baseline: roll {cr[0] - br[0]:+.3f}°, "
                  f"pitch {cp[0] - bp[0]:+.3f}°; n={len(base_roll)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
