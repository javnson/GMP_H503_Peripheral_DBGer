"""Stress a 16 KiB PC-backed I2C EEPROM on two connected boards.

The controller is #1 and the target is #2. No EEPROM contents are written to
MCU Flash; the temporary binary image is stored in tmp/ on the computer.
"""
import argparse
import random
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "gui"))
from eeprom_image import EepromImage
from gmp_api import Api

ADDRESS = 0x50
CAPACITY = 16384
PAGE = 64


def wait_ready(controller, timeout=2.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            controller.i2c_transfer(ADDRESS, b"", 0)
            return
        except RuntimeError:
            time.sleep(0.002)
    raise TimeoutError("EEPROM did not ACK before deadline")


def read(controller, address, count):
    return controller.i2c_transfer(ADDRESS, address.to_bytes(2, "big"), count)


def write(controller, address, payload):
    wait_ready(controller)
    controller.i2c_transfer(ADDRESS, address.to_bytes(2, "big") + payload, 0)


def run(controller_port, target_port, iterations):
    path = ROOT / "tmp" / "eeprom-host-stress-16k.bin"
    path.parent.mkdir(exist_ok=True)
    initial = bytearray((i * 37 + i // 64) & 0xFF for i in range(CAPACITY))
    path.write_bytes(initial)
    image = EepromImage(path, CAPACITY)
    controller, target = Api(), Api()
    stop = threading.Event()
    failures = []
    events = [0]

    def serve():
        try:
            while not stop.is_set():
                if image.service_once(target): events[0] += 1
                else: time.sleep(0.001)
        except Exception as exc:
            failures.append(exc)
            stop.set()

    worker = None
    try:
        controller.connect(controller_port)
        info = target.connect(target_port)
        if (info.protocol_major, info.protocol_minor) < (1, 7):
            raise RuntimeError("target requires protocol 1.7")
        controller.i2c_target_config(0, 0)
        target.i2c_target_config(1, ADDRESS)
        before = target.metrics()
        target.i2c_eeprom_config(14, PAGE)
        target.i2c_target_profile(2, 0, 0)
        after = target.metrics()
        print(f"动态 RAM：绑定前 {before.allocated_bytes} B，绑定后 {after.allocated_bytes} B", flush=True)
        if after.allocated_bytes <= before.allocated_bytes:
            raise AssertionError("EEPROM binding did not allocate RAM")
        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        rng = random.Random(0x24C128)
        for i in range(iterations):
            if failures: raise failures[0]
            address = rng.randrange(CAPACITY)
            count = rng.randrange(1, 33)
            try:
                actual = read(controller, address, count)
            except Exception:
                print(f"读取失败：地址 {address:#06x}，迭代 {i}，页事件 {events[0]}，"
                      f"服务错误 {failures}", flush=True)
                raise
            expected = bytes(initial[(address + j) % CAPACITY] for j in range(count))
            if actual != expected:
                raise AssertionError(f"read mismatch at {address:#06x}, iteration {i}: "
                                     f"actual={actual.hex()} expected={expected.hex()} "
                                     f"events={events[0]} failures={failures}")
            if i % 3 == 0:
                payload = rng.randbytes(rng.randrange(1, 32))
                write(controller, address, payload)
                for offset, value in enumerate(payload):
                    initial[(address & ~(PAGE - 1)) | ((address + offset) & (PAGE - 1))] = value
                wait_ready(controller)
            if (i + 1) % 100 == 0:
                print(f"{i + 1}/{iterations} 次随机读写通过；页事件 {events[0]}", flush=True)
        # Force reads across the end of address space and validate the file.
        actual = read(controller, CAPACITY - 3, 6)
        expected = bytes(initial[(CAPACITY - 3 + j) % CAPACITY] for j in range(6))
        if actual != expected: raise AssertionError("address wrap failed")
        wait_ready(controller)
        stop.set()
        worker.join(timeout=2)
        if failures: raise failures[0]
        if path.read_bytes() != initial: raise AssertionError("host image differs from I2C writes")
        busy, error = target.i2c_eeprom_status()
        if busy or error: raise AssertionError(f"target status: busy={busy}, error={error}")
        print(f"16 KiB 主机镜像与总线一致；{iterations} 次读写，页事件 {events[0]}", flush=True)
    finally:
        stop.set()
        if worker: worker.join(timeout=2)
        controller.close()
        target.close()
        image.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("controller", nargs="?", default="COM5")
    parser.add_argument("target", nargs="?", default="COM4")
    parser.add_argument("--iterations", type=int, default=1000)
    args = parser.parse_args()
    run(args.controller, args.target, args.iterations)
