#!/usr/bin/env python3
"""Run rotary encoder and LED brightness-limit host tests."""

import os
import shlex
import subprocess
import tempfile
from pathlib import Path

drivers = Path(__file__).resolve().parents[1]
compiler = shlex.split(os.environ.get("CXX", "c++"))
cases = [
    ("encoder_logic", ["encoder_logic.cpp", "tests/encoder_logic_test.cpp"], []),
    ("leds_logic", ["tests/leds_logic_test.cpp"], []),
]

with tempfile.TemporaryDirectory(prefix="mosaic-drivers-") as work:
    env = {**os.environ, "TMPDIR": work, "TMP": work, "TEMP": work}
    for name, sources, flags in cases:
        exe = Path(work) / name
        subprocess.run(
            compiler
            + [
                "-std=c++23",
                "-fno-exceptions",
                "-fno-rtti",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                "-UNDEBUG",
            ]
            + flags
            + [str(drivers / source) for source in sources]
            + ["-o", str(exe)],
            check=True,
            env=env,
        )
        subprocess.run([str(exe)], check=True, env=env, timeout=60)
        print(f"{name} tests passed", flush=True)

# Driver logic stays hardware-free so these tests exercise the code that ships.
for path in drivers.glob("*_logic.*pp"):
    text = path.read_text()
    for forbidden in ('"driver/', '"esp_', '"lvgl', '"freertos/'):
        assert forbidden not in text, (path, forbidden)
print("Driver logic boundary checks passed", flush=True)
