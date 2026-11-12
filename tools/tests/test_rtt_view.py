"""Optional offscreen GUI/TCP integration test; never connects to a probe."""

import csv
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
            self.assertEqual(viewer.series.received, 0)
            peers[1].write(packet()[23:] + packet(3, 6000))
            until(lambda: viewer.series.received == 2 and len(viewer.logs) == 2)
            viewer.refresh()
            self.assertEqual(viewer.series.missing, 1)
            self.assertEqual(len(viewer.curves[0][0].getData()[0]), 3)
            viewer.level.setCurrentText("E")
            self.assertIn("lost", viewer.log_view.toPlainText())
            self.assertNotIn("init", viewer.log_view.toPlainText())
            viewer.tag.setText("imu")
            self.assertEqual(viewer.log_view.toPlainText(), "")
            viewer.tag.clear()
            peers[1].disconnectFromHost()
            until(lambda: servers[1].hasPendingConnections())
            peers.append(servers[1].nextPendingConnection())
            peers[-1].write(packet(100, 100000))
            until(lambda: viewer.series.received == 3)
            self.assertEqual(viewer.series.missing, 1)
            self.assertEqual(viewer.series.points[-1][0], 0)
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
            peers[-1].write(burst)
            until(lambda: viewer.series.received == 4003)
            self.assertEqual(viewer.series.missing, 1)
            self.assertLessEqual(len(viewer.series.points), 4001)
            peers[0].write(b"[3][I][imu] filtered but saved\n")
            until(lambda: len(viewer.logs) == 3)
            self.assertNotIn("filtered but saved", viewer.log_view.toPlainText())
            viewer.toggle_recording()
            until(lambda: viewer.writer.done.is_set())
            self.assertIsNone(viewer.writer.error)
            self.assertEqual(sum(viewer.writer.dropped.values()), 0)
            directory = viewer.writer.directory
            with next(directory.glob("imu_*.csv")).open(newline="") as file:
                rows = list(csv.reader(file))
            self.assertEqual(len(rows), 4004)
            self.assertNotEqual(rows[1][0], rows[3][0])
            self.assertIn("filtered but saved", next(directory.glob("events_*.log")).read_text())
            self.assertGreater(
                len(bytes.fromhex(next(directory.glob("data_*.hex")).read_text())), 4000 * 48
            )
            viewer.level.setCurrentText("All")
            viewer.refresh()
            image_path = os.environ.get("RTT_TEST_SCREENSHOT")
            if image_path:
                viewer.grab().save(image_path)
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
