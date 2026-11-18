"""Disk writer tests; no Qt, probe or networking required."""

import csv
from pathlib import Path
import sys
import tempfile
from threading import Event
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "rtt"))
from session_reader import read_session
from records import Frames
from telemetry import TelemetryState
from session_writer import IMU_RAW_COLUMNS, STATS_COLUMNS, SessionWriter


class SessionWriterTests(unittest.TestCase):
    def finish(self, writer):
        writer.stop()
        self.assertTrue(writer.done.wait(3))
        writer.thread.join()

    def test_timestamped_names_content_and_drain(self):
        with tempfile.TemporaryDirectory() as root:
            writer = SessionWriter(root, raw=True)
            self.addCleanup(self.finish, writer)
            stamp = writer.directory.name.removeprefix("telemetry_")
            self.assertRegex(stamp, r"^\d{8}_\d{6}(?:_\d{2,})?$")
            self.assertTrue(
                writer.submit(
                    "imu_raw", (2, 17, 123456, 42, -1, 2, 3, 4, 5, 6, 16, 2, 2000, 400, 400)
                )
            )
            fixture = Path(__file__).resolve().parents[2] / "protocol/fixtures/attitude_v1.hex"
            (frame,) = Frames().feed(bytes.fromhex(fixture.read_text()))
            kind, attitude = TelemetryState().feed(frame, 0)
            self.assertEqual(kind, "attitude")
            self.assertTrue(writer.submit(kind, (2, *attitude)))
            self.assertTrue(writer.submit("stats", (2, 1, 1234, 17, 3, 0, 225, 224, 1, 0, 0)))
            self.assertTrue(writer.submit("log", b"[1][I][imu] ready\n"))
            self.assertTrue(writer.submit("raw", b"\x00\xff\x10"))
            self.finish(writer)
            self.assertIsNone(writer.error)
            self.assertEqual(writer.accepted, writer.processed)
            self.assertFalse(writer.submit("log", b"late\n"))
            self.assertEqual(
                set(p.name for p in writer.directory.iterdir()),
                {
                    f"imu_raw_{stamp}.csv",
                    f"rtt_stats_{stamp}.csv",
                    f"rc_{stamp}.csv",
                    f"rate_{stamp}.csv",
                    f"attitude_{stamp}.csv",
                    f"events_{stamp}.log",
                    f"data_{stamp}.hex",
                },
            )
            with (writer.directory / f"imu_raw_{stamp}.csv").open(newline="") as f:
                rows = list(csv.reader(f))
            with (writer.directory / f"rtt_stats_{stamp}.csv").open(newline="") as f:
                stats_rows = list(csv.reader(f))
            self.assertEqual(stats_rows[0], list(STATS_COLUMNS))
            self.assertEqual(
                stats_rows[1], ["2", "1", "1234", "17", "3", "0", "225", "224", "1", "0", "0"]
            )
            segments, _ = read_session(writer.directory)
            self.assertEqual(segments[2].latest_attitude, attitude)
            self.assertAlmostEqual(segments[2].attitude_points[-1][1], 73.7398, places=3)
            self.assertFalse(list(writer.directory.glob("imu_[0-9]*.csv")))
            self.assertFalse(writer.submit("csv", (1,) * 10))
            self.assertEqual(rows[0], list(IMU_RAW_COLUMNS))
            self.assertEqual(
                rows[1],
                [
                    "2",
                    "17",
                    "123456",
                    "42",
                    "-1",
                    "2",
                    "3",
                    "4",
                    "5",
                    "6",
                    "16",
                    "2",
                    "2000",
                    "400",
                    "400",
                ],
            )
            self.assertEqual(
                (writer.directory / f"events_{stamp}.log").read_bytes(), b"[1][I][imu] ready\n"
            )
            self.assertEqual((writer.directory / f"data_{stamp}.hex").read_text(), "00 ff 10\n")
            self.assertEqual(
                bytes.fromhex((writer.directory / f"data_{stamp}.hex").read_text()), b"\x00\xff\x10"
            )

    def test_compact_timestamp_uses_local_time(self):
        from datetime import datetime, timedelta, timezone

        local_time = datetime(2026, 11, 13, 15, 15, 30, 123456, tzinfo=timezone(timedelta(hours=7)))
        with tempfile.TemporaryDirectory() as root, patch("session_writer.datetime") as clock:
            clock.now.return_value.astimezone.return_value = local_time
            writer = SessionWriter(root)
            self.finish(writer)
            self.assertEqual(writer.directory.name, "telemetry_20261113_151530")
            self.assertTrue((writer.directory / "imu_raw_20261113_151530.csv").is_file())

    def test_raw_hex_round_trip_across_chunks(self):
        with tempfile.TemporaryDirectory() as root:
            writer = SessionWriter(root, raw=True)
            expected = bytes(range(256))
            for chunk in (expected[:3], expected[3:71], expected[71:]):
                self.assertTrue(writer.submit("raw", chunk))
            self.finish(writer)
            text = next(writer.directory.glob("data_*.hex")).read_text()
            self.assertEqual(bytes.fromhex(text), expected)
            self.assertTrue(all(len(line.split()) <= 16 for line in text.splitlines()))

    def test_existing_session_never_overwritten(self):
        with tempfile.TemporaryDirectory() as root, patch("session_writer.datetime") as clock:
            clock.now.return_value.astimezone.return_value.strftime.return_value = "fixed"
            writer = SessionWriter(root)
            self.finish(writer)
            original = next(writer.directory.glob("imu_raw_*.csv")).read_bytes()
            second = SessionWriter(root)
            self.finish(second)
            self.assertEqual(second.directory.name, "telemetry_fixed_01")
            self.assertTrue((second.directory / "imu_raw_fixed_01.csv").is_file())
            self.assertEqual(next(writer.directory.glob("imu_raw_*.csv")).read_bytes(), original)
            self.assertFalse(any(writer.directory.glob("*.hex")))

    def test_queue_overflow_is_separate_from_transport(self):
        entered, release = Event(), Event()
        original = SessionWriter._write

        def blocked(writer, kind, value):
            entered.set()
            release.wait(3)
            original(writer, kind, value)

        with tempfile.TemporaryDirectory() as root, patch.object(SessionWriter, "_write", blocked):
            writer = SessionWriter(root, capacity=1)
            try:
                self.assertTrue(writer.submit("log", b"first\n"))
                self.assertTrue(entered.wait(2))
                self.assertTrue(writer.submit("log", b"second\n"))
                self.assertFalse(writer.submit("imu_raw", (1,) * 10))
                self.assertEqual(
                    writer.dropped,
                    {
                        "imu_raw": 1,
                        "stats": 0,
                        "rc": 0,
                        "rate": 0,
                        "attitude": 0,
                        "log": 0,
                        "raw": 0,
                    },
                )
            finally:
                release.set()
                self.finish(writer)
            self.assertEqual(writer.processed, 2)

    def test_write_error_stops_session_and_is_reported(self):
        with (
            tempfile.TemporaryDirectory() as root,
            patch.object(SessionWriter, "_write", side_effect=OSError("disk full")),
        ):
            writer = SessionWriter(root)
            writer.submit("log", b"test\n")
            self.assertTrue(writer.done.wait(2))
            self.assertIn("disk full", writer.error)
            self.assertFalse(writer.submit("log", b"late\n"))
            self.assertEqual(writer.processed, 0)

    def test_open_failure_and_invalid_capacity(self):
        with tempfile.TemporaryDirectory() as root:
            file = Path(root) / "file"
            file.write_text("keep")
            with self.assertRaises(OSError):
                SessionWriter(file)
            self.assertEqual(file.read_text(), "keep")
            with self.assertRaises(ValueError):
                SessionWriter(root, capacity=0)


if __name__ == "__main__":
    unittest.main()
