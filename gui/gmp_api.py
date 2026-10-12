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
