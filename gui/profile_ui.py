"""Profile-driven pages shared by the physical interface tabs."""
import copy
import json
from pathlib import Path

from PySide6.QtWidgets import (QComboBox, QFileDialog, QFormLayout, QHBoxLayout,
    QLabel, QLineEdit, QPushButton, QTableWidget, QTableWidgetItem,
    QTabWidget, QVBoxLayout, QWidget, QAbstractItemView)
from PySide6.QtCore import Qt

from profiles import (BUS, TYPES, apply_target, decode_can_frame, load, number,
                      query, read_target, validate, write_value)


PRESETS = Path(__file__).with_name("presets")
COLUMNS = ["参数", "寄存器 / 命令 / CAN ID@偏移", "类型", "字节序", "系数", "偏移量", "单位", "权限", "初值", "当前值", "原始响应"]


def display_value(value):
    if isinstance(value, int): return str(value)
    if isinstance(value, float): return f"{value:.12g}"
    return str(value)


class ProfilePage(QWidget):
    def __init__(self, api, interface, role, report_error):
        super().__init__()
        self.api, self.interface, self.role, self.report_error = api, interface, role, report_error
        self.profile = {"schema": 1, "name": "未命名配置", "interface": interface,
                        "role": role, "protocol": "bridge-register",
                        "settings": {}, "parameters": []}
        layout = QVBoxLayout(self); layout.setSpacing(12)
        intro = QLabel("参数绑定保存在 JSON 中；导入后可编辑、查询、导出。应用配置才会改变设备状态。")
        intro.setObjectName("profileHint")
        intro.setWordWrap(True); layout.addWidget(intro)
        choose = QHBoxLayout(); self.preset = QComboBox()
        self.preset.addItem("选择内置示例…", None)
        for path in sorted(PRESETS.glob("*.json")):
            try:
                item = load(path)
                if item["interface"] != interface:
                    if item["interface"] in {"uart", "rs232", "rs485"} and interface in {"uart", "rs232", "rs485"}:
                        item = copy.deepcopy(item); item["interface"] = interface
                    else: continue
                if item["role"] != role:
                    if interface == "can":
                        item = copy.deepcopy(item); item["role"] = role
                    else: continue
                validate(item, interface, role)
            except (ValueError, KeyError):
                continue
            self.preset.addItem(item.get("name", path.stem), item)
        self.preset.currentIndexChanged.connect(self.select_preset)
        choose.addWidget(self.preset)
        for title, handler in (("导入 JSON", self.import_json), ("导出 JSON", self.export_json)):
            button = QPushButton(title); button.clicked.connect(handler); choose.addWidget(button)
        layout.addLayout(choose)
        form = QFormLayout()
        self.name = QLineEdit(self.profile["name"]); form.addRow("配置名称", self.name)
        self.protocol = QComboBox()
        protocols = (["bridge-register", "hdc1080", "raw"] if interface == "i2c" else
                     ["bridge-register", "raw"] if interface in {"i3c", "spi"} else
                     ["bridge-register", "raw", "modbus-rtu", "at"] if interface in {"uart", "rs232", "rs485"} else
                     ["can-raw", "canopen"] if interface == "can" else ["raw"])
        self.protocol.addItems(protocols); form.addRow("协议", self.protocol)
        self.address = QLineEdit("0x40" if interface == "i2c" else "0x30")
        if interface in {"i2c", "i3c"}: form.addRow("设备地址", self.address)
        self.baud = QLineEdit("115200")
        if interface in {"uart", "rs232", "rs485"}: form.addRow("波特率", self.baud)
        self.unit = QLineEdit("1")
        if interface in {"uart", "rs232", "rs485"}: form.addRow("Modbus 站号", self.unit)
        self.mode = QComboBox()
        if interface == "i3c":
            self.mode.addItems(["I3C 控制器", "I²C 主机（待固件支持）"] if role == "master" else
                               ["I3C 目标设备", "I²C 从机（待固件支持）"])
            form.addRow("总线模式", self.mode)
        elif interface == "spi":
            self.mode.addItems(["Mode 0", "Mode 1", "Mode 2", "Mode 3"])
            form.addRow("SPI 时序", self.mode)
        layout.addLayout(form)
        actions = QHBoxLayout()
        for title, handler in (("添加绑定", self.add_row), ("删除选中", self.remove_row),
                               ("查询选中", self.query_selected), ("查询全部", self.query_all),
                               ("写入选中初值", self.write_selected),
                               ("应用接口配置", self.apply), ("刷新端口状态", self.refresh_port_status)):
            button = QPushButton(title); button.clicked.connect(handler); actions.addWidget(button)
        layout.addLayout(actions)
        self.table = QTableWidget(0, len(COLUMNS)); self.table.setHorizontalHeaderLabels(COLUMNS)
        self.table.horizontalHeaderItem(2).setToolTip("u8/i8/u16/i16/u32/i32")
        self.table.horizontalHeaderItem(3).setToolTip("big 或 little")
        self.table.horizontalHeaderItem(7).setToolTip("r 只读；rw 可写")
        self.table.setAlternatingRowColors(True)
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.horizontalHeader().setStretchLastSection(True)
        self.table.setMinimumHeight(270)
        self.table.setColumnWidth(0, 145); self.table.setColumnWidth(1, 205)
        layout.addWidget(self.table)
        self.can_frame = QLineEdit()
        if interface == "can":
            self.can_frame.setPlaceholderText("离线解析示例：0x181 01 02 03 04")
            decode = QPushButton("解析 CAN 帧")
            decode.clicked.connect(self.decode_can)
            can_row = QHBoxLayout(); can_row.addWidget(self.can_frame); can_row.addWidget(decode)
            layout.addLayout(can_row)
            live = QHBoxLayout()
            self.bitrate = QComboBox(); self.bitrate.addItems(["125000", "250000", "500000", "1000000"])
            self.bitrate.setCurrentText("500000"); live.addWidget(QLabel("标称速率")); live.addWidget(self.bitrate)
            self.extended = QComboBox(); self.extended.addItems(["标准 11 位", "扩展 29 位"])
            live.addWidget(self.extended)
            send = QPushButton("发送输入帧"); send.clicked.connect(self.send_can)
            receive = QPushButton("接收并解析一帧"); receive.clicked.connect(self.receive_can)
            live.addWidget(send); live.addWidget(receive); layout.addLayout(live)
        self.status = QLabel("CAN 原始收发需先应用配置；CANopen 使用收到的帧做解析。" if interface == "can" else "未应用配置")
        self.status.setWordWrap(True); layout.addWidget(self.status)
        if self.preset.count() > 1: self.preset.setCurrentIndex(1)

    def select_preset(self):
        profile = self.preset.currentData()
        if profile:
            try: self.show_profile(profile)
            except Exception as error: self.report_error(error)

    def import_json(self):
        path, _ = QFileDialog.getOpenFileName(self, "导入参数配置", str(PRESETS), "JSON (*.json)")
        if path:
            try: self.show_profile(load(path, self.interface, self.role))
            except Exception as error: self.report_error(error)

    def export_json(self):
        try: profile = self.collect()
        except Exception as error: self.report_error(error); return
        path, _ = QFileDialog.getSaveFileName(self, "导出参数配置", "profile.json", "JSON (*.json)")
        if path:
            Path(path).write_text(json.dumps(profile, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            self.status.setText(f"已导出 {path}")

    def show_profile(self, profile):
        self.profile = copy.deepcopy(profile)
        self.name.setText(profile.get("name", ""))
        self.protocol.setCurrentText(profile["protocol"])
        settings = profile.get("settings", {})
        self.address.setText(str(settings.get("address", "0x40" if self.interface == "i2c" else "0x30")))
        self.baud.setText(str(settings.get("baud", 115200)))
        self.unit.setText(str(settings.get("unit", 1)))
        if self.interface == "can": self.bitrate.setCurrentText(str(settings.get("nominal_bitrate", 500000)))
        if self.interface == "i3c": self.mode.setCurrentIndex(1 if settings.get("bus_mode") == "i2c" else 0)
        if self.interface == "spi": self.mode.setCurrentIndex(number(settings.get("mode", 0), 0, 3))
        self.table.setRowCount(0)
        for binding in profile["parameters"]: self.add_row(binding=binding)
        self.status.setText(f"已导入 {profile.get('name', '配置')}，共 {self.table.rowCount()} 个参数")

    def add_row(self, _checked=False, binding=None):
        p = binding or {"name": "新参数", "register": 0, "type": "u8", "access": "r"}
        row = self.table.rowCount(); self.table.insertRow(row)
        if self.interface == "gpio": location = f"PA{number(p.get('pin', 0), 0, 1)}"
        elif self.interface == "qep": location = p.get("source", "position")
        elif self.interface == "can":
            location = f"0x{number(p.get('can_id', 0)):X}@{number(p.get('byte_offset', 0), 0, 7)}"
        elif self.protocol.currentText() == "raw":
            location = f"{p.get('request', '')} -> {p.get('receive', 0)}"
        elif self.protocol.currentText() == "at": location = p.get("command", "AT")
        else: location = f"0x{number(p.get('register', 0), 0, 0xFFFF):X}"
        fields = [p.get("name", ""), location, p.get("type", "u8"), p.get("endian", "big"), str(p.get("scale", 1)),
                  str(p.get("offset", 0)), p.get("unit", ""), p.get("access", "r"),
                  "" if "value" not in p else str(p["value"]), "—", "—"]
        for column, value in enumerate(fields): self.table.setItem(row, column, QTableWidgetItem(str(value)))
        self.table.item(row, 0).setData(Qt.ItemDataRole.UserRole, copy.deepcopy(p))

    def remove_row(self):
        row = self.table.currentRow()
        if row >= 0: self.table.removeRow(row)

    def _binding(self, row):
        cell = lambda column: self.table.item(row, column).text().strip() if self.table.item(row, column) else ""
        p = copy.deepcopy(self.table.item(row, 0).data(Qt.ItemDataRole.UserRole) or {})
        p.update({"name": cell(0), "type": cell(2), "endian": cell(3), "scale": float(cell(4)),
             "offset": float(cell(5)), "unit": cell(6), "access": cell(7)}
        )
        if self.interface == "gpio":
            if cell(1) not in {"PA0", "PA1"}: raise ValueError("GPIO 绑定位置只能是 PA0 或 PA1")
            p["pin"] = int(cell(1)[2])
        elif self.interface == "qep": p["source"] = cell(1)
        elif self.interface == "can":
            parts = cell(1).split("@")
            if len(parts) != 2: raise ValueError("CAN 绑定位置格式为 ID@字节偏移，例如 0x181@0")
            p["can_id"], p["byte_offset"] = number(parts[0]), number(parts[1], 0, 7)
        elif self.protocol.currentText() == "raw":
            parts = cell(1).split("->")
            if len(parts) != 2: raise ValueError("原始绑定格式为 'HEX -> 读字节数'")
            p["request"], p["receive"] = parts[0].strip(), number(parts[1].strip(), 0, 64)
        elif self.protocol.currentText() == "at": p["command"] = cell(1)
        else: p["register"] = number(cell(1), 0, 0xFFFF)
        if cell(8): p["value"] = float(cell(8))
        else: p.pop("value", None)
        return p

    def collect(self):
        profile = copy.deepcopy(self.profile)
        profile.update(name=self.name.text().strip(), interface=self.interface, role=self.role,
                       protocol=self.protocol.currentText(),
                       parameters=[self._binding(row) for row in range(self.table.rowCount())])
        settings = profile.setdefault("settings", {})
        if self.interface in {"i2c", "i3c"}: settings["address"] = number(self.address.text(), 8, 0x77)
        if self.interface in {"uart", "rs232", "rs485"}:
            settings["baud"] = number(self.baud.text(), 1200, 2000000)
            settings["unit"] = number(self.unit.text(), 1, 247)
        if self.interface == "spi": settings["mode"] = self.mode.currentIndex()
        if self.interface == "i3c": settings["bus_mode"] = "i2c" if self.mode.currentIndex() else "i3c"
        if self.interface == "can": settings["nominal_bitrate"] = int(self.bitrate.currentText())
        return validate(profile, self.interface, self.role)

    def apply(self):
        try:
            profile = self.collect()
            if self.interface == "can":
                info = self.api.info()
                if not info.capabilities & (1 << 16):
                    raise RuntimeError("CAN 原始收发需要协议 1.5 固件")
                self.api.can_config(int(self.bitrate.currentText()))
                self.status.setText("经典 CAN 接口已启动；请确认总线终端和另一节点已连接")
                return
            if self.interface == "i3c" and self.mode.currentIndex() == 1:
                raise NotImplementedError("I3C 引脚上的 I²C 兼容模式尚未实现")
            if self.role == "slave":
                if self.interface == "i2c" and profile["protocol"] == "hdc1080":
                    info = self.api.info()
                    if (info.protocol_major, info.protocol_minor) < (1, 5):
                        raise RuntimeError("HDC1080 仿真需要固件协议 1.5；请更新目标板固件")
                message = apply_target(self.api, profile)
            elif self.interface == "i2c":
                self.api.i2c_target_config(0, 0); message = "I²C 主机已启用"
            elif self.interface == "i3c":
                self.api.i3c_config(0); message = "I3C 控制器已启用；请在原始操作页执行 ENTDAA"
            elif self.interface == "spi":
                self.api.spi_config(0, profile["settings"]["mode"], 7); message = "SPI 主机已启用"
            elif self.interface in {"uart", "rs232", "rs485"}:
                self.api.serial_config(BUS[self.interface], 0, profile["settings"]["baud"])
                message = "串口透明收发已启用"
            elif self.interface == "qep":
                self.api.qep_start(); message = "QEP 计数已启用"
            elif self.interface == "gpio": message = "GPIO 参数表已准备，可直接查询"
            else: raise NotImplementedError("该接口暂无硬件后端")
            self.status.setText(message)
        except Exception as error: self.report_error(error)

    def query_selected(self):
        if self.interface == "can":
            self.receive_can(); return
        row = self.table.currentRow()
        if row >= 0: self._query_row(row)

    def refresh_port_status(self):
        try:
            if self.interface == "can":
                info = self.api.info()
                if not info.capabilities & (1 << 16):
                    self.status.setText("当前固件不支持 CAN；请更新到协议 1.5")
                    return
                s = self.api.can_status()
                last_error = "无新错误" if s.last_error == 7 else str(s.last_error)
                self.status.setText(f"CAN ready={s.ready}，TX 空位={s.tx_free}，RX 待取={s.rx_pending}，"
                                    f"bus-off={s.bus_off}，最近错误={last_error}，TX/RX 错误计数={s.tx_error}/{s.rx_error}")
                return
            status = self.api.interface_status()
            if self.interface == "spi":
                message = f"SPI ready={status.spi_ready}，角色={status.spi_role}，错误=0x{status.spi_error:X}"
            elif self.interface == "i3c":
                message = f"I3C ready={status.i3c_ready}，角色={status.i3c_role}，错误=0x{status.i3c_error:X}"
            elif self.interface == "i2c":
                message = f"I²C 从机={status.i2c_target_active}，地址=0x{status.i2c_target_address:02X}"
            elif self.interface in {"uart", "rs232", "rs485"}:
                message = f"{self.interface.upper()} 就绪={bool(status.serial_ready_mask & (1 << BUS[self.interface]))}"
            elif self.interface == "gpio":
                states = [self.api.pin_read(pin) for pin in (0, 1)]
                message = ", ".join(f"PA{pin}: 模式{state[0]} 值{state[1]}"
                                    for pin, state in enumerate(states))
            else:
                position, index, direction = self.api.qep_read()
                message = f"QEP 位置={position}，索引电平={index}，递减={direction}"
            self.status.setText(message)
        except Exception as error: self.report_error(error)

    def query_all(self):
        if self.interface == "can":
            self.receive_can(); return
        for row in range(self.table.rowCount()):
            if not self._query_row(row): break

    def write_selected(self):
        try:
            row = self.table.currentRow()
            if row < 0: raise ValueError("请先选中一条参数")
            profile = self.collect(); binding = profile["parameters"][row]
            if "value" not in binding: raise ValueError("请在“初值”列填入要写入的数值")
            write_value(self.api, profile, binding, binding["value"])
            self.status.setText(f"已写入 {binding['name']}")
            self._query_row(row)
        except Exception as error: self.report_error(error)

    def _query_row(self, row):
        try:
            profile = self.collect(); binding = profile["parameters"][row]
            if self.role == "slave": value, raw = read_target(self.api, profile, binding)
            else: value, raw = query(self.api, profile, binding)
            self.table.item(row, 9).setText(display_value(value))
            self.table.item(row, 10).setText(raw)
            self.status.setText(f"已读取 {binding['name']}")
            return True
        except Exception as error:
            self.status.setText(f"读取失败：{error}")
            self.report_error(error)
            return False

    def decode_can(self):
        try:
            profile = self.collect()
            tokens = self.can_frame.text().split()
            if not tokens: raise ValueError("请输入 CAN ID 和数据字节")
            can_id = number(tokens[0], 0, 0x1FFFFFFF)
            data = bytes(int(token, 16) for token in tokens[1:])
            if len(data) > 8: raise ValueError("经典 CAN 数据最多 8 字节")
            name, values = decode_can_frame(profile, can_id, data)
            self.status.setText(f"{name}；" + (", ".join(f"{k}={display_value(v)}" for k, v in values.items()) or "无匹配绑定"))
            for row, p in enumerate(profile["parameters"]):
                if p["name"] in values: self.table.item(row, 9).setText(display_value(values[p["name"]]))
        except Exception as error: self.report_error(error)

    def send_can(self):
        try:
            tokens = self.can_frame.text().split()
            if not tokens: raise ValueError("请输入 CAN ID 与数据字节")
            identifier = number(tokens[0], 0, 0x1FFFFFFF)
            data = bytes(int(token, 16) for token in tokens[1:])
            if len(data) > 8: raise ValueError("经典 CAN 数据最多 8 字节")
            self.api.can_send(identifier, self.extended.currentIndex() == 1, data)
            self.status.setText(f"帧 0x{identifier:X} 已加入发送队列，{len(data)} 字节")
        except Exception as error: self.report_error(error)

    def receive_can(self):
        try:
            frame = self.api.can_receive()
            if frame is None:
                self.status.setText("接收 FIFO 为空")
                return
            identifier, extended, data = frame
            self.can_frame.setText(f"0x{identifier:X} " + data.hex(" ").upper())
            self.decode_can()
            self.status.setText(self.status.text() + ("；扩展帧" if extended else "；标准帧"))
        except Exception as error: self.report_error(error)


def wrap_interfaces(tabs, api, report_error):
    """Keep proven byte-level tools as a third page for each interface."""
    mapping = {"GPIO": "gpio", "I²C": "i2c", "I3C": "i3c", "SPI": "spi", "QEP": "qep",
               "UART": "uart", "RS232": "rs232", "RS485": "rs485"}
    for index in range(tabs.count()):
        name = tabs.tabText(index)
        if name not in mapping: continue
        old = tabs.widget(index); tabs.removeTab(index)
        container = QTabWidget()
        interface = mapping[name]
        container.addTab(ProfilePage(api, interface, "master", report_error),
                         "参数查询" if interface in {"gpio", "qep"} else "主机配置 / 参数")
        if interface not in {"gpio", "qep"}:
            container.addTab(ProfilePage(api, interface, "slave", report_error), "从机配置 / 参数")
        container.addTab(old, "原始操作")
        tabs.insertTab(index, container, name)
    can = QTabWidget()
    can.addTab(ProfilePage(api, "can", "master", report_error), "主机 / 报文解析")
    can.addTab(ProfilePage(api, "can", "slave", report_error), "从机配置")
    tabs.addTab(can, "CAN")
