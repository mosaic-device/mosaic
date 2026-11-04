#!/usr/bin/env python3
"""Run debug trigger command host tests without ESP-IDF."""

import os
import shlex
import subprocess
import tempfile
from pathlib import Path

feature = Path(__file__).resolve().parents[1]
main = feature.parents[1]
compiler = shlex.split(os.environ.get("CXX", "c++"))
sources = [feature / "commands.cpp", main / "events/status.cpp", feature / "tests/commands_test.cpp"]

with tempfile.TemporaryDirectory(prefix="mosaic-debug-triggers-") as work:
    env = {**os.environ, "TMPDIR": work, "TMP": work, "TEMP": work}
    exe = Path(work) / "commands"
    subprocess.run(
        compiler
        + ["-std=c++23", "-fno-exceptions", "-fno-rtti", "-Wall", "-Wextra", "-Werror", "-pedantic", "-UNDEBUG"]
        + ["-I", str(main / "events/tests/fakes")]
        + [str(source) for source in sources]
        + ["-o", str(exe)],
        check=True,
        env=env,
    )
    subprocess.run([str(exe)], check=True, env=env, timeout=60)
    print("Debug trigger command tests passed", flush=True)

# Commands stay off the console so every console runs the same ones.
text = (feature / "commands.cpp").read_text() + (feature / "commands.hpp").read_text()
for forbidden in ('"esp_console', '"linenoise', '"driver/', '"freertos/', '"lvgl'):
    assert forbidden not in text, forbidden
print("Debug trigger boundary checks passed", flush=True)
