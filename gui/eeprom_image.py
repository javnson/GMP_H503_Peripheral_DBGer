"""PC-backed EEPROM image and the USB page-service loop.

The binary image is authoritative. The MCU holds only one page and a write
change mask; the UI or another client must service events while I2C is active.
"""
from pathlib import Path
import os

CAPACITIES = {256: (8, 8), 16384: (14, 64), 32768: (15, 64), 65536: (16, 128)}


class EepromImage:
    def __init__(self, path, capacity):
        if capacity not in CAPACITIES:
            raise ValueError("EEPROM 容量必须为 256/16384/32768/65536 字节")
        self.path = Path(path)
        if not self.path.is_file() or self.path.stat().st_size != capacity:
            raise ValueError(f"镜像文件必须恰好为 {capacity} 字节")
        self.capacity = capacity
        self.file = self.path.open("r+b", buffering=0)

    def close(self):
        if not self.file.closed: self.file.close()

    def read_page(self, page, page_size):
        start = page * page_size
        if start + page_size > self.capacity:
            raise ValueError("设备请求的 EEPROM 页超出镜像")
        self.file.seek(start)
        data = self.file.read(page_size)
        if len(data) != page_size: raise OSError("读取 EEPROM 镜像失败")
        return data

    def apply_write_event(self, event):
        data = bytearray(self.read_page(event.page, event.page_size))
        for i in range(event.page_size):
            if event.mask[i // 8] & (1 << (i % 8)):
                data[i] = event.data[i]
        self.file.seek(event.page * event.page_size)
        self.file.write(data)
        os.fsync(self.file.fileno())

    def service_once(self, api):
        event = api.i2c_eeprom_event()
        if event.kind == 0: return False
        if event.page_size != CAPACITIES[self.capacity][1]:
            raise ValueError("设备 EEPROM 页大小与镜像不一致")
        if event.kind == 1:
            api.i2c_eeprom_reply(event, self.read_page(event.page, event.page_size))
        elif event.kind == 2:
            self.apply_write_event(event)
            api.i2c_eeprom_reply(event)
        else:
            raise ValueError("未知 EEPROM 服务事件")
        return True

    def sync_small_from_device(self, api):
        """Mirror external I2C writes from the volatile 256-byte target to disk."""
        if self.capacity != 256: raise ValueError("仅 256 字节镜像可整片同步")
        actual = b"".join(api.i2c_eeprom_read(i, 64) for i in range(0, 256, 64))
        self.file.seek(0)
        if self.file.read(256) != actual:
            self.file.seek(0)
            self.file.write(actual)
            os.fsync(self.file.fileno())
            return True
        return False


def load_to_device(api, image):
    """Upload a 256-byte file to the MCU's volatile 24LC02B RAM."""
    if image.capacity != 256: raise ValueError("大容量镜像需启动页服务")
    image.file.seek(0)
    for address in range(0, 256, 32):
        data = image.file.read(32)
        if len(data) != 32: raise OSError("读取 EEPROM 镜像失败")
        api.i2c_eeprom_write(address, data)


def save_from_device(api, path):
    data = b"".join(api.i2c_eeprom_read(i, 64) for i in range(0, 256, 64))
    Path(path).write_bytes(data)
