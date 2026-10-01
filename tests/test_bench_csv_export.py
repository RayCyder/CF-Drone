#!/usr/bin/env python3
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from run_attitude_calibration_bench import parse_csv_export


class BenchCsvExportTests(unittest.TestCase):
    def test_accepts_csv_when_header_count_matches(self):
        rows = parse_csv_export("/logs.csv", b"t,armed\n1,0\n2,1\n", 2)
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[1]["armed"], "1")

    def test_rejects_truncated_csv(self):
        with self.assertRaisesRegex(RuntimeError, "expected 3 CSV rows, got 2"):
            parse_csv_export("/logs.csv", b"t,armed\n1,0\n2,1\n", 3)

    def test_rejects_trace_changed_marker(self):
        data = b"sequence,dt_us\n1,1000\n# trace changed during export; retry while disarmed\n"
        with self.assertRaisesRegex(RuntimeError, "trace changed during export"):
            parse_csv_export("/diag/trace.csv", data, 2)

    def test_rejects_empty_export(self):
        with self.assertRaisesRegex(RuntimeError, "empty"):
            parse_csv_export("/diag/trace.csv", b"", 0)

    def test_accepts_header_only_trace_when_expected_count_is_zero(self):
        self.assertEqual(parse_csv_export("/diag/trace.csv", b"sequence,dt_us\n", 0), [])

    def test_rejects_malformed_rows(self):
        with self.assertRaisesRegex(RuntimeError, "malformed CSV row 1"):
            parse_csv_export("/logs.csv", b"t,armed\n1,0,extra\n", 1)

    def test_rejects_all_empty_data_rows(self):
        with self.assertRaisesRegex(RuntimeError, "empty CSV row 1"):
            parse_csv_export("/logs.csv", b"t,armed\n,\n", 1)


if __name__ == "__main__":
    unittest.main()
