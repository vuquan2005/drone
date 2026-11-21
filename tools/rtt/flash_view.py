"""Manage OpenOCD and export the board's telemetry flash."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import sys
from threading import Event

from PySide6 import QtCore, QtGui, QtWidgets

from flash_export import DEFAULT_EXPORT_ROOT, MAX_ATTEMPTS, erase, export, new_export_directory
from records import Lines
from session_reader import read_flash_session_info
from stream import Stream

LOCALHOST = "127.0.0.1"
RTT_CONFIG = Path(__file__).resolve().parents[2] / "firmware/drone/debug/rtt.cfg"
SERVER_READY = b"Drone flash RTT ready"
NOT_RUNNING = QtCore.QProcess.ProcessState.NotRunning


def export_sessions(host, control_port, data_port, directory, cancel, progress):
    stats = export(
        host, control_port, data_port, 180.0, directory, cancel=cancel, progress=progress
    )
    sessions = []
    for path in sorted(directory.glob("session_*")):
        info = read_flash_session_info(path)
        if info is not None:
            sessions.append((path, info))
    return stats, sessions


class FlashViewer(QtWidgets.QMainWindow):
    flash_progress = QtCore.Signal(int, int)

    def __init__(self, args):
        super().__init__()
        self.setWindowTitle("Drone · Flash export")
        self.resize(820, 640)
        self.args = args
        self.worker = ThreadPoolExecutor(max_workers=1)
        self.job = None
        self.export_directory = None
        self.export_receipt = None
        self.log_offset = 0
        self.closing = False
        self.server_ready = False
        self.server_stopping = False
        self.rtt_block_missing = False

        root = QtWidgets.QWidget()
        self.setCentralWidget(root)
        layout = QtWidgets.QVBoxLayout(root)
        controls = QtWidgets.QHBoxLayout()
        self.server_button = QtWidgets.QPushButton()
        self.server_button.clicked.connect(self.toggle_server)
        self.server_status = QtWidgets.QLabel("OpenOCD stopped")
        self.server_status.setWordWrap(True)
        self.export_button = QtWidgets.QPushButton()
        self.export_button.clicked.connect(self.toggle_export)
        self.erase_button = QtWidgets.QPushButton("Erase flash")
        self.erase_button.clicked.connect(self.confirm_erase)
        controls.addWidget(self.server_button)
        controls.addWidget(self.server_status, 1)
        controls.addWidget(self.export_button)
        controls.addWidget(self.erase_button)
        layout.addLayout(controls)
        self.status = QtWidgets.QLabel("Load the Export firmware, then start OpenOCD.")
        self.status.setTextFormat(QtCore.Qt.TextFormat.PlainText)
        self.status.setWordWrap(True)
        layout.addWidget(self.status)
        self.board_status = QtWidgets.QLabel("STM32 · waiting for RTT log")
        self.board_status.setTextFormat(QtCore.Qt.TextFormat.PlainText)
        self.board_status.setWordWrap(True)
        layout.addWidget(self.board_status)

        splitter = QtWidgets.QSplitter(QtCore.Qt.Orientation.Vertical)
        layout.addWidget(splitter)
        session_group = QtWidgets.QGroupBox("Sessions")
        session_layout = QtWidgets.QVBoxLayout(session_group)
        session_controls = QtWidgets.QHBoxLayout()
        self.session_count = QtWidgets.QLabel("Export flash to find sessions.")
        self.open_button = QtWidgets.QPushButton("Open session")
        self.open_button.clicked.connect(self.open_session)
        session_controls.addWidget(self.session_count, 1)
        session_controls.addWidget(self.open_button)
        session_layout.addLayout(session_controls)
        self.sessions = QtWidgets.QListWidget()
        self.sessions.currentItemChanged.connect(self.update_controls)
        self.sessions.itemActivated.connect(self.open_session)
        session_layout.addWidget(self.sessions)
        splitter.addWidget(session_group)
        self.logs = QtWidgets.QTabWidget()
        self.export_log = QtWidgets.QPlainTextEdit()
        self.board_log = QtWidgets.QPlainTextEdit()
        self.server_log = QtWidgets.QPlainTextEdit()
        for label, log in (
            ("Export / erase log", self.export_log),
            ("STM32 log (RTT 0)", self.board_log),
            ("OpenOCD log", self.server_log),
        ):
            log.setReadOnly(True)
            log.setMaximumBlockCount(2000)
            self.logs.addTab(log, label)
        splitter.addWidget(self.logs)
        splitter.setSizes([220, 340])

        self.server = QtCore.QProcess(self)
        self.server.setProcessChannelMode(QtCore.QProcess.ProcessChannelMode.MergedChannels)
        self.server.readyReadStandardOutput.connect(self.read_server_log)
        self.server.errorOccurred.connect(self.server_error)
        self.server.finished.connect(self.server_finished)
        self.server.stateChanged.connect(self.update_controls)
        self.board_stream = Stream(LOCALHOST, args.log_port, self)
        self.board_stream.paused = True
        self.board_stream.retry.stop()
        self.board_stream.record.connect(self.read_board_log)
        self.kill_timer = QtCore.QTimer(self)
        self.kill_timer.setSingleShot(True)
        self.kill_timer.timeout.connect(self.server.kill)
        self.poll = QtCore.QTimer(self)
        self.poll.timeout.connect(self.poll_job)
        self.poll.start(100)
        self.flash_progress.connect(self.update_export_progress)
        self.update_controls()

    def update_controls(self, *_):
        running = self.server.state() != NOT_RUNNING
        idle = self.job is None and not self.closing
        ready = self.server_ready and running and not self.server_stopping
        exporting = self.job is not None and self.job_kind == "export"
        self.server_button.setText("Stop OpenOCD" if running else "Start OpenOCD")
        self.server_button.setEnabled(idle and not self.server_stopping)
        self.export_button.setText("Cancel export" if exporting else "Export flash")
        self.export_button.setEnabled((idle and ready) or (exporting and not self.cancel.is_set()))
        self.erase_button.setEnabled(idle and ready and self.export_receipt is not None)
        self.open_button.setEnabled(not self.closing and self.sessions.currentItem() is not None)

    def toggle_server(self):
        if self.server.state() != NOT_RUNNING:
            self.stop_server()
            return
        self.server_frames = Lines(limit=4096)
        self.rtt_block_missing = False
        self.server_log.clear()
        self.board_log.clear()
        self.board_status.setText("STM32 · waiting for RTT log")
        self.logs.setCurrentWidget(self.server_log)
        self.server_status.setText("Starting OpenOCD…")
        gdb_port = self.args.gdb_port or "disabled"
        self.server.start(
            self.args.openocd,
            [
                "-c",
                f"set DRONE_GDB_PORT {gdb_port}",
                "-c",
                f"set DRONE_RTT_LOG_PORT {self.args.log_port}",
                "-c",
                f"set DRONE_RTT_DATA_PORT {self.args.data_port}",
                "-f",
                str(RTT_CONFIG),
                "-c",
                f"echo {{{SERVER_READY.decode()}}}",
            ],
        )

    def stop_server(self):
        if self.server_stopping:
            return
        self.server_stopping = True
        self.server_ready = False
        self.export_receipt = None
        self.disconnect_board_log()
        self.server_status.setText("Stopping OpenOCD…")
        self.update_controls()
        self.kill_timer.start(1500)
        self.server.terminate()

    def read_server_log(self):
        output = bytes(self.server.readAllStandardOutput())
        for line in self.server_frames.feed(output):
            self.server_log.appendPlainText(line.decode(errors="replace"))
            if line.endswith(b"rtt: No control block found"):
                self.rtt_block_missing = True
                self.server_status.setText("RTT block missing · boot STM32 and restart OpenOCD")
            if line == SERVER_READY and not self.server_stopping and not self.rtt_block_missing:
                self.server_ready = True
                gdb = f"GDB {LOCALHOST}:{self.args.gdb_port}" if self.args.gdb_port else "GDB off"
                self.server_status.setText(
                    f"RTT {self.args.log_port}/{self.args.data_port} · {gdb}"
                )
                self.board_stream.paused = False
                self.board_stream.retry.start(2000)
                self.board_stream.connect()
                self.update_controls()

    def disconnect_board_log(self):
        self.board_stream.paused = True
        self.board_stream.retry.stop()
        self.board_stream.socket.abort()

    def read_board_log(self, line):
        text = line.decode(errors="replace")
        self.board_log.appendPlainText(text)
        prefix, _, message = text.partition("] ")
        if prefix.endswith(("][runtime", "][flash")):
            self.board_status.setText(f"STM32 · {message}")

    def server_error(self, error):
        if error == QtCore.QProcess.ProcessError.FailedToStart:
            self.server_status.setText(f"Cannot start OpenOCD · {self.server.errorString()}")
            self.server_stopping = False
            self.kill_timer.stop()
            self.update_controls()
            if self.closing:
                QtCore.QTimer.singleShot(0, self.close)

    def server_finished(self, code, _):
        self.read_server_log()
        self.kill_timer.stop()
        self.server_ready = False
        self.export_receipt = None
        self.disconnect_board_log()
        if self.server_stopping:
            self.server_status.setText("OpenOCD stopped")
        else:
            self.server_status.setText(f"OpenOCD exited ({code}) · see OpenOCD log")
            self.logs.setCurrentWidget(self.server_log)
        self.server_stopping = False
        self.update_controls()
        if self.closing:
            QtCore.QTimer.singleShot(0, self.close)

    def toggle_export(self):
        if self.job is not None:
            self.cancel.set()
            self.status.setText("Cancelling flash export…")
            self.update_controls()
            return
        if not self.server_ready:
            return
        self.export_receipt = None
        self.export_directory = new_export_directory(self.args.export_dir)
        self.sessions.clear()
        self.session_count.setText("Reading sessions…")
        self.log_offset = 0
        self.export_log.clear()
        self.logs.setCurrentWidget(self.export_log)
        self.status.setText("Waiting for STM32 to acknowledge the export request.")
        self.cancel = Event()
        self.job_kind = "export"
        self.job = self.worker.submit(
            export_sessions,
            LOCALHOST,
            self.args.log_port,
            self.args.data_port,
            self.export_directory,
            self.cancel,
            self.flash_progress.emit,
        )
        self.update_controls()

    def update_export_progress(self, attempt, pages):
        if self.job is not None and self.job_kind == "export" and not self.cancel.is_set():
            if pages < 0:
                progress = "waiting for BEGIN"
            elif pages == 0:
                progress = "BEGIN received · scanning flash"
            else:
                progress = f"{pages:,} nonblank pages received"
            self.status.setText(f"Exporting flash · attempt {attempt}/{MAX_ATTEMPTS} · {progress}")

    def confirm_erase(self):
        if not self.erase_button.isEnabled():
            return
        answer = QtWidgets.QMessageBox.question(
            self,
            "Erase all telemetry flash?",
            f"Saved image:\n{self.export_directory / 'flash.bin'}\n\n"
            "Keep the board in export mode until it verifies the erase.",
            QtWidgets.QMessageBox.StandardButton.Yes | QtWidgets.QMessageBox.StandardButton.Cancel,
            QtWidgets.QMessageBox.StandardButton.Cancel,
        )
        if answer != QtWidgets.QMessageBox.StandardButton.Yes or not self.erase_button.isEnabled():
            return
        receipt, self.export_receipt = self.export_receipt, None
        self.logs.setCurrentWidget(self.export_log)
        self.status.setText("Checking the saved image and waiting for the board to erase…")
        self.cancel = Event()
        self.job_kind = "erase"
        self.job = self.worker.submit(
            erase,
            LOCALHOST,
            self.args.log_port,
            self.args.data_port,
            self.export_directory,
            receipt,
            self.cancel,
        )
        self.update_controls()

    def poll_job(self):
        if self.job is None:
            return
        self.read_export_log()
        if not self.job.done():
            return
        job, self.job = self.job, None
        self.read_export_log()
        try:
            result = job.result()
        except InterruptedError:
            self.session_count.setText("No export completed.")
            self.status.setText("Flash export cancelled.")
        except Exception as error:
            if self.job_kind == "export":
                self.session_count.setText("No export completed.")
                self.status.setText(f"Flash export failed · {error}")
            else:
                self.status.setText(f"Flash erase failed · {error}")
        else:
            if self.job_kind == "export":
                self.show_export(*result)
            elif not self.cancel.is_set():
                message = "Flash erased and verified."
                if result is not None:
                    message += f" · Could not write export.log: {result}"
                self.status.setText(message)
        self.update_controls()
        if self.closing:
            QtCore.QTimer.singleShot(0, self.close)

    def read_export_log(self):
        try:
            with (self.export_directory / "export.log").open("rb") as log:
                log.seek(self.log_offset)
                data = log.read(16384)
                self.log_offset = log.tell()
        except OSError:
            return
        if data:
            self.export_log.moveCursor(QtGui.QTextCursor.MoveOperation.End)
            self.export_log.insertPlainText(data.decode(errors="replace"))

    def show_export(self, stats, sessions):
        if self.server_ready and not self.cancel.is_set():
            self.export_receipt = stats
        self.status.setText(f"Flash saved · {self.export_directory}")
        self.session_count.setText(
            f"{stats['sessions']} sessions · {stats['interrupted_sessions']} interrupted"
        )
        for path, info in sessions:
            item = QtWidgets.QListWidgetItem(
                f"{path.name} · {info['status']} · {info['data_pages']} data pages"
            )
            item.setData(QtCore.Qt.ItemDataRole.UserRole, str(path))
            self.sessions.addItem(item)
        if self.sessions.count():
            self.sessions.setCurrentRow(0)

    def open_session(self, *_):
        item = self.sessions.currentItem()
        if item is None:
            return
        started, _ = QtCore.QProcess.startDetached(
            sys.executable,
            [
                str(Path(__file__).with_name("view.py")),
                "--session",
                item.data(QtCore.Qt.ItemDataRole.UserRole),
            ],
        )
        if not started:
            QtWidgets.QMessageBox.warning(
                self, "Cannot open session", "Could not start the viewer."
            )

    def closeEvent(self, event):
        self.closing = True
        if self.job is not None:
            self.cancel.set()
            self.update_controls()
            event.ignore()
            return
        if self.server.state() != NOT_RUNNING:
            self.stop_server()
            event.ignore()
            return
        self.poll.stop()
        self.worker.shutdown(wait=False, cancel_futures=True)
        super().closeEvent(event)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--openocd", default="openocd", help="OpenOCD executable")
    parser.add_argument("--gdb-port", type=int, default=3333, help="GDB port (0 disables it)")
    parser.add_argument("--log-port", type=int, default=9092, help="RTT LOG/control port")
    parser.add_argument("--data-port", type=int, default=9093, help="RTT DATA port")
    parser.add_argument("--export-dir", type=Path, default=DEFAULT_EXPORT_ROOT)
    args = parser.parse_args()
    ports = (args.log_port, args.data_port)
    if args.gdb_port:
        ports += (args.gdb_port,)
    if not all(0 < port < 65536 for port in ports) or len(ports) != len(set(ports)):
        parser.error("Enabled TCP ports must be distinct and between 1 and 65535")
    app = QtWidgets.QApplication([sys.argv[0]])
    viewer = FlashViewer(args)
    viewer.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
