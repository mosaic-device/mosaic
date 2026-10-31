#!/usr/bin/env python3
"""Run device settings host tests: the settings rules and the store against a fake NVS."""

import os
import shlex
import subprocess
import tempfile
from pathlib import Path

feature = Path(__file__).resolve().parents[1]
main = feature.parents[1]
compiler = shlex.split(os.environ.get("CXX", "c++"))
sources = [
    feature / "settings.cpp",
    feature / "store.cpp",
    main / "features/audio_cues/cues.cpp",
    main / "events/status.cpp",
    feature / "tests/settings_test.cpp",
]

with tempfile.TemporaryDirectory(prefix="mosaic-settings-") as work:
    env = {**os.environ, "TMPDIR": work, "TMP": work, "TEMP": work}
    exe = Path(work) / "settings"
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
            "-I",
            str(feature / "tests/fakes"),
        ]
        + [str(source) for source in sources]
        + ["-o", str(exe)],
        check=True,
        env=env,
    )
    subprocess.run([str(exe)], check=True, env=env, timeout=60)
    print("Settings tests passed", flush=True)

# The settings rules stay hardware-free; the store touches only NVS.
for path in feature.glob("settings.*pp"):
    text = path.read_text()
    for forbidden in ('"driver/', '"esp_', '"lvgl', '"freertos/', '"nvs'):
        assert forbidden not in text, (path, forbidden)
print("Settings boundary checks passed", flush=True)
