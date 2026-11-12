"""Optional offscreen GUI/TCP integration test; never connects to a probe."""

import importlib.util
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
            peers[1].write(b"IMU1,1,1000,42,1000,0,9800,")
            peers[1].flush()
            # Pump the partial record before sending its suffix.
            for _ in range(5):
                app.processEvents()
            self.assertEqual(viewer.series.received, 0)
            peers[1].write(b"0,0,0\nIMU1,3,6000,43,2000,0,9800,0,0,0\n")
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
            peers[-1].write(b"IMU1,100,100000,42,3000,0,9800,0,0,0\n")
            until(lambda: viewer.series.received == 3)
            self.assertEqual(viewer.series.missing, 1)
            self.assertEqual(viewer.series.points[-1][0], 0)
            burst = b"".join(
                f"IMU1,{101+i},{102500+2500*i},42,{int(1000*math.sin(i/100))},0,9800,0,{int(100*math.cos(i/100))},0\n".encode()
                for i in range(4000)
            )
            peers[-1].write(burst)
            until(lambda: viewer.series.received == 4003)
            self.assertEqual(viewer.series.missing, 1)
            self.assertLessEqual(len(viewer.series.points), 4001)
            viewer.level.setCurrentText("All")
            viewer.refresh()
            image_path = os.environ.get("RTT_TEST_SCREENSHOT")
            if image_path:
                viewer.grab().save(image_path)
        finally:
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
