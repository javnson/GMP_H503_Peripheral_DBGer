"""Exercise the public CLI against a fake CDC transport on a Linux PTY."""
import errno
import os
import pty
import select
import subprocess
import sys
import threading
import time


def crc16(data):
    value = 0xFFFF
    for byte in data:
        value ^= byte << 8
        for _ in range(8):
            value = ((value << 1) ^ (0x1021 if value & 0x8000 else 0)) & 0xFFFF
    return value


def crc8(data):
    value = 0
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = ((value << 1) ^ (7 if value & 0x80 else 0)) & 0xFF
    return value


def register_reply(write):
    value = bytearray([0x5A, 0, 0] if write else
                      [0x5A, 0, 3, 0xAA, 0xBB, 0xCC])
    value.append(crc8(value))
    return bytes(value)


def check_request(data):
    assert data[0] == 0xA5 and data[2:4] == bytes([0x10, 3]), data.hex()
    write = data[1] == 1
    assert data[1] in (0, 1)
    size = 7 if write else 4
    if write:
        assert data[4:7] == bytes([0x11, 0x22, 0x33])
    assert data[size] == crc8(data[:size])
    return write


def serve(master, stop, errors):
    buffer = bytearray()
    serial_pending = b""
    spi_pending = b""
    try:
        while not stop.is_set():
            if not select.select([master], [], [], 0.1)[0]:
                continue
            try:
                data = os.read(master, 4096)
            except OSError as error:
                if error.errno == errno.EIO:
                    time.sleep(0.01)
                    continue
                raise
            buffer.extend(data)
            while len(buffer) >= 9:
                if buffer[:2] != b"\xA5\x5A":
                    del buffer[0]
                    continue
                length = int.from_bytes(buffer[5:7], "little")
                total = length + 9
                if len(buffer) < total:
                    break
                frame = bytes(buffer[:total])
                del buffer[:total]
                assert int.from_bytes(frame[-2:], "little") == crc16(frame[:-2])
                cmd, seq = frame[3], frame[4]
                payload = frame[7:-2]
                result = b""
                if cmd == 0x52:  # serial read
                    result, serial_pending = serial_pending, b""
                elif cmd == 0x51:  # serial write
                    assert payload[0] == 0
                    write = check_request(payload[1:])
                    serial_pending = register_reply(write)
                elif cmd == 0x31:  # SPI fixed frame, one transaction latency
                    if payload[0] == 0xA5:
                        write = check_request(payload)
                        result = bytes(37)
                        spi_pending = register_reply(write).ljust(37, b"\0")
                    else:
                        assert payload == bytes(37)
                        result, spi_pending = spi_pending, b""
                elif cmd == 0x62:  # I3C write + read
                    assert payload[:3] == bytes([0x30, 37, 37])
                    write = check_request(payload[3:])
                    result = register_reply(write).ljust(37, b"\0")
                elif cmd == 0x20:  # I2C pointer write + repeated-start read
                    if payload == bytes([0x42, 1, 3, 0x10]):
                        result = bytes([0xAA, 0xBB, 0xCC])
                    else:
                        assert payload == bytes([0x42, 4, 0, 0x10, 0x11, 0x22, 0x33])
                else:
                    raise AssertionError(f"unexpected command {cmd:02x}")
                body = bytes([0]) + result
                answer = bytearray([0xA5, 0x5A, 1, cmd | 0x80, seq])
                answer += len(body).to_bytes(2, "little") + body
                answer += crc16(answer).to_bytes(2, "little")
                os.write(master, answer)
    except Exception as error:
        errors.append(error)
        stop.set()


def main():
    master, slave = pty.openpty()
    path = os.ttyname(slave)
    stop = threading.Event()
    errors = []
    worker = threading.Thread(target=serve, args=(master, stop, errors), daemon=True)
    worker.start()
    for bus, target in (("uart", "0"), ("spi", "0"),
                        ("i3c", "0x30"), ("i2c", "0x42")):
        result = subprocess.run([sys.argv[1], path, "remote", bus, target,
                                 "get", "0x10", "3"], text=True,
                                capture_output=True, timeout=8)
        assert not errors, errors
        assert result.returncode == 0, (bus, result.stderr)
        assert result.stdout.strip() == "aabbcc", (bus, result.stdout)
        result = subprocess.run([sys.argv[1], path, "remote", bus, target,
                                 "set", "0x10", "112233"], text=True,
                                capture_output=True, timeout=8)
        assert not errors, errors
        assert result.returncode == 0, (bus, result.stderr)
    stop.set()
    worker.join(timeout=2)
    assert not errors, errors
    os.close(slave)
    os.close(master)


if __name__ == "__main__":
    main()
