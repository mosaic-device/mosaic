#!/usr/bin/env python3
"""Decode the shared golden protocol frames with the committed nanopb bindings."""

import os
import shlex
import subprocess
import tempfile
from pathlib import Path

component = Path(__file__).resolve().parents[1]
firmware = component.parents[1]
nanopb = firmware / "components/nanopb"
frames = firmware.parent / "platform/packages/protocols/testdata/frames.txt"
compiler = shlex.split(os.environ.get("CXX", "c++"))
warnings = ["-Wall", "-Wextra", "-Werror", "-pedantic"]
includes = ["-I", str(component / "generated"), "-I", str(nanopb)]
c_sources = [
    nanopb / "pb_common.c",
    nanopb / "pb_decode.c",
    nanopb / "pb_encode.c",
    component / "generated/events.pb.c",
]

with tempfile.TemporaryDirectory(prefix="mosaic-device-protos-") as work:
    env = {**os.environ, "TMPDIR": work, "TMP": work, "TEMP": work}
    objects = []
    for source in c_sources:
        obj = Path(work) / f"{source.stem}.o"
        # The C++ driver compiles the C sources as C, as the firmware build does.
        subprocess.run(
            compiler + ["-x", "c", "-std=c11"] + warnings + includes + ["-c", str(source), "-o", str(obj)],
            check=True,
            env=env,
        )
        objects.append(str(obj))
    exe = Path(work) / "golden_frames"
    subprocess.run(
        compiler
        + ["-std=c++23", "-fno-exceptions", "-fno-rtti", "-UNDEBUG"]
        + warnings
        + includes
        + [str(component / "tests/golden_frames_test.cpp")]
        + objects
        + ["-o", str(exe)],
        check=True,
        env=env,
    )
    subprocess.run([str(exe), str(frames)], check=True, env=env, timeout=60)
    print("Device protocol golden frame tests passed", flush=True)
