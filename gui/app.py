"""GMP Peripheral ISO Host desktop GUI."""
import sys
from PySide6.QtCore import QThread, Signal
from PySide6.QtWidgets import (QApplication, QCheckBox, QFileDialog, QFormLayout,
    QHBoxLayout, QLabel, QLineEdit, QMainWindow, QMessageBox, QPushButton,
    QTabWidget, QVBoxLayout, QWidget, QComboBox)
from gmp_api import Api


class UploadWorker(QThread):
    finished_upload = Signal(str)

    def __init__(self, api, path, autoload):
        super().__init__()
        self.api, self.path, self.autoload = api, path, autoload

    def run(self):
        try:
            self.api.upload(self.path, self.autoload)
            self.finished_upload.emit("镜像已写入并校验。重启后自动加载。" if self.autoload
                                      else "镜像已写入并校验，自动加载已关闭。")
        except Exception as error:
            self.finished_upload.emit(f"上传失败：{error}")


class Window(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("GMP Peripheral ISO Host")
        self.resize(670, 400)
        self.api = Api()
        self.worker = None
        tabs = QTabWidget()
        self.setCentralWidget(tabs)
        connection = QWidget()
        layout = QVBoxLayout(connection)
        row = QHBoxLayout()
        self.port = QLineEdit("COM3" if sys.platform == "win32" else "/dev/ttyACM0")
        self.port.setPlaceholderText("COM3 / /dev/ttyACM0")
        row.addWidget(QLabel("端口")); row.addWidget(self.port)
        button = QPushButton("连接并校验")
        button.clicked.connect(self.connect_device)
        row.addWidget(button); layout.addLayout(row)
        self.identity = QLabel("尚未连接")
        layout.addWidget(self.identity)
        self.state = QLabel("TPLD 状态：—")
        layout.addWidget(self.state)
        refresh = QPushButton("刷新状态")
        refresh.clicked.connect(self.refresh)
        layout.addWidget(refresh)
        file_row = QHBoxLayout()
        self.hex_path = QLineEdit()
        self.hex_path.setPlaceholderText("InterConnect Studio program.hex")
        file_row.addWidget(self.hex_path)
        browse = QPushButton("选择 HEX")
        browse.clicked.connect(self.browse)
        file_row.addWidget(browse)
        layout.addLayout(file_row)
        self.autoload = QCheckBox("重启时自动加载 TPLD")
        self.autoload.setChecked(True)
        layout.addWidget(self.autoload)
        action = QHBoxLayout()
        self.upload_button = QPushButton("上传并校验镜像")
        self.upload_button.clicked.connect(self.upload)
        action.addWidget(self.upload_button)
        reset = QPushButton("重启 STM32")
        reset.clicked.connect(self.reset_device)
        action.addWidget(reset); layout.addLayout(action)
        self.message = QLabel("")
        layout.addWidget(self.message); layout.addStretch()
        tabs.addTab(connection, "设备与 TPLD")

        gpio = QWidget(); gpio_layout = QFormLayout(gpio)
        self.pin = QComboBox(); self.pin.addItems(["PA0", "PA1", "PA4", "PA5"])
        gpio_layout.addRow("引脚", self.pin)
        read = QPushButton("读取")
        read.clicked.connect(self.read_gpio)
        gpio_layout.addRow(read)
        write0 = QPushButton("输出低电平")
        write0.clicked.connect(lambda: self.write_gpio(0))
        gpio_layout.addRow(write0)
        write1 = QPushButton("输出高电平")
        write1.clicked.connect(lambda: self.write_gpio(1))
        gpio_layout.addRow(write1)
        self.gpio_state = QLabel("—")
        gpio_layout.addRow("状态", self.gpio_state)
        tabs.addTab(gpio, "GPIO")

        for name in ("I²C", "SPI", "I3C", "QEP", "UART / RS232 / RS485"):
            page = QWidget(); page_layout = QVBoxLayout(page)
            page_layout.addWidget(QLabel("协议入口已预留；完整参数与从机寄存器服务将在后续迭代加入。"))
            page_layout.addStretch()
            tabs.addTab(page, name)

    def alert(self, error):
        QMessageBox.warning(self, "GMP Peripheral ISO Host", str(error))

    def connect_device(self):
        try:
            info = self.api.connect(self.port.text().strip())
            self.identity.setText(f"{info.name.decode()} | 协议 {info.protocol_major}.{info.protocol_minor} | "
                                  f"功能 0x{info.capabilities:08X}")
            self.refresh()
        except Exception as error:
            self.alert(error)

    def refresh(self):
        try:
            s = self.api.status()
            self.state.setText(f"TPLD 启动结果 {s.tpld_boot_status}，地址 0x{s.tpld_address:02X}；"
                               f"镜像 {'有效' if s.image_present else '无'}，自动加载 {s.autoload}，"
                               f"版本 {s.generation}，CRC32 {s.image_crc32:08X}")
        except Exception as error:
            self.alert(error)

    def browse(self):
        path, _ = QFileDialog.getOpenFileName(self, "选择 TPLD 固件", "", "HEX files (*.hex);;All files (*)")
        if path: self.hex_path.setText(path)

    def upload(self):
        if not self.api.handle or not self.hex_path.text():
            self.alert("请先连接设备并选择 HEX 文件")
            return
        self.centralWidget().setEnabled(False)
        self.message.setText("正在上传、写入 Flash 并校验…")
        self.worker = UploadWorker(self.api, self.hex_path.text(), self.autoload.isChecked())
        self.worker.finished_upload.connect(self.upload_done)
        self.worker.start()

    def upload_done(self, message):
        self.centralWidget().setEnabled(True)
        self.message.setText(message)
        self.refresh()

    def reset_device(self):
        try:
            self.api.reset()
            self.message.setText("重启命令已发送；等待设备重新枚举后可刷新状态。")
        except Exception as error:
            self.alert(error)

    def read_gpio(self):
        try: self.gpio_state.setText(str(self.api.gpio_read(int(self.pin.currentText()[2:]))))
        except Exception as error: self.alert(error)

    def write_gpio(self, value):
        try:
            pin = int(self.pin.currentText()[2:])
            if pin != 0:
                raise ValueError("当前只允许写 PA0；PA1/PA4/PA5 可能由 TPLD 驱动")
            self.api.gpio_write(pin, value)
            self.gpio_state.setText(str(value))
        except Exception as error: self.alert(error)

    def closeEvent(self, event):
        if self.worker and self.worker.isRunning():
            event.ignore(); self.alert("请等待镜像写入完成")
            return
        self.api.close(); event.accept()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = Window(); window.show()
    sys.exit(app.exec())
