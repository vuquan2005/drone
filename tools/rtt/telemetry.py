"""Telemetry validation and session state, independent of Qt and recording."""

from collections import deque
import math

from records import (
    ATTITUDE,
    ATTITUDE_CONTROL,
    ATTITUDE_CONTROL_COMPACT_TYPE,
    ImuSeries,
    RC,
    RC_COMPACT,
    RcCompact,
    RTT_STATS,
    RATE,
    RATE_COMPACT_TYPE,
    decode_compact_attitude_control,
    decode_compact_rate,
)


def attitude_degrees(q):
    w, x, y, z = q
    return (
        math.degrees(math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y))),
        math.degrees(math.asin(max(-1, min(1, 2 * (w * y - z * x))))),
        math.degrees(math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))),
    )


class TelemetryState:
    """Track mixed streams and return accepted records for display and capture.

    The caller supplies host monotonic seconds for freshness and receive rate.
    An IMU batch returns ("imu_batch", samples) with one raw CSV row per sample.
    Reconnects and suspected MCU resets start a new segment while preserving
    cumulative host counters. Snapshot counters and plots reset together.
    """

    def __init__(self):
        self.series = ImuSeries()
        self.arrivals = deque(maxlen=20000)
        self.segment = 0
        self.config = None
        self.latest_rate = self.rate_received_at = self.rate_last = None
        self.rate_points = deque(maxlen=500)
        self.rate_start = None
        self.reset_attitude()
        self.latest_rc = self.rc_last = self.rc_received_at = None
        self.latest_stats = None
        self.stats_last = None
        self.stats_received = 0
        self.stats_received_at = None

    def new_connection(self):
        self.config = None
        self.latest_rate = self.rate_received_at = self.rate_last = None
        self.rate_points = deque(maxlen=500)
        self.rate_start = None
        self.reset_attitude()
        self.latest_rc = self.rc_last = self.rc_received_at = None
        self.latest_stats = self.stats_last = self.stats_received_at = None
        self.segment += 1
        self.series.new_connection()
        self.arrivals.clear()

    def reset_attitude(self):
        self.latest_attitude = self.attitude_received_at = None
        self.attitude_points = deque(maxlen=250)
        self.attitude_start = None
        self.latest_attitude_control = self.attitude_control_received_at = None
        self.attitude_control_points = deque(maxlen=2000)
        self.attitude_control_elapsed_us = 0

    def feed_attitude_control(self, kind, payload, received_at):
        if kind == ATTITUDE_CONTROL_COMPACT_TYPE:
            values = decode_compact_attitude_control(payload)
        else:
            values = (
                ATTITUDE_CONTROL.unpack(payload) if len(payload) == ATTITUDE_CONTROL.size else None
            )
        if values is None:
            self.series.invalid += 1
            return
        seq, stamp, _, flags = values[:4]
        q = values[4:8]
        if (
            flags not in (0, 1)
            or not 0.99 <= sum(v * v for v in q) <= 1.01
            or not all(math.isfinite(v) for v in values[8:])
        ):
            self.series.invalid += 1
            return
        gap = False
        if self.latest_attitude_control is not None:
            ds = (seq - self.latest_attitude_control[0]) % 2**32
            dt = (stamp - self.latest_attitude_control[1]) % 2**32
            if ds >= 2**31 or dt >= 2**31:
                self.series.resets += 1
                self.new_connection()
            elif ds == 0:
                self.series.invalid += 1
                return
            else:
                self.attitude_control_elapsed_us += dt
                gap = ds > 1 or dt > 50000
        t = self.attitude_control_elapsed_us / 1e6
        roll, pitch, _ = attitude_degrees(q)
        target_roll, target_pitch = map(math.degrees, values[8:10])
        correction = values[10:] if flags else (math.nan,) * 4
        points = self.attitude_control_points
        if points and (gap or abs(roll - points[-1][2]) > 180):
            points.append((t, *((math.nan,) * 8)))
        points.append((t, target_roll, roll, target_pitch, pitch, *correction))
        self.latest_attitude_control = values
        self.attitude_control_received_at = received_at
        return "attitude_control", values

    def feed_rc(self, kind, payload, received_at):
        compact = kind == 8
        wire = RC_COMPACT if compact else RC
        if len(payload) != wire.size:
            self.series.invalid += 1
            return
        values = wire.unpack(payload)
        if compact:
            flags = values[-1]
            channels = values[3:-1]
            invalid_flags = (
                flags & 0x80
                or (flags != 0 and not flags & 1)
                or ((flags >> 3) & 3) == 3
                or ((flags >> 5) & 3) == 3
            )
        else:
            channels = values[4:]
            invalid_flags = values[3] > 1
        if invalid_flags or any(value > 2047 for value in channels):
            self.series.invalid += 1
            return
        if self.rc_last is not None:
            ds = (values[0] - self.rc_last[0]) % 2**32
            dt = (values[1] - self.rc_last[1]) % 2**32
            if ds >= 2**31 or dt >= 2**31:
                self.series.resets += 1
                self.new_connection()
            elif ds == 0:
                self.series.invalid += 1
                return
        self.rc_last = values[:2]
        self.latest_rc = RcCompact(*values) if compact else values
        self.rc_received_at = received_at
        return ("rc_compact" if compact else "rc"), values

    def feed(self, line, received_at):
        if line[0] == 1 and line[1] in (7, ATTITUDE_CONTROL_COMPACT_TYPE):
            return self.feed_attitude_control(line[1], line[2], received_at)
        if line[:2] == (1, 6):
            if len(line[2]) != ATTITUDE.size:
                self.series.invalid += 1
                return
            values = ATTITUDE.unpack(line[2])
            q = values[2:]
            if not 0.99 <= sum(v * v for v in q) <= 1.01:
                self.series.invalid += 1
                return
            gap = False
            if self.latest_attitude is not None:
                ds = (values[0] - self.latest_attitude[0]) % 2**32
                dt = (values[1] - self.latest_attitude[1]) % 2**32
                if ds >= 2**31 or dt >= 2**31:
                    self.series.resets += 1
                    self.new_connection()
                elif ds == 0:
                    self.series.invalid += 1
                    return
                else:
                    gap = ds > 1 or dt > 150000
            if self.attitude_start is None:
                self.attitude_start = received_at
            t = received_at - self.attitude_start
            angles = attitude_degrees(q)
            if self.attitude_points and (
                gap or any(abs(a - b) > 180 for a, b in zip(angles, self.attitude_points[-1][1:]))
            ):
                self.attitude_points.append((t, math.nan, math.nan, math.nan))
            self.attitude_points.append((t, *angles))
            self.latest_attitude = values
            self.attitude_received_at = received_at
            return "attitude", values

        if line[0] == 1 and line[1] in (5, RATE_COMPACT_TYPE):
            if line[1] == RATE_COMPACT_TYPE:
                values = decode_compact_rate(line[2])
            else:
                values = RATE.unpack(line[2]) if len(line[2]) == RATE.size else None
            if (
                values is None
                or values[3] not in (1, 3)
                or not all(math.isfinite(x) for x in values[4:])
                or (values[3] == 3 and not 0 < values[2] <= 20000)
                or any(abs(x) > 1 for x in values[10:13])
                or any(not 0 <= x <= 1 for x in values[13:17])
            ):
                self.series.invalid += 1
                return
            if self.rate_last is not None:
                ds = (values[0] - self.rate_last) % 2**32
                if ds == 0:
                    self.series.invalid += 1
                    return
                if ds >= 2**31:
                    self.series.resets += 1
                    self.new_connection()
            if self.rate_start is None:
                self.rate_start = received_at
            t = received_at - self.rate_start
            axes = values[4:] if values[3] & 2 else (float("nan"),) * 16
            self.rate_points.append((t, *axes))
            while self.rate_points and self.rate_points[0][0] < t - 10:
                self.rate_points.popleft()
            self.rate_last = values[0]
            self.latest_rate = values
            self.rate_received_at = received_at
            return "rate", values
        if line[0] == 1 and line[1] in (3, 8):
            return self.feed_rc(line[1], line[2], received_at)
        if line[0] == 1 and line[1] == 2:
            if len(line[2]) != RTT_STATS.size:
                self.series.invalid += 1
                return
            values = RTT_STATS.unpack(line[2])
            if self.stats_last is not None:
                ds = (values[0] - self.stats_last[0]) % 2**32
                dt = (values[1] - self.stats_last[1]) % 2**32
                if ds >= 2**31 or dt >= 2**31:
                    self.series.resets += 1
                    self.new_connection()
                elif ds == 0:
                    self.series.invalid += 1
                    return
            self.stats_last = values[:2]
            self.latest_stats = values
            self.stats_received_at = received_at
            self.stats_received += 1
            return "stats", values
        resets = self.series.resets
        samples = self.series.feed_samples(line)
        if samples:
            if self.series.resets != resets:
                self.config = None
                self.latest_rate = self.rate_received_at = self.rate_last = None
                self.rate_points.clear()
                self.reset_attitude()
                self.rate_start = None
                self.latest_rc = self.rc_last = self.rc_received_at = None
                self.segment += 1
                self.latest_stats = self.stats_last = self.stats_received_at = None
            self.arrivals.extend([received_at] * len(samples))
            return ("imu_batch", samples) if line[1] == 9 else ("imu_raw", samples[0])
