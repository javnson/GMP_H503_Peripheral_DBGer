"""Exercise a #2 volatile 24LC02B-like I2C target via #1.

Usage: python scripts/two-board-eeprom-stress.py COM5 COM4 --iterations 1000
"""
import argparse
import random
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "gui"))
from gmp_api import Api

ADDRESS = 0x50


def wait_ready(controller, target, timeout=2.0):
    deadline = time.monotonic() + timeout
    nacks = 0
    while time.monotonic() < deadline:
        try:
            controller.i2c_transfer(ADDRESS, b"", 0)
            busy, error = target.i2c_eeprom_status()
            if error: raise AssertionError("target EEPROM reported an error")
            if not busy: return nacks
        except RuntimeError:
            nacks += 1
        time.sleep(0.001)
    raise TimeoutError("EEPROM ACK polling timed out")


def write(controller, target, shadow, address, payload):
    wait_ready(controller, target)
    controller.i2c_transfer(ADDRESS, bytes([address]) + payload, 0)
    for offset, value in enumerate(payload):
        shadow[(address & 0xF8) | ((address + offset) & 7)] = value
    return wait_ready(controller, target)


def read(controller, address, count):
    return controller.i2c_transfer(ADDRESS, bytes([address]), count)


def verify(controller, target, shadow):
    actual = b"".join(read(controller, offset, 64) for offset in range(0, 256, 64))
    if actual != shadow:
        index = next(i for i, (a, b) in enumerate(zip(actual, shadow)) if a != b)
        raise AssertionError(f"I2C EEPROM mismatch at 0x{index:02X}: {actual[index]:02X} != {shadow[index]:02X}")
    local = b"".join(target.i2c_eeprom_read(offset, 64) for offset in range(0, 256, 64))
    if local != shadow: raise AssertionError("target SRAM image differs from I2C view")


def run(controller_port, target_port, iterations):
    controller, target = Api(), Api()
    try:
        print(f"controller={controller_port}, EEPROM target={target_port}, iterations={iterations}", flush=True)
        controller.connect(controller_port)
        info = target.connect(target_port)
        if (info.protocol_major, info.protocol_minor) < (1, 7):
            raise RuntimeError("target board requires protocol 1.7 firmware")
        controller.i2c_target_config(0, 0)
        target.i2c_target_config(1, ADDRESS)
        target.i2c_eeprom_config(8, 8)
        target.i2c_target_profile(2, 0, 0)
        shadow = bytearray(b"".join(target.i2c_eeprom_read(i, 64) for i in range(0, 256, 64)))
        nacks = 0
        for page in range(32):
            payload = bytes((page * 17 + i) & 0xFF for i in range(8))
            nacks += write(controller, target, shadow, page * 8, payload)
        verify(controller, target, shadow)
        print("全 256 字节初始化、连续读、目标本地读一致", flush=True)
        nacks += write(controller, target, shadow, 6, b"\x11\x22\x33\x44")
        verify(controller, target, shadow)
        if read(controller, 254, 4) != bytes(shadow[254:] + shadow[:2]):
            raise AssertionError("sequential read did not wrap 0xFF -> 0x00")
        print("跨 8 字节页写回绕与 0xFF 读指针回绕通过", flush=True)
        rng = random.Random(0x24C02B)
        for i in range(iterations):
            address = rng.randrange(256)
            count = rng.randrange(1, 32)
            payload = rng.randbytes(count)
            nacks += write(controller, target, shadow, address, payload)
            if (i + 1) % 100 == 0:
                verify(controller, target, shadow)
                print(f"{i + 1}/{iterations} 随机页写与全片校验通过；累计忙 NACK={nacks}", flush=True)
        verify(controller, target, shadow)
        # Reset is asynchronous to the CDC reply; allow the pending reset to
        # finish before reconnecting and configuring the I2C target again.
        target.reset()
        target.close()
        target = Api()
        time.sleep(1.0)
        deadline = time.monotonic() + 10
        while True:
            try:
                target.connect(target_port)
                break
            except RuntimeError:
                if time.monotonic() > deadline: raise
                time.sleep(0.2)
        target.i2c_target_config(1, ADDRESS)
        target.i2c_eeprom_config(8, 8)
        target.i2c_target_profile(2, 0, 0)
        erased = bytes([0xFF]) * 256
        if b"".join(read(controller, offset, 64) for offset in range(0, 256, 64)) != erased:
            raise AssertionError("SRAM EEPROM did not clear on MCU reset")
        for offset in range(0, 256, 32):
            target.i2c_eeprom_write(offset, shadow[offset:offset + 32])
        wait_ready(controller, target)
        verify(controller, target, shadow)
        print(f"复位后 RAM 清空，上位机重新载入 256 字节后恢复；完成 {iterations + 34} 次页写，忙 NACK={nacks}", flush=True)
    finally:
        controller.close(); target.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("controller", nargs="?", default="COM5")
    parser.add_argument("target", nargs="?", default="COM4")
    parser.add_argument("--iterations", type=int, default=1000)
    args = parser.parse_args()
    run(args.controller, args.target, args.iterations)
