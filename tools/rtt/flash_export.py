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
BEGIN, PAGE, END, ERROR, ERASE_STATUS, SESSION = range(1, 7)
ERASE_STARTED, ERASE_VERIFIED, ERASE_FAILED = range(3)
ERASE_ACK_TIMEOUT_S = 10.0
ERASE_TIMEOUT_S = 300.0
PAGE_BYTES, PAGE_COUNT = PAYLOAD_SIZES[PAGE], 65536
ERASED_PAGE = b"\xff" * PAGE_BYTES
ERASED_BLOCK = ERASED_PAGE * 256
DEFAULT_EXPORT_ROOT = Path(__file__).resolve().parents[2] / "experiments/raw/export"
MAX_ATTEMPTS = 5
BEGIN_TIMEOUT_S = 15.0
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
        if diagnostics is not None:
            diagnostics["bytes_received"] = diagnostics.get("bytes_received", 0) + len(chunk)
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
            continue
        if kind == ERROR:
            (code,) = U32.unpack(payload)
            raise ValueError(f"firmware flash error {code} at page {index}")
        if not started:
            if kind == END:
                raise EOFError("received matching END before BEGIN")
            continue
        if kind == PAGE:
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


def receive_session_list(chunks, request, on_begin):
    sessions = []
    started = False
    upper = PAGE_COUNT
    checksum = 0
    for kind, message_id, index, payload in messages(chunks):
        if message_id != request:
            continue
        if kind == ERROR:
            raise ValueError(f"firmware flash error {U32.unpack(payload)[0]} at page {index}")
        if kind == BEGIN:
            if started or index != 0 or struct.unpack("<II", payload) != (PAGE_BYTES, PAGE_COUNT):
                raise ValueError("invalid session list BEGIN")
            started = True
            on_begin()
        elif started and kind == SESSION:
            last, uptime, complete = struct.unpack("<III", payload)
            if not index <= last < upper or complete not in (0, 1):
                raise ValueError("invalid or overlapping flash session range")
            upper = index
            checksum = zlib.crc32(payload, zlib.crc32(U32.pack(index), checksum))
            sessions.append((index, last, uptime, bool(complete)))
        elif started and kind == END:
            if index != PAGE_COUNT or struct.unpack("<III", payload) != (
                len(sessions),
                0,
                checksum,
            ):
                raise ValueError("incomplete or corrupt session list")
            return sessions
    raise EOFError("connection ended before session list END")


def list_sessions(host, control_port, data_port, cancel):
    request = secrets.randbits(32)
    started = False

    def on_begin():
        nonlocal started
        started = True

    with (
        socket.create_connection((host, data_port), timeout=5.0) as data,
        socket.create_connection((host, control_port), timeout=5.0) as control,
    ):
        if cancel.is_set():
            raise InterruptedError("session scan cancelled")
        control.sendall(f"ELISTS{request:08x}".encode("ascii"))
        try:
            return receive_session_list(
                receive_chunks(data, 180.0, cancel, lambda: started), request, on_begin
            )
        except TimeoutError as error:
            if started:
                raise
            raise TimeoutError(
                "No session list BEGIN. Turn off the transmitter, reset STM32, "
                "restart OpenOCD, and scan again."
            ) from error


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


def receive_chunks(data, timeout, cancel, begin_received):
    """Allow a GUI caller to interrupt an otherwise long idle wait."""
    if cancel is None:
        yield from iter(lambda: data.recv(16384), b"")
        return

    data.settimeout(min(timeout, 0.25))
    deadline = time.monotonic() + timeout
    begin_deadline = time.monotonic() + min(timeout, BEGIN_TIMEOUT_S)
    while not cancel.is_set():
        if not begin_received() and time.monotonic() >= begin_deadline:
            raise TimeoutError("timed out waiting for matching flash export BEGIN")
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


def write_log_line(log, message):
    timestamp = datetime.now().astimezone().isoformat(timespec="seconds")
    log.write(f"{timestamp} {message}\n")


def verify_saved_image(path, stats):
    checksum = count = size = 0
    with path.open("rb") as image:
        for index in range(PAGE_COUNT):
            page = image.read(PAGE_BYTES)
            size += len(page)
            if len(page) != PAGE_BYTES:
                break
            if page != ERASED_PAGE:
                checksum = zlib.crc32(page, zlib.crc32(U32.pack(index), checksum))
                count += 1
        if (
            size != PAGE_COUNT * PAGE_BYTES
            or image.read(1)
            or count != stats["pages_sent"]
            or f"{checksum:08x}" != stats["stream_crc32"]
        ):
            raise ValueError("flash.bin đã thay đổi hoặc không đầy đủ.")


def erase(host, control_port, data_port, directory, stats, cancel):
    """Erase the chip only for the request that produced the saved image."""
    request = stats["request_id"]
    log_error = None

    def report(event, detail=""):
        nonlocal log_error
        message = f"{event} request={request:08x} {detail}".rstrip()
        try:
            with (directory / "export.log").open("a", encoding="utf-8") as log:
                write_log_line(log, message)
        except OSError as error:
            # Keep tracking the board even if the host can no longer write the log.
            log_error = error

    with ExitStack() as connections:
        try:
            verify_saved_image(directory / "flash.bin", stats)
            if cancel.is_set():
                report("erase_not_sent", "reason=cancelled")
                return log_error
            data = connections.enter_context(
                socket.create_connection((host, data_port), timeout=5.0)
            )
            control = connections.enter_context(
                socket.create_connection((host, control_port), timeout=5.0)
            )
            data.settimeout(0.25)
        except (OSError, ValueError) as error:
            report("erase_not_sent", f"error={error}")
            raise
        request_erase(data, control, request, cancel, report)
    return log_error


def request_erase(data, control, request, cancel, report):
    deadline = time.monotonic() + ERASE_ACK_TIMEOUT_S

    def chunks():
        while not cancel.is_set() and time.monotonic() < deadline:
            try:
                chunk = data.recv(16384)
            except socket.timeout:
                continue
            if not chunk:
                return
            yield chunk

    if cancel.is_set():
        report("erase_not_sent", "reason=cancelled")
        return
    started = False
    try:
        control.sendall(f"ERASE!{request:08x}".encode("ascii"))
        report("erase_sent")
        for kind, message_id, index, payload in messages(chunks()):
            if kind != ERASE_STATUS or message_id != request or index != PAGE_COUNT:
                continue
            (status,) = U32.unpack(payload)
            if status == ERASE_STARTED and not started:
                started = True
                deadline = time.monotonic() + ERASE_TIMEOUT_S
                report("erase_acknowledged")
            elif status == ERASE_VERIFIED and started:
                report("erase_verified")
                return
            elif status == ERASE_FAILED and started:
                report("erase_failed", "reason=board_error")
                raise ValueError("Board báo lỗi khi xóa hoặc kiểm tra flash.")
    except OSError as error:
        report("erase_unconfirmed", f"reason=io_error error={error}")
        raise ValueError(
            "Mất kết nối khi gửi hoặc theo dõi lệnh xóa. " "Kiểm tra tab STM32 log."
        ) from error
    if cancel.is_set():
        report("erase_unconfirmed", "reason=monitoring_cancelled")
        return
    if started:
        report("erase_unconfirmed", "reason=no_result")
        raise ValueError("Chưa nhận được kết quả xóa. Kiểm tra tab STM32 log.")
    report("erase_unconfirmed", "reason=no_ack")
    raise ValueError("Board chưa xác nhận lệnh xóa. Kiểm tra tab STM32 log và firmware Export.")


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
    session_id=None,
    previous_of=None,
    last_page=None,
):
    if attempts < 1:
        raise ValueError("attempts must be positive")
    if session_id is not None and previous_of is not None:
        raise ValueError("session and previous are mutually exclusive")
    selected = session_id if previous_of is None else previous_of
    if selected is not None and not 0 <= selected < PAGE_COUNT:
        raise ValueError(f"session ID must be in 0..{PAGE_COUNT - 1}")
    if last_page is not None and (session_id is None or not session_id <= last_page < PAGE_COUNT):
        raise ValueError("invalid selected session range")
    output.mkdir(parents=True, exist_ok=False)
    image_path = output / ("flash.bin" if selected is None else "session.bin")
    partial = image_path.with_suffix(".bin.part")
    with (output / "export.log").open("x", encoding="utf-8", buffering=1) as log:

        def report(message):
            write_log_line(log, message)

        report(
            f"start host={host} control_port={control_port} data_port={data_port} attempts={attempts}"
        )
        try:
            stats = download(
                host,
                control_port,
                data_port,
                timeout,
                partial,
                cancel,
                progress,
                attempts,
                report,
                session_id,
                previous_of,
                last_page,
            )
            if session_id is not None:
                stats["session_id"] = session_id
            if previous_of is not None:
                stats["previous_of"] = previous_of
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


def download(
    host,
    control_port,
    data_port,
    timeout,
    partial,
    cancel,
    progress,
    attempts,
    report,
    session_id=None,
    previous_of=None,
    last_page=None,
):
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
                started = False

                def update_progress(count):
                    nonlocal started
                    if count == 0:
                        started = True
                        report(f"attempt={attempt} request={request:08x} begin_received")
                    if progress is not None:
                        progress(attempt, count)

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
                        if previous_of is not None:
                            command = f"EXPREV{request:08x}{previous_of:08x}"
                        elif last_page is not None:
                            command = f"EXRANG{request:08x}{session_id:08x}{last_page:08x}"
                        elif session_id is not None:
                            command = f"EXSESS{request:08x}{session_id:08x}"
                        else:
                            command = f"EXPORT{request:08x}"
                        control.sendall(command.encode("ascii"))
                        reply = receive_pages(
                            receive_chunks(data, timeout, cancel, lambda: started),
                            request,
                            received,
                            diagnostics,
                            update_progress,
                        )
                except InterruptedError:
                    raise
                except (OSError, EOFError) as error:
                    report(
                        f"attempt={attempt} incomplete received={len(received)} "
                        f"data_bytes={diagnostics.get('bytes_received', 0)} "
                        f"crc_errors={diagnostics['crc_errors']} error={error}"
                    )
                    failure = str(error)
                    continue
                if last_page is not None and any(
                    not session_id <= index <= last_page for index in received
                ):
                    raise ValueError("session export returned pages outside the selected range")
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
                        "request_id": request,
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
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--session", type=int, help="export only this journal session ID")
    selection.add_argument("--previous", type=int, help="export the session before this ID")
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
            session_id=args.session,
            previous_of=args.previous,
        )
    except (OSError, ValueError) as error:
        print(f"Flash export failed: {error} (log: {output / 'export.log'})", file=sys.stderr)
        return 1
    image = output / (
        "flash.bin" if args.session is None and args.previous is None else "session.bin"
    )
    print(f"Saved {image} ({stats['sessions']} sessions)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
