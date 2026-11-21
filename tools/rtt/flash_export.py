"""Copy a read-only W25Q128FV export over OpenOCD RTT and unpack its journal."""

import argparse
from contextlib import ExitStack
import csv
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


def messages(chunks):
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


def receive_image(chunks, request, file, progress=None):
    """Reconstruct every page and validate the END manifest before returning."""
    started = False
    next_index = sent = checksum = 0
    for kind, message_id, index, payload in messages(chunks):
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
            write_erased(file, index - next_index)
            file.write(payload)
            checksum = zlib.crc32(payload, zlib.crc32(U32.pack(index), checksum))
            next_index = index + 1
            sent += 1
            if progress is not None and sent % 128 == 0:
                progress(sent)
        elif kind == END:
            pages, invalid, expected_crc = struct.unpack("<III", payload)
            if index != PAGE_COUNT or sent != pages or invalid > sent or checksum != expected_crc:
                raise ValueError("flash export count or CRC does not match END")
            write_erased(file, PAGE_COUNT - next_index)
            return {"pages_sent": sent, "invalid_pages": invalid, "stream_crc32": f"{checksum:08x}"}
    raise ValueError("connection ended before flash export END")


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


def export(host, control_port, data_port, timeout, output, *, cancel=None, progress=None):
    output.mkdir(parents=True, exist_ok=False)
    partial = output / "flash.bin.part"
    image_path = output / "flash.bin"
    request = secrets.randbits(32)
    connect_timeout = min(timeout, 5.0) if cancel is not None else timeout
    with (
        socket.create_connection((host, data_port), timeout=connect_timeout) as data,
        socket.create_connection((host, control_port), timeout=connect_timeout) as control,
        partial.open("xb") as image,
    ):
        if cancel is not None and cancel.is_set():
            raise InterruptedError("flash export cancelled")
        control.sendall(f"EXPORT{request:08x}".encode("ascii"))
        stats = receive_image(receive_chunks(data, timeout, cancel), request, image, progress)
        image.flush()
        os.fsync(image.fileno())
    partial.rename(image_path)
    with image_path.open("rb") as image:
        stats.update(decode_sessions(image, output))
    write_json(output / "export.json", stats)
    return stats


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="new directory for flash.bin and session CSVs")
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
        stats = export(args.host, args.control_port, args.data_port, args.timeout, args.output)
    except (OSError, ValueError) as error:
        print(f"Flash export failed: {error}", file=sys.stderr)
        return 1
    print(f"Saved 16 MiB to {args.output / 'flash.bin'} ({stats['sessions']} sessions)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
