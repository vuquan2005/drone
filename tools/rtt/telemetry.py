"""Telemetry validation and session state, independent of Qt and recording."""

from collections import deque

from records import IMU, ImuSeries, RC, RTT_STATS


class TelemetryState:
    """Track mixed streams; feed returns (CSV kind, values) only for accepted frames.

    The caller supplies host monotonic seconds for freshness and receive rate.
    Reconnects and suspected MCU resets start a new segment while preserving
    cumulative host counters. Snapshot counters and plots reset together.
    """

    def __init__(self):
        self.series = ImuSeries()
        self.arrivals = deque(maxlen=20000)
        self.segment = 0
        self.latest_rc = self.rc_last = self.rc_received_at = None
        self.latest_stats = None
        self.stats_last = None
        self.stats_received = 0
        self.stats_received_at = None

    def new_connection(self):
        self.latest_rc = self.rc_last = self.rc_received_at = None
        self.latest_stats = self.stats_last = self.stats_received_at = None
        self.segment += 1
        self.series.new_connection()
        self.arrivals.clear()

    def feed(self, line, received_at):
        if line[0] == 1 and line[1] == 3:
            if len(line[2]) != RC.size:
                self.series.invalid += 1
                return
            values = RC.unpack(line[2])
            if values[3] > 1 or any(value > 2047 for value in values[4:]):
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
            self.latest_rc = values
            self.rc_received_at = received_at
            return "rc", values
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
        if self.series.feed(line):
            if self.series.resets != resets:
                self.latest_rc = self.rc_last = self.rc_received_at = None
                self.segment += 1
                self.latest_stats = self.stats_last = self.stats_received_at = None
            self.arrivals.append(received_at)
            return "csv", IMU.unpack(line[2])
