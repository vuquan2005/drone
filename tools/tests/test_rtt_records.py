"""Protocol tests independent of Qt, OpenOCD and hardware."""

import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from rtt_records import ImuSeries, Lines


def record(seq=1, stamp=1000):
    return f"IMU1,{seq},{stamp},42,1000,-2000,9800,100,-200,300".encode()


class RecordTests(unittest.TestCase):
    def test_fragmented_combined_and_oversize_lines(self):
        framing = Lines(limit=8)
        self.assertEqual(list(framing.feed(b"ab")), [])
        self.assertEqual(list(framing.feed(b"c\r\nx\ny")), [b"abc", b"x"])
        self.assertEqual(list(framing.feed(b"123456789")), [])
        self.assertEqual(list(framing.feed(b"suffix\nok\n")), [b"ok"])
        self.assertEqual(framing.errors, 1)
        self.assertEqual(list(framing.feed(b"1\n2\n")), [b"1", b"2"])

    def test_units_gaps_and_bounded_history(self):
        stream = ImuSeries(seconds=10, capacity=10)
        self.assertTrue(stream.feed(record()))
        self.assertEqual(stream.points[-1], (0, 1, -2, 9.8, 0.1, -0.2, 0.3))
        stream.feed(record(4, 2000))
        self.assertEqual(stream.missing, 2)
        self.assertTrue(math.isnan(stream.points[-2][1]))
        self.assertEqual(stream.points[-1][0], 0.001)
        for seq in range(5, 30):
            stream.feed(record(seq, seq * 1000000))
        self.assertLessEqual(len(stream.points), 10)
        self.assertLessEqual(stream.points[-1][0] - stream.points[0][0], 10)

    def test_wrap_reset_and_reconnect(self):
        stream = ImuSeries()
        stream.feed(record(2**32 - 1, 2**32 - 100))
        stream.feed(record(0, 100))
        self.assertEqual(stream.points[-1][0], 0.0002)
        self.assertEqual(stream.missing, 0)
        stream.feed(record(1, 2600))
        stream.feed(record(1, 2600))  # Duplicate, not a reboot or new sample.
        self.assertEqual(stream.invalid, 1)
        stream.feed(record(0, 100))
        self.assertEqual(stream.resets, 1)
        self.assertEqual(len(stream.points), 1)
        self.assertEqual(stream.points[-1][0], 0)
        stream.new_connection()
        stream.feed(record(500, 1000000))
        self.assertEqual(stream.missing, 0)
        self.assertEqual(stream.points[-1][0], 0)

    def test_invalid_and_unknown_are_separate(self):
        stream = ImuSeries()
        for line in (b"IMU1,1", record(-1), record(stamp=2**32), record() + b",extra"):
            self.assertFalse(stream.feed(line))
        self.assertFalse(stream.feed(b"ATT1,1,2"))
        self.assertEqual(stream.invalid, 4)
        self.assertEqual(stream.unknown, 1)
        self.assertEqual(stream.received, 0)


if __name__ == "__main__":
    unittest.main()
