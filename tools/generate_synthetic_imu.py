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
    parser.add_argument("--motion-seed", type=int,
                        help="also randomize attitude, translation, vibration waveforms, and gyro bias")
    parser.add_argument("--translation-g", type=float, default=0.12,
                        help="peak low-frequency translational acceleration in g")
    parser.add_argument("--vibration", type=float, default=0.45,
                        help="peak per-axis high-frequency acceleration vibration in m/s^2")
    parser.add_argument("--gyro-bias", type=float, nargs=3,
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
    if args.gyro_bias is not None and any(not math.isfinite(value) or abs(value) > 1.0
                                          for value in args.gyro_bias):
        parser.error("gyro bias components must be finite and within +/-1 rad/s")
    if any(not math.isfinite(value) or value < 0.0 for value in (args.gyro_noise, args.acc_noise)):
        parser.error("noise levels must be finite and nonnegative")

    imu_path = args.prefix.with_name(args.prefix.name + "-imu.csv")
    truth_path = args.prefix.with_name(args.prefix.name + "-truth.csv")
    imu_path.parent.mkdir(parents=True, exist_ok=True)
    rng = random.Random(args.seed)
    if args.motion_seed is None:
        roll_frequency, roll_secondary_frequency = 0.42, 0.13
        pitch_frequency, pitch_secondary_frequency, yaw_frequency = 0.31, 0.09, 0.11
        roll_phase, roll_secondary_phase = 0.0, 0.0
        pitch_phase, pitch_secondary_phase, yaw_phase = 0.4, 0.0, 0.0
        translation_frequencies = (0.17, 0.21, 0.11)
        translation_phases = (0.0, 0.8, 1.2)
        vibration_frequencies = (187.0, 193.0, 179.0)
        vibration_phases = (0.0, 0.2, 0.5)
        translation_scales = (1.0, 1.0, 0.8)
        vibration_scales = (1.0, 0.78, 0.67)
        gyro_bias = args.gyro_bias or (0.002, -0.001, 0.0015)
    else:
        motion_rng = random.Random(args.motion_seed)
        roll_frequency, roll_secondary_frequency = motion_rng.uniform(.28, .55), motion_rng.uniform(.07, .19)
        pitch_frequency, pitch_secondary_frequency = motion_rng.uniform(.22, .43), motion_rng.uniform(.06, .16)
        yaw_frequency = motion_rng.uniform(.06, .17)
        roll_phase, roll_secondary_phase = motion_rng.uniform(0, 2 * math.pi), motion_rng.uniform(0, 2 * math.pi)
        pitch_phase, pitch_secondary_phase = motion_rng.uniform(0, 2 * math.pi), motion_rng.uniform(0, 2 * math.pi)
        yaw_phase = motion_rng.uniform(0, 2 * math.pi)
        translation_frequencies = tuple(motion_rng.uniform(.08, .35) for _ in range(3))
        translation_phases = tuple(motion_rng.uniform(0, 2 * math.pi) for _ in range(3))
        vibration_frequencies = tuple(motion_rng.uniform(174, 198) for _ in range(3))
        vibration_phases = tuple(motion_rng.uniform(0, 2 * math.pi) for _ in range(3))
        translation_scales = (1.0, 1.0, 1.0)
        vibration_scales = (1.0, 1.0, 1.0)
        gyro_bias = args.gyro_bias or tuple(motion_rng.uniform(-.003, .003) for _ in range(3))
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
            roll = math.radians(14) * math.sin(2 * math.pi * roll_frequency * t + roll_phase) + \
                math.radians(4) * math.sin(2 * math.pi * roll_secondary_frequency * t + roll_secondary_phase)
            pitch = math.radians(10) * math.sin(2 * math.pi * pitch_frequency * t + pitch_phase) + \
                math.radians(3) * math.sin(2 * math.pi * pitch_secondary_frequency * t + pitch_secondary_phase)
            yaw = math.radians(35) * math.sin(2 * math.pi * yaw_frequency * t + yaw_phase)
            roll_rate = math.radians(14) * 2 * math.pi * roll_frequency * math.cos(2 * math.pi * roll_frequency * t + roll_phase) + \
                math.radians(4) * 2 * math.pi * roll_secondary_frequency * math.cos(2 * math.pi * roll_secondary_frequency * t + roll_secondary_phase)
            pitch_rate = math.radians(10) * 2 * math.pi * pitch_frequency * math.cos(2 * math.pi * pitch_frequency * t + pitch_phase) + \
                math.radians(3) * 2 * math.pi * pitch_secondary_frequency * math.cos(2 * math.pi * pitch_secondary_frequency * t + pitch_secondary_phase)
            yaw_rate = math.radians(35) * 2 * math.pi * yaw_frequency * math.cos(2 * math.pi * yaw_frequency * t + yaw_phase)

            gyro = (
                roll_rate - yaw_rate * math.sin(pitch),
                pitch_rate * math.cos(roll) + yaw_rate * math.sin(roll) * math.cos(pitch),
                -pitch_rate * math.sin(roll) + yaw_rate * math.cos(roll) * math.cos(pitch),
            )
            gyro = tuple(value + gyro_bias[axis] + rng.gauss(0.0, args.gyro_noise)
                         for axis, value in enumerate(gyro))

            # At rest the accelerometer reports body-frame world-up. Add bounded
            # translation and high-frequency vibration to challenge gravity gating.
            acc = (
                -math.sin(pitch) * G + args.translation_g * translation_scales[0] * G *
                math.sin(2 * math.pi * translation_frequencies[0] * t + translation_phases[0])
                + args.vibration * vibration_scales[0] *
                math.sin(2 * math.pi * vibration_frequencies[0] * t + vibration_phases[0]),
                math.sin(roll) * math.cos(pitch) * G
                + args.translation_g * translation_scales[1] * G *
                math.sin(2 * math.pi * translation_frequencies[1] * t + translation_phases[1])
                + args.vibration * vibration_scales[1] *
                math.sin(2 * math.pi * vibration_frequencies[1] * t + vibration_phases[1]),
                math.cos(roll) * math.cos(pitch) * G
                + args.translation_g * translation_scales[2] * G *
                math.sin(2 * math.pi * translation_frequencies[2] * t + translation_phases[2])
                + args.vibration * vibration_scales[2] *
                math.sin(2 * math.pi * vibration_frequencies[2] * t + vibration_phases[2]),
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
