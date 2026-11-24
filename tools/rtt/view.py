#!/usr/bin/env python3
"""View existing RTT streams: uv run --group rtt tools/rtt/view.py."""

import argparse
import json
from pathlib import Path
import secrets
import sys
import time
from concurrent.futures import ThreadPoolExecutor

from PySide6 import QtCore, QtWidgets

from panels import (
    ImuPanel,
    ReceiverPanel,
    StatsLabel,
    LogPanel,
    RatePanel,
    AttitudePanel,
    AttitudeControlPanel,
)
from config_record import CONFIG_REQUEST, CONFIG_TYPE, decode_config_reply
from stream import Stream
from telemetry import TelemetryState
from session_writer import SessionWriter
from session_reader import read_session

CONFIG_RETRY_S = 1.0
CONFIG_MAX_ATTEMPTS = 5


class Viewer(QtWidgets.QMainWindow):
    def __init__(self, args):
        super().__init__()
        initial_session = getattr(args, "session", None)
        self.offline = initial_session is not None
        self.setWindowTitle("Drone · Saved telemetry" if self.offline else "Drone · RTT")
        self.resize(1100, 820)
        self.telemetry = TelemetryState()
        self.saved_session = None
        self.load_future = None
        self.loader = ThreadPoolExecutor(max_workers=1)
        self.writer = None
        self.recording = False
        self.close_when_done = False
        self.states = {"LOG": "connecting", "DATA": "connecting"}
        self.config_request_id = None
        self.config_requested_at = 0.0
        self.config_attempts = 0
        self.saved_config_segments = set()
        self.dirty = False
        root = QtWidgets.QWidget()
        self.setCentralWidget(root)
        layout = QtWidgets.QVBoxLayout(root)
        endpoints = QtWidgets.QLabel(
            f"{args.host} · LOG :{args.log_port} · DATA :{args.data_port} · 10 s history"
        )
        self.diagnostics = QtWidgets.QPushButton("Diagnostics")
        self.diagnostics.setCheckable(True)
        self.diagnostics.setChecked(False)
        self.diagnostic_details = QtWidgets.QWidget()
        details_layout = QtWidgets.QVBoxLayout(self.diagnostic_details)
        details_layout.setContentsMargins(0, 0, 0, 0)
        details_layout.addWidget(endpoints)
        self.mcu_stats = StatsLabel()
        details_layout.addWidget(self.mcu_stats)
        self.host_stats = QtWidgets.QLabel()
        self.host_stats.setWordWrap(True)
        details_layout.addWidget(self.host_stats)
        self.diagnostic_details.hide()
        self.diagnostics.toggled.connect(self.diagnostic_details.setVisible)
        controls = QtWidgets.QHBoxLayout()
        self.output_dir = QtWidgets.QLineEdit(str(getattr(args, "output_dir", DEFAULT_OUTPUT)))
        self.browse = QtWidgets.QPushButton("Save folder…")
        self.browse.clicked.connect(self.choose_output)
        self.raw_checkbox = QtWidgets.QCheckBox("Save raw hex")
        self.raw_checkbox.setChecked(getattr(args, "raw", False))
        self.record_button = QtWidgets.QPushButton("Start recording")
        self.record_button.clicked.connect(self.toggle_recording)
        self.record_options_button = QtWidgets.QPushButton("Recording options")
        self.record_options_button.setCheckable(True)
        controls.addWidget(self.diagnostics)
        controls.addWidget(self.record_options_button)
        self.open_button = QtWidgets.QPushButton("Open session…")
        self.open_button.clicked.connect(self.choose_session)
        controls.addWidget(self.open_button)
        self.live_button = QtWidgets.QPushButton("Return to live")
        self.live_button.clicked.connect(self.return_live)
        self.live_button.hide()
        controls.addWidget(self.live_button)
        self.segment_box = QtWidgets.QComboBox()
        self.segment_box.currentIndexChanged.connect(self.show_segment)
        self.segment_box.hide()
        controls.addWidget(self.segment_box)
        controls.addStretch()
        controls.addWidget(self.record_button)
        layout.addLayout(controls)
        layout.addWidget(self.diagnostic_details)
        self.record_options = QtWidgets.QWidget()
        options_layout = QtWidgets.QHBoxLayout(self.record_options)
        options_layout.setContentsMargins(0, 0, 0, 0)
        for widget in (self.output_dir, self.browse, self.raw_checkbox):
            options_layout.addWidget(widget)
        self.record_options.hide()
        self.record_options_button.toggled.connect(self.record_options.setVisible)
        layout.addWidget(self.record_options)
        self.writer_stats = QtWidgets.QLabel()
        self.writer_stats.setWordWrap(True)
        details_layout.addWidget(self.writer_stats)
        self.record_status = QtWidgets.QLabel()
        self.record_status.hide()
        self.record_status.setWordWrap(True)
        layout.addWidget(self.record_status)
        splitter = QtWidgets.QSplitter(QtCore.Qt.Orientation.Vertical)
        layout.addWidget(splitter)
        tabs = self.tabs = QtWidgets.QTabWidget()
        self.imu_panel = ImuPanel()
        self.receiver_panel = ReceiverPanel()
        self.rate_panel = RatePanel()
        self.attitude_panel = AttitudePanel()
        self.attitude_control_panel = AttitudeControlPanel()
        tabs.addTab(self.imu_panel, "IMU · FRD")
        tabs.addTab(self.receiver_panel, "Receiver · RC")
        tabs.addTab(self.rate_panel, "Rate · FRD")
        tabs.addTab(self.attitude_panel, "Attitude")
        tabs.addTab(self.attitude_control_panel, "Attitude Control")
        self.config_panel = QtWidgets.QPlainTextEdit("Waiting for the board configuration.")
        self.config_panel.setReadOnly(True)
        tabs.addTab(self.config_panel, "Configuration")
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
        self.tabs.currentChanged.connect(self.tab_changed)
        if self.offline:
            self.pause_streams()
            endpoints.hide()
            self.record_button.hide()
            self.record_options_button.hide()
            self.load_session(initial_session)
        elif getattr(args, "record", False):
            QtCore.QTimer.singleShot(0, self.toggle_recording)

    def choose_session(self):
        start = self.output_dir.text()
        if self.saved_session is not None:
            start = str(Path(self.load_directory).parent)
        directory = QtWidgets.QFileDialog.getExistingDirectory(
            self, "Open telemetry session", start
        )
        if directory:
            self.load_session(directory)

    def load_session(self, directory):
        if self.recording or (self.writer is not None and not self.writer.done.is_set()):
            return
        if self.load_future is not None:
            return
        self.open_button.setEnabled(False)
        self.record_button.setEnabled(False)
        self.statusBar().showMessage("Loading session…")
        self.pending_directory = str(directory)
        self.load_future = self.loader.submit(read_session, directory)

    def finish_load(self):
        future, self.load_future = self.load_future, None
        self.open_button.setEnabled(True)
        self.record_button.setEnabled(self.saved_session is None)
        try:
            segments, logs = future.result()
        except Exception as error:
            QtWidgets.QMessageBox.warning(self, "Cannot open session", str(error))
            return
        self.load_directory = self.pending_directory
        self.saved_session = segments
        self.pause_streams()
        self.record_button.setEnabled(False)
        self.record_status.hide()
        self.log_panel.clear_log()
        for line in logs:
            self.log_panel.add_line(line)
        self.log_panel.setToolTip("Session-wide log tail; not synchronized to the selected segment")
        self.segment_box.blockSignals(True)
        self.segment_box.clear()
        for segment in sorted(segments):
            self.segment_box.addItem(f"Segment {segment}", segment)
        self.segment_box.blockSignals(False)
        self.segment_box.show()
        self.live_button.setVisible(not self.offline)
        self.show_segment()

    def show_segment(self, *_):
        if self.saved_session is None or self.segment_box.currentData() is None:
            return
        state = self.saved_session[self.segment_box.currentData()]
        self.imu_panel.update_series(state.series, state.config, follow=False)
        self.rate_panel.update_points(state.rate_points, follow=False)
        self.attitude_panel.update_attitude(state)
        self.attitude_control_panel.update_control(state)
        for plot in (*self.imu_panel.plots, *self.rate_panel.plots):
            plot.enableAutoRange()
        for plot in self.rate_panel.plots[-2:]:
            plot.setLabel("bottom", "MCU time from first rate sample", units="s")
        self.receiver_panel.update_sample(state.latest_rc, 0, True, saved=True)
        self.receiver_panel.set_saved_snapshots(state.rc_snapshots)
        self.mcu_stats.update_sample(state.latest_stats, 0)
        self.mcu_stats.setText(
            self.mcu_stats.text().replace("age 0.0s · ", "")
            if state.latest_stats is not None
            else "No saved MCU statistics in this segment"
        )
        self.rate_panel.setText(
            "Saved rate · MCU time · streams have independent time origins"
            if state.latest_rate is not None
            else "No saved rate telemetry in this segment"
        )
        self.host_stats.setText(
            f"Saved IMU: {state.series.received} samples · missing seq {state.series.missing}"
        )
        self.show_config(state.config, "No configuration saved for this segment.")
        self.writer_stats.clear()
        self.statusBar().showMessage(f"Saved session: {self.load_directory} · saved data")

    def return_live(self):
        if self.offline or self.load_future is not None:
            return
        self.saved_session = None
        self.receiver_panel.set_saved_snapshots([])
        self.telemetry = TelemetryState()
        self.log_panel.clear_log()
        self.log_panel.setToolTip("")
        self.segment_box.hide()
        self.live_button.hide()
        self.record_button.setEnabled(True)
        self.imu_panel.update_series(self.telemetry.series)
        self.imu_panel.plots[0].setXRange(0, 10, padding=0)
        self.rate_panel.plots[0].setXRange(0, 10, padding=0)
        for plot in self.rate_panel.plots[-2:]:
            plot.setLabel("bottom", "Host time from segment start", units="s")
        self.resume_streams()

    def pause_streams(self):
        for stream in (self.log_stream, self.data_stream):
            stream.paused = True
            stream.retry.stop()
            stream.socket.abort()

    def resume_streams(self):
        for stream in (self.log_stream, self.data_stream):
            stream.paused = False
            stream.retry.start(2000)
            stream.connect()

    def tab_changed(self, *_):
        # Hidden live panels catch up from the unchanged telemetry history.
        self.dirty = True
        self.refresh()

    def new_session(self):
        self.telemetry.new_connection()
        self.reset_config_request()
        if self.tabs.currentWidget() is self.receiver_panel:
            self.receiver_panel.update_sample(None, 0, False)
        self.dirty = True

    def show_config(self, config, empty_message):
        self.config_panel.setPlainText(
            empty_message if config is None else json.dumps(config, indent=2)
        )

    def reset_config_request(self):
        self.config_request_id = secrets.randbits(32)
        self.config_requested_at = 0.0
        self.config_attempts = 0
        self.show_config(None, "Waiting for the board configuration.")

    def request_config(self, now):
        if (
            self.config_request_id is None
            or self.telemetry.config is not None
            or self.states["LOG"] != "connected"
            or self.states["DATA"] != "connected"
            or now - self.config_requested_at < CONFIG_RETRY_S
        ):
            return
        if self.config_attempts == CONFIG_MAX_ATTEMPTS:
            self.show_config(None, "The board did not return a configuration snapshot.")
            self.config_attempts += 1
        elif self.config_attempts < CONFIG_MAX_ATTEMPTS:
            request = CONFIG_REQUEST.pack(b"C", self.config_request_id)
            if self.log_stream.socket.write(request) == len(request):
                self.config_attempts += 1
            self.config_requested_at = now

    def save_config(self):
        state = self.telemetry
        if (
            self.recording
            and state.config is not None
            and state.segment not in self.saved_config_segments
            and self.writer.submit("config", {"segment": state.segment, **state.config})
        ):
            self.saved_config_segments.add(state.segment)

    def on_data(self, line):
        if self.saved_session is not None:
            return
        if line[:2] == (1, CONFIG_TYPE):
            try:
                request_id, config = decode_config_reply(line[2])
            except ValueError:
                self.telemetry.series.invalid += 1
                return
            if request_id == self.config_request_id and self.telemetry.config is None:
                self.telemetry.config = config
                self.dirty = True
                self.show_config(config, "")
                self.save_config()
            return
        segment = self.telemetry.segment
        record = self.telemetry.feed(line, time.monotonic())
        if record is None:
            return
        if self.telemetry.segment != segment:
            self.reset_config_request()
        kind, values = record
        if self.recording:
            if kind == "imu_batch":
                for sample in values:
                    self.writer.submit("imu_raw", (self.telemetry.segment, *sample))
            else:
                self.writer.submit(kind, (self.telemetry.segment, *values))
        if kind in ("imu_raw", "imu_batch") or self.telemetry.segment != segment:
            self.dirty = True

    def on_raw(self, data):
        if self.recording and self.raw_checkbox.isChecked():
            self.writer.submit("raw", data)

    def on_log(self, line):
        if self.saved_session is not None:
            return
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
        if self.offline or self.saved_session is not None or self.load_future is not None:
            return
        if self.recording:
            self.save_config()
            self.recording = False
            self.writer.stop()
            return
        if self.writer is not None and not self.writer.done.is_set():
            return
        try:
            self.writer = SessionWriter(self.output_dir.text(), raw=self.raw_checkbox.isChecked())
        except OSError as error:
            self.record_status.show()
            self.record_status.setText(f"Recording ERROR: {error}")
            return
        self.recording = True
        self.saved_config_segments.clear()
        self.save_config()
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
        self.record_status.show()
        drop_count = sum(drops.values())
        warning = f" · writer drops {drop_count}" if drop_count else ""
        if self.recording and self.telemetry.segment not in self.saved_config_segments:
            warning += " · configuration pending for current segment"
        self.record_status.setText(f"{state}: {w.directory}{warning}")
        self.writer_stats.setText(
            f"Queue {w.queue.qsize()} · "
            f"writer drops CSV={drops['imu_raw']} rate={drops['rate']} attitude={drops['attitude']} "
            f"attitude control={drops['attitude_control']} stats={drops['stats']} "
            f"RC={drops['rc'] + drops['rc_compact']} config={drops['config']} "
            f"LOG={drops['log']} raw chunks={drops['raw']}"
        )
        if self.close_when_done and not busy:
            QtCore.QTimer.singleShot(0, self.close)

    def closeEvent(self, event):
        if self.writer is not None and not self.writer.done.is_set():
            self.save_config()
            self.recording = False
            self.writer.stop()
            self.close_when_done = True
            self.update_record_status()
            event.ignore()
            return
        self.loader.shutdown(wait=False, cancel_futures=True)
        for stream in (self.log_stream, self.data_stream):
            stream.retry.stop()
            stream.socket.abort()
        super().closeEvent(event)

    def refresh(self):
        if self.load_future is not None:
            if self.load_future.done():
                self.finish_load()
            return
        if self.offline or self.saved_session is not None:
            return
        idle = not self.recording and (self.writer is None or self.writer.done.is_set())
        self.open_button.setEnabled(idle)
        self.update_record_status()
        state = self.telemetry
        now = time.monotonic()
        self.request_config(now)
        self.save_config()
        active_panel = self.tabs.currentWidget()
        if active_panel is self.receiver_panel:
            self.receiver_panel.update_sample(
                state.latest_rc,
                now - state.rc_received_at if state.rc_received_at is not None else 0,
                self.states["DATA"] == "connected",
            )
        if active_panel is self.attitude_panel:
            self.attitude_panel.update_attitude(state, now)
        if active_panel is self.attitude_control_panel:
            self.attitude_control_panel.update_control(state, now)
        if active_panel is self.rate_panel:
            self.rate_panel.update_points(state.rate_points)
            rate = state.latest_rate
            if rate is None:
                self.rate_panel.setText("Waiting for rate telemetry")
            else:
                age = now - state.rate_received_at
                connected = self.states["DATA"] == "connected"
                self.rate_panel.setText(
                    f"Rate · valid={bool(rate[3] & 2)} · "
                    f"{'live' if connected and age < 0.5 else 'telemetry stale'} · "
                    f"age={age:.2f} s · dt={rate[2]} us"
                )
        self.mcu_stats.update_sample(
            state.latest_stats,
            now - state.stats_received_at if state.stats_received_at is not None else 0,
        )
        while state.arrivals and state.arrivals[0] < now - 1:
            state.arrivals.popleft()
        s = state.series
        self.host_stats.setText(
            f"Records {s.received} · missing seq {s.missing} · "
            f"invalid {s.invalid} · unknown {s.unknown} · resets? {s.resets} · "
            f"log oversize {self.log_stream.framing.errors} · frame errors {self.data_stream.framing.errors}"
        )
        self.statusBar().showMessage(
            f"LOG: {self.states['LOG']} | DATA: {self.states['DATA']} | "
            f"RX {len(state.arrivals)}/s"
        )
        if active_panel is self.imu_panel and self.dirty:
            self.dirty = False
            self.imu_panel.update_series(state.series, state.config)


DEFAULT_OUTPUT = Path(__file__).resolve().parents[2] / "experiments/raw/telemetry"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--log-port", type=int, default=9090)
    parser.add_argument("--data-port", type=int, default=9091)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--session", type=Path, help="Open a saved telemetry session offline")
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
    if args.session is not None and args.record:
        parser.error("--session and --record cannot be used together")
    print("RTT viewer starting", flush=True)
    app = QtWidgets.QApplication([sys.argv[0]])
    viewer = Viewer(args)
    viewer.show()
    # Release the VS Code background pre-launch task after Qt initializes.
    print("RTT viewer ready", flush=True)
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
