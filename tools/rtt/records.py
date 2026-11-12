"""Bounded text/binary framing and IMU session tracking; no GUI dependencies."""

from collections import deque
import struct
import zlib


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


MAGIC = b"DRT!"
HEADER = struct.Struct("<4sBBH")
IMU = struct.Struct("<III6i")
RTT_STATS = struct.Struct("<10I")
RTT_STATS_FIELDS = (
    "sequence",
    "uptime_ms",
    "imu_samples",
    "imu_read_errors",
    "rtt_log_full",
    "rtt_data_full",
    "imu_tx_full",
    "stats_tx_full",
    "imu_tx_rejected",
    "stats_tx_rejected",
)
KNOWN_SIZES = {1: IMU.size, 2: RTT_STATS.size}
MAX_PAYLOAD = 256


class Frames:
    """Yield CRC-validated (version, type, payload); retain at most 267 bytes."""

    def __init__(self):
        self.pending = bytearray()
        self.errors = 0

    def feed(self, data):
        self.pending.extend(data)
        while True:
            start = self.pending.find(MAGIC)
            if start < 0:
                # Preserve a possible split magic; all other bytes are discarded.
                self.pending[:] = self.pending[-3:]
                return
            del self.pending[:start]
            if len(self.pending) < HEADER.size:
                return
            _, version, kind, length = HEADER.unpack_from(self.pending)
            if length > MAX_PAYLOAD or (
                version == 1 and kind in KNOWN_SIZES and length != KNOWN_SIZES[kind]
            ):
                self.errors += 1
                del self.pending[0]
                continue
            size = HEADER.size + length + 4
            if len(self.pending) < size:
                return
            expected = struct.unpack_from("<I", self.pending, size - 4)[0]
            if zlib.crc32(self.pending[: size - 4]) != expected:
                self.errors += 1
                del self.pending[0]
                continue
            payload = bytes(self.pending[HEADER.size : size - 4])
            del self.pending[:size]
            yield version, kind, payload


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

    def feed(self, frame):
        version, kind, payload = frame
        if version != 1 or kind != 1:
            self.unknown += 1
            return False
        if len(payload) != IMU.size:
            self.invalid += 1
            return False
        values = IMU.unpack(payload)
        seq, stamp, sensor = values[:3]
        if sensor >= 2**24:
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
