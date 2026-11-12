"""Protocol tests independent of Qt, OpenOCD and hardware."""

import math
from pathlib import Path
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "rtt"))
from records import Frames, ImuSeries, Lines, RTT_STATS, RTT_STATS_FIELDS


def record(seq=1, stamp=1000, sensor=42, axes=(1000, -2000, 9800, 100, -200, 300)):
    return 1, 1, struct.pack("<III6i", seq, stamp, sensor, *axes)


def packet(seq=1, stamp=1000, sensor=42, axes=(1000, -2000, 9800, 100, -200, 300)):
    version, kind, payload = record(seq, stamp, sensor, axes)
    raw = struct.pack("<4sBBH", b"DRT!", version, kind, len(payload)) + payload
    return raw + struct.pack("<I", zlib.crc32(raw))


class RecordTests(unittest.TestCase):
    def test_text_lines_remain_independent(self):
        framing = Lines(limit=8)
        self.assertEqual(list(framing.feed(b"ab")), [])
        self.assertEqual(list(framing.feed(b"c\r\nx\ny")), [b"abc", b"x"])
        self.assertEqual(list(framing.feed(b"123456789")), [])
        self.assertEqual(list(framing.feed(b"suffix\nok\n")), [b"ok"])
        self.assertEqual(framing.errors, 1)

    def test_shared_golden_frame(self):
        raw = bytes.fromhex(
            (Path(__file__).resolve().parents[2] / "protocol/fixtures/imu_v1.hex").read_text()
        )
        expected = record(17, 123456, 0x123456, (1250, -2500, 9806, 1000, -2000, 3000))
        self.assertEqual(len(raw), 48)
        self.assertEqual(list(Frames().feed(raw)), [expected])
        self.assertEqual(packet(17, 123456, 0x123456, (1250, -2500, 9806, 1000, -2000, 3000)), raw)
        self.assertEqual(zlib.crc32(b"123456789"), 0xCBF43926)

    def test_shared_stats_fixture_and_mixed_stream(self):
        raw = bytes.fromhex(
            (Path(__file__).resolve().parents[2] / "protocol/fixtures/rtt_stats_v1.hex").read_text()
        )
        values = (1, 1234, 17, 3, 0, 0, 0, 0, 0, 0)
        self.assertEqual(len(raw), 52)
        decoded = list(Frames().feed(packet() + raw + packet(2)))
        self.assertEqual([frame[1] for frame in decoded], [1, 2, 1])
        self.assertEqual(RTT_STATS.unpack(decoded[1][2]), values)
        self.assertEqual(len(RTT_STATS_FIELDS), len(values))
        for offset in range(len(raw) + 1):
            framing = Frames()
            self.assertEqual(
                list(framing.feed(raw[:offset])) + list(framing.feed(raw[offset:])),
                [(1, 2, RTT_STATS.pack(*values))],
            )
        bad = bytearray(raw)
        bad[6] = 39
        self.assertEqual(list(Frames().feed(bad + raw)), [(1, 2, RTT_STATS.pack(*values))])

    def test_binary_fragmentation_and_midstream_join(self):
        raw = packet()
        for split in range(len(raw) + 1):
            framing = Frames()
            self.assertEqual(
                list(framing.feed(raw[:split])) + list(framing.feed(raw[split:])), [record()]
            )
        for offset in range(1, len(raw)):
            self.assertEqual(list(Frames().feed(raw[offset:] + packet(2))), [record(2)])
        framing = Frames()
        result = []
        for byte in raw + packet(2):
            result.extend(framing.feed(bytes([byte])))
        self.assertEqual(result, [record(), record(2)])

    def test_corruption_length_and_recovery(self):
        raw = packet()
        bad_crc = bytearray(raw)
        bad_crc[24] ^= 0x80
        bad_length = bytearray(raw)
        bad_length[6:8] = b"\xff\xff"
        wrong_imu_size = bytearray(raw)
        wrong_imu_size[6] = 35
        for bad in (bad_crc, bad_length, wrong_imu_size, raw[:20]):
            framing = Frames()
            result = list(framing.feed(b"noise" + bad + raw))
            self.assertEqual(result, [record()])
        framing = Frames()
        list(framing.feed(b"x" * 100000))
        self.assertLessEqual(len(framing.pending), 3)
        # Unsupported types/versions are framed correctly and counted separately.
        for version, kind in ((1, 3), (2, 1)):
            unknown = struct.pack("<4sBBH", b"DRT!", version, kind, 0)
            unknown += struct.pack("<I", zlib.crc32(unknown))
            series = ImuSeries()
            series.feed(list(Frames().feed(unknown))[0])
            self.assertEqual(series.unknown, 1)

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
        stream.feed(record(1, 2600))
        self.assertEqual(stream.invalid, 1)
        stream.feed(record(0, 100))
        self.assertEqual(stream.resets, 1)
        self.assertEqual(stream.points[-1][0], 0)
        stream.new_connection()
        stream.feed(record(500, 1000000))
        self.assertEqual(stream.missing, 0)

    def test_invalid_payload_and_signed_boundaries(self):
        stream = ImuSeries()
        self.assertFalse(stream.feed((1, 1, b"short")))
        self.assertFalse(stream.feed(record(sensor=2**24)))
        self.assertTrue(stream.feed(record(axes=(-(2**31), 2**31 - 1, 0, 0, 0, 0))))
        self.assertEqual(stream.invalid, 2)


if __name__ == "__main__":
    unittest.main()
