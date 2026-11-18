"""IMU, receiver, statistics and log widgets; no transport or disk ownership."""

from collections import deque
import re

import numpy as np
from PySide6 import QtCore, QtGui, QtWidgets
import pyqtgraph as pg

from telemetry import attitude_degrees

AXIS_COLORS = ("#d1495b", "#218739", "#246bc1")
AXIS_LINE_WIDTH = 1


class ImuPanel(pg.GraphicsLayoutWidget):
    def __init__(self):
        super().__init__()
        self.setBackground("w")
        self.plots, self.curves = [], []
        groups = (
            ("Acceleration", "m/s²", ("X", "Y", "Z")),
            ("Gyroscope", "rad/s", ("Roll", "Pitch", "Yaw")),
        )
        for index, (title, unit, labels) in enumerate(groups):
            plot = self.addPlot(row=index, col=0, title=title)
            plot.setLabel("left", unit)
            plot.getAxis("left").enableAutoSIPrefix(False)
            plot.setLabel("bottom", "Elapsed time", units="s")
            plot.showGrid(x=True, y=True, alpha=0.2)
            plot.addLegend()
            self.plots.append(plot)
            self.curves.append(
                [
                    plot.plot(
                        name=label,
                        pen=pg.mkPen(color, width=AXIS_LINE_WIDTH),
                        connect="finite",
                    )
                    for label, color in zip(labels, AXIS_COLORS)
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
        colors = (*AXIS_COLORS, "#8f50aa")
        groups = [
            (
                axis,
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
            plot.getAxis("left").enableAutoSIPrefix(False)
            if row == 2:
                plot.setLabel("bottom", "Elapsed time", units="s")
            plot.showGrid(x=True, y=True, alpha=0.2)
            plot.addLegend()
            plot.setXRange(0, 10, padding=0)
            for name, column, color in traces:
                self.curves.append(
                    (
                        plot.plot(
                            name=name,
                            pen=pg.mkPen(color, width=AXIS_LINE_WIDTH),
                            connect="finite",
                        ),
                        column,
                    )
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


class AttitudeModel(pg.PlotWidget):
    """Orthographic view of body FRD in an NED reference frame."""

    # Screen right and screen up, expressed in NED.
    camera = np.array(((0.6, 0.8, 0), (0.4, -0.3, -np.sqrt(0.75))))

    def __init__(self):
        super().__init__()
        self.setBackground("w")
        self.setMinimumSize(220, 220)
        self.setAspectLocked()
        self.setRange(xRange=(-1.2, 1.2), yRange=(-1.2, 1.2), padding=0)
        self.disableAutoRange()
        self.setMouseEnabled(x=False, y=False)
        self.setMenuEnabled(False)
        self.hideButtons()
        self.hideAxis("left")
        self.hideAxis("bottom")

        # Fixed NED reference plane, projected with the same orthographic camera.
        grid_extent = 0.75
        grid_down = 0.72
        self.grid = []
        grid_pen = pg.mkPen("#e8edf2", width=0.7)
        for offset in np.linspace(-grid_extent, grid_extent, 9):
            for points in (
                ((offset, -grid_extent, grid_down), (offset, grid_extent, grid_down)),
                ((-grid_extent, offset, grid_down), (grid_extent, offset, grid_down)),
            ):
                projected = np.asarray(points) @ self.camera.T
                self.grid.append(self.plot(*projected.T, pen=grid_pen))

        self.axes = []
        for name, color in (
            ("X · Forward", AXIS_COLORS[0]),
            ("Y · Right", AXIS_COLORS[1]),
            ("Z · Down", AXIS_COLORS[2]),
        ):
            line = self.plot(pen=pg.mkPen(color, width=AXIS_LINE_WIDTH))
            label = pg.TextItem(name, color=color, anchor=(0.5, 0.5))
            self.addItem(label)
            self.axes.append((line, label))
        self.set_quaternion(None)

    def set_quaternion(self, q):
        items = tuple(item for pair in self.axes for item in pair)
        for item in items:
            item.setVisible(q is not None)
        if q is None:
            return

        w, x, y, z = np.asarray(q) / np.linalg.norm(q)
        body_to_ned = np.array(
            (
                (1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)),
                (2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)),
                (2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)),
            )
        )
        body_to_screen = self.camera @ body_to_ned
        for axis, (line, label) in enumerate(self.axes):
            tip_x, tip_y = body_to_screen[:, axis]
            line.setData([0, tip_x], [0, tip_y])
            label.setPos(tip_x * 1.15, tip_y * 1.15)


class AttitudePanel(QtWidgets.QWidget):
    def __init__(self):
        super().__init__()
        layout = QtWidgets.QVBoxLayout(self)
        graph = pg.PlotWidget(background="w")
        content = QtWidgets.QSplitter(QtCore.Qt.Orientation.Horizontal)
        self.model = AttitudeModel()
        content.addWidget(self.model)
        content.addWidget(graph)
        content.setStretchFactor(0, 1)
        content.setStretchFactor(1, 2)
        layout.addWidget(content, 1)
        self.status = QtWidgets.QLabel("Waiting for attitude")
        layout.insertWidget(0, self.status)
        self.plot = graph.getPlotItem()
        self.plot.setLabel("left", "Angle (°)")
        self.plot.showGrid(x=True, y=True, alpha=0.2)
        self.plot.addLegend()
        self.curves = [
            self.plot.plot(name=axis, pen=pg.mkPen(color, width=AXIS_LINE_WIDTH), connect="finite")
            for axis, color in zip(("Roll", "Pitch", "Yaw"), AXIS_COLORS)
        ]

    def update_attitude(self, state, now=None):
        saved = now is None
        sample = state.latest_attitude
        age = now - state.attitude_received_at if not saved and sample is not None else 0
        self.model.set_quaternion(sample[2:] if sample is not None and age <= 0.25 else None)
        if sample is None:
            self.status.setText("No saved attitude" if saved else "Waiting for attitude")
        elif age > 0.25:
            self.status.setText(f"Attitude stale · {age:.2f} s")
        else:
            roll, pitch, yaw = attitude_degrees(sample[2:])
            self.status.setText(f"Roll {roll:+.1f}°    Pitch {pitch:+.1f}°    Yaw {yaw:+.1f}°")
        data = np.asarray(state.attitude_points, dtype=float)
        for axis, curve in enumerate(self.curves, 1):
            if data.size:
                curve.setData(data[:, 0], data[:, axis])
            else:
                curve.setData([], [])
        self.plot.setLabel("bottom", "Elapsed time", units="s")
        if saved:
            self.plot.enableAutoRange()
        else:
            end = max(10, data[-1, 0]) if data.size else 10
            self.plot.setXRange(end - 10, end, padding=0)


class AttitudeControlPanel(QtWidgets.QWidget):
    def __init__(self):
        super().__init__()
        layout = QtWidgets.QVBoxLayout(self)
        self.status = QtWidgets.QLabel()
        layout.addWidget(self.status)
        self.graph = pg.GraphicsLayoutWidget()
        self.graph.setBackground("w")
        layout.addWidget(self.graph, 1)
        self.plots, self.curves = [], []
        groups = (
            ("Roll", "°", (("Target", AXIS_COLORS[0]), ("Estimated", "#333333"))),
            ("Pitch", "°", (("Target", AXIS_COLORS[1]), ("Estimated", "#333333"))),
            ("Tilt error", "2 × quaternion vector", (("X", AXIS_COLORS[0]), ("Y", AXIS_COLORS[1]))),
            ("Rate request", "rad/s", (("X", AXIS_COLORS[0]), ("Y", AXIS_COLORS[1]))),
        )
        for index, (title, unit, traces) in enumerate(groups):
            plot = self.graph.addPlot(row=index // 2, col=index % 2, title=title)
            plot.setLabel("left", unit)
            plot.getAxis("left").enableAutoSIPrefix(False)
            if index >= 2:
                plot.setLabel("bottom", "Elapsed time", units="s")
            plot.showGrid(x=True, y=True, alpha=0.2)
            plot.addLegend()
            self.curves.extend(
                plot.plot(name=name, pen=pg.mkPen(color, width=AXIS_LINE_WIDTH), connect="finite")
                for name, color in traces
            )
            if self.plots:
                plot.setXLink(self.plots[0])
            self.plots.append(plot)
        self.status.setText("Waiting for attitude control (SC MIDDLE/HIGH)")

    def update_control(self, state, now=None):
        sample = state.latest_attitude_control
        if sample is None:
            self.status.setText(
                "No saved attitude control"
                if now is None
                else "Waiting for attitude control (SC MIDDLE/HIGH)"
            )
        elif now is not None and now - state.attitude_control_received_at > 0.25:
            self.status.setText("No recent attitude control samples")
        else:
            _, control_us, estimate_us, flags = sample[:4]
            estimate_age_ms = (control_us - estimate_us) % 2**32 / 1000
            source = "Saved" if now is None else "Live"
            status = "valid" if flags else "invalid correction"
            self.status.setText(f"{source} · {status} · estimate age {estimate_age_ms:.2f} ms")
        data = np.asarray(state.attitude_control_points, dtype=float)
        for column, curve in enumerate(self.curves, 1):
            if data.size:
                curve.setData(data[:, 0], data[:, column])
            else:
                curve.setData([], [])
        if now is None:
            for plot in self.plots:
                plot.enableAutoRange()
        else:
            end = max(10, data[-1, 0]) if data.size else 10
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
        self.rc_snapshots = []
        self.replay_controls = QtWidgets.QWidget()
        replay_layout = QtWidgets.QHBoxLayout(self.replay_controls)
        replay_layout.setContentsMargins(0, 0, 0, 0)
        self.replay_slider = QtWidgets.QSlider(QtCore.Qt.Orientation.Horizontal)
        self.replay_slider.setAccessibleName("Saved receiver snapshot")
        self.replay_slider.valueChanged.connect(self.show_saved_snapshot)
        self.replay_position = QtWidgets.QLabel()
        replay_layout.addWidget(self.replay_slider, 1)
        replay_layout.addWidget(self.replay_position)
        self.replay_controls.hide()
        layout.addWidget(self.replay_controls)
        self.details = QtWidgets.QLabel("Raw CRSF (0–2047)")
        self.details.setWordWrap(True)
        self.details.setSizePolicy(
            QtWidgets.QSizePolicy.Policy.Ignored, QtWidgets.QSizePolicy.Policy.Preferred
        )
        layout.addWidget(self.details)
        self.wide = None
        self.update_responsive()

    def set_saved_snapshots(self, snapshots):
        self.rc_snapshots = snapshots
        self.replay_slider.blockSignals(True)
        self.replay_slider.setRange(0, max(0, len(snapshots) - 1))
        self.replay_slider.setValue(max(0, len(snapshots) - 1))
        self.replay_slider.setEnabled(len(snapshots) > 1)
        self.replay_slider.blockSignals(False)
        self.replay_controls.setVisible(bool(snapshots))
        if snapshots:
            self.show_saved_snapshot(self.replay_slider.value())
        else:
            self.replay_position.clear()

    def show_saved_snapshot(self, index):
        if not self.rc_snapshots:
            return
        seconds, sample = self.rc_snapshots[index]
        self.update_sample(sample, 0, True, saved=True)
        self.replay_position.setText(f"{seconds:.2f}s · {index + 1}/{len(self.rc_snapshots)}")

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
        self.rc_status.setText(f"Saved RC: {state}" if saved else f"RC: {state}")
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
