#!/usr/bin/env python3
"""Run connection host tests."""

import os
import shlex
import subprocess
import tempfile
from pathlib import Path

feature = Path(__file__).resolve().parents[1]
compiler = shlex.split(os.environ.get("CXX", "c++"))
# Each test program and the feature sources it covers.
programs = {
    "backoff": [feature / "backoff.cpp", feature / "tests/backoff_test.cpp"],
    "policy": [
        feature / "backoff.cpp",
        feature / "policy.cpp",
        feature / "tests/policy_test.cpp",
    ],
}

with tempfile.TemporaryDirectory(prefix="mosaic-connection-") as work:
    env = {**os.environ, "TMPDIR": work, "TMP": work, "TEMP": work}
    for name, sources in programs.items():
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
            + [str(source) for source in sources]
            + ["-o", str(exe)],
            check=True,
            env=env,
        )
        subprocess.run([str(exe)], check=True, env=env, timeout=60)
    print("Connection tests passed", flush=True)

# Decision logic stays hardware-free so these tests exercise the code that ships.
# Only adapters/ may include IDF, FreeRTOS or LVGL headers.
for path in feature.glob("*.*pp"):
    text = path.read_text()
    for forbidden in ('"driver/', '"esp_', '"lvgl', '"freertos/', '"bsp/'):
        assert forbidden not in text, (path, forbidden)
print("Connection boundary checks passed", flush=True)
