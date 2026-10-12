"""Exercise the device-independent standard CLI configuration contract."""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def main(cli):
    with tempfile.TemporaryDirectory() as directory:
        env = dict(os.environ, GMP_STATE_DIR=directory, GMP_PORT="TEST")

        def call(*args, input_text=None):
            return subprocess.run([cli, *args], input=input_text, text=True,
                                  capture_output=True, env=env, check=False)

        assert "i2c :" in call("list").stdout
        result = call("i2c", "config")
        assert result.returncode == 0, result.stderr
        assert json.loads(result.stdout)["i2c"]["address"] == 80
        profile = Path(directory) / "profile.json"
        profile.write_text('{"i2c":{"role":"slave","address":82}}', encoding="utf-8")
        result = call("i2c", "config", f"-e:{profile}", "role=master", "baud=100k")
        assert result.returncode == 0, result.stderr
        config = json.loads(result.stdout)["i2c"]
        assert config["address"] == 82 and config["role"] == "master"
        assert config["baud"] == 100000
        saved = call("save", "config")
        assert saved.returncode == 0, saved.stderr
        assert json.loads(saved.stdout)["i2c"]["address"] == 82
        assert call("load", "config", input_text="{}").returncode == 0
        assert call("i2c", "config", "address=999").returncode != 0
        assert call("i2c", "config", input_text="{}").returncode == 0
        invalid = Path(directory) / "invalid.json"
        invalid.write_text("{invalid", encoding="utf-8")
        assert call("i2c", "config", f"-e:{invalid}").returncode != 0
        assert call("i2c", "config", "unknown=1").returncode != 0


if __name__ == "__main__":
    main(sys.argv[1])
