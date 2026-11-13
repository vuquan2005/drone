"""IMU, receiver, statistics and log widgets; no transport or disk ownership."""

from collections import deque
import re

import numpy as np
from PySide6 import QtGui, QtWidgets
import pyqtgraph as pg


class ImuPanel(pg.GraphicsLayoutWidget):
    def __init__(self):
        super().__init__()
        self.setBackground("w")
        self.plots, self.curves = [], []
        for index, (title, unit) in enumerate((("Acceleration", "m/s²"), ("Gyroscope", "rad/s"))):
            plot = self.addPlot(row=index, col=0, title=title)
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

    def update_series(self, series):
        points = np.asarray(series.points, dtype=float)
        for group, curves in enumerate(self.curves):
            for axis, curve in enumerate(curves):
                if points.size:
                    curve.setData(points[:, 0], points[:, 1 + group * 3 + axis])
                else:
                    curve.setData([], [])
        if points.size:
            end = max(10, points[-1, 0])
            self.plots[0].setXRange(end - 10, end, padding=0)


class ReceiverPanel(QtWidgets.QWidget):
    def __init__(self):
        super().__init__()
        layout = QtWidgets.QGridLayout(self)
        self.rc_status = QtWidgets.QLabel("RC: waiting for type 3 frames")
        layout.addWidget(self.rc_status, 0, 0, 1, 4)
        self.rc_bars = []
        for i in range(16):
            bar = QtWidgets.QProgressBar()
            bar.setRange(0, 2047)
            bar.setFormat("—")
            layout.addWidget(QtWidgets.QLabel(f"CH{i + 1}"), i % 8 + 1, (i // 8) * 2)
            layout.addWidget(bar, i % 8 + 1, (i // 8) * 2 + 1)
            self.rc_bars.append(bar)

    def update_sample(self, sample, age, data_connected):
        if sample is None:
            self.rc_status.setText("RC: waiting for type 3 frames")
            for bar in self.rc_bars:
                bar.setValue(0)
                bar.setFormat("—")
        else:
            seq, ms, last_ms, connected, *channels = sample
            live = age < 0.5 and data_connected
            state = ("connected" if connected else "disconnected") if live else "telemetry stale"
            self.rc_status.setText(
                f"RC: {state} · snapshot #{seq}, age {age:.1f}s · "
                f"last RC frame age at MCU snapshot {(ms - last_ms) % 2**32}ms · raw CRSF"
            )
            for bar, value in zip(self.rc_bars, channels):
                bar.setValue(value)
                bar.setFormat(str(value))
                bar.setEnabled(live and bool(connected))


class StatsLabel(QtWidgets.QLabel):
    def __init__(self):
        super().__init__("MCU statistics: waiting for type 2 frames")
        self.setWordWrap(True)

    def update_sample(self, sample, age):
        if sample is None:
            self.setText("MCU statistics: waiting for type 2 frames")
        else:
            (
                seq,
                ms,
                samples,
                errors,
                log_full,
                data_full,
                imu_full,
                stats_full,
                imu_reject,
                stats_reject,
            ) = sample
            self.setText(
                f"MCU stats #{seq} · age {age:.1f}s · uptime {ms}ms · IMU reads {samples}, errors {errors} · "
                f"RTT full LOG={log_full} DATA={data_full} (IMU={imu_full}, stats={stats_full}) · "
                f"rejected IMU={imu_reject} stats={stats_reject}"
            )


class LogPanel(QtWidgets.QWidget):
    colors = {"E": "#c62828", "W": "#9a6700", "I": "#1565c0", "D": "#666666"}
    log_pattern = re.compile(r"^\[\d+\]\[([EWID])\]\[([^]]+)\] ")

    def __init__(self):
        super().__init__()
        self.logs = deque(maxlen=2000)
        layout = QtWidgets.QVBoxLayout(self)
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
        layout.addLayout(filters)
        self.log_view = QtWidgets.QPlainTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setMaximumBlockCount(2000)
        self.log_view.setFont(QtGui.QFont("monospace", 10))
        layout.addWidget(self.log_view)

    def add_line(self, line):
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
