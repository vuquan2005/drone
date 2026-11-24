"""Optional offscreen GUI/TCP integration test; never connects to a probe."""

import csv
import json
import struct
import zlib
import tempfile
import importlib.util
from test_rtt_records import packet
import math
import os
from pathlib import Path
import sys
import time
from types import SimpleNamespace
import unittest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "rtt"))
HAS_GUI = all(importlib.util.find_spec(name) for name in ("PySide6", "pyqtgraph"))
if HAS_GUI:
    from PySide6 import QtCore, QtNetwork, QtWidgets
    from view import Viewer


@unittest.skipUnless(HAS_GUI, "Run inside the RTT viewer dependency environment")
class ViewerTests(unittest.TestCase):
    def test_receiver_responsive(self):
        app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        from panels import ReceiverPanel
        from records import RcCompact

        panel = ReceiverPanel()
        panel.resize(1000, 450)
        panel.show()
        app.processEvents()
        self.assertTrue(panel.stick_group.isVisible())
        self.assertTrue(panel.details.isVisible())
        self.assertFalse(panel.rc_bars[0].isVisible())
        sample = (1, 100, 90, 1, *range(16))
        panel.update_sample(sample, 0.1, True)
        self.assertEqual(panel.sticks[0].values, (3, 2))
        self.assertEqual(panel.sticks[1].values, (0, 1))
        self.assertEqual(panel.details.text(), "Raw CRSF (0–2047) · age 0.1s")
        panel.update_sample(sample, 0, True, saved=True)
        self.assertIn("saved", panel.details.text())
        self.assertEqual(panel.rc_status.toolTip(), "")
        panel.set_saved_snapshots([(0, sample), (0.1, (2, 200, 190, 0, *([1811] * 16)))])
        self.assertTrue(panel.replay_controls.isVisible())
        self.assertEqual(panel.rc_bars[0].value(), 1811)
        panel.replay_slider.setValue(0)
        self.assertEqual(panel.rc_bars[0].value(), 0)
        self.assertEqual(panel.sticks[0].values, (3, 2))
        self.assertIn("1/2", panel.replay_position.text())
        panel.set_saved_snapshots([(0, sample)])
        self.assertFalse(panel.replay_slider.isEnabled())
        panel.set_saved_snapshots([])
        self.assertFalse(panel.replay_controls.isVisible())
        panel.resize(500, 250)
        app.processEvents()
        self.assertFalse(panel.stick_group.isVisible())
        self.assertFalse(panel.details.isVisible())
        self.assertTrue(panel.rc_bars[0].isVisible())
        self.assertEqual(panel.rc_bars[2].value(), 2)
        panel.update_sample(sample, 1, False)
        self.assertFalse(panel.sticks[0].active)
        panel.resize(350, 250)
        panel.update_sample(RcCompact(2, 150, 140, 0, 1, 2, 3, 1), 0.1, True)
        app.processEvents()
        self.assertTrue(panel.switches.isVisible())
        self.assertEqual(
            [box.layout().itemAt(0).widget().text() for box in panel.switch_boxes],
            ["SA", "SB", "SC", "SD"],
        )
        self.assertEqual(panel.switch_boxes[0].toolTip(), "CH5 · Arm request")
        self.assertEqual([value.text() for value in panel.switch_values], ["LOW"] * 4)
        self.assertEqual(panel.switch_boxes[0].y(), panel.switch_boxes[1].y())
        self.assertGreater(panel.switch_boxes[2].y(), panel.switch_boxes[0].y())
        panel.resize(500, 250)
        app.processEvents()
        self.assertEqual(len({box.y() for box in panel.switch_boxes}), 1)
        arm_switch = panel.switch_values[0]
        low_style = arm_switch.styleSheet()
        switch_sample = RcCompact(3, 200, 190, 0, 1, 2, 3, 0x33)
        panel.update_sample(switch_sample, 0.1, True)
        self.assertEqual(
            [value.text() for value in panel.switch_values], ["HIGH", "LOW", "HIGH", "MIDDLE"]
        )
        self.assertNotEqual(arm_switch.styleSheet(), low_style)
        panel.update_sample(switch_sample, 0.5, True)
        self.assertEqual([value.text() for value in panel.switch_values], ["—"] * 4)
        panel.update_sample(RcCompact(3, 200, 140, 0, 1, 2, 3, 0), 0.1, True)
        self.assertEqual([value.text() for value in panel.switch_values], ["—"] * 4)
        self.assertFalse(panel.switches.isEnabled())
        panel.update_sample(sample, 0.1, True)
        self.assertTrue(panel.switches.isHidden())
        panel.update_sample(None, 0, False, saved=True)
        self.assertIsNone(panel.sticks[0].values)
        panel.close()

    def test_rate_units(self):
        app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        from panels import RatePanel

        rate = RatePanel()
        self.assertTrue(all(not plot.getAxis("left").autoSIPrefix for plot in rate.plots))
        rate.close()

    def test_live_timer_updates_after_repeated_tab_switches(self):
        app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        from records import RATE, RC
        from unittest.mock import patch

        viewer = Viewer(SimpleNamespace(host="127.0.0.1", log_port=9090, data_port=9091))
        viewer.show()
        viewer.states["DATA"] = "connected"
        sequence = 0
        try:
            for tab in (1, 2, 0, 2, 1):
                viewer.tabs.setCurrentIndex(tab)
                for _ in range(3):
                    sequence += 1
                    value = 200 + sequence * 20
                    rate = sequence / 100
                    viewer.on_data(
                        (1, 3, RC.pack(sequence, sequence * 50, sequence * 50, 1, *([value] * 16)))
                    )
                    viewer.on_data(
                        (1, 5, RATE.pack(sequence, sequence * 50000, 2000, 3, rate, *([0.0] * 15)))
                    )
                    deadline = time.monotonic() + 0.08
                    while time.monotonic() < deadline:
                        app.processEvents()
                        time.sleep(0.002)
                    if tab == 1:
                        self.assertEqual(viewer.receiver_panel.rc_bars[0].value(), value)
                        self.assertEqual(viewer.receiver_panel.sticks[0].values, (value, value))
                    if tab == 2:
                        self.assertAlmostEqual(
                            viewer.rate_panel.curves[0][0].getData()[1][-1], rate
                        )
            # Hidden panels do not receive periodic rendering work.
            viewer.tabs.setCurrentIndex(0)
            with (
                patch.object(viewer.receiver_panel, "update_sample") as receiver,
                patch.object(viewer.rate_panel, "update_points") as rate_plot,
            ):
                viewer.refresh()
                receiver.assert_not_called()
                rate_plot.assert_not_called()
        finally:
            viewer.timer.stop()
            viewer.close()

    def test_flash_view_manages_server_export_erase_and_saved_sessions(self):
        from flash_view import FlashViewer
        from test_rtt_flash_export import export, journal, message, session_listing, transfer
        from unittest.mock import patch

        app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        servers = [QtNetwork.QTcpServer(), QtNetwork.QTcpServer()]
        for server in servers:
            self.assertTrue(server.listen(QtNetwork.QHostAddress.SpecialAddress.LocalHost, 0))
            self.addCleanup(server.close)

        def wait_for(predicate):
            deadline = time.monotonic() + 3
            while not predicate() and time.monotonic() < deadline:
                app.processEvents()
                time.sleep(0.002)
            self.assertTrue(predicate())

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            openocd = root / "openocd"
            openocd.write_text(
                '#!/bin/sh\nprintf "%s\\n" "$@" "Drone flash RTT ready"\nexec sleep 30\n'
            )
            openocd.chmod(0o700)
            viewer = FlashViewer(
                SimpleNamespace(
                    openocd=str(openocd),
                    gdb_port=3333,
                    log_port=servers[0].serverPort(),
                    data_port=servers[1].serverPort(),
                    export_dir=root / "export",
                )
            )
            viewer.show()
            try:
                self.assertFalse(viewer.export_button.isEnabled())
                self.assertFalse(viewer.erase_button.isEnabled())
                viewer.server_button.click()
                wait_for(lambda: getattr(viewer, "job_kind", None) == "list")
                self.assertIn("GDB 127.0.0.1:3333", viewer.server_status.text())
                self.assertIn("set DRONE_GDB_PORT 3333", viewer.server_log.toPlainText())
                wait_for(servers[0].hasPendingConnections)
                board = servers[0].nextPendingConnection()
                board.write(b"[4][I][flash] export_wait preset=Export\n")
                board.flush()
                wait_for(lambda: "export_wait preset=Export" in viewer.board_status.text())
                wait_for(lambda: all(server.hasPendingConnections() for server in servers))
                list_control, list_data = [server.nextPendingConnection() for server in servers]
                wait_for(lambda: list_control.bytesAvailable() >= 14)
                command = bytes(list_control.read(14))
                self.assertTrue(command.startswith(b"ELISTS"))
                rows = [(2, 2, 48, False), (0, 1, 42, True)]
                list_data.write(b"".join(session_listing(int(command[6:], 16), rows)))
                list_data.flush()
                wait_for(lambda: viewer.job is None)
                self.assertEqual(viewer.session_count.text(), "2 on board · 1 interrupted")
                self.assertTrue(viewer.selected_button.isEnabled())
                viewer.selected_button.click()
                wait_for(lambda: all(server.hasPendingConnections() for server in servers))
                selected_control, selected_data = [
                    server.nextPendingConnection() for server in servers
                ]
                wait_for(lambda: selected_control.bytesAvailable() >= 30)
                command = bytes(selected_control.read(30))
                self.assertEqual(command[:6], b"EXRANG")
                self.assertEqual(command[14:], b"0000000200000002")
                selected_data.write(
                    b"".join(
                        transfer(int(command[6:14], 16), [(2, journal(export.BEGIN, 2, 2, 48))])
                    )
                )
                selected_data.flush()
                wait_for(lambda: viewer.job is None)
                self.assertTrue((viewer.export_directory / "session.bin").is_file())
                self.assertFalse(viewer.erase_button.isEnabled())
                viewer.export_button.click()
                self.assertFalse(viewer.server_button.isEnabled())
                self.assertFalse(viewer.erase_button.isEnabled())
                wait_for(lambda: all(server.hasPendingConnections() for server in servers))
                control, data = [server.nextPendingConnection() for server in servers]
                wait_for(lambda: control.bytesAvailable() >= 14)
                command = bytes(control.read(14))
                self.assertTrue(command.startswith(b"EXPORT"))
                request = int(command[6:], 16)
                pages = [
                    (0, journal(export.BEGIN, 0, 0, 42)),
                    (1, journal(export.END, 1, 0, 45, struct.pack("<III", 1, 0, 0))),
                    (2, journal(export.BEGIN, 2, 2, 48)),
                ]
                frames = list(transfer(request, pages))
                data.write(frames[0])
                data.flush()
                wait_for(lambda: "BEGIN received" in viewer.status.text())
                board.write(
                    f"[20][I][flash] export_scan request={request:08x} scanned=4096/65536 pages=0\n".encode()
                )
                board.flush()
                wait_for(lambda: "scanned=4096/65536" in viewer.board_status.text())
                self.assertIn("scanned=4096/65536", viewer.board_log.toPlainText())
                data.write(b"".join(frames[1:]))
                data.flush()
                wait_for(lambda: viewer.job is None)
                self.assertEqual(viewer.export_directory.parent, root / "export")
                self.assertTrue((viewer.export_directory / "export.log").is_file())
                self.assertEqual(viewer.session_count.text(), "2 sessions · 1 interrupted")
                self.assertEqual(viewer.sessions.count(), 2)
                self.assertIn("closed", viewer.sessions.item(0).text())
                self.assertIn("interrupted", viewer.sessions.item(1).text())
                viewer.sessions.setCurrentRow(1)
                with patch.object(
                    QtCore.QProcess, "startDetached", return_value=(True, 12)
                ) as start:
                    viewer.open_button.click()
                self.assertEqual(
                    start.call_args.args[1][1:],
                    ["--session", str(viewer.export_directory / "session_00002")],
                )
                self.assertTrue(viewer.export_button.isEnabled())
                self.assertTrue(viewer.erase_button.isEnabled())
                self.assertEqual(
                    (viewer.export_directory / "flash.bin").stat().st_size,
                    export.PAGE_BYTES * export.PAGE_COUNT,
                )
                with patch.object(
                    QtWidgets.QMessageBox,
                    "question",
                    return_value=QtWidgets.QMessageBox.StandardButton.Cancel,
                ):
                    viewer.erase_button.click()
                    self.assertIsNone(viewer.job)
                    self.assertTrue(viewer.erase_button.isEnabled())
                with patch.object(
                    QtWidgets.QMessageBox,
                    "question",
                    return_value=QtWidgets.QMessageBox.StandardButton.Yes,
                ):
                    viewer.erase_button.click()
                self.assertFalse(viewer.erase_button.isEnabled())
                wait_for(lambda: all(server.hasPendingConnections() for server in servers))
                erase_control, erase_data = [server.nextPendingConnection() for server in servers]
                wait_for(lambda: erase_control.bytesAvailable() >= 14)
                self.assertEqual(bytes(erase_control.read(14)), f"ERASE!{request:08x}".encode())
                for status in (export.ERASE_STARTED, export.ERASE_VERIFIED):
                    erase_data.write(
                        message(
                            export.ERASE_STATUS, request, export.PAGE_COUNT, export.U32.pack(status)
                        )
                    )
                erase_data.flush()
                wait_for(lambda: viewer.job is None)
                self.assertIn("Flash erased and verified", viewer.status.text())
                self.assertFalse(viewer.erase_button.isEnabled())
                self.assertTrue((viewer.export_directory / "flash.bin").is_file())
                log = (viewer.export_directory / "export.log").read_text().splitlines()
                self.assertIn("complete pages=3", log[-4])
                self.assertEqual(
                    [line.split(maxsplit=1)[1] for line in log[-3:]],
                    [
                        f"{event} request={request:08x}"
                        for event in ("erase_sent", "erase_acknowledged", "erase_verified")
                    ],
                )
                self.assertIn("erase_verified", viewer.export_log.toPlainText())
                viewer.server_button.click()
                wait_for(lambda: viewer.server_status.text() == "OpenOCD stopped")
                self.assertFalse(viewer.export_button.isEnabled())
                self.assertTrue(viewer.open_button.isEnabled())
                for connection in (board, control, data, erase_control, erase_data):
                    connection.abort()
            finally:
                viewer.close()
                wait_for(lambda: viewer.job is None and not viewer.isVisible())

    def test_exported_session_opens_offline_without_connecting_to_rtt(self):
        app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        server = QtNetwork.QTcpServer()
        self.assertTrue(server.listen(QtNetwork.QHostAddress.SpecialAddress.LocalHost, 0))
        self.addCleanup(server.close)
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / "session.json").write_text(
                json.dumps({"session_id": 0, "status": "interrupted", "data_pages": 0})
            )
            viewer = Viewer(
                SimpleNamespace(
                    host="127.0.0.1",
                    log_port=server.serverPort(),
                    data_port=server.serverPort(),
                    session=Path(directory),
                )
            )
            viewer.show()
            try:
                deadline = time.monotonic() + 3
                while viewer.load_future is not None and time.monotonic() < deadline:
                    app.processEvents()
                    time.sleep(0.002)
                self.assertIsNotNone(viewer.saved_session)
                self.assertEqual(viewer.segment_box.currentData(), 1)
                self.assertTrue(viewer.live_button.isHidden())
                self.assertTrue(viewer.log_stream.paused and viewer.data_stream.paused)
                self.assertFalse(server.hasPendingConnections())
            finally:
                viewer.close()

    def test_dual_tcp_plot_filter_and_reconnect(self):
        app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        servers = [QtNetwork.QTcpServer(), QtNetwork.QTcpServer()]
        for server in servers:
            self.assertTrue(server.listen(QtNetwork.QHostAddress.SpecialAddress.LocalHost, 0))
        viewer = Viewer(
            SimpleNamespace(
                host="127.0.0.1",
                log_port=servers[0].serverPort(),
                data_port=servers[1].serverPort(),
            )
        )
        self.assertEqual(len(viewer.rate_panel.plots), 6)
        self.assertTrue(
            all(
                curve.getData()[0] is None or len(curve.getData()[0]) == 0
                for curve, _ in viewer.rate_panel.curves
            )
        )
        viewer.show()
        output = tempfile.TemporaryDirectory()
        self.addCleanup(output.cleanup)
        viewer.output_dir.setText(output.name)
        viewer.raw_checkbox.setChecked(True)

        def until(predicate):
            deadline = time.monotonic() + 4
            while not predicate() and time.monotonic() < deadline:
                app.processEvents()
                time.sleep(0.005)
            self.assertTrue(predicate())

        peers = []

        def respond_to_config(data_peer):
            until(lambda: peers[0].bytesAvailable() >= 5)
            request = bytes(peers[0].read(5))
            self.assertEqual(request[:1], b"C")
            fixture = Path(__file__).resolve().parents[2] / "protocol/fixtures/config_v1.hex"
            frame = bytes.fromhex(fixture.read_text())
            response = frame[:8] + request[1:] + frame[12:-4]
            data_peer.write(response + struct.pack("<I", zlib.crc32(response)))
            until(lambda: viewer.telemetry.config is not None)
            self.assertEqual(viewer.telemetry.config["imu"]["accel_odr_hz"], 800)
            self.assertEqual(viewer.telemetry.config["rate"]["axes"]["pitch"]["kp_s_per_rad"], 0.5)

        try:
            until(lambda: all(server.hasPendingConnections() for server in servers))
            peers = [server.nextPendingConnection() for server in servers]
            respond_to_config(peers[1])
            self.assertIn('"accel_filter": "normal_avg4"', viewer.config_panel.toPlainText())
            viewer.toggle_recording()
            self.assertTrue(viewer.recording)
            peers[0].write(b"[1][I][imu] init\n[2][E][rx] lost\n")
            peers[1].write(packet()[:23])
            peers[1].flush()
            # Pump the partial record before sending its suffix.
            for _ in range(5):
                app.processEvents()
            self.assertEqual(viewer.telemetry.series.received, 0)
            peers[1].write(packet()[23:] + packet(3, 6000))
            until(lambda: viewer.telemetry.series.received == 2 and len(viewer.log_panel.logs) == 2)
            viewer.refresh()
            self.assertEqual(viewer.telemetry.series.missing, 1)
            self.assertEqual(len(viewer.imu_panel.curves[0][0].getData()[0]), 3)
            rate_frame = bytes.fromhex(
                (
                    Path(__file__).resolve().parents[2] / "protocol/fixtures/rate_compact_v1.hex"
                ).read_text()
            )
            peers[1].write(rate_frame)
            until(lambda: viewer.telemetry.latest_rate is not None)
            viewer.refresh()
            # Rate history is collected/recorded while its tab stays hidden.
            self.assertTrue(
                viewer.rate_panel.curves[0][0].getData()[0] is None
                or len(viewer.rate_panel.curves[0][0].getData()[0]) == 0
            )
            viewer.tabs.setCurrentWidget(viewer.rate_panel)
            self.assertEqual(len(viewer.rate_panel.curves[0][0].getData()[0]), 1)
            self.assertAlmostEqual(viewer.rate_panel.curves[0][0].getData()[1][0], 0.25)
            self.assertIn("dt=2000 us", viewer.rate_panel.text())
            self.assertIn("valid=True", viewer.rate_panel.text())
            self.assertNotIn("Setpoint", viewer.rate_panel.text())
            viewer.log_panel.level.setCurrentText("E")
            self.assertIn("lost", viewer.log_panel.log_view.toPlainText())
            self.assertNotIn("init", viewer.log_panel.log_view.toPlainText())
            viewer.log_panel.tag.setText("imu")
            self.assertEqual(viewer.log_panel.log_view.toPlainText(), "")
            viewer.log_panel.tag.clear()
            peers[1].disconnectFromHost()
            until(lambda: servers[1].hasPendingConnections())
            peers.append(servers[1].nextPendingConnection())
            respond_to_config(peers[-1])
            peers[-1].write(packet(100, 100000))
            until(lambda: viewer.telemetry.series.received == 3)
            self.assertEqual(viewer.telemetry.series.missing, 1)
            self.assertEqual(viewer.telemetry.series.points[-1][0], 0)
            burst = b"".join(
                packet(
                    101 + i,
                    102500 + 2500 * i,
                    axes=(
                        int(1000 * math.sin(i / 100)),
                        0,
                        9800,
                        0,
                        int(100 * math.cos(i / 100)),
                        0,
                    ),
                )
                for i in range(4000)
            )
            stats_payload = struct.pack("<10I", 1, 11000, 4003, 7, 2, 226, 225, 1, 0, 0)
            stats_frame = struct.pack("<4sBBH", b"DRT!", 1, 2, len(stats_payload)) + stats_payload
            stats_frame += struct.pack("<I", zlib.crc32(stats_frame))
            peers[-1].write(burst + stats_frame)
            until(
                lambda: viewer.telemetry.series.received == 4003
                and viewer.telemetry.stats_received == 1
            )
            viewer.refresh()
            self.assertEqual(viewer.telemetry.series.unknown, 0)
            self.assertIn("IMU=225", viewer.mcu_stats.text())
            self.assertIn("errors 7", viewer.mcu_stats.text())
            self.assertEqual(viewer.telemetry.series.missing, 1)
            self.assertLessEqual(len(viewer.telemetry.series.points), 4001)
            rc_frame = bytes.fromhex(
                (Path(__file__).resolve().parents[2] / "protocol/fixtures/rc_v1.hex").read_text()
            )
            peers[-1].write(rc_frame[:19])
            peers[-1].flush()
            for _ in range(5):
                app.processEvents()
            self.assertIsNone(viewer.telemetry.latest_rc)
            peers[-1].write(rc_frame[19:])
            until(lambda: viewer.telemetry.latest_rc is not None)
            viewer.refresh()
            self.assertEqual(viewer.receiver_panel.rc_bars[2].format(), "—")
            viewer.tabs.setCurrentWidget(viewer.receiver_panel)
            self.assertIn("RC: connected", viewer.receiver_panel.rc_status.text())
            self.assertEqual(viewer.receiver_panel.rc_bars[2].value(), 1811)
            self.assertEqual(viewer.telemetry.series.unknown, 0)
            viewer.on_data(
                (1, 3, struct.pack("<4I16H", 2, 1334, 1224, 0, *viewer.telemetry.latest_rc[4:]))
            )
            viewer.refresh()
            self.assertIn("RC: disconnected", viewer.receiver_panel.rc_status.text())
            self.assertFalse(viewer.receiver_panel.rc_bars[0].isEnabled())
            self.assertEqual(viewer.receiver_panel.rc_bars[0].value(), 172)
            viewer.telemetry.rc_received_at -= 1
            viewer.refresh()
            self.assertIn("telemetry stale", viewer.receiver_panel.rc_status.text())
            invalid_before = viewer.telemetry.series.invalid
            viewer.on_data((1, 3, struct.pack("<4I16H", 3, 1384, 1224, 2, *([0] * 16))))
            self.assertEqual(viewer.telemetry.series.invalid, invalid_before + 1)
            viewer.on_data((1, 8, struct.pack("<3I4HB", 3, 1384, 1374, 172, 992, 1811, 2047, 0x4F)))
            viewer.refresh()
            self.assertEqual(viewer.receiver_panel.rc_bars[3].value(), 2047)
            self.assertTrue(viewer.receiver_panel.rc_bars[4].isHidden())
            self.assertEqual(
                [value.text() for value in viewer.receiver_panel.switch_values],
                ["HIGH", "HIGH", "MIDDLE", "HIGH"],
            )
            control_frame = bytes.fromhex(
                (
                    Path(__file__).resolve().parents[2]
                    / "protocol/fixtures/attitude_control_compact_v1.hex"
                ).read_text()
            )
            peers[-1].write(control_frame)
            until(lambda: viewer.telemetry.latest_attitude_control is not None)
            viewer.tabs.setCurrentWidget(viewer.attitude_control_panel)
            self.assertIn("Live · valid", viewer.attitude_control_panel.status.text())
            self.assertIn("estimate age 0.04 ms", viewer.attitude_control_panel.status.text())
            self.assertAlmostEqual(
                viewer.attitude_control_panel.curves[0].getData()[1][-1], math.degrees(0.25)
            )
            self.assertEqual(viewer.attitude_control_panel.curves[6].getData()[1][-1], 0.5)
            peers[0].write(b"[3][I][imu] filtered but saved\n")
            until(lambda: len(viewer.log_panel.logs) == 3)
            self.assertNotIn("filtered but saved", viewer.log_panel.log_view.toPlainText())
            viewer.toggle_recording()
            until(lambda: viewer.writer.done.is_set())
            self.assertIsNone(viewer.writer.error)
            self.assertEqual(sum(viewer.writer.dropped.values()), 0)
            directory = viewer.writer.directory
            stamp = directory.name.removeprefix("telemetry_")
            with (directory / f"imu_raw_{stamp}.csv").open(newline="") as file:
                rows = list(csv.reader(file))
            self.assertEqual(len(rows), 4004)
            with next(directory.glob("rtt_stats_*.csv")).open(newline="") as file:
                stats_rows = list(csv.reader(file))
            self.assertEqual(len(stats_rows), 2)
            self.assertEqual(
                stats_rows[1][1:], ["1", "11000", "4003", "7", "2", "226", "225", "1", "0", "0"]
            )
            with next(directory.glob("rc_*.csv")).open(newline="") as file:
                rc_rows = list(csv.reader(file))
            self.assertEqual(len(rc_rows), 3)
            self.assertEqual(rc_rows[1][1:5], ["1", "1234", "1224", "1"])
            self.assertEqual(rc_rows[2][4], "0")
            with (directory / f"receiver_compact_{stamp}.csv").open(newline="") as file:
                compact_rows = list(csv.reader(file))
            self.assertEqual(
                compact_rows[1][1:], ["3", "1384", "1374", "172", "992", "1811", "2047", "79"]
            )
            self.assertNotEqual(rows[1][0], rows[3][0])
            configs = [
                json.loads(line) for line in (directory / "config.jsonl").read_text().splitlines()
            ]
            self.assertEqual(
                [config["segment"] for config in configs], [int(rows[1][0]), int(rows[3][0])]
            )
            self.assertEqual(configs[0]["config_sha256"], configs[1]["config_sha256"])
            self.assertIn("filtered but saved", next(directory.glob("events_*.log")).read_text())
            self.assertGreater(
                len(bytes.fromhex(next(directory.glob("data_*.hex")).read_text())), 4000 * 44
            )
            # Open the recorded session without live updates replacing its plots.
            viewer.load_session(directory)
            until(lambda: viewer.saved_session is not None)
            self.assertFalse(viewer.record_button.isEnabled())
            self.assertTrue(viewer.log_stream.paused)
            self.assertIn("Saved session:", viewer.statusBar().currentMessage())
            viewer.segment_box.setCurrentIndex(viewer.segment_box.count() - 1)
            self.assertIn('"source": "flash"', viewer.config_panel.toPlainText())
            self.assertEqual(viewer.receiver_panel.switch_values[3].text(), "HIGH")
            self.assertIn("0.15s", viewer.receiver_panel.replay_position.text())
            self.assertIn("Saved · valid", viewer.attitude_control_panel.status.text())
            self.assertEqual(viewer.attitude_control_panel.curves[6].getData()[1][-1], 0.5)
            self.assertGreater(len(viewer.imu_panel.curves[0][0].getData()[0]), 0)
            viewer.return_live()
            self.assertIsNone(viewer.saved_session)
            self.assertFalse(viewer.data_stream.paused)
            # Batched samples update the plot in SI/FRD and each get a CSV row.
            viewer.toggle_recording()
            raw_fixture = bytes.fromhex(
                (
                    Path(__file__).resolve().parents[2] / "protocol/fixtures/imu_batch_v1.hex"
                ).read_text()
            )
            from records import Frames

            viewer.tabs.setCurrentWidget(viewer.imu_panel)
            viewer.on_data(next(Frames().feed(raw_fixture)))
            self.assertTrue(viewer.dirty)
            viewer.refresh()
            _, y = viewer.imu_panel.curves[0][0].getData()
            self.assertAlmostEqual(y[-1], 118 * 16 * 9.80665 / 32768)
            viewer.toggle_recording()
            until(lambda: viewer.writer.done.is_set())
            with next(viewer.writer.directory.glob("imu_raw_*.csv")).open(newline="") as file:
                raw_rows = list(csv.reader(file))
            self.assertEqual(len(raw_rows), 3)
            self.assertEqual(
                raw_rows[1][1:10],
                ["1000", "1000000", "1193046", "-120", "35", "2048", "10", "-20", "30"],
            )
            self.assertEqual(
                raw_rows[2][1:10],
                ["1001", "1001250", "1193078", "-118", "37", "2050", "11", "-18", "31"],
            )
            self.assertEqual(raw_rows[2][10:], ["16", "2", "2000", "800", "800"])
            viewer.log_panel.level.setCurrentText("All")
            viewer.refresh()
            image_path = os.environ.get("RTT_TEST_SCREENSHOT")
            if image_path:
                viewer.findChild(QtWidgets.QTabWidget).setCurrentIndex(1)
                app.processEvents()
                viewer.grab().save(image_path)
            # The IMU fixture reset the stream and cleared the stats baseline.
            viewer.on_data((1, 2, struct.pack("<10I", 100, 10000, *([0] * 8))))
            old_segment = viewer.telemetry.segment
            viewer.on_data((1, 2, struct.pack("<10I", 1, 100, *([0] * 8))))
            self.assertEqual(viewer.telemetry.segment, old_segment + 1)
            viewer.on_data((1, 2, struct.pack("<10I", 2**32 - 1, 2**32 - 100, *([0] * 8))))
            old_segment = viewer.telemetry.segment
            viewer.on_data((1, 2, struct.pack("<10I", 0, 100, *([0] * 8))))
            self.assertEqual(viewer.telemetry.segment, old_segment)  # Natural uint32 wrap.
            viewer.toggle_recording()
            viewer.on_log(b"[4][W][test] flush on close")
            viewer.close()
            until(lambda: viewer.writer.done.is_set() and not viewer.isVisible())
            self.assertIn(
                "flush on close", next(viewer.writer.directory.glob("events_*.log")).read_text()
            )
        finally:
            if viewer.writer is not None:
                viewer.writer.stop()
                viewer.writer.done.wait(3)
            viewer.timer.stop()
            for stream in (viewer.log_stream, viewer.data_stream):
                stream.retry.stop()
                stream.socket.abort()
            viewer.close()
            for peer in peers:
                peer.abort()
            for server in servers:
                server.close()


if __name__ == "__main__":
    unittest.main()
