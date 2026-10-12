"""Portable profile validation and conversion checks (no board needed)."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "gui"))

from profiles import (crc16_modbus, decode, decode_can_frame, encode, load,
                      validate, write_value)


class FakeApi:
    def __init__(self):
        self.calls = []

    def register_write(self, port, reg, raw):
        self.calls.append((port, reg, raw))


class ProfileTests(unittest.TestCase):
    def test_all_preset_files_validate(self):
        files = list((ROOT / "gui" / "presets").glob("*.json"))
        self.assertGreaterEqual(len(files), 10)
        for path in files:
            with self.subTest(path=path.name):
                load(path)

    def test_hdc_temperature_encoding_and_ids(self):
        profile = load(ROOT / "gui" / "presets" / "hdc1080-slave.json")
        temperature = profile["parameters"][0]
        raw = encode(25, temperature)
        self.assertAlmostEqual(decode(raw, temperature), 25, places=2)
        self.assertEqual(profile["settings"]["address"], 0x40)

    def test_modbus_crc_known_request(self):
        self.assertEqual(crc16_modbus(bytes.fromhex("01 03 00 00 00 01")), bytes.fromhex("84 0A"))

    def test_canopen_pdo_binding(self):
        profile = load(ROOT / "gui" / "presets" / "canopen-master.json")
        name, values = decode_can_frame(profile, 0x181, bytes.fromhex("34 12 78 56 34 12"))
        self.assertIn("TPDO1", name)
        self.assertEqual(list(values.values()), [0x1234, 0x12345678])
        self.assertIsInstance(values["节点 1 计数"], int)
        heartbeat, _ = decode_can_frame(profile, 0x701, bytes([5]))
        self.assertIn("运行", heartbeat)

    def test_reject_wrong_role_and_bad_offsets(self):
        path = ROOT / "gui" / "presets" / "hdc1080-master.json"
        with self.assertRaises(ValueError):
            load(path, "i2c", "slave")
        data = load(ROOT / "gui" / "presets" / "can-raw-master.json")
        data["parameters"][0]["byte_offset"] = 7
        with self.assertRaises(ValueError):
            validate(data)

    def test_local_register_write(self):
        profile = {"schema": 1, "name": "x", "interface": "spi", "role": "slave",
                   "protocol": "bridge-register", "settings": {},
                   "parameters": [{"name": "v", "register": 16, "type": "u16",
                                   "endian": "little", "access": "rw"}]}
        validate(profile)
        api = FakeApi()
        write_value(api, profile, profile["parameters"][0], 0x1234)
        self.assertEqual(api.calls, [(3, 16, bytes.fromhex("34 12"))])


if __name__ == "__main__":
    unittest.main()
