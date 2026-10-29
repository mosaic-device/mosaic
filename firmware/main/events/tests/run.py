#!/usr/bin/env python3
"""Run event bus host tests against a synchronous esp_event fake."""

import os
import shlex
import subprocess
import tempfile
from pathlib import Path

events = Path(__file__).resolve().parents[1]
compiler = shlex.split(os.environ.get("CXX", "c++"))
cases = [
    ("status", ["status.cpp", "tests/status_test.cpp"], []),
    ("bus", ["status.cpp", "bus.cpp", "tests/bus_test.cpp"], ["-I", str(events / "tests/fakes")]),
]

with tempfile.TemporaryDirectory(prefix="mosaic-events-") as work:
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
            + [str(events / source) for source in sources]
            + ["-o", str(exe)],
            check=True,
            env=env,
        )
        subprocess.run([str(exe)], check=True, env=env, timeout=60)
        print(f"{name} tests passed", flush=True)

# The bus is shared infrastructure: it must not depend on any feature.
for path in events.glob("*.*pp"):
    assert "features/" not in path.read_text(), path
print("Event bus boundary checks passed", flush=True)
