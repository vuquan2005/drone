"""Read saved CSV sessions without Qt or modifying source files."""

from collections import deque
import csv
import json
from pathlib import Path
import struct

from records import (
    ATTITUDE,
    ATTITUDE_COLUMNS,
    ATTITUDE_CONTROL,
    ATTITUDE_CONTROL_COLUMNS,
    IMU_RAW,
    RATE,
    RC,
    RC_COMPACT,
    RTT_STATS,
)
from session_writer import (
    IMU_RAW_COLUMNS,
    RATE_FIELDS,
    RC_COLUMNS,
    RC_COMPACT_COLUMNS,
    STATS_COLUMNS,
)
from telemetry import TelemetryState

SCHEMAS = (
    ("imu_raw", IMU_RAW_COLUMNS, IMU_RAW, 4),
    ("rate", ("segment", *RATE_FIELDS), RATE, 5),
    ("attitude", ATTITUDE_COLUMNS, ATTITUDE, 6),
    ("attitude_control", ATTITUDE_CONTROL_COLUMNS, ATTITUDE_CONTROL, 7),
    ("rc", RC_COLUMNS, RC, 3),
    ("receiver_compact", RC_COMPACT_COLUMNS, RC_COMPACT, 8),
    ("rtt_stats", STATS_COLUMNS, RTT_STATS, 2),
)
FLOAT_START = {5: 4, 6: 2, 7: 4}


def read_flash_session_info(directory):
    path = Path(directory) / "session.json"
    try:
        info = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return None
    if (
        not isinstance(info, dict)
        or type(info.get("session_id")) is not int
        or info.get("status") not in ("closed", "interrupted")
        or type(info.get("data_pages")) is not int
    ):
        raise ValueError(f"{path}: unsupported flash session metadata")
    return info


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
        float_start = FLOAT_START.get(kind, len(columns))
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
                            float(value) if i >= float_start else int(value)
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
                        if kind == 7 and state.attitude_control_points.maxlen is not None:
                            state.attitude_control_points = deque(state.attitude_control_points)
                        # Rate CSV has MCU timestamps, unlike the live host-time plot.
                        if state.feed((1, kind, payload), elapsed / 1e6) is None:
                            raise ValueError("invalid telemetry payload")
                        if kind in (3, 8):
                            snapshots = state.rc_snapshots
                            rc_seconds = 0
                            if snapshots:
                                previous_seconds, previous_sample = snapshots[-1]
                                rc_seconds = (
                                    previous_seconds + (stamp - previous_sample[1]) % 2**32 / 1000
                                )
                            snapshots.append((rc_seconds, state.latest_rc))
                        if kind == 5:
                            point = state.rate_points[-1]
                            history = rate_history.setdefault(segment, deque())
                            if gap:
                                history.append((point[0], *([float("nan")] * 16)))
                            history.append(point)
                        previous[segment] = (elapsed, seq, last_stamp)
                    except (ValueError, OverflowError, struct.error) as error:
                        raise ValueError(f"{path.name}:{line_number}: {error}") from error
    config_path = directory / "config.jsonl"
    if config_path.is_file():
        with config_path.open(encoding="utf-8") as source:
            for line_number, line in enumerate(source, 1):
                try:
                    if len(line) > 8192 or line_number > 1_000_000:
                        raise ValueError("configuration record limit exceeded")
                    saved = json.loads(line)
                    if not isinstance(saved, dict) or saved.get("schema_version") != 1:
                        raise ValueError("unsupported CONFIG schema")
                    segment = saved.pop("segment", None)
                    if type(segment) is not int or segment < 1:
                        raise ValueError("invalid CONFIG segment")
                    if segment not in segments:
                        segments[segment] = TelemetryState()
                        segments[segment].rc_snapshots = []
                    if segments[segment].config is not None:
                        raise ValueError("duplicate CONFIG segment")
                    segments[segment].config = saved
                except ValueError as error:
                    raise ValueError(f"{config_path.name}:{line_number}: {error}") from error
    if not segments:
        if read_flash_session_info(directory) is None:
            raise ValueError("No telemetry rows found in this folder")
        state = segments[1] = TelemetryState()
        state.rc_snapshots = []
    for segment, points in rate_history.items():
        segments[segment].rate_points = points
    logs = deque(maxlen=2000)
    for path in sorted(directory.glob("events_*.log")):
        with path.open("rb") as source:
            for line in source:
                logs.append(line.rstrip(b"\r\n"))
    return segments, logs
