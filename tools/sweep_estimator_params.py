#!/usr/bin/env python3
"""Cross-validate complementary-estimator parameters on deterministic truth traces."""

from __future__ import annotations

import argparse
import math
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True
from compare_estimator_replay import compile_driver, read_output, read_truth  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PROFILES = ((0.0, 0.0), (0.0, 0.70), (0.12, 0.45),
            (0.20, 0.45), (0.30, 0.70), (0.30, 0.0))


def comma_floats(value: str) -> list[float]:
    try:
        values = [float(part) for part in value.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected comma-separated finite numbers") from error
    if not values or any(not math.isfinite(item) for item in values):
        raise argparse.ArgumentTypeError("expected comma-separated finite numbers")
    return values


def gyro_bias(value: str) -> tuple[float, float, float]:
    values = comma_floats(value)
    if len(values) != 3 or any(abs(item) > 1.0 for item in values):
        raise argparse.ArgumentTypeError("gyro bias must be three values in [-1, 1] rad/s")
    return values[0], values[1], values[2]


def parse_seed_range(value: str) -> list[int]:
    try:
        seeds = [int(part) for part in value.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected comma-separated integer seeds") from error
    if not seeds or any(seed < 0 for seed in seeds):
        raise argparse.ArgumentTypeError("seeds must be nonnegative integers")
    return seeds


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path,
                        help="retain generated CSV traces here (default: temporary directory)")
    parser.add_argument("--motion-seeds", type=parse_seed_range,
                        default=[301, 302, 303, 304])
    parser.add_argument("--noise-seeds", type=parse_seed_range, default=[1, 2])
    parser.add_argument("--duration", type=float, default=15.0)
    parser.add_argument("--weights", type=comma_floats, default=[0.0005, 0.001, 0.003])
    parser.add_argument("--raw-tolerances", type=comma_floats, default=[0.05, 0.075, 0.10])
    parser.add_argument("--innovation-max-deg", type=comma_floats, default=[15.0, 25.0])
    parser.add_argument("--adaptive-min-weights", type=comma_floats, default=[0.0005],
                        help="minimum acceleration correction weight used above the innovation floor")
    parser.add_argument("--fusion-alphas", type=comma_floats, default=[0.2],
                        help="coefficient for the gravity-fusion accelerometer low-pass path")
    parser.add_argument("--gyro-bias", type=gyro_bias, default=(0.0, 0.0, 0.0),
                        help="additional bias applied to each generated gyro stream")
    args = parser.parse_args()
    if not math.isfinite(args.duration) or args.duration <= 1.0:
        parser.error("--duration must be finite and greater than one second")
    if any(not 0.0 <= item <= 1.0 for item in args.weights):
        parser.error("weights must be in [0, 1]")
    if any(not 0.0 < item <= 1.0 for item in args.raw_tolerances):
        parser.error("raw tolerances must be in (0, 1]")
    if any(not 5.0 < item <= 180.0 for item in args.innovation_max_deg):
        parser.error("innovation upper bounds must be in (5, 180]")
    if any(not 0.0 <= item <= 1.0 for item in args.adaptive_min_weights):
        parser.error("adaptive minimum weights must be in [0, 1]")
    if any(not 0.0 < item <= 1.0 for item in args.fusion_alphas):
        parser.error("fusion filter alpha must be in (0, 1]")

    import shutil
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("A C++17 compiler is required")
    configs = [(weight, tolerance, maximum, minimum, alpha)
               for weight in args.weights
               for tolerance in args.raw_tolerances
               for maximum in args.innovation_max_deg
               for minimum in args.adaptive_min_weights
               for alpha in args.fusion_alphas]

    def run(work_dir: Path) -> int:
        traces: list[tuple[Path, dict[int, tuple[float, float, float]], tuple[float, float]]] = []
        for profile_index, (translation, vibration) in enumerate(PROFILES):
            for motion_seed in args.motion_seeds:
                for noise_seed in args.noise_seeds:
                    prefix = work_dir / f"profile-{profile_index}-motion-{motion_seed}-noise-{noise_seed}"
                    subprocess.run([
                        sys.executable, str(ROOT / "tools/generate_synthetic_imu.py"),
                        str(prefix), "--duration", str(args.duration),
                        "--motion-seed", str(motion_seed), "--seed", str(noise_seed),
                        "--translation-g", str(translation), "--vibration", str(vibration),
                    ], check=True, stdout=subprocess.DEVNULL)
                    imu_path = prefix.with_name(prefix.name + "-imu.csv")
                    truth_path = prefix.with_name(prefix.name + "-truth.csv")
                    traces.append((imu_path, read_truth(truth_path), (translation, vibration)))

        binaries: dict[tuple[float, float, float], Path] = {}
        for index, config in enumerate(configs):
            weight, tolerance, maximum, minimum, alpha = config
            binary = work_dir / f"replay-{index}"
            compile_driver(compiler, ROOT / "estimate.ino", binary, tolerance, maximum, minimum, alpha)
            binaries[config] = binary

        print(f"traces={len(traces)} configs={len(configs)} duration={args.duration:g}s "
              f"gyro_bias_extra={args.gyro_bias} rad/s")
        print("profiles=" + ",".join(
            f"p{index}:translation={translation:g}g,vibration={vibration:g}m/s^2"
            for index, (translation, vibration) in enumerate(PROFILES)))
        profile_names = [f"p{index}_R/P/Y" for index in range(len(PROFILES))]
        print("weight,tolerance,innovation_deg,adaptive_min_weight,fusion_alpha,mean_R_RMSE_deg,mean_P_RMSE_deg,"
              "mean_Y_RMSE_deg,worst_profile_R+P_deg," + ",".join(profile_names))
        for config, binary in binaries.items():
            weight, tolerance, maximum, minimum, alpha = config
            per_profile: dict[tuple[float, float], list[list[float]]] = {
                profile: [[], [], []] for profile in PROFILES
            }
            for imu_path, truth, profile in traces:
                output = read_output(binary, imu_path, truth[min(truth)], weight, args.gyro_bias)
                indices = [index for index in output
                           if index >= 1000 and index in truth]
                if not indices:
                    raise RuntimeError(f"no truth overlap for {imu_path}")
                for axis in range(3):
                    rmse = math.sqrt(statistics.fmean(
                        (output[index][axis] - truth[index][axis]) ** 2 for index in indices))
                    per_profile[profile][axis].append(rmse)
            profile_means = {
                profile: [statistics.fmean(values) for values in axes]
                for profile, axes in per_profile.items()
            }
            means = [statistics.fmean(profile_means[profile][axis] for profile in PROFILES)
                     for axis in range(3)]
            worst = max(profile_means[profile][0] + profile_means[profile][1]
                        for profile in PROFILES)
            print(f"{weight:.6g},{tolerance:.6g},{maximum:.6g},{minimum:.6g},{alpha:.6g},"
                  f"{means[0]:.4f},{means[1]:.4f},{means[2]:.4f},{worst:.4f}," +
                  ",".join("/".join(f"{value:.4f}" for value in profile_means[profile])
                            for profile in PROFILES))
        return 0

    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=True)
        return run(args.output_dir.resolve())
    with tempfile.TemporaryDirectory(prefix="cf-estimator-sweep-") as temporary:
        return run(Path(temporary))


if __name__ == "__main__":
    raise SystemExit(main())
