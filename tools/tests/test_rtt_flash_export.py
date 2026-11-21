"""Flash image transfer and journal decoding without Qt or a board."""

import csv
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
from threading import Event
import unittest
from unittest.mock import patch
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "rtt"))
import flash_export as export
from records import Frames
from session_reader import read_session

FIXTURES = Path(__file__).resolve().parents[2] / "protocol" / "fixtures"


def message(kind, request, index, payload):
    frame = export.HEADER.pack(b"DRE!", 1, kind, len(payload), request, index) + payload
    return frame + export.U32.pack(zlib.crc32(frame))


def journal(kind, index, session, uptime, payload=b""):
    prefix = struct.pack("<4sBBHIII", b"DRJ!", 1, kind, len(payload), index, session, uptime)
    crc = zlib.crc32(payload, zlib.crc32(prefix))
    return (prefix + export.U32.pack(crc) + payload).ljust(export.PAGE_BYTES, b"\xff")


def transfer(request, pages, invalid=0, crc_flip=0):
    yield message(export.BEGIN, request, 0, struct.pack("<II", 256, 65536))
    crc = 0
    for index, payload in pages:
        yield message(export.PAGE, request, index, payload)
        crc = zlib.crc32(export.U32.pack(index) + payload, crc)
    yield message(
        export.END,
        request,
        export.PAGE_COUNT,
        struct.pack("<III", len(pages), invalid, crc ^ crc_flip),
    )


class FakeSocket:
    def __init__(self, chunks):
        self.chunks = iter(chunks)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass

    def sendall(self, data):
        self.command = data

    def recv(self, size):
        return next(self.chunks, b"")


class TestFlashExport(unittest.TestCase):
    def test_fragmented_export_preserves_legacy_pages_and_extracts_sessions(self):
        imu = bytes.fromhex((FIXTURES / "imu_batch_v1.hex").read_text())
        config = bytes.fromhex((FIXTURES / "config_v1.hex").read_text())
        unknown = b"legacy".ljust(256, b"\xff")
        pages = [
            (3, unknown),
            (5, journal(export.BEGIN, 5, 5, 100)),
            (6, journal(export.PAGE, 6, 5, 110, config)),
            (7, journal(export.PAGE, 7, 5, 120, imu)),
            (8, journal(export.END, 8, 5, 130, struct.pack("<III", 2, 2, 0))),
            (9, journal(export.PAGE, 9, 5, 140, imu)),
            (
                export.PAGE_COUNT - 1,
                journal(export.BEGIN, export.PAGE_COUNT - 1, export.PAGE_COUNT - 1, 150),
            ),
        ]
        stale = list(transfer(6, [(0, unknown)]))
        wire = b"noise" + b"".join((*stale[1:], *transfer(7, pages, invalid=1)))
        image = io.BytesIO()
        received = {}
        count, invalid, crc = export.receive_pages(
            (wire[i : i + 17] for i in range(0, len(wire), 17)), 7, received
        )
        self.assertEqual(crc, export.page_checksum(received))
        export.write_image(image, received, complete=True)
        self.assertEqual(image.tell(), 16 * 1024 * 1024)
        self.assertEqual(count, len(pages))
        self.assertEqual(invalid, 1)
        image.seek(0)
        self.assertEqual(image.read(768), export.ERASED_PAGE * 3)
        self.assertEqual(image.read(256), unknown)
        image.seek(0)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertEqual(
                export.decode_sessions(image, root),
                {"sessions": 2, "interrupted_sessions": 1, "orphan_pages": 1},
            )
            saved = root / "session_00005"
            with (saved / "imu_raw_00005.csv").open(newline="") as imu_csv:
                rows = list(csv.DictReader(imu_csv))
            self.assertEqual(len(rows), imu[19])
            self.assertTrue(all(row["gyro_odr_hz"] == "800" for row in rows))
            self.assertIn("config_sha256", json.loads((saved / "config.jsonl").read_text()))
            info = json.loads((saved / "session.json").read_text())
            self.assertEqual((info["status"], info["written_frames"]), ("closed", 2))
            info = json.loads((root / "session_65535" / "session.json").read_text())
            self.assertEqual(info["status"], "interrupted")

            segments, _ = read_session(saved)
            self.assertEqual(segments[1].series.received, imu[19])

    def test_live_parser_skips_nested_telemetry_from_a_stale_export(self):
        imu = bytes.fromhex((FIXTURES / "imu_batch_v1.hex").read_text())
        page = journal(export.PAGE, 1, 0, 120, imu)
        stale = message(export.PAGE, 7, 1, page)
        parser = Frames()
        self.assertEqual(list(parser.feed(stale[:64])), [])
        self.assertEqual(list(parser.feed(stale[64:] + imu)), list(Frames().feed(imu)))
        self.assertEqual(parser.errors, 0)

    def test_rejects_truncation_and_out_of_order_pages(self):
        pages = [(1, b"a" * 256), (2, b"b" * 256)]
        begin, first, second, end = transfer(8, pages)
        with self.assertRaisesRegex(EOFError, "before flash export END"):
            export.receive_pages((begin, first), 8, {})
        with self.assertRaisesRegex(ValueError, "out-of-order"):
            export.receive_pages((begin, second, first, end), 8, {})

    def test_recovers_complementary_attempts_and_logs_crc_errors(self):
        pages = [(1, b"a" * 256), (2, b"b" * 256)]
        begin, first, second, end = transfer(8, pages)
        corrupt = second[:-1] + bytes([second[-1] ^ 1])
        other_begin, _, other_second, other_end = transfer(9, pages)
        sockets = [
            FakeSocket([begin, first, corrupt, end]),
            FakeSocket([]),
            FakeSocket([other_begin, other_second, other_end]),
            FakeSocket([]),
        ]
        with (
            tempfile.TemporaryDirectory() as directory,
            patch.object(export.secrets, "randbits", side_effect=[8, 9]),
            patch.object(export.socket, "create_connection", side_effect=sockets),
        ):
            out = Path(directory) / "export"
            stats = export.export("localhost", 9090, 9091, 5, out, attempts=2)
            self.assertEqual((stats["pages_sent"], stats["attempts"]), (2, 2))
            self.assertEqual(stats["request_id"], 9)
            with (out / "flash.bin").open("rb") as image:
                self.assertEqual(image.read(768), export.ERASED_PAGE + pages[0][1] + pages[1][1])
            log = (out / "export.log").read_text()
            self.assertIn("crc_errors=1 recovered=1/2", log)
            self.assertIn("crc_errors=0 recovered=2/2", log)
            self.assertIn("complete pages=2", log)
            self.assertFalse((out / "flash.bin.part").exists())

    def test_erase_checks_saved_image_before_connecting(self):
        pages = {1: b"a" * export.PAGE_BYTES}
        stats = {"pages_sent": 1, "stream_crc32": f"{export.page_checksum(pages):08x}"}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with (root / "flash.bin").open("w+b") as image:
                export.write_image(image, pages, complete=True)
                image.seek(export.PAGE_BYTES)
                image.write(b"b")
            with (
                patch.object(export.socket, "create_connection") as connect,
                self.assertRaisesRegex(ValueError, "flash.bin"),
            ):
                export.erase("localhost", 9090, 9091, root, stats, Event())
            connect.assert_not_called()

    def test_never_finalizes_inconsistent_export(self):
        pages = [(1, b"a" * 256), (2, b"b" * 256)]
        begin, first, _, end = transfer(8, pages)
        next_begin, _, next_second, next_end = transfer(9, pages, crc_flip=1)
        duplicate = message(export.PAGE, 9, 1, b"c" * 256)
        same_end = list(transfer(9, pages))[-1]
        for second_attempt, error in (
            ([], "count or CRC"),
            ([next_begin, next_second, next_end], "END changed"),
            ([next_begin, duplicate, same_end], "page 1 changed"),
        ):
            with (
                self.subTest(error=error),
                tempfile.TemporaryDirectory() as directory,
                patch.object(export.secrets, "randbits", side_effect=[8, 9]),
            ):
                out = Path(directory) / "export"
                sockets = [FakeSocket([begin, first, end]), FakeSocket([])]
                if second_attempt:
                    sockets.extend([FakeSocket(second_attempt), FakeSocket([])])
                with (
                    patch.object(export.socket, "create_connection", side_effect=sockets),
                    self.assertRaisesRegex(ValueError, error),
                ):
                    export.export("localhost", 9090, 9091, 5, out, attempts=len(sockets) // 2)
                self.assertTrue((out / "flash.bin.part").is_file())
                self.assertFalse((out / "flash.bin").exists())
                self.assertIn("failed", (out / "export.log").read_text())

    def test_command_and_incomplete_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for complete in (False, True):
                with self.subTest(complete=complete):
                    wire = list(transfer(0xF1, []))
                    data = FakeSocket(wire if complete else wire[:-1])
                    control = FakeSocket([])
                    out = root / str(complete)
                    with (
                        patch.object(export.secrets, "randbits", return_value=0xF1),
                        patch.object(
                            export.socket, "create_connection", side_effect=[data, control]
                        ),
                    ):
                        if complete:
                            stats = export.export("localhost", 9090, 9091, 5, out, attempts=1)
                            self.assertEqual(stats["sessions"], 0)
                        else:
                            with self.assertRaisesRegex(ValueError, "before flash export END"):
                                export.export("localhost", 9090, 9091, 5, out, attempts=1)
                    self.assertEqual(control.command, b"EXPORT000000f1")
                    self.assertEqual((out / "flash.bin").is_file(), complete)
                    self.assertEqual((out / "flash.bin.part").is_file(), not complete)


if __name__ == "__main__":
    unittest.main()
