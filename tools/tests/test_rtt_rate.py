"""Cross-language rate preview fixture and recording checks."""

import csv
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "rtt"))
from records import Frames, RATE, RATE_FIELDS
from telemetry import TelemetryState
from session_writer import SessionWriter


class RateTests(unittest.TestCase):
    def fixture(self):
        return bytes.fromhex(
            (Path(__file__).resolve().parents[2] / "protocol/fixtures/rate_v1.hex").read_text()
        )

    def test_fixture_and_record(self):
        state = TelemetryState()
        frames = list(Frames().feed(self.fixture()))
        kind, values = state.feed(frames[0], 10)
        self.assertEqual(kind, "rate")
        self.assertEqual(values[:4], (1, 123456, 2000, 3))
        self.assertAlmostEqual(values[7], 0.1)
        self.assertEqual(values[13:17], (0.25, 0.5, 0.75, 1))
        self.assertIsNone(state.feed(frames[0], 11))
        with tempfile.TemporaryDirectory() as root:
            writer = SessionWriter(root)
            self.assertTrue(writer.submit(kind, (state.segment, *values)))
            writer.stop()
            self.assertTrue(writer.done.wait(3))
            writer.thread.join()
            self.assertIsNone(writer.error)
            with next(writer.directory.glob("rate_*.csv")).open() as file:
                rows = list(csv.reader(file))
            self.assertEqual(rows[0], ["segment", *RATE_FIELDS])
            self.assertEqual(len(rows), 2)
        state.new_connection()
        self.assertIsNone(state.latest_rate)

    def test_invalid_reset_and_crc(self):
        state = TelemetryState()
        frame = list(Frames().feed(self.fixture()))[0]
        values = list(RATE.unpack(frame[2]))
        for index, bad in ((2, 0), (3, 2), (7, float("nan")), (13, 1.1)):
            invalid = values.copy()
            invalid[index] = bad
            self.assertIsNone(state.feed((1, 5, RATE.pack(*invalid)), 1))
        self.assertIsNotNone(state.feed(frame, 1))
        values[0] = 0
        self.assertIsNotNone(state.feed((1, 5, RATE.pack(*values)), 2))
        self.assertEqual(state.segment, 1)
        broken = bytearray(self.fixture())
        broken[30] ^= 1
        self.assertEqual(list(Frames().feed(broken)), [])


if __name__ == "__main__":
    unittest.main()
