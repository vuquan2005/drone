"""Optional offscreen GUI/TCP integration test; never connects to a probe."""

import csv
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
    from PySide6 import QtNetwork, QtWidgets
    from view import Viewer


@unittest.skipUnless(HAS_GUI, "Run inside the RTT viewer dependency environment")
class ViewerTests(unittest.TestCase):
    def test_receiver_responsive(self):
        app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        from panels import ReceiverPanel

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
        viewer.toggle_recording()
        self.assertTrue(viewer.recording)

        def until(predicate):
            deadline = time.monotonic() + 4
            while not predicate() and time.monotonic() < deadline:
                app.processEvents()
                time.sleep(0.005)
            self.assertTrue(predicate())

        peers = []
        try:
            until(lambda: all(server.hasPendingConnections() for server in servers))
            peers = [server.nextPendingConnection() for server in servers]
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
                (Path(__file__).resolve().parents[2] / "protocol/fixtures/rate_v1.hex").read_text()
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
            control_frame = bytes.fromhex(
                (
                    Path(__file__).resolve().parents[2]
                    / "protocol/fixtures/attitude_control_v1.hex"
                ).read_text()
            )
            peers[-1].write(control_frame)
            until(lambda: viewer.telemetry.latest_attitude_control is not None)
            viewer.tabs.setCurrentWidget(viewer.attitude_control_panel)
            self.assertIn("Live · valid", viewer.attitude_control_panel.status.text())
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
            self.assertNotEqual(rows[1][0], rows[3][0])
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
            self.assertIn("Saved · valid", viewer.attitude_control_panel.status.text())
            self.assertEqual(viewer.attitude_control_panel.curves[6].getData()[1][-1], 0.5)
            self.assertGreater(len(viewer.imu_panel.curves[0][0].getData()[0]), 0)
            viewer.return_live()
            self.assertIsNone(viewer.saved_session)
            self.assertFalse(viewer.data_stream.paused)
            # Raw frames update the plot in SI/FRD and record original counts.
            viewer.toggle_recording()
            raw_fixture = bytes.fromhex(
                (
                    Path(__file__).resolve().parents[2] / "protocol/fixtures/imu_raw_v1.hex"
                ).read_text()
            )
            from records import Frames

            viewer.tabs.setCurrentWidget(viewer.imu_panel)
            viewer.on_data(next(Frames().feed(raw_fixture)))
            self.assertTrue(viewer.dirty)
            viewer.refresh()
            _, y = viewer.imu_panel.curves[0][0].getData()
            self.assertAlmostEqual(y[-1], -16 * 9.80665)
            viewer.toggle_recording()
            until(lambda: viewer.writer.done.is_set())
            with next(viewer.writer.directory.glob("imu_raw_*.csv")).open(newline="") as file:
                raw_rows = list(csv.reader(file))
            self.assertEqual(raw_rows[1][4:10], ["-32768", "-1", "32767", "1", "-32768", "32767"])
            viewer.log_panel.level.setCurrentText("All")
            viewer.refresh()
            image_path = os.environ.get("RTT_TEST_SCREENSHOT")
            if image_path:
                viewer.findChild(QtWidgets.QTabWidget).setCurrentIndex(1)
                app.processEvents()
                viewer.grab().save(image_path)
            # The raw fixture reset the IMU stream and cleared the stats baseline.
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
