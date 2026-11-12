#!/usr/bin/env python3
"""View existing RTT streams: uv run --group rtt tools/rtt/view.py."""

import argparse
from collections import deque
import re
from pathlib import Path
import sys
import time

import numpy as np
from PySide6 import QtCore, QtGui, QtNetwork, QtWidgets
import pyqtgraph as pg

from records import Frames, IMU, ImuSeries, Lines
from session_writer import SessionWriter


class Stream(QtCore.QObject):
    record = QtCore.Signal(object)
    raw = QtCore.Signal(bytes)
    state = QtCore.Signal(str)
    connected = QtCore.Signal()

    def __init__(self, host, port, parent, binary=False):
        super().__init__(parent)
        self.host, self.port = host, port
        self.framing = Frames() if binary else Lines()
        self.socket = QtNetwork.QTcpSocket(self)
        self.socket.setReadBufferSize(65536)
        self.socket.connected.connect(self.on_connected)
        self.socket.readyRead.connect(self.read)
        self.socket.disconnected.connect(lambda: self.state.emit("disconnected; retrying"))
        self.socket.errorOccurred.connect(lambda _: self.state.emit(self.socket.errorString()))
        self.retry = QtCore.QTimer(self)
        self.retry.timeout.connect(self.connect)
        self.retry.start(2000)
        QtCore.QTimer.singleShot(0, self.connect)

    def connect(self):
        if self.socket.state() == QtNetwork.QAbstractSocket.SocketState.UnconnectedState:
            self.state.emit("connecting")
            self.socket.connectToHost(self.host, self.port)

    def on_connected(self):
        self.framing.pending.clear()
        if isinstance(self.framing, Lines):
            self.framing.discarding = False
        self.state.emit("connected")
        self.connected.emit()

    def read(self):
        # A bounded batch lets repaint/other sockets run even under continuous traffic.
        data = bytes(self.socket.read(16384))
        self.raw.emit(data)
        for line in self.framing.feed(data):
            self.record.emit(line)
        if self.socket.bytesAvailable():
            QtCore.QTimer.singleShot(0, self.read)


class Viewer(QtWidgets.QMainWindow):
    colors = {"E": "#c62828", "W": "#9a6700", "I": "#1565c0", "D": "#666666"}
    log_pattern = re.compile(r"^\[\d+\]\[([EWID])\]\[([^]]+)\] ")

    def __init__(self, args):
        super().__init__()
        self.setWindowTitle("Drone · RTT")
        self.resize(1100, 820)
        self.series = ImuSeries()
        self.writer = None
        self.recording = False
        self.close_when_done = False
        self.segment = 0
        self.logs = deque(maxlen=2000)
        self.arrivals = deque(maxlen=20000)
        self.states = {"LOG": "connecting", "DATA": "connecting"}
        self.dirty = False
        root = QtWidgets.QWidget()
        self.setCentralWidget(root)
        layout = QtWidgets.QVBoxLayout(root)
        endpoints = QtWidgets.QLabel(
            f"{args.host} · LOG :{args.log_port} · DATA :{args.data_port} · 10 s history"
        )
        layout.addWidget(endpoints)
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
        graphs = pg.GraphicsLayoutWidget()
        graphs.setBackground("w")
        splitter.addWidget(graphs)
        self.plots, self.curves = [], []
        for index, (title, unit) in enumerate((("Acceleration", "m/s²"), ("Gyroscope", "rad/s"))):
            plot = graphs.addPlot(row=index, col=0, title=title)
            plot.setLabel("left", unit)
            plot.getAxis("left").enableAutoSIPrefix(False)
            plot.setLabel("bottom", "MCU time from session start", units="s")
            plot.showGrid(x=True, y=True, alpha=0.2)
            plot.addLegend()
            self.plots.append(plot)
            self.curves.append(
                [
                    plot.plot(name=axis, pen=pg.mkPen(color, width=1), connect="finite")
                    for axis, color in zip("XYZ", ("#d1495b", "#218739", "#246bc1"))
                ]
            )
        self.plots[1].setXLink(self.plots[0])
        panel = QtWidgets.QWidget()
        panel_layout = QtWidgets.QVBoxLayout(panel)
        filters = QtWidgets.QHBoxLayout()
        self.level = QtWidgets.QComboBox()
        self.level.addItems(["All", "E", "W", "I", "D"])
        self.level.currentTextChanged.connect(self.refilter)
        filters.addWidget(QtWidgets.QLabel("Level"))
        filters.addWidget(self.level)
        self.tag = QtWidgets.QLineEdit()
        self.tag.setPlaceholderText("Tag contains…")
        self.tag.textChanged.connect(self.refilter)
        filters.addWidget(self.tag)
        clear = QtWidgets.QPushButton("Clear log")
        clear.clicked.connect(self.clear_log)
        filters.addWidget(clear)
        panel_layout.addLayout(filters)
        self.log_view = QtWidgets.QPlainTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setMaximumBlockCount(2000)
        self.log_view.setFont(QtGui.QFont("monospace", 10))
        panel_layout.addWidget(self.log_view)
        splitter.addWidget(panel)
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
        self.segment += 1
        self.series.new_connection()
        self.arrivals.clear()
        self.dirty = True

    def on_data(self, line):
        resets = self.series.resets
        if self.series.feed(line):
            if self.series.resets != resets:
                self.segment += 1
            if self.recording:
                self.writer.submit("csv", (self.segment, *IMU.unpack(line[2])))
            self.arrivals.append(time.monotonic())
            self.dirty = True

    def on_raw(self, data):
        if self.recording and self.raw_checkbox.isChecked():
            self.writer.submit("raw", data)

    def on_log(self, line):
        if self.recording:
            self.writer.submit("log", line + b"\n")
        text = line.decode("utf-8", errors="replace")
        match = self.log_pattern.match(text)
        level, tag = match.groups() if match else ("?", "")
        entry = (level, tag, text)
        self.logs.append(entry)
        self.append_log(entry)

    def append_log(self, entry):
        level, tag, text = entry
        if self.level.currentText() not in ("All", level):
            return
        if self.tag.text().lower() not in tag.lower():
            return
        scroll = self.log_view.verticalScrollBar()
        follow = scroll.value() == scroll.maximum()
        cursor = QtGui.QTextCursor(self.log_view.document())
        cursor.movePosition(QtGui.QTextCursor.MoveOperation.End)
        fmt = QtGui.QTextCharFormat()
        fmt.setForeground(QtGui.QColor(self.colors.get(level, "#666666")))
        cursor.insertText(text + "\n", fmt)
        if follow:
            scroll.setValue(scroll.maximum())

    def refilter(self):
        self.log_view.clear()
        for entry in self.logs:
            self.append_log(entry)

    def clear_log(self):
        self.logs.clear()
        self.log_view.clear()

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
            f"writer drops CSV={drops['csv']} LOG={drops['log']} raw chunks={drops['raw']}"
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
        now = time.monotonic()
        while self.arrivals and self.arrivals[0] < now - 1:
            self.arrivals.popleft()
        s = self.series
        self.statusBar().showMessage(
            f"LOG: {self.states['LOG']} | DATA: {self.states['DATA']} | "
            f"RX {len(self.arrivals)}/s · records {s.received} · missing seq {s.missing} · "
            f"invalid {s.invalid} · unknown {s.unknown} · resets? {s.resets} · "
            f"log oversize {self.log_stream.framing.errors} · frame errors {self.data_stream.framing.errors}"
        )
        if not self.dirty:
            return
        self.dirty = False
        points = np.asarray(s.points, dtype=float)
        for group, curves in enumerate(self.curves):
            for axis, curve in enumerate(curves):
                if points.size:
                    curve.setData(points[:, 0], points[:, 1 + group * 3 + axis])
                else:
                    curve.setData([], [])
        if points.size:
            end = max(10, points[-1, 0])
            self.plots[0].setXRange(end - 10, end, padding=0)


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
    app = QtWidgets.QApplication([sys.argv[0]])
    viewer = Viewer(args)
    viewer.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
