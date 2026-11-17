"""Raw IMU wire compatibility, scaling and lossless recording."""

import csv
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "rtt"))
from records import Frames, IMU_RAW, ImuSeries
from telemetry import TelemetryState
from session_writer import IMU_RAW_COLUMNS, SessionWriter


def raw_record(
    seq=17,
    stamp=123456,
    sensor=0x123456,
    axes=(-32768, -1, 32767, 1, -32768, 32767),
    config=(16, 1, 2000, 400, 400),
):
    return 1, 4, IMU_RAW.pack(seq, stamp, sensor, *axes, *config)


class RawImuTests(unittest.TestCase):
    def test_shared_fixture_and_fragmentation(self):
        raw = bytes.fromhex(
            (Path(__file__).resolve().parents[2] / "protocol/fixtures/imu_raw_v1.hex").read_text()
        )
        version, kind, payload = raw_record()
        encoded = struct.pack("<4sBBH", b"DRT!", version, kind, len(payload)) + payload
        self.assertEqual(raw, encoded + struct.pack("<I", zlib.crc32(encoded)))
        self.assertEqual(len(raw), 44)
        for split in range(len(raw) + 1):
            framing = Frames()
            self.assertEqual(
                list(framing.feed(raw[:split])) + list(framing.feed(raw[split:])), [raw_record()]
            )
        for offset in (6, 24, 43):
            broken = bytearray(raw)
            broken[offset] ^= 1
            self.assertEqual(list(Frames().feed(broken + raw)), [raw_record()])

    def test_si_flu_conversion_preserves_int16_extrema(self):
        stream = ImuSeries()
        stream.feed(raw_record(axes=(2048, -2048, -2048, 16384, -16384, -32768)))
        expected = (
            9.80665,
            9.80665,
            9.80665,
            math.radians(1000),
            math.radians(1000),
            math.radians(2000),
        )
        for actual, value in zip(stream.points[-1][1:], expected):
            self.assertAlmostEqual(actual, value)
        stream.feed(raw_record(18, 125956, axes=(1,) * 6, config=(2, 1, 125, 100, 200)))
        self.assertAlmostEqual(stream.points[-1][1], 2 * 9.80665 / 32768)
        self.assertAlmostEqual(stream.points[-1][4], math.radians(125) / 32768)

    def test_invalid_metadata_does_not_advance_state(self):
        stream = ImuSeries()
        stream.feed(raw_record())
        for config in (
            (3, 1, 2000, 400, 400),
            (16, 2, 2000, 400, 400),
            (16, 1, 123, 400, 400),
            (16, 1, 2000, 0, 400),
            (16, 1, 2000, 400, 0),
        ):
            self.assertFalse(stream.feed(raw_record(18, config=config)))
        self.assertFalse(stream.feed(raw_record(18, sensor=2**24)))
        self.assertFalse(stream.feed((1, 4, b"short")))
        self.assertEqual(stream.received, 1)
        self.assertEqual(stream.invalid, 7)
        self.assertTrue(stream.feed(raw_record(18, 125956)))
        self.assertEqual(stream.missing, 0)

    def test_raw_csv_keeps_counts_and_metadata_through_reset(self):
        state = TelemetryState()
        state.new_connection()
        frame = raw_record()
        kind, values = state.feed(frame, 1)
        self.assertEqual(kind, "imu_raw")
        self.assertEqual(values, IMU_RAW.unpack(frame[2]))
        with tempfile.TemporaryDirectory() as root:
            writer = SessionWriter(root)
            try:
                self.assertTrue(writer.submit(kind, (state.segment, *values)))
            finally:
                writer.stop()
                self.assertTrue(writer.done.wait(3))
                writer.thread.join()
            self.assertIsNone(writer.error)
            with next(writer.directory.glob("imu_raw_*.csv")).open(newline="") as file:
                rows = list(csv.reader(file))
            self.assertEqual(rows[0], list(IMU_RAW_COLUMNS))
            self.assertEqual(tuple(map(int, rows[1])), (1, *values))
        self.assertIsNone(state.feed(frame, 2))  # Duplicate.
        self.assertIsNotNone(state.feed(raw_record(1, 100), 3))
        self.assertEqual(state.segment, 2)
        self.assertEqual(state.series.resets, 1)


if __name__ == "__main__":
    unittest.main()
