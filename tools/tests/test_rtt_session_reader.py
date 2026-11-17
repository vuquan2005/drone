"""Saved-session timing, validation and unit conversion."""

import csv
import math
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "rtt"))
from session_reader import read_session
from session_writer import IMU_RAW_COLUMNS, RATE_FIELDS, RC_COLUMNS


class SessionReaderTests(unittest.TestCase):
    def test_full_history_wrap_gap_segments_and_raw_units(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)
            with (path / "imu_raw_20261118_000000.csv").open("w", newline="") as file:
                writer = csv.writer(file)
                writer.writerow(IMU_RAW_COLUMNS)
                writer.writerows(
                    [
                        (1, 1, 2**32 - 1000, 0, *([1000] * 6), 16, 2, 2000, 400, 400),
                        (1, 3, 11000000, 0, *([2000] * 6), 16, 2, 2000, 400, 400),
                        (2, 1, 100, 0, *([3000] * 6), 16, 2, 2000, 400, 400),
                    ]
                )
            with (path / "imu_raw_20261118.csv").open("w", newline="") as file:
                writer = csv.writer(file)
                writer.writerow(IMU_RAW_COLUMNS)
                writer.writerow((3, 1, 100, 0, 16384, 16384, 0, 0, 0, 0, 2, 1, 250, 400, 400))
            with (path / "rate_20261118.csv").open("w", newline="") as file:
                writer = csv.writer(file)
                writer.writerow(("segment", *RATE_FIELDS))
                for seq, stamp, flags in [(1, 100, 3), (3, 12000100, 1)]:
                    writer.writerow((1, seq, stamp, 2000, flags, *([0.0] * 16)))
            segments, _ = read_session(path)
            self.assertEqual(sorted(segments), [1, 2, 3])
            points = segments[1].series.points
            self.assertEqual(len(points), 3)
            self.assertEqual(points[0][0], 0)
            self.assertAlmostEqual(points[-1][0], 11.001)
            self.assertTrue(math.isnan(points[1][1]))
            self.assertEqual(segments[1].series.missing, 1)
            self.assertAlmostEqual(segments[3].series.points[0][1], 9.80665)
            self.assertAlmostEqual(segments[3].series.points[0][2], 9.80665)
            rate = segments[1].rate_points
            self.assertEqual(len(rate), 3)
            self.assertEqual(rate[-1][0], 12)
            self.assertTrue(math.isnan(rate[-1][1]))

    def test_rc_history_wrap_and_segment_isolation(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "rc_20261118.csv"
            with path.open("w", newline="") as file:
                writer = csv.writer(file)
                writer.writerow(RC_COLUMNS)
                writer.writerows(
                    [
                        (1, 1, 2**32 - 25, 0, 1, *([172] * 16)),
                        (1, 3, 75, 50, 0, *([1811] * 16)),
                        (2, 1, 10, 5, 1, *([992] * 16)),
                    ]
                )
            segments, _ = read_session(root)
            self.assertEqual([t for t, _ in segments[1].rc_snapshots], [0, 0.1])
            self.assertEqual(segments[1].rc_snapshots[0][1][4], 172)
            self.assertEqual(segments[1].rc_snapshots[1][1][3], 0)
            self.assertEqual(len(segments[2].rc_snapshots), 1)
            self.assertEqual(segments[2].rc_snapshots[0][0], 0)

    def test_retired_csv_is_ignored_and_preserved(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)
            legacy = path / "imu_20261118.csv"
            original = b"retired SI header\nretired sample\n"
            legacy.write_bytes(original)
            with self.assertRaisesRegex(ValueError, "No telemetry"):
                read_session(root)
            with (path / "imu_raw_20261118.csv").open("w", newline="") as file:
                writer = csv.writer(file)
                writer.writerow(IMU_RAW_COLUMNS)
                writer.writerow((1, 1, 100, 0, 0, 0, 2048, 0, 0, 0, 16, 2, 2000, 400, 400))
            segments, _ = read_session(root)
            self.assertEqual(segments[1].series.received, 1)
            self.assertAlmostEqual(segments[1].series.points[0][3], -9.80665)
            self.assertEqual(legacy.read_bytes(), original)

    def test_rejects_bad_rows_and_empty_sessions(self):
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaisesRegex(ValueError, "No telemetry"):
                read_session(root)
            path = Path(root) / "imu_raw_20261118.csv"
            with path.open("w", newline="") as file:
                writer = csv.writer(file)
                writer.writerow(IMU_RAW_COLUMNS)
                writer.writerow((1, 1))
            with self.assertRaisesRegex(ValueError, "imu_raw_20261118.csv:2"):
                read_session(root)
