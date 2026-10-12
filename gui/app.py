"""GMP Peripheral Host desktop GUI."""
import sys
from PySide6.QtCore import QThread, Signal, QTimer
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (QApplication, QCheckBox, QFileDialog, QFormLayout,
    QHBoxLayout, QLabel, QLineEdit, QMainWindow, QMessageBox, QPushButton,
    QTabWidget, QVBoxLayout, QWidget, QComboBox, QTableWidget, QTableWidgetItem,
    QSpinBox, QScrollArea)
from gmp_api import Api
from profile_ui import wrap_interfaces
from resource_view import ResourceChart, ResourceTable


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
        self.setWindowTitle("GMP Peripheral Host")
        if sys.platform == "win32": self.setFont(QFont("Microsoft YaHei", 9))
        self.resize(1150, 740)
        self.setStyleSheet("""
            QWidget { background: #f7f9fc; color: #17243a; }
            QTabWidget::pane { border: 1px solid #d9e1ec; background: white; }
            QTabBar::tab { background: #eaf0f7; padding: 9px 16px;
                           border: 1px solid #d9e1ec; border-bottom: 0; }
            QTabBar::tab:selected { background: white; color: #1357a6; font-weight: 600; }
            QPushButton { background: #edf3fb; border: 1px solid #c9d7e8;
                          border-radius: 4px; padding: 6px 10px; }
            QPushButton:hover { background: #dcecff; }
            QPushButton:pressed { background: #c4def9; }
            QLineEdit, QComboBox, QSpinBox { background: white; border: 1px solid #c9d7e8;
                                            border-radius: 4px; padding: 5px; }
            QTableWidget { background: white; alternate-background-color: #f2f6fb;
                           gridline-color: #e1e8f1; }
            QHeaderView::section { background: #e9f0f9; padding: 5px;
                                   border: 1px solid #d9e1ec; font-weight: 600; }
            QLabel#profileHint { color: #52657d; padding: 4px; }
        """)
        self.api = Api()
        self.worker = None
        tabs = QTabWidget()
        self.setCentralWidget(tabs)
        connection = QWidget()
        layout = QVBoxLayout(connection)
        row = QHBoxLayout()
        self.port = QLineEdit("COM5" if sys.platform == "win32" else "/dev/ttyACM0")
        self.port.setPlaceholderText("COM5 or COM4 / /dev/ttyACM0")
        row.addWidget(QLabel("端口")); row.addWidget(self.port)
        button = QPushButton("连接并校验")
        button.clicked.connect(self.connect_device)
        row.addWidget(button); layout.addLayout(row)
        self.identity = QLabel("尚未连接")
        layout.addWidget(self.identity)
        self.state = QLabel("TPLD 状态：—")
        layout.addWidget(self.state)
        self.interface_state = QLabel("外设状态：—")
        layout.addWidget(self.interface_state)
        self.resource_hint = QLabel("资源监控：连接协议 1.7 设备后，每秒记录 CPU 与 RAM 使用情况。")
        layout.addWidget(self.resource_hint)
        self.resource_table = ResourceTable()
        layout.addWidget(self.resource_table)
        self.resource_chart = ResourceChart()
        layout.addWidget(self.resource_chart)
        self.resource_timer = QTimer(self)
        self.resource_timer.setInterval(1000)
        self.resource_timer.timeout.connect(self.poll_resources)
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
        connection_scroll = QScrollArea()
        connection_scroll.setWidgetResizable(True)
        connection_scroll.setWidget(connection)
        tabs.addTab(connection_scroll, "设备与 TPLD")

        self.add_gpio_page(tabs)
        self.add_i2c_page(tabs)
        self.add_i3c_page(tabs)
        self.add_spi_page(tabs)
        self.add_qep_page(tabs)
        for name, port in (("UART", 0), ("RS232", 1), ("RS485", 2)):
            self.add_serial_page(tabs, name, port)
        wrap_interfaces(tabs, self.api, self.alert)

    @staticmethod
    def hex_bytes(text, limit):
        data = bytes.fromhex(text.replace(",", " "))
        if len(data) > limit:
            raise ValueError(f"最多 {limit} 字节")
        return data

    @staticmethod
    def parse_number(text):
        return int(text.strip(), 0)

    @staticmethod
    def add_scroll_tab(tabs, page, name):
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(page)
        tabs.addTab(scroll, name)

    def add_gpio_page(self, tabs):
        page = QWidget(); layout = QVBoxLayout(page)
        layout.addWidget(QLabel("GPIO_PA0 / GPIO_PA1。PA1 与 TPLD 相连，当前固件禁止将其设为输出；ADC 为 12 位读数。"))
        table = QTableWidget(2, 4)
        table.setHorizontalHeaderLabels(["引脚编号", "方向 / 模式", "设定低 / 高 / 值", "当前值"])
        table.verticalHeader().setVisible(False)
        table.setColumnWidth(0, 120); table.setColumnWidth(1, 200)
        table.setColumnWidth(2, 350); table.setColumnWidth(3, 130)
        self.gpio_modes, self.gpio_values, self.gpio_current = {}, {}, {}
        for pin in (0, 1):
            row = pin
            table.setItem(row, 0, QTableWidgetItem(f"PA{pin}"))
            mode = QComboBox()
            mode.addItems(["输入", "输出", "ADC", "DAC"])
            mode.model().item(3).setEnabled(False)
            if pin == 1: mode.model().item(1).setEnabled(False)
            mode.currentIndexChanged.connect(lambda selected, p=pin: self.configure_pin(p, selected))
            table.setCellWidget(row, 1, mode)
            self.gpio_modes[pin] = mode
            setting = QWidget(); actions = QHBoxLayout(setting)
            actions.setContentsMargins(2, 0, 2, 0)
            low = QPushButton("低 0"); high = QPushButton("高 1")
            low.clicked.connect(lambda _, p=pin: self.set_pin(p, 0))
            high.clicked.connect(lambda _, p=pin: self.set_pin(p, 1))
            value = QSpinBox(); value.setRange(0, 4095); value.setEnabled(False)
            apply = QPushButton("设置值"); apply.setEnabled(False)
            apply.clicked.connect(lambda _, p=pin, v=value: self.set_pin(p, v.value()))
            actions.addWidget(low); actions.addWidget(high)
            actions.addWidget(value); actions.addWidget(apply)
            table.setCellWidget(row, 2, setting)
            current = QLabel("—"); table.setCellWidget(row, 3, current)
            self.gpio_values[pin] = (low, high, value, apply)
            self.gpio_current[pin] = current
            self.update_pin_controls(pin)
        layout.addWidget(table)
        refresh = QPushButton("刷新当前值")
        refresh.clicked.connect(self.refresh_pins)
        layout.addWidget(refresh); layout.addStretch()
        tabs.addTab(page, "GPIO")

    def update_pin_controls(self, pin):
        mode = self.gpio_modes[pin].currentIndex()
        low, high, value, apply = self.gpio_values[pin]
        low.setEnabled(mode == 1); high.setEnabled(mode == 1)
        value.setEnabled(mode == 3); apply.setEnabled(mode == 3)

    def configure_pin(self, pin, mode):
        self.update_pin_controls(pin)
        if not self.api.handle: return
        try:
            self.api.pin_config(pin, mode)
            self.read_pin(pin)
        except Exception as error: self.alert(error)

    def set_pin(self, pin, value):
        try:
            self.api.pin_set(pin, value)
            self.read_pin(pin)
        except Exception as error: self.alert(error)

    def read_pin(self, pin):
        mode, value = self.api.pin_read(pin)
        self.gpio_modes[pin].blockSignals(True)
        self.gpio_modes[pin].setCurrentIndex(mode)
        self.gpio_modes[pin].blockSignals(False)
        self.update_pin_controls(pin)
        self.gpio_current[pin].setText(str(value))

    def refresh_pins(self):
        try:
            for pin in (0, 1): self.read_pin(pin)
        except Exception as error: self.alert(error)

    def add_i2c_page(self, tabs):
        page = QWidget(); layout = QFormLayout(page)
        role = QComboBox(); role.addItems(["主机", "软件从机 / 寄存器应答"])
        address = QLineEdit("0x50")
        tx = QLineEdit(); tx.setPlaceholderText("十六进制字节，例如 00 12 AB")
        count = QSpinBox(); count.setRange(0, 64)
        result = QLabel("—")
        configure = QPushButton("配置角色")
        def configure_role():
            try:
                self.api.i2c_target_config(role.currentIndex(), self.parse_number(address.text()))
                result.setText("软件从机已启用" if role.currentIndex() else "已切回主机")
            except Exception as error: self.alert(error)
        configure.clicked.connect(configure_role)
        button = QPushButton("传输")
        def transfer():
            try:
                addr = self.parse_number(address.text())
                data = self.hex_bytes(tx.text(), 64)
                if not 0x08 <= addr <= 0x77 or addr == 0x79:
                    raise ValueError("请输入合法的 7 位设备地址")
                result.setText(self.api.i2c_transfer(addr, data, count.value()).hex(" ").upper() or "完成")
            except Exception as error: self.alert(error)
        button.clicked.connect(transfer)
        layout.addRow("角色", role); layout.addRow("7 位地址", address)
        layout.addRow(configure)
        layout.addRow("写入数据", tx); layout.addRow("读取字节数", count)
        layout.addRow(button)
        self.add_target_register_controls(layout, 5, result)
        self.add_remote_register_controls(layout, 5,
                                          lambda: self.parse_number(address.text()), result)
        layout.addRow("结果", result)
        layout.addRow(QLabel("PB5/PB6 为软件 I²C；从机使用 EXTI 边沿处理，双板低速寄存器读写已验证。"))
        self.add_scroll_tab(tabs, page, "I²C")

    def add_i3c_page(self, tabs):
        page = QWidget(); layout = QFormLayout(page)
        role = QComboBox(); role.addItems(["控制器", "目标设备 / 寄存器应答"])
        layout.addRow("角色", role)
        status = QLabel("双板动态地址与寄存器读写已验证")
        configure = QPushButton("配置 I3C2")
        def apply_role():
            try:
                self.api.i3c_config(role.currentIndex())
                status.setText("已配置，目标模式等待控制器分配动态地址")
            except Exception as error: self.alert(error)
        configure.clicked.connect(apply_role)
        layout.addRow(configure)
        base = QLineEdit("0x30")
        maximum = QSpinBox(); maximum.setRange(1, 8); maximum.setValue(1)
        assign = QPushButton("动态地址分配 ENTDAA")
        def do_assign():
            try:
                found = self.api.i3c_assign(self.parse_number(base.text()), maximum.value())
                status.setText("分配地址：" + (", ".join(f"0x{x:02X}" for x in found) or "无设备"))
            except Exception as error: self.alert(error)
        assign.clicked.connect(do_assign)
        layout.addRow("起始动态地址", base); layout.addRow("最多设备数", maximum)
        layout.addRow(assign)
        address = QLineEdit("0x30")
        tx = QLineEdit(); tx.setPlaceholderText("十六进制写入数据，最多 64 字节")
        count = QSpinBox(); count.setRange(0, 64)
        transfer = QPushButton("私有传输")
        def send():
            try:
                data = self.hex_bytes(tx.text(), 64)
                result = self.api.i3c_transfer(self.parse_number(address.text()), data, count.value())
                status.setText(result.hex(" ").upper() or "传输完成")
            except Exception as error: self.alert(error)
        transfer.clicked.connect(send)
        layout.addRow("目标动态地址", address); layout.addRow("写入数据", tx)
        layout.addRow("读取字节数", count); layout.addRow(transfer)
        self.add_target_register_controls(layout, 4, status)
        self.add_remote_register_controls(layout, 4,
                                          lambda: self.parse_number(address.text()), status)
        layout.addRow("状态 / 结果", status)
        layout.addRow(QLabel("I3C2：PB3 SCL / PB4 SDA。目标模式使用固定 37 字节写入后读取协议。"))
        self.add_scroll_tab(tabs, page, "I3C")

    def add_target_register_controls(self, layout, port, status):
        address = QLineEdit("0x00")
        data = QLineEdit(); data.setPlaceholderText("寄存器预置 HEX，最多 32 字节")
        length = QSpinBox(); length.setRange(1, 32)
        write = QPushButton("预置本机从机寄存器")
        read = QPushButton("读取本机从机寄存器")
        def preset():
            try:
                value = self.hex_bytes(data.text(), 32)
                if not value: raise ValueError("请输入至少 1 字节")
                self.api.register_write(port, self.parse_number(address.text()), value)
                status.setText("寄存器已预置")
            except Exception as error: self.alert(error)
        def inspect():
            try:
                value = self.api.register_read(port, self.parse_number(address.text()), length.value())
                status.setText(value.hex(" ").upper())
            except Exception as error: self.alert(error)
        write.clicked.connect(preset); read.clicked.connect(inspect)
        layout.addRow("本机寄存器地址", address)
        layout.addRow("预置数据", data); layout.addRow(write)
        layout.addRow("读取长度", length); layout.addRow(read)

    def add_remote_register_controls(self, layout, bus, target_getter, status):
        address = QLineEdit("0x00")
        data = QLineEdit(); data.setPlaceholderText("写入远端的 HEX 数据")
        length = QSpinBox(); length.setRange(1, 32)
        write = QPushButton("写入远端寄存器")
        read = QPushButton("读取远端寄存器")
        def send():
            try:
                value = self.hex_bytes(data.text(), 32)
                if not value: raise ValueError("请输入至少 1 字节")
                self.api.remote_register_write(bus, target_getter(),
                                               self.parse_number(address.text()), value)
                status.setText("远端寄存器已写入")
            except Exception as error: self.alert(error)
        def receive():
            try:
                value = self.api.remote_register_read(bus, target_getter(),
                                                      self.parse_number(address.text()), length.value())
                status.setText(value.hex(" ").upper())
            except Exception as error: self.alert(error)
        write.clicked.connect(send); read.clicked.connect(receive)
        layout.addRow("远端寄存器地址", address)
        layout.addRow("远端写入数据", data); layout.addRow(write)
        layout.addRow("远端读取长度", length); layout.addRow(read)

    def add_spi_page(self, tabs):
        page = QWidget(); layout = QFormLayout(page)
        role = QComboBox(); role.addItems(["主机", "从机 / 寄存器应答"])
        mode = QComboBox(); mode.addItems([f"Mode {n}" for n in range(4)])
        divider = QComboBox(); divider.addItems([f"/{2 ** (n + 1)}" for n in range(8)])
        tx = QLineEdit(); tx.setPlaceholderText("十六进制发送数据，最多 64 字节")
        result = QLabel("—")
        def configure():
            try: self.api.spi_config(role.currentIndex(), mode.currentIndex(), divider.currentIndex())
            except Exception as error: self.alert(error)
        def transfer():
            try:
                data = self.hex_bytes(tx.text(), 64)
                if not data: raise ValueError("请输入至少 1 字节")
                result.setText(self.api.spi_transfer(data).hex(" ").upper())
            except Exception as error: self.alert(error)
        start = QPushButton("配置 SPI2"); start.clicked.connect(configure)
        send = QPushButton("片选并传输"); send.clicked.connect(transfer)
        layout.addRow("角色", role); layout.addRow("时序", mode); layout.addRow("时钟分频", divider)
        layout.addRow(start); layout.addRow("发送数据", tx); layout.addRow(send); layout.addRow("收到数据", result)
        self.add_target_register_controls(layout, 3, result)
        self.add_remote_register_controls(layout, 3, lambda: 0, result)
        layout.addRow(QLabel("从机每次片选恰好 37 字节；四种模式已双板验证。Mode 1/2/3 主机为低速 GPIO，分频设置暂不生效。"))
        layout.addRow(QLabel("SPI2：PA8 NSS、PB2 SCK、PB14 MISO、PB15 MOSI。"))
        self.add_scroll_tab(tabs, page, "SPI")

    def add_qep_page(self, tabs):
        page = QWidget(); layout = QFormLayout(page)
        value = QLabel("—")
        for title, operation in (("启用编码器", self.api.qep_start),
                                 ("清零计数", self.api.qep_reset)):
            button = QPushButton(title)
            def action(_, op=operation):
                try: op(); value.setText("完成")
                except Exception as error: self.alert(error)
            button.clicked.connect(action); layout.addRow(button)
        read = QPushButton("读取位置 / 索引 / 方向")
        def read_qep():
            try:
                pos, index, direction = self.api.qep_read()
                value.setText(f"位置 {pos}；I 电平 {index}；方向 {'递减' if direction else '递增'}")
            except Exception as error: self.alert(error)
        read.clicked.connect(read_qep)
        layout.addRow(read); layout.addRow("当前值", value)
        layout.addRow(QLabel("TIM3 编码器：PA6=A、PA7=B；PB7=I 电平。"))
        self.add_scroll_tab(tabs, page, "QEP")

    def add_serial_page(self, tabs, name, port):
        page = QWidget(); layout = QFormLayout(page)
        role = QComboBox(); role.addItems(["透明收发", "从机寄存器应答"])
        baud = QSpinBox(); baud.setRange(1200, 2000000); baud.setValue(115200)
        config = QPushButton("配置接口")
        def configure():
            try: self.api.serial_config(port, role.currentIndex(), baud.value())
            except Exception as error: self.alert(error)
        config.clicked.connect(configure)
        tx = QLineEdit(); tx.setPlaceholderText("十六进制字节")
        output = QLabel("—")
        send = QPushButton("发送")
        def send_data():
            try:
                data = self.hex_bytes(tx.text(), 64)
                if not data: raise ValueError("请输入至少 1 字节")
                self.api.serial_write(port, data)
                output.setText("已发送")
            except Exception as error: self.alert(error)
        send.clicked.connect(send_data)
        receive = QPushButton("读取接收缓冲")
        def read_data():
            try: output.setText(self.api.serial_read(port).hex(" ").upper() or "无数据")
            except Exception as error: self.alert(error)
        receive.clicked.connect(read_data)
        address = QLineEdit("0x00")
        reg_data = QLineEdit(); reg_data.setPlaceholderText("寄存器预置值，最多 32 字节")
        reg_length = QSpinBox(); reg_length.setRange(1, 32); reg_length.setValue(1)
        preset = QPushButton("预置寄存器")
        def write_reg():
            try:
                data = self.hex_bytes(reg_data.text(), 32)
                if not data: raise ValueError("请输入寄存器数据")
                self.api.register_write(port, self.parse_number(address.text()), data)
                output.setText("寄存器已预置")
            except Exception as error: self.alert(error)
        preset.clicked.connect(write_reg)
        inspect = QPushButton("读取寄存器")
        def read_reg():
            try:
                data = self.api.register_read(port, self.parse_number(address.text()), reg_length.value())
                output.setText(data.hex(" ").upper())
            except Exception as error: self.alert(error)
        inspect.clicked.connect(read_reg)
        layout.addRow("角色", role); layout.addRow("波特率", baud); layout.addRow(config)
        layout.addRow("发送 HEX", tx); layout.addRow(send); layout.addRow(receive)
        layout.addRow("寄存器地址", address); layout.addRow("预置数据", reg_data)
        layout.addRow(preset); layout.addRow("读取长度", reg_length); layout.addRow(inspect)
        self.add_remote_register_controls(layout, port, lambda: 0, output)
        layout.addRow("结果", output)
        layout.addRow(QLabel("从机帧格式：A5 操作 地址 长度 [写入数据] CRC8；回复：5A 状态 长度 [数据] CRC8。"))
        self.add_scroll_tab(tabs, page, name)

    def alert(self, error):
        QMessageBox.warning(self, "GMP Peripheral Host", str(error))

    def connect_device(self):
        try:
            info = self.api.connect(self.port.text().strip())
            self.resource_timer.stop()
            self.identity.setText(f"{info.name.decode()} | 协议 {info.protocol_major}.{info.protocol_minor} | "
                                  f"功能 0x{info.capabilities:08X}")
            self.refresh()
            self.refresh_pins()
            if (info.protocol_major, info.protocol_minor) >= (1, 7):
                self.poll_resources()
                self.resource_timer.start()
            else:
                self.resource_hint.setText("资源监控需要协议 1.7 固件；当前设备不提供该数据。")
        except Exception as error:
            self.alert(error)

    def poll_resources(self):
        if not self.api.handle:
            self.resource_timer.stop()
            return
        try:
            metrics = self.api.metrics()
            self.resource_table.update_metrics(metrics, self.api.eeprom_image_size)
            self.resource_chart.add_sample(metrics.cpu_percent,
                                           metrics.static_bytes + metrics.allocated_bytes)
            self.resource_hint.setText("每秒采样一次；蓝线为主循环忙碌率，绿线为 RAM 占用率。")
        except Exception as error:
            self.resource_timer.stop()
            self.resource_hint.setText(f"资源监控已暂停：{error}")

    def refresh(self):
        try:
            s = self.api.status()
            self.state.setText(f"TPLD 启动结果 {s.tpld_boot_status}，地址 0x{s.tpld_address:02X}；"
                               f"镜像 {'有效' if s.image_present else '无'}，自动加载 {s.autoload}，"
                               f"版本 {s.generation}，CRC32 {s.image_crc32:08X}")
            x = self.api.interface_status()
            self.interface_state.setText(
                f"SPI: {x.spi_ready}/角色{x.spi_role}/错误0x{x.spi_error:X}；"
                f"I3C: {x.i3c_ready}/角色{x.i3c_role}/错误0x{x.i3c_error:X}；"
                f"I²C 从机: {x.i2c_target_active}/0x{x.i2c_target_address:02X}；"
                f"串口就绪掩码: 0x{x.serial_ready_mask:X}")
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

    def closeEvent(self, event):
        if self.worker and self.worker.isRunning():
            event.ignore(); self.alert("请等待镜像写入完成")
            return
        self.resource_timer.stop()
        self.api.close(); event.accept()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = Window(); window.show()
    sys.exit(app.exec())
