"""Cross-language rate preview fixture and recording checks."""

import csv
import math
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "rtt"))
from records import Frames, RATE, RATE_COMPACT, RATE_COMPACT_TYPE, RATE_FIELDS
from telemetry import TelemetryState
from session_writer import SessionWriter


class RateTests(unittest.TestCase):
    def fixture(self, name="rate_v1.hex"):
        return bytes.fromhex(
            (Path(__file__).resolve().parents[2] / "protocol/fixtures" / name).read_text()
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
        self.assertEqual(len(state.rate_points), 0)

    def test_compact_fixture_recovers_original_fields(self):
        raw = self.fixture("rate_compact_v1.hex")
        self.assertEqual(len(raw), 66)
        (frame,) = Frames().feed(raw)
        self.assertEqual(frame[:2], (1, RATE_COMPACT_TYPE))
        legacy = RATE.unpack(self.fixture()[8:-4])
        expected = (*legacy[:17], -15892 / 32768, -983 / 32768, 10486 / 32768)
        state = TelemetryState()
        self.assertEqual(state.feed(frame, 1), ("rate", expected))

    def test_compact_reset_and_signed_rounding(self):
        state = TelemetryState()

        def feed(seq, stamp, dt, output=(0, 0, 0), motors=(0, 0, 0, 0)):
            wire = RATE_COMPACT.pack(seq, stamp, dt, *([0] * 6), *output, *motors)
            return state.feed((1, RATE_COMPACT_TYPE, wire), seq)

        self.assertIsNone(feed(1, 123456, 20001))
        self.assertIsNone(feed(1, 123456, 2000, motors=(32769, 0, 0, 0)))
        self.assertIsNone(feed(1, 123456, 2000, output=(float("nan"), 0, 0)))
        _, reset = feed(1, 123456, 0)
        self.assertEqual(reset[:4], (1, 123456, 0, 1))
        self.assertTrue(all(math.isnan(value) for value in state.rate_points[-1][1:]))
        _, active = feed(2, 125456, 2000, (0.5 / 32768, -0.5 / 32768, 0), (0, 1, 0, 0))
        self.assertEqual(active[3], 3)
        self.assertEqual(active[-3:], (1 / 32768, -1 / 32768, 0))

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
