#!/usr/bin/env python3
"""Run ambient brightness policy host tests."""

import os
import shlex
import subprocess
import tempfile
from pathlib import Path

feature = Path(__file__).resolve().parents[1]
compiler = shlex.split(os.environ.get("CXX", "c++"))
sources = [feature / "policy.cpp", feature / "tests/policy_test.cpp"]

with tempfile.TemporaryDirectory(prefix="mosaic-brightness-") as work:
    env = {**os.environ, "TMPDIR": work, "TMP": work, "TEMP": work}
    exe = Path(work) / "policy"
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
    print("Brightness policy tests passed", flush=True)

# The policy stays hardware-free so these tests exercise the code that ships.
for path in feature.glob("policy.*pp"):
    text = path.read_text()
    for forbidden in ('"driver/', '"esp_', '"lvgl', '"freertos/', '"bsp/'):
        assert forbidden not in text, (path, forbidden)
print("Brightness boundary checks passed", flush=True)
