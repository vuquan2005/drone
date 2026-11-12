#!/usr/bin/env python3
"""View existing RTT streams: uv run --group rtt tools/rtt/view.py."""

import argparse
from collections import deque
import re
import sys
import time

import numpy as np
from PySide6 import QtCore, QtGui, QtNetwork, QtWidgets
import pyqtgraph as pg

from records import ImuSeries, Lines


class Stream(QtCore.QObject):
    line = QtCore.Signal(bytes)
    state = QtCore.Signal(str)
    connected = QtCore.Signal()

    def __init__(self, host, port, parent):
        super().__init__(parent)
        self.host, self.port = host, port
        self.framing = Lines()
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
        self.framing.discarding = False
        self.state.emit("connected")
        self.connected.emit()

    def read(self):
        # A bounded batch lets repaint/other sockets run even under continuous traffic.
        for line in self.framing.feed(bytes(self.socket.read(16384))):
            self.line.emit(line)
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
        self.data_stream = Stream(args.host, args.data_port, self)
        self.log_stream.line.connect(self.on_log)
        self.data_stream.line.connect(self.on_data)
        self.data_stream.connected.connect(self.new_session)
        self.log_stream.state.connect(lambda value: self.states.update(LOG=value))
        self.data_stream.state.connect(lambda value: self.states.update(DATA=value))
        self.timer = QtCore.QTimer(self)
        self.timer.timeout.connect(self.refresh)
        self.timer.start(40)

    def new_session(self):
        self.series.new_connection()
        self.arrivals.clear()
        self.dirty = True

    def on_data(self, line):
        if self.series.feed(line):
            self.arrivals.append(time.monotonic())
            self.dirty = True

    def on_log(self, line):
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

    def refresh(self):
        now = time.monotonic()
        while self.arrivals and self.arrivals[0] < now - 1:
            self.arrivals.popleft()
        s = self.series
        self.statusBar().showMessage(
            f"LOG: {self.states['LOG']} | DATA: {self.states['DATA']} | "
            f"RX {len(self.arrivals)}/s · records {s.received} · missing seq {s.missing} · "
            f"invalid {s.invalid} · unknown {s.unknown} · resets? {s.resets} · "
            f"oversize lines {self.log_stream.framing.errors + self.data_stream.framing.errors}"
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--log-port", type=int, default=9090)
    parser.add_argument("--data-port", type=int, default=9091)
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
