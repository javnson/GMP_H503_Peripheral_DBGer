"""ctypes binding for the cross-platform C ABI in host/include/gmp_host.h."""
import ctypes as c
import os
from pathlib import Path
import sys


class Info(c.Structure):
    _fields_ = [("protocol_major", c.c_uint8), ("protocol_minor", c.c_uint8),
                ("capabilities", c.c_uint32), ("name", c.c_char * 64)]


class Status(c.Structure):
    _fields_ = [("tpld_boot_status", c.c_uint8), ("tpld_address", c.c_uint8),
                ("image_present", c.c_uint8), ("autoload", c.c_uint8),
                ("generation", c.c_uint32), ("image_crc32", c.c_uint32)]


class InterfaceStatus(c.Structure):
    _fields_ = [("spi_ready", c.c_uint8), ("spi_role", c.c_uint8),
                ("spi_error", c.c_uint32),
                ("i3c_ready", c.c_uint8), ("i3c_role", c.c_uint8),
                ("i3c_error", c.c_uint32),
                ("i2c_target_active", c.c_uint8),
                ("i2c_target_address", c.c_uint8),
                ("serial_ready_mask", c.c_uint8)]


class CanStatus(c.Structure):
    _fields_ = [(name, c.c_uint8) for name in
                ("ready", "tx_free", "rx_pending", "bus_off", "last_error", "tx_error", "rx_error")]


class Api:
    def __init__(self):
        root = Path(__file__).resolve().parents[1]
        name = "gmp_host.dll" if sys.platform == "win32" else "libgmp_host.so"
        paths = [os.environ.get("GMP_HOST_LIBRARY"), root / "host" / "dist" / "bin" / name,
                 root / "host" / "dist" / "lib" / name,
                 root / "host" / "dist-linux" / "lib" / name]
        found = next((str(p) for p in paths if p and Path(p).exists()), None)
        if not found:
            raise FileNotFoundError("Build host library first or set GMP_HOST_LIBRARY")
        self.lib = c.CDLL(found)
        self.lib.gmp_open.argtypes = [c.c_char_p, c.POINTER(c.c_void_p)]
        self.lib.gmp_open.restype = c.c_int
        self.lib.gmp_close.argtypes = [c.c_void_p]
        self.lib.gmp_info_get.argtypes = [c.c_void_p, c.POINTER(Info)]
        self.lib.gmp_info_get.restype = c.c_int
        self.lib.gmp_status_get.argtypes = [c.c_void_p, c.POINTER(Status)]
        self.lib.gmp_status_get.restype = c.c_int
        self.lib.gmp_interface_status_get.argtypes = [c.c_void_p, c.POINTER(InterfaceStatus)]
        self.lib.gmp_interface_status_get.restype = c.c_int
        self.lib.gmp_load_tpld_hex.argtypes = [c.c_char_p, c.POINTER(c.c_uint8)]
        self.lib.gmp_load_tpld_hex.restype = c.c_int
        self.lib.gmp_tpld_upload.argtypes = [c.c_void_p, c.POINTER(c.c_uint8), c.c_uint8]
        self.lib.gmp_tpld_upload.restype = c.c_int
        self.lib.gmp_reset.argtypes = [c.c_void_p]
        self.lib.gmp_reset.restype = c.c_int
        self.lib.gmp_gpio_read.argtypes = [c.c_void_p, c.c_uint8, c.c_uint8,
                                           c.POINTER(c.c_uint8)]
        self.lib.gmp_gpio_read.restype = c.c_int
        self.lib.gmp_gpio_write.argtypes = [c.c_void_p, c.c_uint8, c.c_uint8, c.c_uint8]
        self.lib.gmp_gpio_write.restype = c.c_int
        signatures = {
            "gmp_pin_config": [c.c_void_p, c.c_uint8, c.c_uint8],
            "gmp_pin_read": [c.c_void_p, c.c_uint8, c.POINTER(c.c_uint8), c.POINTER(c.c_uint16)],
            "gmp_pin_set": [c.c_void_p, c.c_uint8, c.c_uint16],
            "gmp_i2c_transfer": [c.c_void_p, c.c_uint8, c.POINTER(c.c_uint8), c.c_uint8,
                                 c.POINTER(c.c_uint8), c.c_uint8],
            "gmp_i2c_target_config": [c.c_void_p, c.c_uint8, c.c_uint8],
            "gmp_i2c_target_profile": [c.c_void_p, c.c_uint8, c.c_uint8, c.c_uint16],
            "gmp_i2c_target_word": [c.c_void_p, c.c_uint8, c.POINTER(c.c_uint16)],
            "gmp_can_config": [c.c_void_p, c.c_uint32],
            "gmp_can_send": [c.c_void_p, c.c_uint32, c.c_uint8, c.POINTER(c.c_uint8), c.c_uint8],
            "gmp_can_receive": [c.c_void_p, c.POINTER(c.c_uint8), c.POINTER(c.c_uint32),
                                c.POINTER(c.c_uint8), c.POINTER(c.c_uint8), c.POINTER(c.c_uint8)],
            "gmp_can_status_get": [c.c_void_p, c.POINTER(CanStatus)],
            "gmp_i3c_config": [c.c_void_p, c.c_uint8],
            "gmp_i3c_assign": [c.c_void_p, c.c_uint8, c.c_uint8,
                               c.POINTER(c.c_uint8), c.POINTER(c.c_uint8)],
            "gmp_i3c_transfer": [c.c_void_p, c.c_uint8, c.POINTER(c.c_uint8), c.c_uint8,
                                 c.POINTER(c.c_uint8), c.c_uint8],
            "gmp_spi_config": [c.c_void_p, c.c_uint8, c.c_uint8, c.c_uint8],
            "gmp_spi_transfer": [c.c_void_p, c.POINTER(c.c_uint8), c.POINTER(c.c_uint8), c.c_uint8],
            "gmp_qep_start": [c.c_void_p],
            "gmp_qep_read": [c.c_void_p, c.POINTER(c.c_int32), c.POINTER(c.c_uint8), c.POINTER(c.c_uint8)],
            "gmp_qep_reset": [c.c_void_p],
            "gmp_serial_config": [c.c_void_p, c.c_uint8, c.c_uint8, c.c_uint32],
            "gmp_serial_write": [c.c_void_p, c.c_uint8, c.POINTER(c.c_uint8), c.c_uint8],
            "gmp_serial_read": [c.c_void_p, c.c_uint8, c.POINTER(c.c_uint8), c.c_uint8, c.POINTER(c.c_uint8)],
            "gmp_register_write": [c.c_void_p, c.c_uint8, c.c_uint8, c.POINTER(c.c_uint8), c.c_uint8],
            "gmp_register_read": [c.c_void_p, c.c_uint8, c.c_uint8, c.POINTER(c.c_uint8), c.c_uint8],
            "gmp_remote_register_read": [c.c_void_p, c.c_uint8, c.c_uint8, c.c_uint8,
                                           c.POINTER(c.c_uint8), c.c_uint8],
            "gmp_remote_register_write": [c.c_void_p, c.c_uint8, c.c_uint8, c.c_uint8,
                                            c.POINTER(c.c_uint8), c.c_uint8],
        }
        for name, args in signatures.items():
            method = getattr(self.lib, name)
            method.argtypes = args
            method.restype = c.c_int
        self.lib.gmp_error_string.argtypes = [c.c_int]
        self.lib.gmp_error_string.restype = c.c_char_p
        self.handle = c.c_void_p()

    def check(self, rc):
        if rc:
            raise RuntimeError(self.lib.gmp_error_string(rc).decode("utf-8"))

    def connect(self, port):
        self.close()
        self.check(self.lib.gmp_open(port.encode(), c.byref(self.handle)))
        return self.info()

    def close(self):
        if self.handle:
            self.lib.gmp_close(self.handle)
            self.handle = c.c_void_p()

    def info(self):
        value = Info()
        self.check(self.lib.gmp_info_get(self.handle, c.byref(value)))
        return value

    def status(self):
        value = Status()
        self.check(self.lib.gmp_status_get(self.handle, c.byref(value)))
        return value

    def interface_status(self):
        value = InterfaceStatus()
        self.check(self.lib.gmp_interface_status_get(self.handle, c.byref(value)))
        return value

    def upload(self, path, autoload):
        image = (c.c_uint8 * 512)()
        self.check(self.lib.gmp_load_tpld_hex(str(path).encode(), image))
        self.check(self.lib.gmp_tpld_upload(self.handle, image, int(autoload)))

    def reset(self):
        self.check(self.lib.gmp_reset(self.handle))

    def gpio_read(self, pin):
        value = c.c_uint8()
        self.check(self.lib.gmp_gpio_read(self.handle, 0, pin, c.byref(value)))
        return value.value

    def gpio_write(self, pin, value):
        self.check(self.lib.gmp_gpio_write(self.handle, 0, pin, int(value)))

    @staticmethod
    def _bytes(data):
        return (c.c_uint8 * len(data)).from_buffer_copy(data)

    def pin_config(self, pin, mode):
        self.check(self.lib.gmp_pin_config(self.handle, pin, mode))

    def pin_read(self, pin):
        mode, value = c.c_uint8(), c.c_uint16()
        self.check(self.lib.gmp_pin_read(self.handle, pin, c.byref(mode), c.byref(value)))
        return mode.value, value.value

    def pin_set(self, pin, value):
        self.check(self.lib.gmp_pin_set(self.handle, pin, value))

    def i2c_transfer(self, address, tx, rx_length):
        rx = (c.c_uint8 * rx_length)()
        self.check(self.lib.gmp_i2c_transfer(self.handle, address, self._bytes(tx), len(tx), rx, rx_length))
        return bytes(rx)

    def i2c_target_config(self, enabled, address):
        self.check(self.lib.gmp_i2c_target_config(self.handle, int(enabled), address))

    def i2c_target_profile(self, profile, register, value):
        self.check(self.lib.gmp_i2c_target_profile(self.handle, profile, register, value))

    def i2c_target_word(self, register):
        value = c.c_uint16()
        self.check(self.lib.gmp_i2c_target_word(self.handle, register, c.byref(value)))
        return value.value

    def can_config(self, bitrate):
        self.check(self.lib.gmp_can_config(self.handle, bitrate))

    def can_send(self, identifier, extended, data):
        self.check(self.lib.gmp_can_send(self.handle, identifier, int(extended), self._bytes(data), len(data)))

    def can_receive(self):
        present, identifier, extended, length = c.c_uint8(), c.c_uint32(), c.c_uint8(), c.c_uint8()
        data = (c.c_uint8 * 8)()
        self.check(self.lib.gmp_can_receive(self.handle, c.byref(present), c.byref(identifier),
                                             c.byref(extended), data, c.byref(length)))
        return (identifier.value, bool(extended.value), bytes(data[:length.value])) if present.value else None

    def can_status(self):
        value = CanStatus()
        self.check(self.lib.gmp_can_status_get(self.handle, c.byref(value)))
        return value

    def spi_config(self, role, mode, divider):
        self.check(self.lib.gmp_spi_config(self.handle, role, mode, divider))

    def i3c_config(self, role):
        self.check(self.lib.gmp_i3c_config(self.handle, role))

    def i3c_assign(self, base, maximum):
        addresses, actual = (c.c_uint8 * maximum)(), c.c_uint8()
        self.check(self.lib.gmp_i3c_assign(self.handle, base, maximum,
                                            addresses, c.byref(actual)))
        return list(addresses[:actual.value])

    def i3c_transfer(self, address, tx, rx_length):
        rx = (c.c_uint8 * rx_length)()
        self.check(self.lib.gmp_i3c_transfer(self.handle, address,
                                              self._bytes(tx), len(tx), rx, rx_length))
        return bytes(rx)

    def spi_transfer(self, tx):
        rx = (c.c_uint8 * len(tx))()
        self.check(self.lib.gmp_spi_transfer(self.handle, self._bytes(tx), rx, len(tx)))
        return bytes(rx)

    def qep_start(self):
        self.check(self.lib.gmp_qep_start(self.handle))

    def qep_read(self):
        position, index, direction = c.c_int32(), c.c_uint8(), c.c_uint8()
        self.check(self.lib.gmp_qep_read(self.handle, c.byref(position), c.byref(index), c.byref(direction)))
        return position.value, index.value, direction.value

    def qep_reset(self):
        self.check(self.lib.gmp_qep_reset(self.handle))

    def serial_config(self, port, role, baud):
        self.check(self.lib.gmp_serial_config(self.handle, port, role, baud))

    def serial_write(self, port, data):
        self.check(self.lib.gmp_serial_write(self.handle, port, self._bytes(data), len(data)))

    def serial_read(self, port, capacity=64):
        data, actual = (c.c_uint8 * capacity)(), c.c_uint8()
        self.check(self.lib.gmp_serial_read(self.handle, port, data, capacity, c.byref(actual)))
        return bytes(data[:actual.value])

    def register_write(self, port, address, data):
        self.check(self.lib.gmp_register_write(self.handle, port, address, self._bytes(data), len(data)))

    def register_read(self, port, address, length):
        data = (c.c_uint8 * length)()
        self.check(self.lib.gmp_register_read(self.handle, port, address, data, length))
        return bytes(data)

    def remote_register_read(self, bus, target, address, length):
        data = (c.c_uint8 * length)()
        self.check(self.lib.gmp_remote_register_read(self.handle, bus, target,
                                                     address, data, length))
        return bytes(data)

    def remote_register_write(self, bus, target, address, data):
        self.check(self.lib.gmp_remote_register_write(self.handle, bus, target,
                                                      address, self._bytes(data), len(data)))
