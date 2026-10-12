"""Validated, portable parameter profiles for the desktop bridge UI.

Profiles are data only.  Importing JSON never evaluates code or touches hardware;
the user explicitly applies a target profile or queries a controller binding.
"""
from __future__ import annotations

import json
import math
import re
import time
from pathlib import Path

INTERFACES = {"gpio", "i2c", "i3c", "spi", "uart", "rs232", "rs485", "qep", "can"}
PROTOCOLS = {"bridge-register", "hdc1080", "raw", "modbus-rtu", "at", "can-raw", "canopen"}
ALLOWED_PROTOCOLS = {
    "gpio": {"raw"}, "qep": {"raw"},
    "i2c": {"bridge-register", "hdc1080", "raw"},
    "i3c": {"bridge-register", "raw"}, "spi": {"bridge-register", "raw"},
    "uart": {"bridge-register", "raw", "modbus-rtu", "at"},
    "rs232": {"bridge-register", "raw", "modbus-rtu", "at"},
    "rs485": {"bridge-register", "raw", "modbus-rtu", "at"},
    "can": {"can-raw", "canopen"},
}
TYPES = {"u8": (1, False), "i8": (1, True), "u16": (2, False),
         "i16": (2, True), "u32": (4, False), "i32": (4, True)}
BUS = {"uart": 0, "rs232": 1, "rs485": 2, "spi": 3, "i3c": 4, "i2c": 5}


def number(value, low=0, high=0xFFFFFFFF):
    if isinstance(value, bool):
        raise ValueError("数字不能是布尔值")
    result = int(value, 0) if isinstance(value, str) else int(value)
    if not low <= result <= high:
        raise ValueError(f"数值 {result} 超出 {low}..{high}")
    return result


def validate(data, expected_interface=None, expected_role=None):
    if not isinstance(data, dict) or data.get("schema") != 1:
        raise ValueError("配置文件必须使用 schema: 1")
    interface, role, protocol = data.get("interface"), data.get("role"), data.get("protocol")
    if interface not in INTERFACES or role not in {"master", "slave"} or protocol not in PROTOCOLS:
        raise ValueError("接口、角色或协议无效")
    if protocol not in ALLOWED_PROTOCOLS[interface]:
        raise ValueError("该协议不能绑定到当前接口")
    if expected_interface and interface != expected_interface:
        raise ValueError(f"此页面只接受 {expected_interface} 配置")
    if expected_role and role != expected_role:
        raise ValueError(f"此页面只接受 {expected_role} 配置")
    if protocol == "hdc1080" and interface != "i2c":
        raise ValueError("HDC1080 仅适用于 I²C")
    if protocol in {"modbus-rtu", "at"} and interface not in {"uart", "rs232", "rs485"}:
        raise ValueError("该协议仅适用于串口")
    if protocol in {"can-raw", "canopen"} and interface != "can":
        raise ValueError("CAN 配置只能在 CAN 页面导入")
    settings = data.get("settings", {})
    if not isinstance(settings, dict):
        raise ValueError("settings 必须是对象")
    if interface == "i2c":
        number(settings.get("address", 0x40), 8, 0x77)
    if interface == "i3c" and settings.get("bus_mode", "i3c") not in {"i3c", "i2c"}:
        raise ValueError("I3C 接口 bus_mode 必须是 i3c 或 i2c")
    if interface in {"uart", "rs232", "rs485"}:
        number(settings.get("baud", 115200), 1200, 2000000)
    if interface == "can" and number(settings.get("nominal_bitrate", 500000)) not in {125000, 250000, 500000, 1000000}:
        raise ValueError("CAN 标称速率不支持")
    if protocol == "modbus-rtu":
        number(settings.get("unit", 1), 1, 247)
    params = data.get("parameters", [])
    if not isinstance(params, list) or len(params) > 128:
        raise ValueError("parameters 必须是最多 128 项的数组")
    seen = set()
    for p in params:
        if not isinstance(p, dict) or not isinstance(p.get("name"), str) or not p["name"].strip():
            raise ValueError("每个参数必须有名称")
        if p["name"] in seen:
            raise ValueError("参数名称重复")
        seen.add(p["name"])
        if p.get("type", "u8") not in TYPES or p.get("access", "r") not in {"r", "rw"}:
            raise ValueError(f"参数 {p['name']} 的类型或访问权限无效")
        if p.get("endian", "big") not in {"big", "little"}:
            raise ValueError("字节序无效")
        width = TYPES[p.get("type", "u8")][0]
        if protocol in {"bridge-register", "hdc1080", "modbus-rtu"}:
            upper = 0xFFFF if protocol == "modbus-rtu" else 255
            reg = number(p.get("register"), 0, upper)
            if protocol == "bridge-register" and reg + width > 256:
                raise ValueError("寄存器访问越界")
            if protocol == "hdc1080" and reg not in {0, 1, 2, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF}:
                raise ValueError("HDC1080 寄存器地址无效")
            if protocol == "hdc1080" and width != 2:
                raise ValueError("HDC1080 寄存器必须按 16 位读取")
        if protocol == "at" and not isinstance(p.get("command"), str):
            raise ValueError("AT 参数需要 command 字符串")
        if protocol == "raw" and interface in BUS:
            request = p.get("request", "")
            if not isinstance(request, str): raise ValueError("原始请求必须是 HEX 字符串")
            try: payload = bytes.fromhex(request)
            except ValueError as error: raise ValueError("原始请求 HEX 无效") from error
            if len(payload) > 64: raise ValueError("原始请求最多 64 字节")
            number(p.get("receive", 0), 0, 64)
        if interface == "gpio" and number(p.get("pin", 0), 0, 1) not in {0, 1}:
            raise ValueError("GPIO 绑定只能使用 PA0/PA1")
        if interface == "qep" and p.get("source", "position") not in {"position", "index", "direction"}:
            raise ValueError("QEP 来源必须是 position/index/direction")
        if protocol in {"can-raw", "canopen"}:
            number(p.get("can_id"), 0, 0x1FFFFFFF)
            if number(p.get("byte_offset", 0), 0, 7) + width > 8:
                raise ValueError("CAN 参数超出经典 CAN 的 8 字节数据区")
        scale, offset = float(p.get("scale", 1)), float(p.get("offset", 0))
        if not math.isfinite(scale) or not math.isfinite(offset) or scale == 0:
            raise ValueError("scale/offset 必须是有限数，scale 不能为零")
    return data


def load(path, expected_interface=None, expected_role=None):
    source = Path(path)
    if source.stat().st_size > 256 * 1024:
        raise ValueError("配置文件过大")
    return validate(json.loads(source.read_text(encoding="utf-8")), expected_interface, expected_role)


def decode(raw, binding):
    kind = binding.get("type", "u8")
    width, signed = TYPES[kind]
    if len(raw) != width:
        raise ValueError("返回长度与绑定类型不符")
    value = int.from_bytes(raw, binding.get("endian", "big"), signed=signed)
    scale, offset = float(binding.get("scale", 1)), float(binding.get("offset", 0))
    return value if scale == 1 and offset == 0 else value * scale + offset


def encode(value, binding):
    kind = binding.get("type", "u8")
    width, signed = TYPES[kind]
    scale = float(binding.get("scale", 1))
    if scale == 0:
        raise ValueError("scale 不能为零")
    raw = round((float(value) - float(binding.get("offset", 0))) / scale)
    low = -(1 << (width * 8 - 1)) if signed else 0
    high = (1 << (width * 8 - (1 if signed else 0))) - 1
    if not low <= raw <= high:
        raise ValueError("设定值超出参数范围")
    return raw.to_bytes(width, binding.get("endian", "big"), signed=signed)


def crc16_modbus(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc.to_bytes(2, "little")


def _serial_response(api, port, timeout=0.8):
    end = time.monotonic() + timeout
    received = bytearray()
    while time.monotonic() < end:
        chunk = api.serial_read(port)
        if chunk:
            received.extend(chunk)
            if len(chunk) < 64:
                # A short idle gap lets a multipart UART reply finish.
                time.sleep(0.02)
                chunk = api.serial_read(port)
                received.extend(chunk)
                if not chunk:
                    break
        else:
            time.sleep(0.01)
    return bytes(received)


def _serial_drain(api, port):
    for _ in range(4):
        if not api.serial_read(port): break


def query(api, profile, binding):
    interface, protocol = profile["interface"], profile["protocol"]
    width = TYPES[binding.get("type", "u8")][0]
    register = number(binding.get("register", 0), 0, 0xFFFF)
    settings = profile.get("settings", {})
    if interface == "qep":
        position, index, direction = api.qep_read()
        key = binding.get("source", "position")
        value = {"position": position, "index": index, "direction": direction}[key]
        return value, str(value)
    if interface == "gpio":
        mode, value = api.pin_read(number(binding.get("pin", 0), 0, 1))
        return value, f"模式 {mode} / 值 {value}"
    if protocol == "raw":
        tx = bytes.fromhex(binding.get("request", ""))
        receive = number(binding.get("receive", 0), 0, 64)
        if interface == "i2c":
            raw = api.i2c_transfer(number(settings.get("address", 0x40), 8, 0x77), tx, receive)
        elif interface == "i3c":
            raw = api.i3c_transfer(number(settings.get("address", 0x30), 8, 0x77), tx, receive)
        elif interface == "spi":
            if not tx: raise ValueError("SPI 原始传输至少需要 1 字节")
            raw = api.spi_transfer(tx)
        elif interface in {"uart", "rs232", "rs485"}:
            if tx: api.serial_write(BUS[interface], tx)
            raw = _serial_response(api, BUS[interface]) if receive else b""
            raw = raw[:receive]
        else: raise NotImplementedError("此原始接口没有传输后端")
        return raw.hex(" ").upper(), raw.hex(" ").upper()
    if protocol == "bridge-register":
        raw = api.remote_register_read(BUS[interface], number(settings.get("address", 0x30), 0, 127), register, width)
    elif protocol == "hdc1080":
        address = number(settings.get("address", 0x40), 8, 0x77)
        api.i2c_transfer(address, bytes([register]), 0)
        if register in {0, 1}:
            time.sleep(0.02)
        raw = api.i2c_transfer(address, b"", width)
    elif protocol == "modbus-rtu":
        unit = number(settings.get("unit", 1), 1, 247)
        count = (width + 1) // 2
        payload = bytes([unit, 3]) + register.to_bytes(2, "big") + count.to_bytes(2, "big")
        _serial_drain(api, BUS[interface])
        api.serial_write(BUS[interface], payload + crc16_modbus(payload))
        answer = _serial_response(api, BUS[interface])
        if (len(answer) < 5 or answer[0] != unit or answer[1] != 3 or
                answer[2] != count * 2 or answer[-2:] != crc16_modbus(answer[:-2])):
            raise ValueError("Modbus RTU 应答无效或超时")
        raw = answer[3:3 + width]
    elif protocol == "at":
        command = binding["command"].encode("ascii") + b"\r\n"
        _serial_drain(api, BUS[interface])
        api.serial_write(BUS[interface], command)
        response = _serial_response(api, BUS[interface]).decode("utf-8", "replace")
        pattern = binding.get("pattern")
        if pattern:
            match = re.search(pattern, response)
            if not match:
                raise ValueError("AT 响应不匹配预设")
            return match.group(1) if match.lastindex else match.group(0), response
        return response.strip(), response
    else:
        raise NotImplementedError("该预设当前没有实时查询后端")
    return decode(raw, binding), raw.hex(" ").upper()


def apply_target(api, profile):
    """Configure a supported target and preload writable/sample parameters."""
    validate(profile)
    interface, protocol = profile["interface"], profile["protocol"]
    settings = profile.get("settings", {})
    if protocol not in {"bridge-register", "hdc1080"}:
        raise NotImplementedError("此协议尚无设备端自动应答实现")
    if interface == "i2c":
        address = number(settings.get("address", 0x40), 8, 0x77)
        api.i2c_target_config(1, address)
        if protocol == "hdc1080":
            for p in profile["parameters"]:
                reg = number(p["register"], 0, 255)
                if "value" in p and reg in {0, 1, 2}:
                    api.i2c_target_profile(1, reg, int.from_bytes(encode(p["value"], p), "big"))
            return "HDC1080 仿真已启用"
        api.i2c_target_profile(0, 0, 0)
    elif interface == "i3c":
        api.i3c_config(1)
    elif interface == "spi":
        api.spi_config(1, number(settings.get("mode", 0), 0, 3), 7)
    elif interface in {"uart", "rs232", "rs485"}:
        if protocol != "bridge-register":
            raise NotImplementedError("固件从机当前仅支持桥接寄存器帧；Modbus/AT 预设可在主机页查询")
        api.serial_config(BUS[interface], 1, number(settings.get("baud", 115200), 1200, 2000000))
    else:
        raise NotImplementedError("该接口尚无可用的物理从机后端")
    if protocol != "bridge-register":
        raise NotImplementedError("该协议暂不支持目标端加载")
    for p in profile["parameters"]:
        if "value" in p:
            api.register_write(BUS[interface], number(p["register"], 0, 255), encode(p["value"], p))
    return "从机配置与初值已写入设备"


def read_target(api, profile, binding):
    if profile["protocol"] == "hdc1080":
        reg = number(binding["register"], 0, 255)
        raw = api.i2c_target_word(reg).to_bytes(2, "big")
        return decode(raw, binding), raw.hex(" ").upper()
    if profile["protocol"] != "bridge-register":
        raise NotImplementedError("此从机协议尚无设备端参数读取实现")
    width = TYPES[binding.get("type", "u8")][0]
    raw = api.register_read(BUS[profile["interface"]], number(binding["register"], 0, 255), width)
    return decode(raw, binding), raw.hex(" ").upper()


def write_value(api, profile, binding, value):
    if binding.get("access", "r") != "rw":
        raise ValueError("该参数为只读")
    interface, protocol = profile["interface"], profile["protocol"]
    register = number(binding.get("register", 0), 0, 0xFFFF)
    raw = encode(value, binding)
    if profile["role"] == "slave":
        if protocol == "hdc1080":
            if register not in {0, 1, 2}: raise ValueError("该 HDC1080 寄存器不可写")
            api.i2c_target_profile(1, register, int.from_bytes(raw, "big"))
        elif protocol == "bridge-register":
            api.register_write(BUS[interface], register, raw)
        else: raise NotImplementedError("此从机协议暂不支持修改绑定值")
    elif protocol == "bridge-register":
        api.remote_register_write(BUS[interface],
                                  number(profile.get("settings", {}).get("address", 0x30), 0, 127),
                                  register, raw)
    elif protocol == "hdc1080" and register == 2:
        address = number(profile.get("settings", {}).get("address", 0x40), 8, 0x77)
        api.i2c_transfer(address, bytes([2]) + raw, 0)
    elif protocol == "modbus-rtu" and len(raw) == 2:
        unit = number(profile.get("settings", {}).get("unit", 1), 1, 247)
        request = bytes([unit, 6]) + register.to_bytes(2, "big") + raw
        _serial_drain(api, BUS[interface])
        api.serial_write(BUS[interface], request + crc16_modbus(request))
        answer = _serial_response(api, BUS[interface])
        if answer != request + crc16_modbus(request):
            raise ValueError("Modbus 写单寄存器应答无效或超时")
    else:
        raise NotImplementedError("此协议暂不支持写入绑定值")


def decode_can_frame(profile, can_id, data):
    if len(data) > 8 or not 0 <= can_id <= 0x1FFFFFFF:
        raise ValueError("请输入合法的经典 CAN 帧")
    name = "原始 CAN"
    if profile["protocol"] == "canopen" and can_id <= 0x7FF:
        family = can_id & 0x780
        names = {0x180: "TPDO1", 0x200: "RPDO1",
                 0x280: "TPDO2", 0x300: "RPDO2", 0x380: "TPDO3", 0x400: "RPDO3",
                 0x480: "TPDO4", 0x500: "RPDO4", 0x580: "SDO 响应", 0x600: "SDO 请求",
                 0x700: "心跳"}
        if can_id == 0: name = "NMT 控制"
        elif can_id == 0x80: name = "SYNC"
        elif can_id == 0x100: name = "TIME"
        elif 0x81 <= can_id <= 0xFF: name = f"EMCY / 节点 {can_id & 0x7F}"
        else: name = f"{names.get(family, 'CANopen')} / 节点 {can_id & 0x7F}"
    values = {}
    for p in profile.get("parameters", []):
        if number(p.get("can_id"), 0, 0x1FFFFFFF) == can_id:
            start = number(p.get("byte_offset", 0), 0, 7)
            width = TYPES[p.get("type", "u8")][0]
            if start + width <= len(data):
                values[p["name"]] = decode(data[start:start + width], p)
    if profile["protocol"] == "canopen" and 0x701 <= can_id <= 0x77F and data:
        state = {0: "启动", 4: "停止", 5: "运行", 127: "预运行"}.get(data[0], f"0x{data[0]:02X}")
        name += f" / {state}"
    return name, values
