#!/usr/bin/env python3
"""Run clock host tests."""

import os
import shlex
import subprocess
import tempfile
from pathlib import Path

feature = Path(__file__).resolve().parents[1]
compiler = shlex.split(os.environ.get("CXX", "c++"))
sources = [feature / "clock.cpp", feature / "tests/clock_test.cpp"]

with tempfile.TemporaryDirectory(prefix="mosaic-clock-") as work:
    env = {**os.environ, "TMPDIR": work, "TMP": work, "TEMP": work}
    exe = Path(work) / "clock"
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
            # setenv, tzset and localtime_r are POSIX, not standard C++.
            "-D_POSIX_C_SOURCE=200809L",
        ]
        + [str(source) for source in sources]
        + ["-o", str(exe)],
        check=True,
        env=env,
    )
    subprocess.run([str(exe)], check=True, env=env, timeout=60)
    print("Clock tests passed", flush=True)

# Clock logic stays hardware-free so these tests exercise the code that ships.
for path in feature.glob("clock.*pp"):
    text = path.read_text()
    for forbidden in ('"driver/', '"esp_', '"lvgl', '"freertos/', '"bsp/'):
        assert forbidden not in text, (path, forbidden)
print("Clock boundary checks passed", flush=True)
