#!/usr/bin/env python3
"""Run IDF-free secure-erasure tests, including an optimized build."""

import os
import shlex
import subprocess
import tempfile
from pathlib import Path

source = Path(__file__).resolve().with_name("secure_erase_test.cpp")
compiler = shlex.split(os.environ.get("CXX", "c++"))
with tempfile.TemporaryDirectory(prefix="mosaic-secure-erase-") as work:
    env = {**os.environ, "TMPDIR": work, "TMP": work, "TEMP": work}
    for optimization in ("-O0", "-O2"):
        exe = Path(work) / ("secure_erase_test" + optimization)
        subprocess.run(
            compiler
            + [
                "-std=c++23",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                optimization,
                str(source),
                "-o",
                str(exe),
            ],
            check=True,
            env=env,
        )
        subprocess.run([str(exe)], check=True, env=env)
print("secure-erasure tests passed (-O0 and -O2)")
