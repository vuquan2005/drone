"""Bounded text/binary framing and IMU session tracking; no GUI dependencies."""

from collections import deque
from typing import NamedTuple
import struct
import zlib
import math

from config_record import CONFIG_REPLY_BYTES, CONFIG_TYPE


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
IMU_RAW = struct.Struct("<III6hBBHHH")
IMU_BATCH_FIRST = struct.Struct("<II3sB6h")
IMU_BATCH_NEXT = struct.Struct("<HB6h")
IMU_BATCH_CAPACITY = 8
IMU_BATCH_CONFIG = (16, 2, 2000, 800, 800)
IMU_RAW_FIELDS = (
    "sequence",
    "t_read_us",
    "sensor_time_ticks",
    "ax_raw",
    "ay_raw",
    "az_raw",
    "gx_raw",
    "gy_raw",
    "gz_raw",
    "accel_range_g",
    "axis_map",
    "gyro_range_dps",
    "accel_odr_hz",
    "gyro_odr_hz",
)
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
RC = struct.Struct("<4I16H")
RC_FIELDS = (
    "sequence",
    "uptime_ms",
    "last_frame_ms",
    "connected",
    *(f"ch{i}_raw" for i in range(1, 17)),
)
RC_COMPACT = struct.Struct("<3I4HB")


class RcCompact(NamedTuple):
    sequence: int
    uptime_ms: int
    last_frame_ms: int
    ch1_raw: int
    ch2_raw: int
    ch3_raw: int
    ch4_raw: int
    flags: int


RATE = struct.Struct("<4I16f")
RATE_FIELDS = (
    "sequence",
    "t_sample_us",
    "dt_us",
    "flags",
    *(f"setpoint_{axis}_radps" for axis in ("roll", "pitch", "yaw")),
    *(f"gyro_{axis}_radps" for axis in ("roll", "pitch", "yaw")),
    *(f"pid_{axis}" for axis in ("roll", "pitch", "yaw")),
    *(f"motor_{i}_thrust_norm" for i in range(1, 5)),
    *(f"residual_{axis}" for axis in ("roll", "pitch", "yaw")),
)
ATTITUDE = struct.Struct("<II4f")
ATTITUDE_COLUMNS = ("segment", "sequence", "t_sample_us", "qw", "qx", "qy", "qz")
ATTITUDE_CONTROL = struct.Struct("<4I10f")
ATTITUDE_CONTROL_COLUMNS = (
    "segment",
    "sequence",
    "t_control_us",
    "t_estimate_us",
    "flags",
    "qw",
    "qx",
    "qy",
    "qz",
    "target_roll_rad",
    "target_pitch_rad",
    "tilt_error_x",
    "tilt_error_y",
    "rate_request_x_radps",
    "rate_request_y_radps",
)
KNOWN_SIZES = {
    2: RTT_STATS.size,
    3: RC.size,
    4: IMU_RAW.size,
    5: RATE.size,
    6: ATTITUDE.size,
    7: ATTITUDE_CONTROL.size,
    8: RC_COMPACT.size,
    CONFIG_TYPE: CONFIG_REPLY_BYTES,
}
MAX_PAYLOAD = 256
IMU_BATCH_SIZES = {
    IMU_BATCH_FIRST.size + i * IMU_BATCH_NEXT.size for i in range(IMU_BATCH_CAPACITY)
}


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
                version == 1
                and (
                    (kind in KNOWN_SIZES and length != KNOWN_SIZES[kind])
                    or (kind == 9 and length not in IMU_BATCH_SIZES)
                )
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


def imu_batch_samples(payload):
    """Return type 9 samples in the legacy raw CSV field order."""
    if len(payload) not in IMU_BATCH_SIZES:
        return ()
    seq, stamp, sensor_bytes, count, *axes = IMU_BATCH_FIRST.unpack_from(payload)
    tail = payload[IMU_BATCH_FIRST.size :]
    if count != 1 + len(tail) // IMU_BATCH_NEXT.size:
        return ()
    sensor = int.from_bytes(sensor_bytes, "little")
    samples = [(seq, stamp, sensor, *axes, *IMU_BATCH_CONFIG)]
    for read_delta, sensor_delta, *axes in IMU_BATCH_NEXT.iter_unpack(tail):
        seq = (seq + 1) % 2**32
        stamp = (stamp + read_delta) % 2**32
        sensor = (sensor + sensor_delta) % 2**24
        samples.append((seq, stamp, sensor, *axes, *IMU_BATCH_CONFIG))
    return tuple(samples)


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
        return bool(self.feed_samples(frame))

    def feed_samples(self, frame):
        """Accept an IMU frame and return the samples accepted for recording."""
        version, kind, payload = frame
        if version != 1 or kind not in (4, 9):
            self.unknown += 1
            return ()
        if kind == 9:
            samples = imu_batch_samples(payload)
        elif len(payload) == IMU_RAW.size:
            samples = (IMU_RAW.unpack(payload),)
        else:
            samples = ()
        if not samples or self.last == samples[-1][:2]:
            self.invalid += 1
            return ()
        if not self._append_sample(samples[0]):
            return ()
        for sample in samples[1:]:
            self._append_sample(sample)
        return samples

    def _append_sample(self, values):
        seq, stamp, sensor = values[:3]
        if sensor >= 2**24:
            self.invalid += 1
            return False
        accel_range, axis_map, gyro_range, accel_odr, gyro_odr = values[9:]
        if (
            accel_range not in (2, 4, 8, 16)
            or axis_map not in (1, 2)
            or gyro_range not in (125, 250, 500, 1000, 2000)
            or accel_odr == 0
            or gyro_odr == 0
        ):
            self.invalid += 1
            return False
        scales = (accel_range * 9.80665 / 32768,) * 3 + (gyro_range * math.pi / 180 / 32768,) * 3
        # Map 1 historically encoded sensor -> FLU [X,-Y,-Z].
        # Convert that mounting to FRD; map 2 describes the new mounting.
        signs = (1, 1, 1) if axis_map == 1 else (-1, 1, -1)
        axes = tuple(
            value * scale * sign for value, scale, sign in zip(values[3:9], scales, signs * 2)
        )
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
        self.points.append((t, *axes))
        while self.points and self.points[0][0] < t - self.seconds:
            self.points.popleft()
        self.last = (seq, stamp)
        self.received += 1
        return True
