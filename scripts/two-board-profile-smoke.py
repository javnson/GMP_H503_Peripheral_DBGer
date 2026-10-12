"""Live CAN and HDC1080 profile smoke test for the two connected boards.

Usage: python scripts/two-board-profile-smoke.py COM5 COM4 --iterations 5
The first port is #1 / J-Link; the second is #2 / ST-Link.
"""
import argparse
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "gui"))
from gmp_api import Api
from profiles import apply_target, decode_can_frame, load, query


def receive(api, timeout=1.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        frame = api.can_receive()
        if frame is not None:
            return frame
        time.sleep(0.005)
    raise TimeoutError("CAN 接收超时")


def drain(api):
    while api.can_receive() is not None:
        pass


def run(port1, port2, iterations):
    first, second = Api(), Api()
    try:
        for api, port in ((first, port1), (second, port2)):
            info = api.connect(port)
            if not info.capabilities & (1 << 16):
                raise RuntimeError(f"{port} 固件未启用 CAN")
            print(f"{port}: {info.name.decode()} v{info.protocol_major}.{info.protocol_minor}")
        passed = 0
        for bitrate in (125000, 250000, 500000, 1000000):
            first.can_config(bitrate); second.can_config(bitrate)
            drain(first); drain(second)
            for index in range(iterations):
                for sender, receiver, board in ((first, second, 1), (second, first, 2)):
                    extended = index % 2 == 1
                    identifier = (0x1ABCDE00 + index) if extended else (0x120 + index)
                    length = (0, 1, 8)[index % 3]
                    payload = bytes((board + index + offset) & 0xFF for offset in range(length))
                    sender.can_send(identifier, extended, payload)
                    got = receive(receiver)
                    expected = (identifier, extended, payload)
                    if got != expected:
                        raise AssertionError(f"{bitrate}: 期望 {expected}，实际 {got}")
                    passed += 1
            for port, api in ((port1, first), (port2, second)):
                s = api.can_status()
                if s.bus_off or s.tx_error or s.rx_error or s.rx_pending:
                    raise AssertionError(f"{port} CAN 错误：bus-off={s.bus_off}，TX/RX={s.tx_error}/{s.rx_error}，FIFO={s.rx_pending}")
            print(f"CAN {bitrate}: {iterations * 2} 帧双向通过")
        first.can_config(500000); second.can_config(500000)
        canopen = load(ROOT / "gui" / "presets" / "canopen-master.json")
        first.can_send(0x181, False, bytes.fromhex("34 12 78 56 34 12"))
        identifier, extended, payload = receive(second)
        family, values = decode_can_frame(canopen, identifier, payload)
        if extended or "TPDO1" not in family or list(values.values()) != [0x1234, 0x12345678]:
            raise AssertionError(f"CANopen PDO 解析错误：{family}, {values}")
        print("CANopen TPDO1 帧映射解析通过")
        target = load(ROOT / "gui" / "presets" / "hdc1080-slave.json")
        master = load(ROOT / "gui" / "presets" / "hdc1080-master.json")
        first.i2c_target_config(0, 0)
        apply_target(second, target)
        observations = {}
        for binding in master["parameters"]:
            observations[binding["name"]] = query(first, master, binding)
        temperature = observations["温度"][0]
        humidity = observations["相对湿度"][0]
        if abs(temperature - 25) > 0.1 or abs(humidity - 50) > 0.1:
            raise AssertionError(f"HDC1080 温湿度不符：{temperature}, {humidity}")
        if observations["厂商 ID"][0] != 0x5449 or observations["器件 ID"][0] != 0x1050:
            raise AssertionError(f"HDC1080 ID 不符：{observations}")
        print(f"HDC1080 仿真通过：{temperature:.2f} °C，{humidity:.2f} %RH，ID 5449/1050")
        print(f"合计 {passed} 帧 CAN、1 帧 CANopen PDO 解析和 5 项 HDC1080 查询通过")
    finally:
        first.close(); second.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("first", nargs="?", default="COM5")
    parser.add_argument("second", nargs="?", default="COM4")
    parser.add_argument("--iterations", type=int, default=5)
    args = parser.parse_args()
    run(args.first, args.second, args.iterations)
