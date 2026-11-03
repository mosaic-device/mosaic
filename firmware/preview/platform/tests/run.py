#!/usr/bin/env python3
"""Run the preview's host platform tests: its FreeRTOS, esp_event and NVS stand-ins."""

import os
import shlex
import subprocess
import tempfile
from pathlib import Path

platform = Path(__file__).resolve().parents[1]
compiler = shlex.split(os.environ.get("CXX", "c++"))
sources = [platform / "src" / name for name in ("system.cpp", "freertos.cpp", "esp_event.cpp", "nvs.cpp")]

with tempfile.TemporaryDirectory(prefix="mosaic-preview-platform-") as work:
    env = {**os.environ, "TMPDIR": work, "TMP": work, "TEMP": work}
    exe = Path(work) / "platform"
    subprocess.run(
        compiler
        + ["-std=c++23", "-Wall", "-Wextra", "-Werror", "-pedantic", "-UNDEBUG", "-pthread"]
        + ["-I", str(platform / "include")]
        + [str(source) for source in sources + [platform / "tests/platform_test.cpp"]]
        + ["-o", str(exe)],
        check=True,
        env=env,
    )
    nvs = Path(work) / "nvs.bin"
    subprocess.run([str(exe), "write", str(nvs)], check=True, env=env, timeout=60)
    subprocess.run([str(exe), "read", str(nvs)], check=True, env=env, timeout=60)
    print("Preview platform tests passed", flush=True)
