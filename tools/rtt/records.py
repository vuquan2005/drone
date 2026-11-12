"""Bounded newline framing and IMU1 session tracking; no GUI dependencies."""

from collections import deque


class Lines:
    """Discard oversized records through newline, never parse their suffix."""

    def __init__(self, limit=1024):
        self.limit = limit
        self.pending = bytearray()
        self.discarding = False
        self.errors = 0

    def feed(self, data):
        # Iterate chunks delimited by LF, including a possibly incomplete tail.
        parts = data.split(b"\n")
        for index, part in enumerate(parts):
            complete = index < len(parts) - 1
            if not self.discarding:
                if len(self.pending) + len(part) > self.limit:
                    self.pending.clear()
                    self.discarding = True
                    self.errors += 1
                else:
                    self.pending.extend(part)
            if complete:
                if not self.discarding:
                    yield bytes(self.pending).rstrip(b"\r")
                self.pending.clear()
                self.discarding = False


class ImuSeries:
    """Track one connection's IMU stream; counter gaps are not RTT drop attribution."""

    def __init__(self, seconds=10, capacity=20000):
        self.seconds = seconds
        self.points = deque(maxlen=capacity)
        self.received = self.missing = self.invalid = self.unknown = self.resets = 0
        self.last = None
        self.elapsed_us = 0

    def new_connection(self):
        self.last = None
        self.elapsed_us = 0
        self.points.clear()

    def feed(self, line):
        fields = line.split(b",")
        if fields[0] != b"IMU1":
            self.unknown += 1
            return False
        try:
            if len(fields) != 10:
                raise ValueError
            values = [int(field) for field in fields[1:]]
            seq, stamp, sensor = values[:3]
            if not (0 <= seq < 2**32 and 0 <= stamp < 2**32 and 0 <= sensor < 2**24):
                raise ValueError
            if any(not -(2**31) <= value < 2**31 for value in values[3:]):
                raise ValueError
        except ValueError:
            self.invalid += 1
            return False
        gap = False
        if self.last is not None:
            delta_seq = (seq - self.last[0]) % 2**32
            delta_us = (stamp - self.last[1]) % 2**32
            if delta_seq == 0:
                self.invalid += 1
                return False
            if delta_seq >= 2**31 or delta_us >= 2**31:
                # Backwards counters suggest reboot, rather than billions of drops.
                self.resets += 1
                self.new_connection()
            else:
                self.missing += delta_seq - 1
                gap = delta_seq > 1
                self.elapsed_us += delta_us
        t = self.elapsed_us / 1e6
        if gap:
            self.points.append((t, *([float("nan")] * 6)))
        self.points.append((t, *(value / 1000 for value in values[3:])))
        while self.points and self.points[0][0] < t - self.seconds:
            self.points.popleft()
        self.last = (seq, stamp)
        self.received += 1
        return True
