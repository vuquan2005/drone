"""Write bounded telemetry sessions independently of Qt and socket handling."""

import csv
from datetime import datetime
from pathlib import Path
from queue import Empty, Full, Queue
from threading import Event, Thread
import time

from records import RTT_STATS_FIELDS

COLUMNS = (
    "segment",
    "sequence",
    "t_read_us",
    "sensor_time_ticks",
    "ax_mmps2",
    "ay_mmps2",
    "az_mmps2",
    "gx_mradps",
    "gy_mradps",
    "gz_mradps",
)


STATS_COLUMNS = ("segment", *RTT_STATS_FIELDS)


class SessionWriter:
    """One producer, one disk worker. Stop drains accepted items, then flushes/closes.

    Submit never waits for disk. Queue overflow counts CSV rows, LOG lines and
    raw chunks separately. A disk error stops accepting items and exposes error;
    processed counts are not a durability guarantee. No per-record fsync.
    """

    def __init__(self, root, raw=False, capacity=4096):
        if capacity < 1:
            raise ValueError("Queue capacity must be positive")
        base_stamp = datetime.now().astimezone().strftime("%Y%m%d_%H%M%S")
        root = Path(root)
        root.mkdir(parents=True, exist_ok=True)
        suffix = 0
        while True:
            stamp = base_stamp if suffix == 0 else f"{base_stamp}_{suffix:02d}"
            self.directory = root / f"telemetry_{stamp}"
            try:
                self.directory.mkdir()
                break
            except FileExistsError:
                suffix += 1
        self.files = {}
        try:
            self.files["csv"] = (self.directory / f"imu_{stamp}.csv").open(
                "x", newline="", encoding="utf-8"
            )
            self.files["stats"] = (self.directory / f"rtt_stats_{stamp}.csv").open(
                "x", newline="", encoding="utf-8"
            )
            self.files["log"] = (self.directory / f"events_{stamp}.log").open("xb")
            if raw:
                self.files["raw"] = (self.directory / f"data_{stamp}.hex").open("xb")
            self.csv_writers = {kind: csv.writer(self.files[kind]) for kind in ("csv", "stats")}
            for kind, columns in (("csv", COLUMNS), ("stats", STATS_COLUMNS)):
                self.csv_writers[kind].writerow(columns)
                self.files[kind].flush()
        except OSError:
            for file in self.files.values():
                file.close()
            raise
        self.queue = Queue(maxsize=capacity)
        self.stopping = Event()
        self.done = Event()
        self.error = None
        self.accepted = 0
        self.processed = 0
        self.dropped = dict.fromkeys(("csv", "stats", "log", "raw"), 0)
        self.thread = Thread(target=self._run, name="telemetry-writer", daemon=True)
        self.thread.start()

    def submit(self, kind, value):
        if kind not in self.files or self.stopping.is_set() or self.error is not None:
            return False
        try:
            self.queue.put_nowait((kind, value))
        except Full:
            self.dropped[kind] += 1
            return False
        self.accepted += 1
        return True

    def stop(self):
        """Called by the producer after its final submit; completion is asynchronous."""
        self.stopping.set()

    def _write(self, kind, value):
        if kind in self.csv_writers:
            self.csv_writers[kind].writerow(value)
        elif kind == "raw":
            # Plain hex text, not Intel HEX; whitespace is not a frame boundary.
            if value:
                text = "\n".join(value[i : i + 16].hex(" ") for i in range(0, len(value), 16))
                self.files[kind].write((text + "\n").encode("ascii"))
        else:
            self.files[kind].write(value)

    def _flush(self):
        for file in self.files.values():
            file.flush()

    def _run(self):
        last_flush = time.monotonic()
        try:
            while not self.stopping.is_set() or not self.queue.empty():
                try:
                    kind, value = self.queue.get(timeout=0.1)
                except Empty:
                    pass
                else:
                    self._write(kind, value)
                    self.processed += 1
                if time.monotonic() - last_flush >= 1:
                    self._flush()
                    last_flush = time.monotonic()
            self._flush()
        except (OSError, csv.Error, UnicodeError) as error:
            self.error = str(error)
            self.stopping.set()
        finally:
            for file in self.files.values():
                try:
                    file.close()
                except OSError as error:
                    self.error = self.error or str(error)
            self.done.set()
