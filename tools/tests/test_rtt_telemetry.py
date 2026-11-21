"""Mixed-stream validation and segment tests without Qt, TCP or a board."""

from pathlib import Path
import math
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "rtt"))
from records import (
    ATTITUDE_CONTROL,
    ATTITUDE_CONTROL_COMPACT,
    ATTITUDE_CONTROL_COMPACT_TYPE,
    IMU_RAW,
    RC,
    RTT_STATS,
)
from telemetry import TelemetryState


def imu(seq=1, stamp=1000):
    return 1, 4, IMU_RAW.pack(seq, stamp, 42, *([0] * 6), 16, 2, 2000, 400, 400)


def rc(seq=1, stamp=1000, connected=1, channel=992):
    return 1, 3, RC.pack(seq, stamp, stamp, connected, *([channel] * 16))


def stats(seq=1, stamp=1000):
    return 1, 2, RTT_STATS.pack(seq, stamp, *([0] * 8))


class TelemetryTests(unittest.TestCase):
    def test_attitude_control_uses_mcu_time_and_breaks_on_missing_or_invalid_samples(self):
        state = TelemetryState()

        def feed(seq, stamp, flags=1, rate=0.5, *, legacy=False):
            age = 32768 if legacy else 44
            values = (
                seq,
                stamp,
                (stamp - age) % 2**32,
                flags,
                1,
                0,
                0,
                0,
                0.25,
                0,
                0.125 if flags else 0,
                0,
                rate if flags else 0,
                0,
            )
            if legacy:
                frame = (1, 7, ATTITUDE_CONTROL.pack(*values))
            else:
                payload = ATTITUDE_CONTROL_COMPACT.pack(
                    seq, stamp, (flags << 15) | age, *values[4:]
                )
                frame = (1, ATTITUDE_CONTROL_COMPACT_TYPE, payload)
            result = state.feed(frame, 12.5)
            if result is not None:
                self.assertEqual(result[1], values)
            return result

        self.assertEqual(feed(1, 2**32 - 5000)[0], "attitude_control")
        self.assertEqual(feed(3, 20, legacy=True)[0], "attitude_control")
        self.assertEqual(feed(4, 30, flags=0)[0], "attitude_control")
        points = state.attitude_control_points
        self.assertAlmostEqual(points[-2][0], 0.005020)
        self.assertAlmostEqual(points[-1][1], math.degrees(0.25))
        self.assertEqual(points[-2][7], 0.5)
        self.assertTrue(math.isnan(points[-3][2]))
        self.assertTrue(math.isnan(points[-1][7]))
        self.assertIsNone(feed(4, 30))
        self.assertIsNone(feed(5, 35000, rate=math.inf))
        self.assertIsNone(state.feed((1, ATTITUDE_CONTROL_COMPACT_TYPE, b"short"), 12.5))
        self.assertEqual(state.series.invalid, 3)
        state.new_connection()
        self.assertIsNone(state.latest_attitude_control)
        self.assertFalse(state.attitude_control_points)

    def test_mixed_stream_routes_records_and_reconnect_clears_state(self):
        state = TelemetryState()
        state.new_connection()
        for frame, kind in ((imu(), "imu_raw"), (stats(), "stats"), (rc(), "rc")):
            self.assertEqual(state.feed(frame, 12.5)[0], kind)
        self.assertEqual(state.segment, 1)
        self.assertEqual(state.rc_received_at, 12.5)
        self.assertEqual(state.stats_received_at, 12.5)
        self.assertEqual(state.series.unknown, 0)
        state.new_connection()
        self.assertEqual(state.segment, 2)
        self.assertIsNone(state.latest_rc)
        self.assertIsNone(state.latest_stats)
        self.assertFalse(state.series.points)
        self.assertFalse(state.arrivals)
        self.assertEqual(state.stats_received, 1)
        self.assertEqual(state.series.received, 1)

    def test_reset_in_any_stream_clears_other_streams(self):
        for reset_frame in (imu, stats, rc):
            with self.subTest(stream=reset_frame.__name__):
                state = TelemetryState()
                state.new_connection()
                for frame in (imu(100, 100000), stats(100, 100000), rc(100, 100000)):
                    state.feed(frame, 1)
                state.feed(reset_frame(), 2)
                self.assertEqual(state.segment, 2)
                self.assertEqual(state.series.resets, 1)
                if reset_frame is not imu:
                    self.assertFalse(state.series.points)
                if reset_frame is not rc:
                    self.assertIsNone(state.latest_rc)
                if reset_frame is not stats:
                    self.assertIsNone(state.latest_stats)

    def test_snapshot_wrap_duplicate_and_invalid_payloads(self):
        state = TelemetryState()
        state.new_connection()
        for frame in (rc, stats):
            state.feed(frame(2**32 - 1, 2**32 - 100), 1)
            self.assertIsNotNone(state.feed(frame(0, 100), 2))
            self.assertIsNone(state.feed(frame(0, 100), 3))
        self.assertEqual(state.segment, 1)
        previous_rc = state.latest_rc
        for frame in (
            (1, 3, b"short"),
            rc(1, 150, connected=2),
            rc(1, 150, channel=2048),
            (1, 2, b"short"),
        ):
            self.assertIsNone(state.feed(frame, 4))
        self.assertEqual(state.latest_rc, previous_rc)
        self.assertEqual(state.rc_received_at, 2)
        self.assertEqual(state.series.invalid, 6)
        self.assertIsNone(state.feed((1, 99, b""), 4))
        self.assertEqual(state.series.unknown, 1)


if __name__ == "__main__":
    unittest.main()
