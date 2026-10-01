#!/usr/bin/env python3
"""Regression tests for compare_estimator_replay helper math."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from compare_estimator_replay import first_active_index, read_capture_timing, truth_rmse  # noqa: E402


class CompareEstimatorReplayTests(unittest.TestCase):
    def test_skip_start_ms_uses_capture_timing(self) -> None:
        with tempfile.TemporaryDirectory(prefix="cf-drone-replay-test-") as directory:
            capture = Path(directory) / "capture.csv"
            with capture.open("w", encoding="utf-8") as stream:
                stream.write("time_us,gyro_x_rad_s,gyro_y_rad_s,gyro_z_rad_s,acc_x_m_s2,acc_y_m_s2,acc_z_m_s2\n")
                for sample in range(600):
                    stream.write(f"{sample * 2000},0,0,0,0,0,9.81\n")

            _, median_dt_us = read_capture_timing(capture)

        self.assertEqual(median_dt_us, 2000)
        self.assertEqual(first_active_index(100.0, median_dt_us), 500)

    def test_capture_timing_rejects_nonpositive_median_interval(self) -> None:
        with tempfile.TemporaryDirectory(prefix="cf-drone-replay-test-") as directory:
            capture = Path(directory) / "capture.csv"
            with capture.open("w", encoding="utf-8") as stream:
                stream.write("time_us,gyro_x_rad_s,gyro_y_rad_s,gyro_z_rad_s,acc_x_m_s2,acc_y_m_s2,acc_z_m_s2\n")
                for _ in range(600):
                    stream.write("1234,0,0,0,0,0,9.81\n")

            with self.assertRaisesRegex(RuntimeError, "nonpositive median sample interval"):
                read_capture_timing(capture)

    def test_truth_rmse_wraps_yaw_across_180_degrees(self) -> None:
        samples = {
            1000: (0.0, 0.0, 179.0),
            1001: (0.0, 0.0, -179.0),
        }
        truth = {
            1000: (0.0, 0.0, -179.0),
            1001: (0.0, 0.0, 179.0),
        }

        self.assertEqual(truth_rmse(samples, truth, [1000, 1001]), (0.0, 0.0, 2.0))

    def test_truth_rmse_uses_caller_supplied_common_indices(self) -> None:
        baseline = {
            1000: (0.0, 0.0, 0.0),
            1001: (10.0, 0.0, 0.0),
        }
        candidate = {
            1001: (20.0, 0.0, 0.0),
        }
        truth = {
            1000: (0.0, 0.0, 0.0),
            1001: (0.0, 0.0, 0.0),
        }
        common_indices = [index for index in baseline if index in candidate and index in truth]

        self.assertEqual(truth_rmse(baseline, truth, common_indices), (10.0, 0.0, 0.0))
        self.assertEqual(truth_rmse(candidate, truth, common_indices), (20.0, 0.0, 0.0))


if __name__ == "__main__":
    unittest.main()
