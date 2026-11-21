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
from records import Frames, IMU_RAW, IMU_BATCH_FIRST, IMU_BATCH_NEXT, ImuSeries
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
    def test_shared_batch_fixture_and_fragmentation(self):
        path = Path(__file__).resolve().parents[2] / "protocol/fixtures/imu_batch_v1.hex"
        raw = bytes.fromhex(path.read_text())
        self.assertEqual(len(raw), 51)
        frame = (1, 9, raw[8:-4])
        for split in range(len(raw) + 1):
            framing = Frames()
            self.assertEqual(
                list(framing.feed(raw[:split])) + list(framing.feed(raw[split:])), [frame]
            )
        state = TelemetryState()
        kind, samples = state.feed(frame, 1)
        self.assertEqual(kind, "imu_batch")
        self.assertEqual(
            samples,
            (
                (1000, 1000000, 0x123456, -120, 35, 2048, 10, -20, 30, 16, 2, 2000, 800, 800),
                (1001, 1001250, 0x123476, -118, 37, 2050, 11, -18, 31, 16, 2, 2000, 800, 800),
            ),
        )
        self.assertEqual(len(state.arrivals), 2)
        self.assertEqual(state.series.received, 2)
        self.assertIsNone(state.feed(frame, 2))
        self.assertEqual(state.series.resets, 0)

    def test_batch_counter_wrap_missing_samples_and_invalid_count(self):
        base = IMU_BATCH_FIRST.pack(2**32 - 1, 2**32 - 500, b"\xf0\xff\xff", 2, *([0] * 6))
        payload = base + IMU_BATCH_NEXT.pack(1250, 255, *([1] * 6))
        stream = ImuSeries()
        samples = stream.feed_samples((1, 9, payload))
        self.assertEqual(samples[1][:3], (0, 750, 239))
        self.assertEqual(stream.points[-1][0], 0.00125)
        self.assertEqual(stream.missing, 0)
        bad_count = payload[:11] + b"\x09" + payload[12:]
        self.assertFalse(stream.feed((1, 9, bad_count)))
        self.assertFalse(stream.feed((1, 9, payload[:-1])))
        self.assertEqual(stream.invalid, 2)
        self.assertEqual(stream.received, 2)
        self.assertTrue(stream.feed(raw_record(seq=9, stamp=12000)))
        self.assertEqual(stream.missing, 8)

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

    def test_legacy_mount_to_frd_conversion_preserves_int16_extrema(self):
        stream = ImuSeries()
        stream.feed(raw_record(axes=(2048, -2048, -2048, 16384, -16384, -32768)))
        expected = (
            9.80665,
            -9.80665,
            -9.80665,
            math.radians(1000),
            -math.radians(1000),
            -math.radians(2000),
        )
        for actual, value in zip(stream.points[-1][1:], expected):
            self.assertAlmostEqual(actual, value)
        stream.feed(raw_record(18, 125956, axes=(1,) * 6, config=(2, 1, 125, 100, 200)))
        self.assertAlmostEqual(stream.points[-1][1], 2 * 9.80665 / 32768)
        self.assertAlmostEqual(stream.points[-1][4], math.radians(125) / 32768)

    def test_new_mount_frd_and_shared_encoder_fixture(self):
        expected = (
            16 * 9.80665,
            -16 * 9.80665 / 32768,
            -32767 * 16 * 9.80665 / 32768,
            -math.radians(2000) / 32768,
            -math.radians(2000),
            -32767 * math.radians(2000) / 32768,
        )
        fixtures = Path(__file__).resolve().parents[2] / "protocol/fixtures"
        for odr_hz, name in (
            (400, "imu_raw_frd_v1.hex"),
            (800, "imu_raw_frd_800hz_v1.hex"),
        ):
            with self.subTest(odr_hz=odr_hz):
                raw = bytes.fromhex((fixtures / name).read_text())
                frame = raw_record(config=(16, 2, 2000, odr_hz, odr_hz))
                self.assertEqual(list(Frames().feed(raw)), [frame])
                stream = ImuSeries()
                self.assertTrue(stream.feed(frame))
                for actual, value in zip(stream.points[-1][1:], expected):
                    self.assertAlmostEqual(actual, value)
        stream = ImuSeries()
        # A stationary upright board and the three hand motions in FRD.
        for seq, axes, expected_signs in (
            (18, (0, 0, 2048, -100, 0, 0), (1, 0, 0)),
            (19, (0, 0, 2048, 0, -100, 0), (0, -1, 0)),
            (20, (0, 0, 2048, 0, 0, 100), (0, 0, -1)),
        ):
            self.assertTrue(
                stream.feed(raw_record(seq, seq * 2500, axes=axes, config=(16, 2, 2000, 400, 400)))
            )
            self.assertAlmostEqual(stream.points[-1][3], -9.80665)
            for value, sign in zip(stream.points[-1][4:], expected_signs):
                self.assertEqual((value > 0) - (value < 0), sign)

    def test_invalid_metadata_does_not_advance_state(self):
        stream = ImuSeries()
        stream.feed(raw_record())
        for config in (
            (3, 1, 2000, 400, 400),
            (16, 3, 2000, 400, 400),
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
