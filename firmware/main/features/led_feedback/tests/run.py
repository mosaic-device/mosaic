#!/usr/bin/env python3
"""Run LED feedback pattern host tests."""

import os
import shlex
import subprocess
import tempfile
from pathlib import Path

feature = Path(__file__).resolve().parents[1]
main = feature.parents[1]
compiler = shlex.split(os.environ.get("CXX", "c++"))
sources = [
    feature / "patterns.cpp",
    main / "events/status.cpp",
    feature / "tests/patterns_test.cpp",
]

with tempfile.TemporaryDirectory(prefix="mosaic-led-feedback-") as work:
    env = {**os.environ, "TMPDIR": work, "TMP": work, "TEMP": work}
    exe = Path(work) / "patterns"
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
        + [str(source) for source in sources]
        + ["-o", str(exe)],
        check=True,
        env=env,
    )
    subprocess.run([str(exe)], check=True, env=env, timeout=60)
    print("LED feedback pattern tests passed", flush=True)

# Patterns stay hardware-free so these tests exercise the code that ships.
for path in feature.glob("patterns.*pp"):
    text = path.read_text()
    for forbidden in ('"driver/', '"esp_', '"lvgl', '"freertos/'):
        assert forbidden not in text, (path, forbidden)
print("LED feedback boundary checks passed", flush=True)
