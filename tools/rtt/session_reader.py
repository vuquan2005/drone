"""Read saved CSV sessions without Qt or modifying source files."""

from collections import deque
import csv
from pathlib import Path
import struct

from records import ATTITUDE, ATTITUDE_COLUMNS, IMU_RAW, RATE, RC, RTT_STATS
from session_writer import IMU_RAW_COLUMNS, RATE_FIELDS, RC_COLUMNS, STATS_COLUMNS
from telemetry import TelemetryState

SCHEMAS = (
    ("imu_raw", IMU_RAW_COLUMNS, IMU_RAW, 4),
    ("rate", ("segment", *RATE_FIELDS), RATE, 5),
    ("attitude", ATTITUDE_COLUMNS, ATTITUDE, 6),
    ("rc", RC_COLUMNS, RC, 3),
    ("rtt_stats", STATS_COLUMNS, RTT_STATS, 2),
)


def read_session(directory):
    """Return segment snapshots and session-wide log tail; reject malformed rows.

    Each stream keeps its own MCU time origin. No cross-stream alignment is
    inferred. A finite row budget prevents accidentally exhausting host memory.
    """
    directory = Path(directory)
    segments = {}
    rate_history = {}
    rows_read = 0
    for prefix, columns, wire, kind in SCHEMAS:
        # Match only the writer's timestamp suffix.
        paths = sorted(directory.glob(f"{prefix}_[0-9]*.csv"))
        for path in paths:
            previous = {}
            with path.open(newline="", encoding="utf-8") as source:
                reader = csv.reader(source)
                if next(reader, None) != list(columns):
                    raise ValueError(f"{path.name}: unsupported CSV header")
                for line_number, row in enumerate(reader, 2):
                    rows_read += 1
                    if rows_read > 1_000_000:
                        raise ValueError("Session exceeds the 1,000,000-row viewing limit")
                    try:
                        if len(row) != len(columns):
                            raise ValueError("wrong column count")
                        segment = int(row[0])
                        if segment < 1:
                            raise ValueError("invalid segment")
                        values = [
                            (
                                float(value)
                                if (kind == 5 and i >= 4) or (kind == 6 and i >= 2)
                                else int(value)
                            )
                            for i, value in enumerate(row[1:])
                        ]
                        payload = wire.pack(*values)
                        if segment not in segments:
                            segments[segment] = TelemetryState()
                            segments[segment].rc_snapshots = []
                        state = segments[segment]
                        state.series.seconds = float("inf")
                        if state.series.points.maxlen is not None:
                            state.series.points = deque(state.series.points)
                        seq, stamp = values[:2]
                        # A reset Rate frame has no sample timestamp. Keep it as a
                        # plot break at the last known time until a timestamp arrives.
                        if kind == 5 and values[1:4] == [0, 0, 1]:
                            stamp = None
                        elapsed, last_seq, last_stamp = previous.get(segment, (0, None, None))
                        gap = False
                        if last_seq is not None:
                            ds = (seq - last_seq) % 2**32
                            if ds == 0 or ds >= 2**31:
                                raise ValueError("nonmonotonic stream within segment")
                            gap = ds > 1
                        if stamp is not None:
                            if last_stamp is not None:
                                dt = (stamp - last_stamp) % 2**32
                                if dt >= 2**31:
                                    raise ValueError("nonmonotonic stream within segment")
                                elapsed += dt
                            last_stamp = stamp
                        if kind == 6 and state.attitude_points.maxlen is not None:
                            state.attitude_points = deque(state.attitude_points)
                        # Rate CSV has MCU timestamps, unlike the live host-time plot.
                        if state.feed((1, kind, payload), elapsed / 1e6) is None:
                            raise ValueError("invalid telemetry payload")
                        if kind == 3:
                            state.rc_snapshots.append((elapsed / 1000, state.latest_rc))
                        if kind == 5:
                            point = state.rate_points[-1]
                            history = rate_history.setdefault(segment, deque())
                            if gap:
                                history.append((point[0], *([float("nan")] * 16)))
                            history.append(point)
                        previous[segment] = (elapsed, seq, last_stamp)
                    except (ValueError, OverflowError, struct.error) as error:
                        raise ValueError(f"{path.name}:{line_number}: {error}") from error
    if not segments:
        raise ValueError("No telemetry rows found in this folder")
    for segment, points in rate_history.items():
        segments[segment].rate_points = points
    logs = deque(maxlen=2000)
    for path in sorted(directory.glob("events_*.log")):
        with path.open("rb") as source:
            for line in source:
                logs.append(line.rstrip(b"\r\n"))
    return segments, logs
