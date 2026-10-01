#!/usr/bin/env python3
"""Generate deterministic six-axis IMU data and matching attitude truth."""

from __future__ import annotations

import argparse
import csv
import math
import random
from pathlib import Path

G = 9.80665


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path, help="output prefix for -imu.csv and -truth.csv")
    parser.add_argument("--duration", type=float, default=20.0, help="trace duration in seconds")
    parser.add_argument("--seed", type=int, default=1, help="deterministic noise seed")
    parser.add_argument("--translation-g", type=float, default=0.12,
                        help="peak low-frequency translational acceleration in g")
    parser.add_argument("--vibration", type=float, default=0.45,
                        help="peak per-axis high-frequency acceleration vibration in m/s^2")
    parser.add_argument("--gyro-bias", type=float, nargs=3, default=(0.002, -0.001, 0.0015),
                        metavar=("X", "Y", "Z"), help="constant gyro bias in rad/s")
    parser.add_argument("--gyro-noise", type=float, default=0.003,
                        help="gyro white-noise standard deviation in rad/s")
    parser.add_argument("--acc-noise", type=float, default=0.04,
                        help="accelerometer white-noise standard deviation in m/s^2")
    args = parser.parse_args()
    if not math.isfinite(args.duration) or args.duration <= 1.0:
        parser.error("--duration must be finite and greater than one second")
    if not math.isfinite(args.translation_g) or args.translation_g < 0.0:
        parser.error("--translation-g must be finite and nonnegative")
    if not math.isfinite(args.vibration) or args.vibration < 0.0:
        parser.error("--vibration must be finite and nonnegative")
    if any(not math.isfinite(value) or abs(value) > 1.0 for value in args.gyro_bias):
        parser.error("gyro bias components must be finite and within +/-1 rad/s")
    if any(not math.isfinite(value) or value < 0.0 for value in (args.gyro_noise, args.acc_noise)):
        parser.error("noise levels must be finite and nonnegative")

    imu_path = args.prefix.with_name(args.prefix.name + "-imu.csv")
    truth_path = args.prefix.with_name(args.prefix.name + "-truth.csv")
    imu_path.parent.mkdir(parents=True, exist_ok=True)
    rng = random.Random(args.seed)
    count = round(args.duration * 1000.0)
    with imu_path.open("w", newline="", encoding="utf-8") as imu_stream, \
            truth_path.open("w", newline="", encoding="utf-8") as truth_stream:
        imu_writer = csv.writer(imu_stream)
        truth_writer = csv.writer(truth_stream)
        imu_writer.writerow(("time_us", "gyro_x_rad_s", "gyro_y_rad_s", "gyro_z_rad_s",
                             "acc_x_m_s2", "acc_y_m_s2", "acc_z_m_s2"))
        truth_writer.writerow(("sample", "roll_deg", "pitch_deg", "yaw_deg"))
        for sample in range(count + 1):
            t = sample * 0.001
            roll = math.radians(14) * math.sin(2 * math.pi * 0.42 * t) + \
                math.radians(4) * math.sin(2 * math.pi * 0.13 * t)
            pitch = math.radians(10) * math.sin(2 * math.pi * 0.31 * t + 0.4) + \
                math.radians(3) * math.sin(2 * math.pi * 0.09 * t)
            yaw = math.radians(35) * math.sin(2 * math.pi * 0.11 * t)
            roll_rate = math.radians(14) * 2 * math.pi * 0.42 * math.cos(2 * math.pi * 0.42 * t) + \
                math.radians(4) * 2 * math.pi * 0.13 * math.cos(2 * math.pi * 0.13 * t)
            pitch_rate = math.radians(10) * 2 * math.pi * 0.31 * math.cos(2 * math.pi * 0.31 * t + 0.4) + \
                math.radians(3) * 2 * math.pi * 0.09 * math.cos(2 * math.pi * 0.09 * t)
            yaw_rate = math.radians(35) * 2 * math.pi * 0.11 * math.cos(2 * math.pi * 0.11 * t)

            gyro = (
                roll_rate - yaw_rate * math.sin(pitch),
                pitch_rate * math.cos(roll) + yaw_rate * math.sin(roll) * math.cos(pitch),
                -pitch_rate * math.sin(roll) + yaw_rate * math.cos(roll) * math.cos(pitch),
            )
            bias = args.gyro_bias
            gyro = tuple(value + bias[axis] + rng.gauss(0.0, args.gyro_noise)
                         for axis, value in enumerate(gyro))

            # At rest the accelerometer reports body-frame world-up. Add bounded
            # translation and high-frequency vibration to challenge gravity gating.
            acc = (
                -math.sin(pitch) * G + args.translation_g * G * math.sin(2 * math.pi * 0.17 * t)
                + args.vibration * math.sin(2 * math.pi * 187 * t),
                math.sin(roll) * math.cos(pitch) * G
                + args.translation_g * G * math.sin(2 * math.pi * 0.21 * t + 0.8)
                + args.vibration * 0.78 * math.sin(2 * math.pi * 193 * t + 0.2),
                math.cos(roll) * math.cos(pitch) * G
                + args.translation_g * 0.8 * G * math.sin(2 * math.pi * 0.11 * t + 1.2)
                + args.vibration * 0.67 * math.sin(2 * math.pi * 179 * t + 0.5),
            )
            acc = tuple(value + rng.gauss(0.0, args.acc_noise) for value in acc)
            imu_writer.writerow((sample * 1000, *(f"{value:.8f}" for value in (*gyro, *acc))))
            truth_writer.writerow((sample, *(f"{math.degrees(value):.8f}" for value in
                                             (roll, pitch, yaw))))
    print(f"IMU: {imu_path} ({count + 1} samples)")
    print(f"Truth: {truth_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
