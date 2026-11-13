#!/usr/bin/env python3
"""View existing RTT streams: uv run --group rtt tools/rtt/view.py."""

import argparse
from pathlib import Path
import sys
import time

from PySide6 import QtCore, QtWidgets

from panels import ImuPanel, ReceiverPanel, StatsLabel, LogPanel
from stream import Stream
from telemetry import TelemetryState
from session_writer import SessionWriter


class Viewer(QtWidgets.QMainWindow):
    def __init__(self, args):
        super().__init__()
        self.setWindowTitle("Drone · RTT")
        self.resize(1100, 820)
        self.telemetry = TelemetryState()
        self.writer = None
        self.recording = False
        self.close_when_done = False
        self.states = {"LOG": "connecting", "DATA": "connecting"}
        self.dirty = False
        root = QtWidgets.QWidget()
        self.setCentralWidget(root)
        layout = QtWidgets.QVBoxLayout(root)
        endpoints = QtWidgets.QLabel(
            f"{args.host} · LOG :{args.log_port} · DATA :{args.data_port} · 10 s history"
        )
        layout.addWidget(endpoints)
        self.mcu_stats = StatsLabel()
        layout.addWidget(self.mcu_stats)
        controls = QtWidgets.QHBoxLayout()
        self.output_dir = QtWidgets.QLineEdit(str(getattr(args, "output_dir", DEFAULT_OUTPUT)))
        self.browse = QtWidgets.QPushButton("Save folder…")
        self.browse.clicked.connect(self.choose_output)
        self.raw_checkbox = QtWidgets.QCheckBox("Save raw hex")
        self.raw_checkbox.setChecked(getattr(args, "raw", False))
        self.record_button = QtWidgets.QPushButton("Start recording")
        self.record_button.clicked.connect(self.toggle_recording)
        controls.addWidget(self.output_dir)
        controls.addWidget(self.browse)
        controls.addWidget(self.raw_checkbox)
        controls.addWidget(self.record_button)
        layout.addLayout(controls)
        self.record_status = QtWidgets.QLabel("Recording off")
        self.record_status.setWordWrap(True)
        layout.addWidget(self.record_status)
        splitter = QtWidgets.QSplitter(QtCore.Qt.Orientation.Vertical)
        layout.addWidget(splitter)
        tabs = QtWidgets.QTabWidget()
        self.imu_panel = ImuPanel()
        self.receiver_panel = ReceiverPanel()
        tabs.addTab(self.imu_panel, "IMU")
        tabs.addTab(self.receiver_panel, "Receiver · RC")
        splitter.addWidget(tabs)
        self.log_panel = LogPanel()
        splitter.addWidget(self.log_panel)
        splitter.setSizes([500, 230])
        self.log_stream = Stream(args.host, args.log_port, self)
        self.data_stream = Stream(args.host, args.data_port, self, binary=True)
        self.log_stream.record.connect(self.on_log)
        self.data_stream.record.connect(self.on_data)
        self.data_stream.raw.connect(self.on_raw)
        self.data_stream.connected.connect(self.new_session)
        self.log_stream.state.connect(lambda value: self.states.update(LOG=value))
        self.data_stream.state.connect(lambda value: self.states.update(DATA=value))
        self.timer = QtCore.QTimer(self)
        self.timer.timeout.connect(self.refresh)
        self.timer.start(40)
        if getattr(args, "record", False):
            QtCore.QTimer.singleShot(0, self.toggle_recording)

    def new_session(self):
        self.telemetry.new_connection()
        self.receiver_panel.update_sample(None, 0, False)
        self.dirty = True

    def on_data(self, line):
        segment = self.telemetry.segment
        record = self.telemetry.feed(line, time.monotonic())
        if record is None:
            return
        kind, values = record
        if self.recording:
            self.writer.submit(kind, (self.telemetry.segment, *values))
        if kind == "csv" or self.telemetry.segment != segment:
            self.dirty = True

    def on_raw(self, data):
        if self.recording and self.raw_checkbox.isChecked():
            self.writer.submit("raw", data)

    def on_log(self, line):
        if self.recording:
            self.writer.submit("log", line + b"\n")
        self.log_panel.add_line(line)

    def choose_output(self):
        directory = QtWidgets.QFileDialog.getExistingDirectory(
            self, "Telemetry output folder", self.output_dir.text()
        )
        if directory:
            self.output_dir.setText(directory)

    def toggle_recording(self):
        if self.recording:
            self.recording = False
            self.writer.stop()
            return
        if self.writer is not None and not self.writer.done.is_set():
            return
        try:
            self.writer = SessionWriter(self.output_dir.text(), raw=self.raw_checkbox.isChecked())
        except OSError as error:
            self.record_status.setText(f"Recording ERROR: {error}")
            return
        self.recording = True
        self.update_record_status()

    def update_record_status(self):
        if self.writer is None:
            return
        w = self.writer
        if w.error is not None:
            self.recording = False
            w.stop()
        busy = not w.done.is_set()
        self.record_button.setEnabled(self.recording or not busy)
        self.record_button.setText(
            "Stop recording" if self.recording else "Stopping…" if busy else "Start recording"
        )
        for widget in (self.output_dir, self.browse, self.raw_checkbox):
            widget.setEnabled(not busy)
        state = "Recording" if self.recording else "Flushing" if busy else "Saved"
        if w.error:
            state = f"Recording ERROR: {w.error} (session may be incomplete)"
        drops = w.dropped
        self.record_status.setText(
            f"{state}: {w.directory} · queue {w.queue.qsize()} · "
            f"writer drops CSV={drops['csv']} stats={drops['stats']} RC={drops['rc']} LOG={drops['log']} raw chunks={drops['raw']}"
        )
        if self.close_when_done and not busy:
            QtCore.QTimer.singleShot(0, self.close)

    def closeEvent(self, event):
        if self.writer is not None and not self.writer.done.is_set():
            self.recording = False
            self.writer.stop()
            self.close_when_done = True
            self.update_record_status()
            event.ignore()
            return
        for stream in (self.log_stream, self.data_stream):
            stream.retry.stop()
            stream.socket.abort()
        super().closeEvent(event)

    def refresh(self):
        self.update_record_status()
        state = self.telemetry
        now = time.monotonic()
        self.receiver_panel.update_sample(
            state.latest_rc,
            now - state.rc_received_at if state.rc_received_at is not None else 0,
            self.states["DATA"] == "connected",
        )
        self.mcu_stats.update_sample(
            state.latest_stats,
            now - state.stats_received_at if state.stats_received_at is not None else 0,
        )
        while state.arrivals and state.arrivals[0] < now - 1:
            state.arrivals.popleft()
        s = state.series
        self.statusBar().showMessage(
            f"LOG: {self.states['LOG']} | DATA: {self.states['DATA']} | "
            f"RX {len(state.arrivals)}/s · records {s.received} · missing seq {s.missing} · "
            f"invalid {s.invalid} · unknown {s.unknown} · resets? {s.resets} · "
            f"log oversize {self.log_stream.framing.errors} · frame errors {self.data_stream.framing.errors}"
        )
        if not self.dirty:
            return
        self.dirty = False
        self.imu_panel.update_series(state.series)


DEFAULT_OUTPUT = Path(__file__).resolve().parents[2] / "experiments/raw/telemetry"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--log-port", type=int, default=9090)
    parser.add_argument("--data-port", type=int, default=9091)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--record", action="store_true", help="Start a recording session immediately"
    )
    parser.add_argument(
        "--raw", action="store_true", help="Include raw DATA as readable .hex text when recording"
    )
    args = parser.parse_args()
    if not all(0 < port < 65536 for port in (args.log_port, args.data_port)):
        parser.error("Ports must be between 1 and 65535")
    if args.log_port == args.data_port:
        parser.error("LOG and DATA need different TCP ports")
    print("RTT viewer starting", flush=True)
    app = QtWidgets.QApplication([sys.argv[0]])
    viewer = Viewer(args)
    viewer.show()
    # Release the VS Code background pre-launch task after Qt initializes.
    print("RTT viewer ready", flush=True)
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
