#!/usr/bin/env python3
"""Summarize raw-gyro capture means and exploratory temperature slopes."""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from pathlib import Path

AXES = "xyz"


def read_capture(path: Path) -> tuple[float, tuple[float, float, float], tuple[float, float, float], int]:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    required = {"temperature_c", *(f"gyro_sensor_uncorrected_{axis}_rad_s" for axis in AXES)}
    if not rows or not required.issubset(rows[0]):
        raise ValueError(f"{path}: expected a raw-temperature CSV with {sorted(required)}")

    temperatures = [float(row["temperature_c"]) for row in rows]
    means = tuple(statistics.mean(float(row[f"gyro_sensor_uncorrected_{axis}_rad_s"]) for row in rows)
                  for axis in AXES)
    standard_errors = tuple(
        statistics.stdev(float(row[f"gyro_sensor_uncorrected_{axis}_rad_s"]) for row in rows) / math.sqrt(len(rows))
        if len(rows) > 1 else float("nan")
        for axis in AXES
    )
    return statistics.mean(temperatures), means, standard_errors, len(rows)


def fit_slope(temperatures: list[float], values: list[float]) -> tuple[float, float, float]:
    mean_temp, mean_value = statistics.mean(temperatures), statistics.mean(values)
    sxx = sum((temperature - mean_temp) ** 2 for temperature in temperatures)
    if len(temperatures) < 3 or sxx == 0:
        return float("nan"), float("nan"), float("nan")
    slope = sum((temperature - mean_temp) * (value - mean_value)
                for temperature, value in zip(temperatures, values)) / sxx
    intercept = mean_value - slope * mean_temp
    residuals = [value - (intercept + slope * temperature)
                 for temperature, value in zip(temperatures, values)]
    standard_error = math.sqrt(sum(error * error for error in residuals) / (len(temperatures) - 2) / sxx)
    total = sum((value - mean_value) ** 2 for value in values)
    r_squared = 1.0 - sum(error * error for error in residuals) / total if total else float("nan")
    return slope, standard_error, r_squared


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="+", type=Path, help="raw-temperature CSV files")
    args = parser.parse_args()

    points = []
    for path in args.captures:
        temperature, means, errors, rows = read_capture(path)
        points.append((temperature, means))
        print(f"{path}: rows={rows} T={temperature:.3f} C "
              f"raw_mean=" + ",".join(f"{value:.6f}" for value in means) +
              " mean_SE=" + ",".join(f"{value:.6f}" for value in errors))

    temperatures = [point[0] for point in points]
    span = max(temperatures) - min(temperatures)
    print(f"capture_count={len(points)} temperature_span_C={span:.3f}")
    for axis_index, axis in enumerate(AXES):
        slope, standard_error, r_squared = fit_slope(
            temperatures, [point[1][axis_index] for point in points])
        print(f"{axis}: slope={slope:.8f} +/- {standard_error:.8f} rad/s/C R2={r_squared:.3f}")

    if len(points) < 5 or span < 5.0:
        print("INCONCLUSIVE: use at least five independent captures spanning 5 C before interpreting a temperature model")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
