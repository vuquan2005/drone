"""Copy a read-only W25Q128FV export over OpenOCD RTT and unpack its journal."""

import argparse
from contextlib import ExitStack
import csv
from datetime import datetime
import json
import os
from pathlib import Path
import secrets
import socket
import struct
import sys
import time
import zlib

from config_record import CONFIG_TYPE, decode_config_reply
from records import (
    FLASH_HEADER as HEADER,
    FLASH_MAGIC as MAGIC,
    FLASH_PAYLOAD_SIZES as PAYLOAD_SIZES,
    Frames,
    HEADER as DRT_HEADER,
    RATE_FIELDS,
)
from session_writer import (
    ATTITUDE_COLUMNS,
    ATTITUDE_CONTROL_COLUMNS,
    IMU_RAW_COLUMNS,
    RC_COLUMNS,
    RC_COMPACT_COLUMNS,
    STATS_COLUMNS,
)
from telemetry import TelemetryState

JOURNAL = struct.Struct("<4sBBHIIII")
U32 = struct.Struct("<I")
BEGIN, PAGE, END, ERROR = range(1, 5)
PAGE_BYTES, PAGE_COUNT = PAYLOAD_SIZES[PAGE], 65536
ERASED_PAGE = b"\xff" * PAGE_BYTES
ERASED_BLOCK = ERASED_PAGE * 256
DEFAULT_EXPORT_ROOT = Path(__file__).resolve().parents[2] / "experiments/raw/export"
MAX_ATTEMPTS = 5
CSV_COLUMNS = {
    "imu_raw": IMU_RAW_COLUMNS,
    "stats": STATS_COLUMNS,
    "rc": RC_COLUMNS,
    "rc_compact": RC_COMPACT_COLUMNS,
    "rate": ("segment", *RATE_FIELDS),
    "attitude": ATTITUDE_COLUMNS,
    "attitude_control": ATTITUDE_CONTROL_COLUMNS,
}
CSV_NAMES = {"rc_compact": "receiver_compact", "stats": "rtt_stats"}


def messages(chunks, diagnostics=None):
    """Find complete CRC-valid export messages in arbitrary TCP chunks."""
    pending = bytearray()
    for chunk in chunks:
        pending.extend(chunk)
        while True:
            start = pending.find(MAGIC)
            if start < 0:
                pending[:] = pending[-(len(MAGIC) - 1) :]
                break
            del pending[:start]
            if len(pending) < HEADER.size:
                break
            _, version, kind, length, request, index = HEADER.unpack_from(pending)
            if version != 1 or PAYLOAD_SIZES.get(kind) != length:
                del pending[0]
                continue
            size = HEADER.size + length + U32.size
            if len(pending) < size:
                break
            if zlib.crc32(pending[: size - U32.size]) != U32.unpack_from(pending, size - 4)[0]:
                if diagnostics is not None:
                    diagnostics["crc_errors"] += 1
                del pending[0]
                continue
            payload = bytes(pending[HEADER.size : HEADER.size + length])
            del pending[:size]
            yield kind, request, index, payload


def write_erased(file, count):
    while count:
        pages = min(count, len(ERASED_BLOCK) // PAGE_BYTES)
        file.write(ERASED_BLOCK[: pages * PAGE_BYTES])
        count -= pages


def receive_pages(chunks, request, pages, diagnostics=None, progress=None):
    """Collect one attempt; return only after its END manifest is received."""
    started = False
    next_index = 0
    for kind, message_id, index, payload in messages(chunks, diagnostics):
        if message_id != request:
            continue
        if kind == BEGIN:
            if started or index != 0 or struct.unpack("<II", payload) != (PAGE_BYTES, PAGE_COUNT):
                raise ValueError("invalid flash export BEGIN")
            started = True
            if progress is not None:
                progress(0)
        elif not started:
            continue
        elif kind == ERROR:
            (code,) = U32.unpack(payload)
            raise ValueError(f"firmware flash error {code} at page {index}")
        elif kind == PAGE:
            if not next_index <= index < PAGE_COUNT or payload == ERASED_PAGE:
                raise ValueError(f"invalid or out-of-order flash page {index}")
            pages[index] = payload
            next_index = index + 1
            if progress is not None and len(pages) % 128 == 0:
                progress(len(pages))
        elif kind == END:
            sent, invalid, expected_crc = struct.unpack("<III", payload)
            if index != PAGE_COUNT or not len(pages) <= sent <= PAGE_COUNT or invalid > sent:
                raise ValueError("invalid flash export END counts")
            return sent, invalid, expected_crc
    raise EOFError("connection ended before flash export END")


def page_checksum(pages):
    checksum = 0
    for index, payload in sorted(pages.items()):
        checksum = zlib.crc32(payload, zlib.crc32(U32.pack(index), checksum))
    return checksum


def write_image(file, pages, complete):
    next_index = 0
    for index, payload in sorted(pages.items()):
        write_erased(file, index - next_index)
        file.write(payload)
        next_index = index + 1
    if complete:
        write_erased(file, PAGE_COUNT - next_index)


def journal_record(page, index):
    """Decode a DRJ! envelope and validate physical placement."""
    magic, version, kind, size, physical, session, uptime, checksum = JOURNAL.unpack_from(page)
    if magic != b"DRJ!" or version != 1 or physical != index or size > PAGE_BYTES - JOURNAL.size:
        return None
    payload = page[JOURNAL.size : JOURNAL.size + size]
    if zlib.crc32(payload, zlib.crc32(page[:20])) != checksum:
        return None
    if kind == BEGIN and size == 0 and session == index:
        return kind, session, uptime, payload
    if session >= index:
        return None
    if kind == END and size == 12 and U32.unpack_from(payload)[0] in (1, 2):
        return kind, session, uptime, payload
    if kind != PAGE or not payload:
        return None
    offset = 0
    while offset + DRT_HEADER.size + U32.size <= size:
        magic, _, _, length = DRT_HEADER.unpack_from(payload, offset)
        if magic != b"DRT!":
            return None
        offset += DRT_HEADER.size + length + U32.size
    if offset == size:
        return kind, session, uptime, payload
    return None


class FlightSession:
    """One journal session, with synchronous CSV output and bounded decoder state."""

    def __init__(self, root, session_id, uptime):
        self.directory = root / f"session_{session_id:05d}"
        self.directory.mkdir()
        self.files = ExitStack()
        self.writers = {}
        self.decoder = TelemetryState()
        self.decoder.new_connection()
        self.config = None
        self.info = {
            "session_id": session_id,
            "start_uptime_ms": uptime,
            "status": "interrupted",
            "data_pages": 0,
            "invalid_telemetry": 0,
        }

    def write_data(self, payload):
        self.info["data_pages"] += 1
        decoder = Frames()
        for frame in decoder.feed(payload):
            if frame[:2] == (1, CONFIG_TYPE):
                try:
                    _, self.config = decode_config_reply(frame[2])
                except ValueError:
                    self.info["invalid_telemetry"] += 1
                continue
            record = self.decoder.feed(frame, 0.0)
            if record is None:
                continue
            kind, values = record
            rows = values if kind == "imu_batch" else (values,)
            if kind == "imu_batch":
                kind = "imu_raw"
            if kind not in self.writers:
                name = CSV_NAMES.get(kind, kind)
                file = self.files.enter_context(
                    (self.directory / f"{name}_{self.info['session_id']:05d}.csv").open(
                        "x", newline="", encoding="utf-8"
                    )
                )
                writer = csv.writer(file)
                writer.writerow(CSV_COLUMNS[kind])
                self.writers[kind] = writer
            self.writers[kind].writerows((self.decoder.segment, *row) for row in rows)
        self.info["invalid_telemetry"] += decoder.errors + bool(decoder.pending)

    def finish(self, end_page=None):
        self.files.close()
        if end_page is not None:
            index, uptime, payload = end_page
            reason, written, dropped = struct.unpack("<III", payload)
            self.info.update(
                status="closed",
                end_page=index,
                end_uptime_ms=uptime,
                reason={1: "stopped", 2: "full"}[reason],
                written_frames=written,
                dropped_frames=dropped,
            )
        self.info["invalid_telemetry"] += self.decoder.series.invalid
        write_json(self.directory / "session.json", self.info)
        if self.config is not None:
            config = {"segment": 1, **self.config}
            (self.directory / "config.jsonl").write_text(
                json.dumps(config, allow_nan=False) + "\n", encoding="utf-8"
            )


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def decode_sessions(image, root):
    current = None
    orphan_pages = sessions = interrupted = 0
    try:
        for index in range(PAGE_COUNT):
            page = image.read(PAGE_BYTES)
            if len(page) != PAGE_BYTES:
                raise ValueError("flash image has incorrect length")
            if page == ERASED_PAGE:
                continue
            record = journal_record(page, index)
            if record is None:
                continue
            kind, session, uptime, payload = record
            if kind == BEGIN:
                if current:
                    current.finish()
                    interrupted += 1
                current = FlightSession(root, session, uptime)
                sessions += 1
            elif current is None or current.info["session_id"] != session:
                orphan_pages += 1
            elif kind == PAGE:
                current.write_data(payload)
            else:
                current.finish((index, uptime, payload))
                current = None
        if current:
            current.finish()
            interrupted += 1
    finally:
        if current:
            current.files.close()
    return {"sessions": sessions, "interrupted_sessions": interrupted, "orphan_pages": orphan_pages}


def receive_chunks(data, timeout, cancel):
    """Allow a GUI caller to interrupt an otherwise long idle wait."""
    if cancel is None:
        yield from iter(lambda: data.recv(16384), b"")
        return

    data.settimeout(min(timeout, 0.25))
    deadline = time.monotonic() + timeout
    while not cancel.is_set():
        try:
            chunk = data.recv(16384)
        except socket.timeout:
            if time.monotonic() >= deadline:
                raise TimeoutError("timed out waiting for flash export data") from None
            continue
        if not chunk:
            return
        deadline = time.monotonic() + timeout
        yield chunk
    raise InterruptedError("flash export cancelled")


def new_export_directory(root=DEFAULT_EXPORT_ROOT):
    stamp = datetime.now().astimezone().strftime("%Y%m%d_%H%M%S_%f")
    return root / f"flash_export_{stamp}"


def export(
    host,
    control_port,
    data_port,
    timeout,
    output,
    *,
    cancel=None,
    progress=None,
    attempts=MAX_ATTEMPTS,
):
    if attempts < 1:
        raise ValueError("attempts must be positive")
    output.mkdir(parents=True, exist_ok=False)
    partial = output / "flash.bin.part"
    image_path = output / "flash.bin"
    with (output / "export.log").open("x", encoding="utf-8", buffering=1) as log:

        def report(message):
            timestamp = datetime.now().astimezone().isoformat(timespec="seconds")
            log.write(f"{timestamp} {message}\n")

        report(
            f"start host={host} control_port={control_port} data_port={data_port} attempts={attempts}"
        )
        try:
            stats = download(
                host, control_port, data_port, timeout, partial, cancel, progress, attempts, report
            )
            partial.rename(image_path)
            with image_path.open("rb") as image:
                stats.update(decode_sessions(image, output))
            write_json(output / "export.json", stats)
        except (OSError, ValueError) as error:
            report(f"failed {error}")
            raise
        report(
            f"complete pages={stats['pages_sent']} crc={stats['stream_crc32']} sessions={stats['sessions']}"
        )
    return stats


def download(host, control_port, data_port, timeout, partial, cancel, progress, attempts, report):
    pages = {}
    manifest = None
    connect_timeout = min(timeout, 5.0) if cancel is not None else timeout
    with partial.open("xb") as image:
        try:
            for attempt in range(1, attempts + 1):
                if cancel is not None and cancel.is_set():
                    raise InterruptedError("flash export cancelled")
                if progress is not None:
                    progress(attempt, -1)
                request = secrets.randbits(32)
                received = {}
                diagnostics = {"crc_errors": 0}
                report(f"attempt={attempt} request={request:08x} requesting")
                try:
                    with (
                        socket.create_connection(
                            (host, data_port), timeout=connect_timeout
                        ) as data,
                        socket.create_connection(
                            (host, control_port), timeout=connect_timeout
                        ) as control,
                    ):
                        if cancel is not None and cancel.is_set():
                            raise InterruptedError("flash export cancelled")
                        control.sendall(f"EXPORT{request:08x}".encode("ascii"))
                        reply = receive_pages(
                            receive_chunks(data, timeout, cancel),
                            request,
                            received,
                            diagnostics,
                            None if progress is None else lambda count: progress(attempt, count),
                        )
                except InterruptedError:
                    raise
                except (OSError, EOFError) as error:
                    report(
                        f"attempt={attempt} incomplete received={len(received)} "
                        f"crc_errors={diagnostics['crc_errors']} error={error}"
                    )
                    failure = str(error)
                    continue
                if manifest is None:
                    manifest = reply
                elif manifest != reply:
                    report(f"attempt={attempt} previous_end={manifest} current_end={reply}")
                    raise ValueError("flash export END changed between attempts")
                for index, payload in received.items():
                    if pages.setdefault(index, payload) != payload:
                        raise ValueError(f"flash page {index} changed between attempts")
                expected_count, invalid, expected_crc = manifest
                checksum = page_checksum(pages)
                report(
                    f"attempt={attempt} received={len(received)} received_crc={page_checksum(received):08x} "
                    f"crc_errors={diagnostics['crc_errors']} recovered={len(pages)}/{expected_count} "
                    f"crc={checksum:08x} expected_crc={expected_crc:08x} invalid_pages={invalid}"
                )
                if len(pages) > expected_count:
                    raise ValueError("combined export exceeds the board's page count")
                if len(pages) == expected_count and checksum == expected_crc:
                    write_image(image, pages, complete=True)
                    image.flush()
                    os.fsync(image.fileno())
                    return {
                        "pages_sent": expected_count,
                        "invalid_pages": invalid,
                        "stream_crc32": f"{checksum:08x}",
                        "attempts": attempt,
                    }
                failure = (
                    f"flash export count or CRC does not match END "
                    f"({len(pages)}/{expected_count} pages, {checksum:08x}/{expected_crc:08x})"
                )
            raise ValueError(failure)
        except (OSError, ValueError):
            if image.tell() == 0:
                write_image(image, pages, complete=False)
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "output",
        type=Path,
        nargs="?",
        help="new directory (default: experiments/raw/export/flash_export_*)",
    )
    parser.add_argument("--attempts", type=int, default=MAX_ATTEMPTS, help="maximum read attempts")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--control-port", type=int, default=9090)
    parser.add_argument("--data-port", type=int, default=9091)
    parser.add_argument(
        "--timeout", type=float, default=180.0, help="socket idle timeout (seconds)"
    )
    args = parser.parse_args()
    if not 0 < args.timeout or not all(
        0 < port < 65536 for port in (args.control_port, args.data_port)
    ):
        parser.error("timeout must be positive and ports must be in 1..65535")
    try:
        output = args.output or new_export_directory()
        stats = export(
            args.host,
            args.control_port,
            args.data_port,
            args.timeout,
            output,
            attempts=args.attempts,
        )
    except (OSError, ValueError) as error:
        print(f"Flash export failed: {error} (log: {output / 'export.log'})", file=sys.stderr)
        return 1
    print(f"Saved 16 MiB to {output / 'flash.bin'} ({stats['sessions']} sessions)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
