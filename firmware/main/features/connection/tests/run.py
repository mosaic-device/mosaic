#!/usr/bin/env python3
"""Run connection host tests."""

import os
import shlex
import subprocess
import tempfile
from pathlib import Path

feature = Path(__file__).resolve().parents[1]
firmware = feature.parents[2]
protos = firmware / "components/device_protos/generated"
nanopb = firmware / "components/nanopb"
golden = firmware.parent / "platform/packages/protocols/testdata/frames.txt"
compiler = shlex.split(os.environ.get("CXX", "c++"))
warnings = ["-Wall", "-Wextra", "-Werror", "-pedantic"]
includes = ["-I", str(protos), "-I", str(nanopb)]
# The committed protocol bindings, compiled as C the way the firmware build does.
protocol_sources = [
    nanopb / "pb_common.c",
    nanopb / "pb_decode.c",
    nanopb / "pb_encode.c",
    protos / "events.pb.c",
]
# Each test program, the feature sources it covers, and its arguments.
programs = {
    "backoff": ([feature / "backoff.cpp", feature / "tests/backoff_test.cpp"], []),
    "policy": (
        [feature / "backoff.cpp", feature / "policy.cpp", feature / "tests/policy_test.cpp"],
        [],
    ),
    "frames": ([feature / "frames.cpp", feature / "tests/frames_test.cpp"], [str(golden)]),
}

with tempfile.TemporaryDirectory(prefix="mosaic-connection-") as work:
    env = {**os.environ, "TMPDIR": work, "TMP": work, "TEMP": work}
    protocol_objects = []
    for source in protocol_sources:
        obj = Path(work) / f"{source.stem}.o"
        subprocess.run(
            compiler + ["-x", "c", "-std=c11"] + warnings + includes + ["-c", str(source), "-o", str(obj)],
            check=True,
            env=env,
        )
        protocol_objects.append(str(obj))
    for name, (sources, arguments) in programs.items():
        exe = Path(work) / name
        subprocess.run(
            compiler
            + ["-std=c++23", "-fno-exceptions", "-fno-rtti", "-UNDEBUG"]
            + warnings
            + includes
            + [str(source) for source in sources]
            + protocol_objects
            + ["-o", str(exe)],
            check=True,
            env=env,
        )
        subprocess.run([str(exe), *arguments], check=True, env=env, timeout=60)
    print("Connection tests passed", flush=True)

# Decision logic stays hardware-free so these tests exercise the code that ships.
# Only adapters/ may include IDF, FreeRTOS or LVGL headers.
for path in feature.glob("*.*pp"):
    text = path.read_text()
    for forbidden in ('"driver/', '"esp_', '"lvgl', '"freertos/', '"bsp/'):
        assert forbidden not in text, (path, forbidden)
print("Connection boundary checks passed", flush=True)
