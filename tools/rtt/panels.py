"""IMU, receiver, statistics and log widgets; no transport or disk ownership."""

from collections import deque
import re

import numpy as np
from PySide6 import QtCore, QtGui, QtWidgets
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

    def update_series(self, series, follow=True):
        points = np.asarray(series.points, dtype=float)
        for group, curves in enumerate(self.curves):
            for axis, curve in enumerate(curves):
                if points.size:
                    curve.setData(points[:, 0], points[:, 1 + group * 3 + axis])
                else:
                    curve.setData([], [])
        if points.size and follow:
            end = max(10, points[-1, 0])
            self.plots[0].setXRange(end - 10, end, padding=0)


class RatePanel(QtWidgets.QWidget):
    """Plots exist before the first snapshot; invalid samples break traces."""

    def __init__(self):
        super().__init__()
        layout = QtWidgets.QVBoxLayout(self)
        self.status = QtWidgets.QLabel("Waiting for rate telemetry")
        self.status.setWordWrap(True)
        layout.addWidget(self.status)
        self.graph = pg.GraphicsLayoutWidget()
        self.graph.setBackground("w")
        layout.addWidget(self.graph)
        self.curves = []
        self.plots = []
        colors = ("#d1495b", "#218739", "#246bc1", "#8f50aa")
        groups = [
            (
                axis + " · FRD",
                "rad/s",
                row,
                0,
                [("Setpoint", 1 + row, colors[row]), ("Gyro", 4 + row, "#333333")],
            )
            for row, axis in enumerate(("Roll", "Pitch", "Yaw"))
        ]
        groups += [
            (
                "PID",
                "normalized",
                0,
                1,
                [(axis, 7 + i, colors[i]) for i, axis in enumerate(("Roll", "Pitch", "Yaw"))],
            ),
            (
                "Mixer thrust",
                "normalized",
                1,
                1,
                [(f"M{i+1}", 10 + i, colors[i]) for i in range(4)],
            ),
            (
                "Allocation residual",
                "normalized",
                2,
                1,
                [(axis, 14 + i, colors[i]) for i, axis in enumerate(("Roll", "Pitch", "Yaw"))],
            ),
        ]
        for title, unit, row, col, traces in groups:
            plot = self.graph.addPlot(row=row, col=col, title=title)
            plot.setLabel("left", unit)
            if row == 2:
                plot.setLabel("bottom", "Host time from segment start", units="s")
            plot.showGrid(x=True, y=True, alpha=0.2)
            plot.addLegend()
            plot.setXRange(0, 10, padding=0)
            for name, column, color in traces:
                self.curves.append(
                    (plot.plot(name=name, pen=pg.mkPen(color, width=1), connect="finite"), column)
                )
            if self.plots:
                plot.setXLink(self.plots[0])
            self.plots.append(plot)

    def setText(self, text):
        self.status.setText(text)

    def text(self):
        return self.status.text()

    def update_points(self, points, follow=True):
        data = np.asarray(points, dtype=float)
        for curve, column in self.curves:
            curve.setData(data[:, 0], data[:, column]) if data.size else curve.setData([], [])
        if data.size and follow:
            end = max(10, data[-1, 0])
            self.plots[0].setXRange(end - 10, end, padding=0)


class ChannelStick(QtWidgets.QWidget):
    """Read-only XY display of two raw channels."""

    def __init__(self, horizontal, vertical, horizontal_name, vertical_name):
        super().__init__()
        self.horizontal, self.vertical = horizontal, vertical
        self.horizontal_name, self.vertical_name = horizontal_name, vertical_name
        self.values = None
        self.active = False
        self.setMinimumSize(150, 160)
        self.setSizePolicy(
            QtWidgets.QSizePolicy.Policy.Expanding, QtWidgets.QSizePolicy.Policy.Expanding
        )

    def set_channels(self, values, active):
        self.values, self.active = values, active
        self.update()

    def paintEvent(self, event):
        painter = QtGui.QPainter(self)
        painter.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing)
        palette = self.palette()
        color = palette.color(QtGui.QPalette.ColorRole.Text)
        if not self.active:
            color.setAlpha(100)
        painter.setPen(QtGui.QPen(color, 1))
        side = max(1, min(self.width() - 28, self.height() - 60))
        box = QtCore.QRectF((self.width() - side) / 2, 12, side, side)
        painter.drawRect(box)
        painter.drawLine(
            QtCore.QPointF(box.center().x(), box.top()),
            QtCore.QPointF(box.center().x(), box.bottom()),
        )
        painter.drawLine(
            QtCore.QPointF(box.left(), box.center().y()),
            QtCore.QPointF(box.right(), box.center().y()),
        )
        label = (
            f"{self.horizontal_name} CH{self.horizontal} ↔ / "
            f"{self.vertical_name} CH{self.vertical} ↕"
        )
        if self.values is not None:
            x, y = self.values
            point = QtCore.QPointF(box.left() + x / 2047 * side, box.bottom() - y / 2047 * side)
            painter.setBrush(color)
            painter.drawEllipse(point, 6, 6)
            label += f"\n{x} / {y}"
        else:
            label += "\n— / —"
        painter.drawText(
            QtCore.QRectF(0, box.bottom() + 6, self.width(), 42),
            QtCore.Qt.AlignmentFlag.AlignCenter,
            label,
        )


class ReceiverPanel(QtWidgets.QWidget):
    def __init__(self):
        super().__init__()
        layout = QtWidgets.QVBoxLayout(self)
        self.rc_status = QtWidgets.QLabel("RC: waiting for data")
        layout.addWidget(self.rc_status)
        body = QtWidgets.QHBoxLayout()
        layout.addLayout(body, 1)
        self.stick_group = QtWidgets.QWidget()
        stick_layout = QtWidgets.QHBoxLayout(self.stick_group)
        stick_layout.setContentsMargins(0, 0, 0, 0)
        # Jumper T20 DA-Drone channel mapping: firmware/Jumper-T20/README.md.
        self.sticks = [
            ChannelStick(4, 3, "Yaw", "Throttle"),
            ChannelStick(1, 2, "Roll", "Pitch"),
        ]
        for stick in self.sticks:
            stick_layout.addWidget(stick)
        body.addWidget(self.stick_group, 1)
        self.bar_group = QtWidgets.QWidget()
        grid = self.bar_layout = QtWidgets.QGridLayout(self.bar_group)
        grid.setContentsMargins(0, 0, 0, 0)
        body.addWidget(self.bar_group, 1)
        self.rc_bars, self.channel_labels = [], []
        for i in range(16):
            bar = QtWidgets.QProgressBar()
            bar.setRange(0, 2047)
            bar.setFormat("—")
            label = QtWidgets.QLabel(f"CH{i + 1}")
            grid.addWidget(label, i % 8, (i // 8) * 2)
            grid.addWidget(bar, i % 8, (i // 8) * 2 + 1)
            self.channel_labels.append(label)
            self.rc_bars.append(bar)
        self.details = QtWidgets.QLabel("Raw CRSF (0–2047)")
        self.details.setWordWrap(True)
        self.details.setSizePolicy(
            QtWidgets.QSizePolicy.Policy.Ignored, QtWidgets.QSizePolicy.Policy.Preferred
        )
        layout.addWidget(self.details)
        self.wide = None
        self.update_responsive()

    def update_responsive(self):
        wide = self.width() >= 850 and self.height() >= 280
        self.stick_group.setVisible(wide)
        if wide != self.wide:
            self.wide = wide
            start, rows = (4, 6) if wide else (0, 8)
            for index in range(start, 16):
                row, col = (index - start) % rows, ((index - start) // rows) * 2
                self.bar_layout.addWidget(self.channel_labels[index], row, col)
                self.bar_layout.addWidget(self.rc_bars[index], row, col + 1)
        for label, bar in zip(self.channel_labels[:4], self.rc_bars[:4]):
            label.setVisible(not wide)
            bar.setVisible(not wide)
        self.details.setVisible(self.width() >= 600 and self.height() >= 300)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self.update_responsive()

    def update_sample(self, sample, age, data_connected, saved=False):
        if sample is None:
            self.rc_status.setText(
                "No saved RC in this segment" if saved else "RC: waiting for data"
            )
            self.details.setText("Raw CRSF (0–2047)")
            for bar in self.rc_bars:
                bar.setValue(0)
                bar.setFormat("—")
                bar.setEnabled(False)
            for stick in self.sticks:
                stick.set_channels(None, False)
            return
        _, _, _, connected, *channels = sample
        live = age < 0.5 and data_connected
        state = ("connected" if connected else "disconnected") if live else "telemetry stale"
        self.rc_status.setText(f"Saved RC: {state} · final snapshot" if saved else f"RC: {state}")
        freshness = "saved" if saved else f"age {age:.1f}s"
        self.details.setText(f"Raw CRSF (0–2047) · {freshness}")
        for bar, value in zip(self.rc_bars, channels):
            bar.setValue(value)
            bar.setFormat(str(value))
            bar.setEnabled(live and bool(connected))
        for stick in self.sticks:
            stick.set_channels(
                (channels[stick.horizontal - 1], channels[stick.vertical - 1]),
                live and bool(connected),
            )


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
