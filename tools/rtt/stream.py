"""Reconnectable Qt TCP streams with bounded text/binary framing."""

from PySide6 import QtCore, QtNetwork

from records import Frames, Lines


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
